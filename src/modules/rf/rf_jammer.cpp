#include "rf_jammer.h"
#include "core/display.h"
#include "core/settings.h"
#include "rf_utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// ── Session-persistent settings (reset on reboot) ────────────────
static JamFreqMode s_freqMode = JAM_SINGLE_FREQ;
static JamFreqBand s_freqBand = JAM_BAND_433; // default group: 433 MHz
static uint32_t s_hopIntervalMs = 500;        // default hop interval

// ── Band info ────────────────────────────────────────────────────
struct BandInfo {
    const char *label; // shown in menu / display
    const float *freqs;
    uint8_t count;
};
static const float FREQ_GROUP_300[] = {300.00f, 303.875f, 315.00f, 318.00f, 330.00f, 345.00f};
static const float FREQ_GROUP_433[] = {418.00f, 433.075f, 433.92f, 434.42f, 438.90f};
static const float FREQ_GROUP_868[] = {868.35f, 868.95f, 915.00f, 925.00f};
static const BandInfo BAND_INFO[] = {
    {"300 MHz band", FREQ_GROUP_300, sizeof(FREQ_GROUP_300) / sizeof(float)},
    {"433 MHz band", FREQ_GROUP_433, sizeof(FREQ_GROUP_433) / sizeof(float)},
    {"868/915 MHz",  FREQ_GROUP_868, sizeof(FREQ_GROUP_868) / sizeof(float)},
};
static const uint8_t BAND_COUNT = 3;

// ════════════════════════════════════════════════════════════════
//  Menu entry point — called from RFMenu
// ════════════════════════════════════════════════════════════════
void rf_jammerMenu() {
    bool isCC1101 = (bruceConfigPins.rfModule == CC1101_SPI_MODULE);

    // Loop so the user can adjust settings and then launch
    while (true) {
        int action = 0;

        // ── Build dynamic labels ──────────────────────────────────
        String freqLabel;
        if (!isCC1101) {
            freqLabel = ""; // not shown
        } else if (s_freqMode == JAM_SINGLE_FREQ) {
            freqLabel = "Freq: Single [" + String(bruceConfigPins.rfFreq, 2) + " MHz]";
        } else {
            freqLabel = String("Freq: Group [") + BAND_INFO[s_freqBand].label + "]";
        }

        String hopLabel = "Hop: " + String(s_hopIntervalMs) + " ms";

        // ── Hop interval options ──────────────────────────────────
        static const uint32_t HOP_OPTS[] = {100, 200, 500, 1000};
        static const char *const HOP_LABELS[] = {"100 ms", "200 ms", "500 ms", "1000 ms"};
        static const uint8_t HOP_OPTS_COUNT = 4;

        // ── Build menu ────────────────────────────────────────────
        options.clear();
        options.push_back({"Full Jammer", [&]() { action = 1; }});
#if !defined(LITE_VERSION)
        options.push_back({"Intermittent Jammer", [&]() { action = 2; }});
#endif
        if (isCC1101) {
            options.push_back({freqLabel.c_str(), [&]() { action = 3; }});
            if (s_freqMode == JAM_FREQ_GROUP) {
                options.push_back({hopLabel.c_str(), [&]() { action = 4; }});
            }
        }
        options.push_back({"Back", [&]() { action = 0; }});

        loopOptions(options, MENU_TYPE_SUBMENU, "RF Jammer");
        options.clear();

        if (returnToMenu || action == 0) return;

        // ── Freq mode config ─────────────────────────────────────
        if (action == 3) {
            // Step 1: Single vs Group
            int freqAction = 0;
            options.clear();
            options.push_back({"Single Frequency", [&]() { freqAction = 1; }, s_freqMode == JAM_SINGLE_FREQ});
            options.push_back({"Frequency Group", [&]() { freqAction = 2; }, s_freqMode == JAM_FREQ_GROUP});
            loopOptions(options);
            options.clear();
            if (returnToMenu) return;

            if (freqAction == 1) {
                s_freqMode = JAM_SINGLE_FREQ;
                setRFFreqMenu(); // Pick exact frequency
                if (returnToMenu) return;
            } else if (freqAction == 2) {
                s_freqMode = JAM_FREQ_GROUP;
                // Step 2: pick which band
                options.clear();
                for (uint8_t i = 0; i < BAND_COUNT; i++) {
                    uint8_t idx = i;
                    options.push_back(
                        {BAND_INFO[i].label,
                         [idx]() { s_freqBand = (JamFreqBand)idx; },
                         s_freqBand == (JamFreqBand)i}
                    );
                }
                loopOptions(options);
                options.clear();
                if (returnToMenu) return;
            }
            continue; // Back to jammer menu with updated label
        }

        // ── Hop timing config ─────────────────────────────────────
        if (action == 4) {
            options.clear();
            for (uint8_t i = 0; i < HOP_OPTS_COUNT; i++) {
                uint32_t val = HOP_OPTS[i];
                options.push_back(
                    {HOP_LABELS[i], [val]() { s_hopIntervalMs = val; }, s_hopIntervalMs == val}
                );
            }
            loopOptions(options);
            options.clear();
            if (returnToMenu) return;
            continue; // Back to jammer menu with updated label
        }

        // ── Launch jammer ─────────────────────────────────────────
        JamMode mode = (action == 1) ? JAM_FULL : JAM_INTERMITTENT;
        RFJammer(mode, s_freqMode, s_hopIntervalMs, s_freqBand);
        return; // Return to RFMenu after jamming ends
    }
}

