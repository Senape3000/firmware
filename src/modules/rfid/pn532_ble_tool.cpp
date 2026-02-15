#ifndef LITE_VERSION
#include "pn532_ble_tool.h"

extern volatile bool EscPress;

Pn532BleTool *Pn532BleTool::s_instance = nullptr;

namespace {
class ToolClientCallbacks : public NimBLEClientCallbacks {
public:
    explicit ToolClientCallbacks(Pn532BleTool *owner) : _owner(owner) {}

    void onConnect(NimBLEClient *pClient) override { (void)pClient; }

    void onDisconnect(NimBLEClient *pClient, int reason) override {
        (void)pClient;
        (void)reason;
        if (_owner) { _owner->disconnect(); }
    }

private:
    Pn532BleTool *_owner;
};
} // namespace

Pn532BleTool::Pn532BleTool() : _responseReady(false) {
    s_instance = this;
    Serial.println("[BLE_TOOL] Constructor called");
}

Pn532BleTool::~Pn532BleTool() { disconnect(); }

bool Pn532BleTool::searchForDevice() {
    Serial.println("[BLE_TOOL] searchForDevice() - Starting scan...");

    // CRITICAL: Set MTU before init for large JSON responses (>252 bytes)
    // NimBLE requires this to be set before NimBLEDevice::init()
    NimBLEDevice::setMTU(517);
    Serial.println("[BLE_TOOL] Set preferred MTU to 517");

    NimBLEDevice::init("");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->clearResults();

    NimBLEUUID nusServiceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

    Serial.println("[BLE_TOOL] Starting 6s BLE scan...");
    // NOTE: getResults() is blocking, cannot check ESC during scan
    // TODO: Future enhancement - implement async scan with ESC support
    NimBLEScanResults results = scan->getResults(6000, false);

    Serial.printf("[BLE_TOOL] Scan complete. Found %d devices\\n", results.getCount());

    for (int i = 0; i < results.getCount(); i++) {
        const NimBLEAdvertisedDevice *adv = results.getDevice(i);
        std::string name = adv->getName();
        std::string addr = adv->getAddress().toString();
        Serial.printf("[BLE_TOOL] Device %d: %s [%s]\\n", i, name.c_str(), addr.c_str());

        if (adv->isAdvertisingService(nusServiceUUID)) {
            Serial.println("[BLE_TOOL] Found ESP32-NFC-Tool by UUID!");
            _device = *adv;
            _hasDevice = true;
            return true;
        }

        if (!name.empty() && name.find("ESP32-NFC-Tool") != std::string::npos) {
            Serial.println("[BLE_TOOL] Found ESP32-NFC-Tool by name!");
            _device = *adv;
            _hasDevice = true;
            return true;
        }
    }

    Serial.println("[BLE_TOOL] ESP32-NFC-Tool not found");
    return false;
}

bool Pn532BleTool::connect() {
    Serial.println("[BLE_TOOL] connect() - Starting connection...");
    if (!ensureDevice()) {
        Serial.println("[BLE_TOOL] connect() - Failed: no device");
        return false;
    }

    if (!_client) {
        Serial.println("[BLE_TOOL] Creating BLE client...");
        _client = NimBLEDevice::createClient();
        _client->setClientCallbacks(new ToolClientCallbacks(this), false);
    }

    Serial.printf("[BLE_TOOL] Connecting to %s...\n", _device.getAddress().toString().c_str());
    if (!_client->connect(&_device, false)) {
        Serial.println("[BLE_TOOL] connect() - Failed: connection refused");
        return false;
    }

    // CRITICAL: Negotiate MTU for large responses (>252 bytes)
    // Default MTU is 23 bytes (20 payload), we need 512+ for JSON responses
    uint16_t mtu = _client->getMTU();
    Serial.printf("[BLE_TOOL] Initial MTU: %d\n", mtu);

    // Always request MTU exchange to ensure server agrees on larger MTU
    Serial.println("[BLE_TOOL] Requesting MTU exchange...");
    bool mtuOk = _client->exchangeMTU();
    Serial.printf("[BLE_TOOL] MTU exchange %s\n", mtuOk ? "SUCCESS" : "FAILED");
    delay(100); // Give time for MTU exchange to complete
    mtu = _client->getMTU();
    Serial.printf("[BLE_TOOL] Negotiated MTU: %d\n", mtu);

    // CRITICAL: Warn if MTU is too low for reliable communication
    // ESP32-NFC-Tool uses 80% of (MTU-3) as chunk threshold
    // With MTU 23 (default), max payload is only 20 bytes - unusable for JSON
    if (mtu < MIN_USEFUL_MTU) {
        Serial.printf("[BLE_TOOL] WARNING: Low MTU (%d)! Large responses may fail or timeout.\n", mtu);
        Serial.println("[BLE_TOOL] Expected MTU >= 255 for reliable operation.");
    }

    Serial.println("[BLE_TOOL] Connected! Discovering characteristics...");
    return discoverCharacteristics();
}

