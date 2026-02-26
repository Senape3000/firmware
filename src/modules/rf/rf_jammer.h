#ifndef __RF_JAMMER_H__
#define __RF_JAMMER_H__

#include <Arduino.h>

enum JamMode : uint8_t { JAM_FULL, JAM_INTERMITTENT };
enum JamFreqMode : uint8_t { JAM_SINGLE_FREQ, JAM_FREQ_GROUP };
enum JamFreqBand : uint8_t { JAM_BAND_300, JAM_BAND_433, JAM_BAND_868 };

// Entry point from RFMenu — shows Jammer submenu
void rf_jammerMenu();

class RFJammer {
public:
    // hopIntervalMs: time between freq hops in group mode (CC1101 only)
    // freqBand: which frequency group to hop (CC1101 + group mode only)
    RFJammer(JamMode mode, JamFreqMode freqMode, uint32_t hopIntervalMs, JamFreqBand freqBand);
    ~RFJammer();

private:
    static const uint32_t MAX_JAM_TIME_MS = 30000; // 30s hard cap, not user-configurable
    static const uint32_t MAX_SEQUENCE = 50;
    static const uint32_t DURATION_CYCLES = 3;
    static const uint8_t MAX_FREQ_GROUP = 8;

    JamMode jamMode;
    JamFreqMode freqMode;
    JamFreqBand freqBand;   // which group to use in group mode
    uint32_t hopIntervalMs; // runtime-configurable hop interval
    int nTransmitterPin = -1;
    bool sendRF = false;
    bool useCC1101 = false;
    bool timedOut = false; // true when MAX_JAM_TIME_MS reached

    // Frequency hopping state
    float freqGroup[MAX_FREQ_GROUP];
    uint8_t freqGroupSize = 0;
    uint8_t currentFreqIdx = 0;
    uint32_t lastHopTime = 0;

    bool init();
    void run();
    void run_full_jammer();
    void run_itmt_jammer();
    void send_optimized_pulse(int width);
    void send_random_pattern(int numPulses);
    void buildFreqGroup();
    bool hopToNextFreq();
    bool checkAbort(uint32_t startTime, uint32_t &lastCheckTime, uint32_t interval);
    void display_jamming_screen();
    void display_complete(bool aborted);
};

#endif