// ════════════════════════════════════════════════════════════════
//  Constructor / Destructor
// ════════════════════════════════════════════════════════════════
RFJammer::RFJammer(JamMode mode, JamFreqMode fMode, uint32_t hopMs, JamFreqBand band)
    : jamMode(mode), freqMode(fMode), freqBand(band), hopIntervalMs(hopMs) {
    if (init()) { run(); }
}

RFJammer::~RFJammer() { deinitRfModule(); }

// ════════════════════════════════════════════════════════════════
//  Initialization — RF module setup + CC1101 best practices
// ════════════════════════════════════════════════════════════════
bool RFJammer::init() {
    useCC1101 = (bruceConfigPins.rfModule == CC1101_SPI_MODULE);
    nTransmitterPin = bruceConfigPins.rfTx;

    if (!initRfModule("tx")) {
        displayError("RF module init failed", true);
        return false;
    }

    if (useCC1101) {
        nTransmitterPin = bruceConfigPins.CC1101_bus.io0;
        // initRfModule already sets: PA=12 (+12dBm max), ASK/OOK, async serial
        // Disable DC blocking filter — not needed for TX, reduces overhead
        ELECHOUSE_cc1101.setDcFilterOff(1);

        if (freqMode == JAM_FREQ_GROUP) {
            buildFreqGroup();
            if (freqGroupSize < 2) {
                freqMode = JAM_SINGLE_FREQ; // not enough freqs → fallback
            }
        }
    } else {
        freqMode = JAM_SINGLE_FREQ; // Simple TX pin: single freq only
    }

    return true;
}

// ════════════════════════════════════════════════════════════════
//  Build frequency group — uses the user-selected band
// ════════════════════════════════════════════════════════════════
void RFJammer::buildFreqGroup() {
    uint8_t idx = (uint8_t)freqBand;
    if (idx >= BAND_COUNT) idx = 1; // safety: default to 433
    const BandInfo &bi = BAND_INFO[idx];
    freqGroupSize = min(bi.count, MAX_FREQ_GROUP);
    memcpy(freqGroup, bi.freqs, freqGroupSize * sizeof(float));
    currentFreqIdx = 0;
}

// ════════════════════════════════════════════════════════════════
//  Hop to next frequency (CC1101 + group mode only)
// ════════════════════════════════════════════════════════════════
bool RFJammer::hopToNextFreq() {
    if (!useCC1101 || freqMode != JAM_FREQ_GROUP || freqGroupSize < 2) return false;

    uint32_t now = millis();
    if (now - lastHopTime < hopIntervalMs) return false;
    lastHopTime = now;

    currentFreqIdx = (currentFreqIdx + 1) % freqGroupSize;

    // CC1101 datasheet: must be IDLE before changing frequency
    ELECHOUSE_cc1101.setSidle();
    setMHZ(freqGroup[currentFreqIdx]);
    ELECHOUSE_cc1101.SetTx();
    pinMode(nTransmitterPin, OUTPUT); // Re-assert GPIO direction after mode change

    return true;
}

// ════════════════════════════════════════════════════════════════
//  Abort check — ESC key OR 30s hard cap
// ════════════════════════════════════════════════════════════════
bool RFJammer::checkAbort(uint32_t startTime, uint32_t &lastCheckTime, uint32_t interval) {
    uint32_t now = millis();
    if (now - lastCheckTime < interval) return false;
    lastCheckTime = now;

    if (now - startTime >= MAX_JAM_TIME_MS) {
        timedOut = true;
        sendRF = false;
        return true;
    }
    if (check(EscPress)) {
        sendRF = false;
        returnToMenu = true;
        return true;
    }
    return false;
}

// ════════════════════════════════════════════════════════════════
//  Display helpers
// ════════════════════════════════════════════════════════════════
void RFJammer::display_jamming_screen() {
    const char *modeStr = (jamMode == JAM_FULL) ? "Full" : "Intermittent";
    const char *freqStr = (freqMode == JAM_FREQ_GROUP) ? "Group" : "Single";

    drawMainBorderWithTitle("RF Jammer");
    printSubtitle(String(modeStr) + " | " + String(freqStr));
    padprintln("");

    if (freqMode == JAM_SINGLE_FREQ) {
        padprintln("Freq: " + String(bruceConfigPins.rfFreq, 2) + " MHz");
    } else {
        padprintln(String("Group: ") + BAND_INFO[freqBand].label + " (" + String(freqGroupSize) + " freqs)");
        padprintln("Hop every " + String(hopIntervalMs) + " ms");
    }
    padprintln("Max: " + String(MAX_JAM_TIME_MS / 1000) + "s");
    padprintln("");

    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    padprintln(">>> JAMMING <<<");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);
    padprintln("Press [ESC] to stop.");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