void Pn532BleTool::disconnect() {
    Serial.println("[BLE_TOOL] disconnect() - Cleaning up connection...");
    if (_client && _client->isConnected()) {
        Serial.println("[BLE_TOOL] Disconnecting BLE client...");
        _client->disconnect();
    }

    _txChar = nullptr;
    _rxChar = nullptr;
    _responseReady.store(false);
    _rxBuffer = "";
    _lastResponse = "";
    Serial.println("[BLE_TOOL] disconnect() - Complete");
}

bool Pn532BleTool::isConnected() const { return _client && _client->isConnected() && _txChar && _rxChar; }

bool Pn532BleTool::reconnectIfNeeded(uint32_t intervalMs) {
    if (isConnected()) { return true; }

    uint32_t now = millis();
    if (now - _lastReconnectAttempt < intervalMs) { return false; }

    _lastReconnectAttempt = now;
    return connect();
}

String Pn532BleTool::getDeviceName() const {
    return _hasDevice ? String(_device.getName().c_str()) : String("");
}

String Pn532BleTool::getDeviceAddress() const {
    return _hasDevice ? String(_device.getAddress().toString().c_str()) : String("");
}

bool Pn532BleTool::getSystemInfo(String &response) {
    return sendCommand("system", "{\"action\":\"info\"}", "sys_info", response, 15000);
}

bool Pn532BleTool::getBtStatus(String &response) {
    return sendCommand("bt", "{\"action\":\"status\"}", "bt_status", response, 15000);
}

bool Pn532BleTool::getHeapInfo(String &response) {
    return sendCommand("system", "{\"action\":\"heap\"}", "sys_heap", response, 10000);
}

bool Pn532BleTool::getDiagnostics(String &response) {
    return sendCommand("system", "{\"action\":\"diag\"}", "sys_diag", response, 15000);
}

bool Pn532BleTool::getWifiStatus(String &response) {
    return sendCommand("wifi", "{\"action\":\"status\"}", "wifi_status", response, 10000);
}

bool Pn532BleTool::restartDevice() {
    String response;
    return sendCommand("system", "{\"action\":\"restart\"}", "sys_restart", response, 5000);
}

bool Pn532BleTool::readSrix(String &response) {
    return sendCommand("nfc", "{\"action\":\"read_srix\",\"timeout\":10}", "nfc_srix", response, 20000);
}

bool Pn532BleTool::saveSrixDump(const String &filename, String &response) {
    String params = "{\"action\":\"save\",\"filename\":\"" + filename + "\",\"protocol\":\"srix\"}";
    return sendCommand("nfc", params, "nfc_save_srix", response, 15000);
}

bool Pn532BleTool::loadSrixDump(const String &filename, String &response) {
    String params = "{\"action\":\"load\",\"filename\":\"" + filename + "\",\"protocol\":\"srix\"}";
    return sendCommand("nfc", params, "nfc_load_srix", response, 15000);
}

bool Pn532BleTool::readMifareUid(String &response) {
    return sendCommand("nfc", "{\"action\":\"mifare_uid\",\"timeout\":5}", "nfc_mf_uid", response, 16000);
}

bool Pn532BleTool::readMifareFull(String &response) {
    return sendCommand("nfc", "{\"action\":\"mifare_read\",\"timeout\":15}", "nfc_mf_full", response, 30000);
}

bool Pn532BleTool::saveMifareDump(const String &filename, String &response) {
    String params = "{\"action\":\"save\",\"filename\":\"" + filename + "\",\"protocol\":\"mifare\"}";
    return sendCommand("nfc", params, "nfc_save_mf", response, 15000);
}

