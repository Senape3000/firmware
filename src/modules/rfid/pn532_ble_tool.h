#ifndef __PN532_BLE_TOOL_H__
#define __PN532_BLE_TOOL_H__

#ifndef LITE_VERSION
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

class Pn532BleTool {
public:
    Pn532BleTool();
    ~Pn532BleTool();

    // Connection management
    bool searchForDevice();
    bool connect();
    void disconnect();
    bool isConnected() const;
    bool reconnectIfNeeded(uint32_t intervalMs = 3000);

    String getDeviceName() const;
    String getDeviceAddress() const;

    // System commands
    bool getSystemInfo(String &response);
    bool getBtStatus(String &response);
    bool getHeapInfo(String &response);
    bool getDiagnostics(String &response);
    bool getWifiStatus(String &response);
    bool restartDevice();

    // SRIX commands
    bool readSrix(String &response);
    bool saveSrixDump(const String &filename, String &response);
    bool loadSrixDump(const String &filename, String &response);

    // Mifare commands
    bool readMifareUid(String &response);
    bool readMifareFull(String &response);
    bool saveMifareDump(const String &filename, String &response);
    bool loadMifareDump(const String &filename, String &response);

    // File management
    bool listFiles(const String &protocol, String &response);
    bool listFiles(String &response); // Default: SRIX
    bool deleteFile(const String &filename, const String &protocol, String &response);

    // Heartbeat for connection keep-alive
    bool sendHeartbeat();
    void updateHeartbeat();
    bool isHeartbeatNeeded() const;

private:
    bool ensureDevice();
    bool discoverCharacteristics();
    bool sendCommand(
        const String &cmd, const String &params, const String &id, String &response, uint32_t timeoutMs = 5000
    );
    String buildJsonCommand(const String &cmd, const String &params, const String &id) const;
    void resetResponseState();
    void resetChunkState(); // Explicit chunk cleanup for errors/cancellation
    void processChunkedData(const String &chunk);
    bool isChunkedResponse(const String &data) const;
    void assembleChunkedResponse();

    static void notifyCallback(
        NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify
    );

    NimBLEAdvertisedDevice _device;
    bool _hasDevice = false;

    NimBLEClient *_client = nullptr;
    NimBLERemoteCharacteristic *_txChar = nullptr;
    NimBLERemoteCharacteristic *_rxChar = nullptr;

    String _rxBuffer;
    String _lastResponse;
    std::atomic<bool> _responseReady;
    uint32_t _lastChunkTime = 0; // Track last chunk arrival for timeout detection
    uint32_t _lastReconnectAttempt = 0;
    uint32_t _lastHeartbeat = 0; // Track last heartbeat for keep-alive

    // Chunked response tracking
    String _chunkBuffer;             // Accumulates _d data from chunks
    int _expectedChunks = 0;         // Total chunks expected (_t)
    int _receivedChunks = 0;         // Chunks received so far
    bool _isReceivingChunks = false; // Currently in chunked transfer

    // Constants
    static constexpr uint32_t CHUNK_TIMEOUT_MS = 5000;       // Max 5s between chunks
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 15000; // 15s heartbeat (server timeout is 30s)
    static constexpr uint16_t MIN_USEFUL_MTU = 50;           // Warn if MTU below this

    static Pn532BleTool *s_instance;
};

#endif
#endif