void RFJammer::display_complete(bool aborted) {
    drawMainBorderWithTitle("RF Jammer");
    padprintln("");

    if (aborted) {
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        padprintln("Stopped by user.");
    } else {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        padprintln("Jamming complete.");
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln("Press any key...");

    while (!check(AnyKeyPress)) { delay(50); }
}

// ════════════════════════════════════════════════════════════════
//  Main run dispatcher
// ════════════════════════════════════════════════════════════════
void RFJammer::run() {
    sendRF = true;
    timedOut = false;
    lastHopTime = millis();

    display_jamming_screen();

    if (jamMode == JAM_FULL) run_full_jammer();
    else run_itmt_jammer();

    // Safety: ensure TX pin is LOW on any exit path
    digitalWrite(nTransmitterPin, LOW);

    // timedOut=true → time cap reached → "complete"; false → ESC → "stopped"
    display_complete(!timedOut);
}

// ════════════════════════════════════════════════════════════════
//  Full Jammer — continuous carrier + micro-glitches
// ════════════════════════════════════════════════════════════════
void RFJammer::run_full_jammer() {
    digitalWrite(nTransmitterPin, HIGH);
    uint32_t startTime = millis();
    uint32_t lastCheckTime = startTime;

    while (sendRF) {
        // 1 µs glitch every ~100 µs to spread spectrum
        if (micros() % 100 < 2) {
            digitalWrite(nTransmitterPin, LOW);
            delayMicroseconds(1);
            digitalWrite(nTransmitterPin, HIGH);
        }

        // 5 µs glitch every ~500 ms
        uint32_t now = millis();
        if (now % 500 < 5) {
            digitalWrite(nTransmitterPin, LOW);
            delayMicroseconds(5);
            digitalWrite(nTransmitterPin, HIGH);
        }

        hopToNextFreq();

        if (checkAbort(startTime, lastCheckTime, 100)) break;
    }
    digitalWrite(nTransmitterPin, LOW);
}

// ════════════════════════════════════════════════════════════════
//  Intermittent Jammer — sequenced pulse widths + random bursts
// ════════════════════════════════════════════════════════════════
void RFJammer::run_itmt_jammer() {
    uint32_t startTime = millis();
    uint32_t lastCheckTime = startTime;

    // Pre-compute sequence (10µs … 500µs)
    uint32_t seq[MAX_SEQUENCE];
    for (uint32_t i = 0; i < MAX_SEQUENCE; i++) seq[i] = 10 * (i + 1);

    while (sendRF) {
        for (uint32_t s = 0; s < MAX_SEQUENCE && sendRF; s++) {
            for (uint32_t d = 0; d < DURATION_CYCLES && sendRF; d++) {
                send_optimized_pulse(seq[s]);
                hopToNextFreq();
                if (checkAbort(startTime, lastCheckTime, 50)) break;
            }
        }
        if (sendRF) send_random_pattern(100);
    }
    digitalWrite(nTransmitterPin, LOW);
}

// ════════════════════════════════════════════════════════════════
//  Optimized pulse: HIGH burst with micro-gaps, then LOW gap
// ════════════════════════════════════════════════════════════════
void RFJammer::send_optimized_pulse(int width) {
    for (int i = 0; i < width; i += 10) {
        digitalWrite(nTransmitterPin, HIGH);
        delayMicroseconds(5);
        if (i % 20 == 0) {
            digitalWrite(nTransmitterPin, LOW);
            delayMicroseconds(2);
        }
        digitalWrite(nTransmitterPin, HIGH);
        delayMicroseconds(3);
    }
    digitalWrite(nTransmitterPin, LOW);

    // LOW gap proportional to pulse width
    uint32_t gap = (uint32_t)width + (width % 23);
    delayMicroseconds(gap);
}

// ════════════════════════════════════════════════════════════════
//  Random pattern burst (max 100 ms)
// ════════════════════════════════════════════════════════════════
void RFJammer::send_random_pattern(int numPulses) {
    uint32_t burstStart = millis();
    for (int i = 0; i < numPulses && sendRF; i++) {
        uint32_t pw = 5 + (millis() % 46);
        digitalWrite(nTransmitterPin, HIGH);
        delayMicroseconds(pw);
        digitalWrite(nTransmitterPin, LOW);
        delayMicroseconds(5 + (micros() % 96));
        if (millis() - burstStart > 100) break;
    }
}