bool Pn532BleTool::loadMifareDump(const String &filename, String &response) {
    String params = "{\"action\":\"load\",\"filename\":\"" + filename + "\",\"protocol\":\"mifare\"}";
    return sendCommand("nfc", params, "nfc_load_mf", response, 15000);
}

bool Pn532BleTool::listFiles(const String &protocol, String &response) {
    String params = "{\"action\":\"list\",\"protocol\":\"" + protocol + "\"}";
    return sendCommand("files", params, "files_list", response, 16000);
}

bool Pn532BleTool::listFiles(String &response) { return listFiles("srix", response); }

bool Pn532BleTool::deleteFile(const String &filename, const String &protocol, String &response) {
    String params =
        "{\"action\":\"delete\",\"filename\":\"" + filename + "\",\"protocol\":\"" + protocol + "\"}";
    return sendCommand("files", params, "files_delete", response, 10000);
}

bool Pn532BleTool::sendHeartbeat() {
    String response;
    bool ok = sendCommand("bt", "{\"action\":\"status\"}", "heartbeat", response, 5000);
    if (ok) { _lastHeartbeat = millis(); }
    return ok;
}

void Pn532BleTool::updateHeartbeat() { _lastHeartbeat = millis(); }

bool Pn532BleTool::isHeartbeatNeeded() const {
    // Heartbeat needed if >15 seconds since last activity (server timeout is 30s)
    return (millis() - _lastHeartbeat) > HEARTBEAT_INTERVAL_MS;
}

bool Pn532BleTool::ensureDevice() {
    if (_hasDevice) { return true; }

    return searchForDevice();
}

