# ESP32-NFC-Tool BLE Integration Guide

**Status**: ✅ **PRODUCTION READY** - Fully Tested
**Last Updated**: February 3, 2026
**Version**: 1.1

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [BLE Protocol](#ble-protocol)
4. [Response Chunking](#response-chunking)
5. [Implementation Details](#implementation-details)
6. [API Reference](#api-reference)
7. [Porting to Other Devices](#porting-to-other-devices)
8. [Troubleshooting](#troubleshooting)
9. [File Reference](#file-reference)

---

## Overview

### What is ESP32-NFC-Tool?

ESP32-NFC-Tool is a standalone ESP32-based NFC reader that communicates via Bluetooth Low Energy (BLE). It supports:
- **SRIX tag** reading/writing
- **Mifare Classic** UID reading and full dump
- **File management** on SD card
- **System diagnostics** and monitoring

### Integration with Bruce

Bruce firmware can connect to ESP32-NFC-Tool as a BLE client, enabling:
- Remote NFC operations without built-in NFC hardware
- Dual-device support (PN532 BLE library + ESP32-NFC-Tool)
- Automatic device detection and reconnection
- User-cancellable operations via ESC key

---

## Architecture

### Dual-Device Design

```
┌─────────────────────────────────────────────────────────────┐
│                    Bruce Firmware                           │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              pn532ble.cpp (UI Layer)                │   │
│  │  - Menu handling                                    │   │
│  │  - Device detection routing                         │   │
│  │  - JSON response display                            │   │
│  │  - usingToolDevice flag for routing                 │   │
│  └─────────────────────────────────────────────────────┘   │
│                          │                                  │
│          ┌───────────────┼───────────────┐                 │
│          ▼                               ▼                 │
│  ┌───────────────┐              ┌───────────────────┐      │
│  │  PN532_BLE    │              │  Pn532BleTool     │      │
│  │  Library      │              │  (Custom Class)   │      │
│  │  (3rd party)  │              │                   │      │
│  └───────────────┘              └───────────────────┘      │
│          │                               │                  │
│          ▼                               ▼                  │
│  ┌───────────────┐              ┌───────────────────┐      │
│  │  PN532 BLE    │              │  ESP32-NFC-Tool   │      │
│  │  Device       │              │  Device           │      │
│  └───────────────┘              └───────────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | File | Purpose |
|-----------|------|---------|
| `Pn532BleTool` | `pn532_ble_tool.h/cpp` | BLE client for ESP32-NFC-Tool |
| `pn532ble` | `pn532ble.h/cpp` | UI, menu, device routing |
| `usingToolDevice` | Flag in `pn532ble.cpp` | Routes commands to correct backend |

---

## BLE Protocol

### Service Information

```
Service Name:    Nordic UART Service (NUS)
Service UUID:    6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Device Name:     ESP32-NFC-Tool
```

### Characteristics

| Name | UUID | Direction | Properties |
|------|------|-----------|------------|
| TX | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Device → Client | NOTIFY, READ |
| RX | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Client → Device | WRITE |

### Command Format

All commands are JSON objects:

```json
{
  "cmd": "category",
  "params": {
    "action": "specific_action",
    "param1": "value1"
  },
  "id": "optional_tracking_id"
}
```

### Response Format

```json
{
  "success": true,
  "cmd": "command_name",
  "timestamp": 12345,
  "id": "matching_id",
  "message": "Human readable message",
  "data": { /* response payload */ }
}
```

**Error Response**:
```json
{
  "success": false,
  "cmd": "command_name",
  "error": "Error description",
  "error_code": -1
}
```

---

## Response Chunking

### Why Chunking is Needed

BLE has a Maximum Transmission Unit (MTU) that limits packet size. With typical MTU of 255 bytes, the actual payload is **252 bytes** (MTU - 3 for ATT header).

Responses like `system_info` (344 bytes) or `diagnostics` (394 bytes) exceed this limit.

### Chunking Protocol

ESP32-NFC-Tool automatically chunks responses exceeding **80% of MTU payload**:

| MTU | Max Payload | Chunk Threshold |
|-----|-------------|-----------------|
| 255 | 252 bytes | 201 bytes |
| 512 | 509 bytes | 407 bytes |

### Chunk Envelope Format

Large responses are split into multiple JSON packets:

```json
{"_c":1,"_t":3,"_d":"{\"success\":true,\"cmd\":\"system_info\"..."}
{"_c":2,"_t":3,"_d":"...continuation of JSON..."}
{"_c":3,"_t":3,"_d":"...final part of JSON...\"}"}
```

| Field | Type | Description |
|-------|------|-------------|
| `_c` | int | Current chunk number (1-based) |
| `_t` | int | Total number of chunks |
| `_d` | string | **Escaped** JSON data payload |

### Client Implementation (Bruce)

**Critical**: The `_d` field contains **escaped JSON**. The client MUST:

1. **Detect** chunked response by checking for `_c`, `_t`, `_d` fields
2. **Extract** and **unescape** the `_d` content (`\"` → `"`, `\\` → `\`)
3. **Accumulate** all chunks in order
4. **Parse** the complete JSON after final chunk

**Bruce Implementation** (in `processChunkedData()`):

```cpp
// Extract _d field
String data = chunk.substring(dStart, dEnd);

// CRITICAL: Unescape JSON content
data.replace("\\\"", "\"");
data.replace("\\\\", "\\");

// Accumulate
_chunkBuffer += data;

// When all chunks received, assemble
if (_receivedChunks >= _expectedChunks) {
    _lastResponse = _chunkBuffer;
    _responseReady.store(true);
}
```

### Timing Considerations

- **Inter-chunk delay**: Server sends 30ms between chunks
- **Chunk timeout**: Bruce waits up to 5 seconds between chunks
- **Total timeout**: Configurable per command (default 5-20 seconds)

---

## Implementation Details

### MTU Negotiation

Bruce requests high MTU for efficient transfers:

```cpp
// Before NimBLEDevice::init()
NimBLEDevice::setMTU(517);

// After connection
_client->exchangeMTU();
uint16_t mtu = _client->getMTU();  // Typically 255 on ESP32
```

### Connection Management

**Auto-reconnect**: Bruce automatically reconnects if connection is lost (3-second throttle):

```cpp
bool Pn532BleTool::reconnectIfNeeded(uint32_t intervalMs) {
    if (isConnected()) return true;

    uint32_t now = millis();
    if (now - _lastReconnectAttempt < intervalMs) return false;

    _lastReconnectAttempt = now;
    return connect();
}
```

**Heartbeat**: Bruce sends periodic status requests to keep connection alive (15-second interval):

```cpp
bool Pn532BleTool::isHeartbeatNeeded() const {
    return (millis() - _lastHeartbeat) > HEARTBEAT_INTERVAL_MS;  // 15000ms
}
```

### Thread Safety

- `std::atomic<bool> _responseReady` prevents race conditions
- Notification callback runs in BLE stack context
- Main loop polls `_responseReady` with atomic load

### Non-Blocking Operations

All blocking operations check for ESC key:

```cpp
while (!_responseReady.load() && (millis() - start) < timeoutMs) {
    if (EscPress) {
        resetChunkState();
        return false;
    }
    delay(10);
    yield();
}
```

---

## API Reference

### Pn532BleTool Class

#### Connection Methods

| Method | Description |
|--------|-------------|
| `searchForDevice()` | Scan for ESP32-NFC-Tool (6s timeout) |
| `connect()` | Connect to discovered device |
| `disconnect()` | Clean disconnect and resource cleanup |
| `isConnected()` | Check connection status |
| `reconnectIfNeeded(intervalMs)` | Auto-reconnect with throttle |

#### System Commands

| Method | Command | Response Data |
|--------|---------|---------------|
| `getSystemInfo(response)` | `system/info` | chip_model, cpu_freq, flash_size, free_heap, uptime_ms |
| `getBtStatus(response)` | `bt/status` | connected_count, device_name, max_mtu, current_mtu |
| `getHeapInfo(response)` | `system/heap` | free_heap, min_free_heap, heap_size |
| `getDiagnostics(response)` | `system/diag` | Combined BLE + system info |
| `getWifiStatus(response)` | `wifi/status` | connected, ssid, ip, rssi |
| `restartDevice()` | `system/restart` | Device reboots |

#### NFC Commands

| Method | Command | Response Data |
|--------|---------|---------------|
| `readSrix(response)` | `nfc/read_srix` | SRIX tag data |
| `readMifareUid(response)` | `nfc/mifare_uid` | UID, SAK, ATQA |
| `readMifareFull(response)` | `nfc/mifare_read` | Full 1K/4K dump |

#### File Commands

| Method | Command | Response Data |
|--------|---------|---------------|
| `listFiles(protocol, response)` | `files/list` | Array of filenames |
| `saveSrixDump(filename, response)` | `nfc/save` | Save confirmation |
| `loadSrixDump(filename, response)` | `nfc/load` | Loaded data |
| `deleteFile(filename, protocol, response)` | `files/delete` | Delete confirmation |

---

## Porting to Other Devices

### Requirements

To port this integration to another device/firmware:

1. **BLE Client Library**: NimBLE-Arduino or equivalent
2. **JSON Parser**: ArduinoJson v7+ recommended
3. **Atomic Types**: `<atomic>` for thread-safe flags

### Step-by-Step Guide

#### 1. Create BLE Client Class

```cpp
class MyBleNfcClient {
public:
    bool searchForDevice();
    bool connect();
    void disconnect();
    bool sendCommand(const String& cmd, const String& params, String& response);

private:
    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _txChar = nullptr;
    NimBLERemoteCharacteristic* _rxChar = nullptr;

    // Chunk handling
    String _chunkBuffer;
    int _expectedChunks = 0;
    int _receivedChunks = 0;
    bool _isReceivingChunks = false;

    // Response handling
    std::atomic<bool> _responseReady;
    String _lastResponse;
};
```

#### 2. Implement Device Discovery

```cpp
bool MyBleNfcClient::searchForDevice() {
    NimBLEDevice::setMTU(517);
    NimBLEDevice::init("");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);

    NimBLEUUID nusUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    NimBLEScanResults results = scan->getResults(6000, false);

    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice* dev = results.getDevice(i);

        // Check by UUID first
        if (dev->isAdvertisingService(nusUUID)) {
            _device = *dev;
            return true;
        }

        // Fallback: check name
        if (dev->getName().find("ESP32-NFC-Tool") != std::string::npos) {
            _device = *dev;
            return true;
        }
    }
    return false;
}
```

#### 3. Implement Chunk Handling

```cpp
void MyBleNfcClient::processChunk(const String& chunk) {
    // Parse envelope
    int c = extractInt(chunk, "_c");
    int t = extractInt(chunk, "_t");
    String d = extractString(chunk, "_d");

    // CRITICAL: Unescape
    d.replace("\\\"", "\"");
    d.replace("\\\\", "\\");

    // Accumulate
    if (c == 1) {
        _chunkBuffer = "";
        _expectedChunks = t;
        _receivedChunks = 0;
    }

    _chunkBuffer += d;
    _receivedChunks = c;

    // Complete?
    if (_receivedChunks >= _expectedChunks) {
        _lastResponse = _chunkBuffer;
        _responseReady.store(true);

        // Reset state
        _chunkBuffer = "";
        _expectedChunks = 0;
        _receivedChunks = 0;
        _isReceivingChunks = false;
    }
}
```

#### 4. Implement Notification Callback

```cpp
static void notifyCallback(NimBLERemoteCharacteristic* pChar,
                           uint8_t* pData, size_t length, bool isNotify) {
    String incoming((char*)pData, length);

    // Check for chunked response
    if (incoming.indexOf("\"_c\":") >= 0 &&
        incoming.indexOf("\"_t\":") >= 0 &&
        incoming.indexOf("\"_d\":") >= 0) {
        s_instance->processChunk(incoming);
        return;
    }

    // Non-chunked: check for complete JSON
    s_instance->_rxBuffer += incoming;
    if (isCompleteJson(s_instance->_rxBuffer)) {
        s_instance->_lastResponse = s_instance->_rxBuffer;
        s_instance->_responseReady.store(true);
        s_instance->_rxBuffer = "";
    }
}
```

#### 5. Parse Responses

```cpp
void displayResponse(const String& json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);

    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
    }

    bool success = doc["success"] | false;
    const char* message = doc["message"] | "";
    const char* error = doc["error"] | "";

    if (success) {
        JsonObject data = doc["data"];
        for (JsonPair kv : data) {
            Serial.printf("%s: %s\n", kv.key().c_str(),
                          kv.value().as<String>().c_str());
        }
    } else {
        Serial.printf("Error: %s\n", error);
    }
}
```

### Key Considerations

| Aspect | Recommendation |
|--------|----------------|
| **MTU** | Request 517, expect 255 in practice |
| **Timeouts** | 5-20s for commands, 5s chunk timeout |
| **Buffer size** | 4KB minimum for large responses |
| **Thread safety** | Use atomics for callback→main thread |
| **Escaping** | Always unescape `_d` field content |

---

## Troubleshooting

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| "JSON parse failed: InvalidInput" | Chunks not unescaped | Add `data.replace("\\\"", "\"")` |
| Timeout waiting for response | MTU too low | Check MTU ≥ 255 after exchange |
| Incomplete responses | Chunk timeout | Increase `CHUNK_TIMEOUT_MS` |
| Connection drops | Server 30s timeout | Implement heartbeat (15s) |
| Device not found | Wrong UUID | Verify NUS UUID matches |

### Debug Output

Enable debug logging with prefix filtering:
```
[BLE_TOOL] - Low-level BLE operations
[BRUCE_BLE] - UI/JSON parsing
```

Monitor command:
```bash
platformio device monitor --port COM11 --baud 115200
```

### Verification Test

Send system info command and expect:
```
[BLE_TOOL] Chunk 1/2 received (222 bytes payload, unescaped)
[BLE_TOOL] Chunk 2/2 received (137 bytes payload, unescaped)
[BLE_TOOL] Assembling 2 chunks (359 bytes total)
[BRUCE_BLE] JSON parsed successfully
```

---

## File Reference

### Created Files

| File | Purpose |
|------|---------|
| [pn532_ble_tool.h](../src/modules/rfid/pn532_ble_tool.h) | BLE client class declaration |
| [pn532_ble_tool.cpp](../src/modules/rfid/pn532_ble_tool.cpp) | BLE client implementation |

### Modified Files

| File | Changes |
|------|---------|
| [pn532ble.h](../src/modules/rfid/pn532ble.h) | Added AppModes, tool member |
| [pn532ble.cpp](../src/modules/rfid/pn532ble.cpp) | Dual detection, JSON display, handlers |

### Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| NimBLE-Arduino | 2.3.7 | BLE stack |
| ArduinoJson | 7.4.2 | JSON parsing |
| Arduino ESP32 | 3.3.6 | Framework |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | Feb 3, 2026 | Initial implementation |
| 1.1 | Feb 3, 2026 | Added chunk unescape fix, improved validation |

---

## References

- **ESP32-NFC-Tool API**: See `ESP32-NFC-Tool/docs/ESP32_NFC_TOOL_BLE_API.md`
- **NimBLE-Arduino**: https://github.com/h2zero/NimBLE-Arduino
- **ArduinoJson**: https://arduinojson.org/