bool Pn532BleTool::discoverCharacteristics() {
    if (!_client || !_client->isConnected()) {
        Serial.println("[BLE_TOOL] discoverCharacteristics() - Not connected!");
        return false;
    }

    Serial.println("[BLE_TOOL] Getting NUS service...");
    NimBLERemoteService *service = _client->getService(NimBLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
    if (!service) {
        Serial.println("[BLE_TOOL] NUS service not found!");
        return false;
    }
    Serial.println("[BLE_TOOL] NUS service found!");

    Serial.println("[BLE_TOOL] Getting TX characteristic (notifications)...");
    _txChar = service->getCharacteristic(NimBLEUUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"));

    Serial.println("[BLE_TOOL] Getting RX characteristic (write)...");
    _rxChar = service->getCharacteristic(NimBLEUUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"));

    if (!_txChar || !_rxChar) {
        Serial.printf("[BLE_TOOL] Characteristics missing! TX=%p, RX=%p\n", _txChar, _rxChar);
        return false;
    }
    Serial.println("[BLE_TOOL] Both characteristics found!");

    // Subscribe to notifications on TX characteristic
    Serial.println("[BLE_TOOL] Subscribing to TX notifications...");
    bool subscribed = _txChar->subscribe(true, notifyCallback);
    if (!subscribed) {
        Serial.println("[BLE_TOOL] WARNING: Failed to subscribe to notifications!");
    } else {
        Serial.println("[BLE_TOOL] Successfully subscribed to notifications!");
    }

    // Small delay to ensure subscription is active
    delay(50);

    return subscribed;
}

bool Pn532BleTool::sendCommand(
    const String &cmd, const String &params, const String &id, String &response, uint32_t timeoutMs
) {
    Serial.printf(
        "[BLE_TOOL] sendCommand() - cmd=%s, id=%s, timeout=%lums\n", cmd.c_str(), id.c_str(), timeoutMs
    );

    // Critical: Check connection BEFORE attempting any operation
    if (!isConnected()) {
        Serial.println("[BLE_TOOL] sendCommand() - Not connected, attempting reconnect...");
        if (!connect()) {
            Serial.println("[BLE_TOOL] sendCommand() - Failed: cannot connect");
            return false;
        }
    }

    // Double-check connection after potential reconnect
    if (!_client || !_client->isConnected()) {
        Serial.println("[BLE_TOOL] sendCommand() - Failed: connection lost before send");
        return false;
    }

    resetResponseState();

    String payload = buildJsonCommand(cmd, params, id);
    Serial.printf("[BLE_TOOL] Sending JSON: %s\n", payload.c_str());

    std::string data(payload.c_str());
    if (!_rxChar->writeValue(reinterpret_cast<const uint8_t *>(data.data()), data.size(), true)) {
        Serial.println("[BLE_TOOL] sendCommand() - Failed: write failed");
        return false;
    }

    Serial.println("[BLE_TOOL] Command sent, waiting for response...");

    uint32_t start = millis();
    uint32_t lastProgress = 0;
    uint32_t lastChunkCheck = millis();

    while (!_responseReady.load() && (millis() - start) < timeoutMs) {
        // Critical: Check ESC key to allow user to cancel
        if (EscPress) {
            Serial.println("[BLE_TOOL] sendCommand() - Cancelled by ESC key");
            resetChunkState(); // Clean up any partial chunks
            return false;
        }

        // Check if connection died during wait
        if (_client && !_client->isConnected()) {
            Serial.println("[BLE_TOOL] sendCommand() - Connection lost during wait");
            resetChunkState(); // Clean up any partial chunks
            return false;
        }

        // CRITICAL: Check for stalled chunk transfer
        // If we're receiving chunks but none arrived in CHUNK_TIMEOUT_MS, abort
        if (_isReceivingChunks && _lastChunkTime > 0) {
            uint32_t chunkAge = millis() - _lastChunkTime;
            if (chunkAge > CHUNK_TIMEOUT_MS) {
                Serial.printf(
                    "[BLE_TOOL] ERROR: Chunk transfer stalled! Last chunk %lums ago, received %d/%d\n",
                    chunkAge,
                    _receivedChunks,
                    _expectedChunks
                );
                resetChunkState();
                return false;
            }
        }

        // Check for incomplete JSON - if chunks are arriving, keep waiting longer
        if (millis() - lastChunkCheck >= 1000) {
            if (_lastChunkTime > 0 && (millis() - _lastChunkTime) < 500) {
                // Last chunk arrived recently, chunks are still flowing
                Serial.printf(
                    "[BLE_TOOL] Chunks still arriving (last: %lums ago), extending wait...\n",
                    millis() - _lastChunkTime
                );
            }
            lastChunkCheck = millis();
        }

        // Progress debug every 500ms
        if (millis() - lastProgress >= 500) {
            Serial.printf(
                "[BLE_TOOL] Waiting... %lums / %lums (buffer: %d bytes)\n",
                millis() - start,
                timeoutMs,
                _rxBuffer.length()
            );
            lastProgress = millis();
        }

        delay(10);
        yield();
    }

    if (!_responseReady.load()) {
        Serial.printf(
            "[BLE_TOOL] sendCommand() - Timeout after %lums. Buffer state: %d bytes, last chunk: %lums ago\n",
            timeoutMs,
            _rxBuffer.length(),
            (millis() - _lastChunkTime)
        );
        // Clear incomplete buffer
        if (!_rxBuffer.isEmpty()) {
            Serial.println("[BLE_TOOL] Incomplete JSON in buffer (showing first 200 chars):");
            Serial.println(_rxBuffer.substring(0, 200));
        }
        _rxBuffer = "";
        _lastChunkTime = 0;
        return false;
    }

    Serial.printf("[BLE_TOOL] Response received! Length: %d bytes\n", _lastResponse.length());
    response = _lastResponse;
    return true;
}

String Pn532BleTool::buildJsonCommand(const String &cmd, const String &params, const String &id) const {
    String json = "{";
    json += "\"cmd\":\"" + cmd + "\"";
    if (params.length() > 0) { json += ",\"params\":" + params; }
    if (id.length() > 0) { json += ",\"id\":\"" + id + "\""; }
    json += "}";
    return json;
}

void Pn532BleTool::resetResponseState() {
    Serial.println("[BLE_TOOL] resetResponseState()");
    _rxBuffer = "";
    _lastResponse = "";
    _lastChunkTime = 0;
    _responseReady.store(false);
    resetChunkState();
}

void Pn532BleTool::resetChunkState() {
    // Explicit chunk state cleanup - call this on errors/cancellation
    if (_isReceivingChunks) {
        Serial.printf(
            "[BLE_TOOL] Abandoning incomplete chunk transfer (%d/%d received)\n",
            _receivedChunks,
            _expectedChunks
        );
    }
    _chunkBuffer = "";
    _expectedChunks = 0;
    _receivedChunks = 0;
    _isReceivingChunks = false;
}

bool Pn532BleTool::isChunkedResponse(const String &data) const {
    // Check if this is a chunked response envelope: {"_c":N,"_t":M,"_d":"..."}
    return data.indexOf("\"_c\":") >= 0 && data.indexOf("\"_t\":") >= 0 && data.indexOf("\"_d\":") >= 0;
}

void Pn532BleTool::processChunkedData(const String &chunk) {
    // Parse chunk envelope to extract _c, _t, _d
    // Simple parsing without ArduinoJson to save memory

    int cStart = chunk.indexOf("\"_c\":") + 5;
    int cEnd = chunk.indexOf(",", cStart);
    if (cStart < 5 || cEnd < 0) {
        Serial.println("[BLE_TOOL] ERROR: Malformed chunk - missing _c field");
        return;
    }
    int currentChunk = chunk.substring(cStart, cEnd).toInt();

    int tStart = chunk.indexOf("\"_t\":") + 5;
    int tEnd = chunk.indexOf(",", tStart);
    if (tEnd < 0) tEnd = chunk.indexOf("}", tStart);
    if (tStart < 5 || tEnd < 0) {
        Serial.println("[BLE_TOOL] ERROR: Malformed chunk - missing _t field");
        return;
    }
    int totalChunks = chunk.substring(tStart, tEnd).toInt();

    int dStart = chunk.indexOf("\"_d\":\"") + 6;
    int dEnd = chunk.lastIndexOf("\"");
    if (dStart < 6 || dEnd <= dStart) {
        Serial.println("[BLE_TOOL] ERROR: Malformed chunk - missing or invalid _d field");
        return;
    }
    String data = chunk.substring(dStart, dEnd);

    // CRITICAL: Unescape JSON string content from _d field
    // Server sends: {"_d":"{\"success\":true,...}"}
    // IMPORTANT: Order matters! Must unescape \\\\ BEFORE \\\"
    // Example: \\\" should become " (not \")
    //   Step 1: \\\\ -> \\ (no match in this case)
    //   Step 2: \\\" -> \"  (wrong if done first!)
    // Correct order:
    //   Step 1: \\\\ -> \\
    //   Step 2: \\\" -> "
    data.replace("\\\\", "\x01"); // Temp placeholder to avoid double processing
    data.replace("\\\"", "\"");   // Unescape quotes
    data.replace("\x01", "\\");   // Restore actual backslashes

    Serial.printf(
        "[BLE_TOOL] Chunk %d/%d received (%d bytes payload, unescaped)\n",
        currentChunk,
        totalChunks,
        data.length()
    );

    if (!_isReceivingChunks) {
        // First chunk of a new chunked response
        if (currentChunk != 1) {
            Serial.printf(
                "[BLE_TOOL] WARNING: First chunk received is %d, not 1 - resetting\n", currentChunk
            );
        }
        _isReceivingChunks = true;
        _expectedChunks = totalChunks;
        _receivedChunks = 0;
        _chunkBuffer = "";
        // Pre-allocate buffer based on expected size
        // ESP32-NFC-Tool sends ~217 bytes per chunk with MTU 255
        _chunkBuffer.reserve(totalChunks * 250);
    } else {
        // Validate we're in the same transfer
        if (totalChunks != _expectedChunks) {
            Serial.printf(
                "[BLE_TOOL] WARNING: Chunk says %d total but expected %d - using new value\n",
                totalChunks,
                _expectedChunks
            );
            _expectedChunks = totalChunks;
        }
    }

    // Validate chunk sequence
    if (currentChunk != _receivedChunks + 1) {
        Serial.printf(
            "[BLE_TOOL] WARNING: Expected chunk %d, got %d (possible gap)\n",
            _receivedChunks + 1,
            currentChunk
        );
        // Continue anyway - we can't request retransmission
        // Gap handling: if chunk is ahead, we've lost data but continue
    }

    _chunkBuffer += data;
    _receivedChunks = currentChunk;
    _lastChunkTime = millis();

    Serial.printf(
        "[BLE_TOOL] Chunk buffer: %d bytes, %d/%d chunks\n",
        _chunkBuffer.length(),
        _receivedChunks,
        _expectedChunks
    );

    // Check if all chunks received
    if (_receivedChunks >= _expectedChunks) { assembleChunkedResponse(); }
}

void Pn532BleTool::assembleChunkedResponse() {
    Serial.printf(
        "[BLE_TOOL] Assembling %d chunks (%d bytes total)\n", _expectedChunks, _chunkBuffer.length()
    );

    _lastResponse = _chunkBuffer;
    _responseReady.store(true);

    // Reset chunking state
    _chunkBuffer = "";
    _expectedChunks = 0;
    _receivedChunks = 0;
    _isReceivingChunks = false;
    _lastChunkTime = 0;

    Serial.println("[BLE_TOOL] Chunked response assembled and ready!");
}

void Pn532BleTool::notifyCallback(
    NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify
) {
    (void)pRemoteCharacteristic;
    (void)isNotify;

    if (!s_instance || !pData || length == 0) {
        Serial.println("[BLE_TOOL] notifyCallback() - Invalid parameters");
        return;
    }

    Serial.printf("[BLE_TOOL] notifyCallback() - Received %d bytes\n", length);

    String incoming;
    incoming.reserve(length);
    for (size_t i = 0; i < length; i++) { incoming += static_cast<char>(pData[i]); }

    // Check for short responses (debugging)
    if (length < 100) {
        Serial.printf("[BLE_TOOL] Data: %s\n", incoming.c_str());
    } else {
        Serial.printf("[BLE_TOOL] Data: %.100s... (truncated)\n", incoming.c_str());
    }

    s_instance->_lastChunkTime = millis();

    // ============================================
    // CHUNKED RESPONSE DETECTION
    // ============================================
    // Server sends chunked data as: {"_c":1,"_t":3,"_d":"...data..."}
    // We need to reassemble all chunks before processing

    if (s_instance->isChunkedResponse(incoming)) {
        Serial.println("[BLE_TOOL] Detected chunked response envelope");
        s_instance->processChunkedData(incoming);
        return; // processChunkedData will set _responseReady when complete
    }

    // ============================================
    // NON-CHUNKED RESPONSE (normal flow)
    // ============================================

    // Buffer overflow protection (4KB max)
    if (s_instance->_rxBuffer.length() + incoming.length() > 4096) {
        Serial.println("[BLE_TOOL] ERROR: Buffer overflow! Clearing buffer.");
        s_instance->_rxBuffer = "";
        s_instance->_lastChunkTime = 0;
        return;
    }

    s_instance->_rxBuffer += incoming;
    Serial.printf("[BLE_TOOL] Buffer size: %d bytes\n", s_instance->_rxBuffer.length());

    // JSON completeness check (balanced braces)
    int depth = 0;
    bool hasOpenBrace = false;
    for (size_t i = 0; i < s_instance->_rxBuffer.length(); i++) {
        char c = s_instance->_rxBuffer[i];
        if (c == '{') {
            depth++;
            hasOpenBrace = true;
        }
        if (c == '}') depth--;
    }

    Serial.printf("[BLE_TOOL] JSON depth: %d, hasOpenBrace: %d\n", depth, hasOpenBrace);

    // Complete JSON detected
    if (depth == 0 && hasOpenBrace && s_instance->_rxBuffer.startsWith("{")) {
        Serial.println("[BLE_TOOL] Complete JSON received!");

        s_instance->_lastResponse = s_instance->_rxBuffer;
        s_instance->_responseReady.store(true);
        s_instance->_rxBuffer = "";
        s_instance->_lastChunkTime = 0;

        Serial.printf("[BLE_TOOL] Response ready! (%d bytes)\n", s_instance->_lastResponse.length());
    } else if (depth < 0) {
        // Malformed JSON - too many closing braces
        Serial.println("[BLE_TOOL] ERROR: Malformed JSON (negative depth). Clearing buffer.");
        s_instance->_rxBuffer = "";
        s_instance->_lastChunkTime = 0;
    } else if (depth > 0) {
        // Incomplete JSON - waiting for more data
        Serial.printf("[BLE_TOOL] Incomplete JSON (depth=%d), waiting...\n", depth);
    }
}

#endif
