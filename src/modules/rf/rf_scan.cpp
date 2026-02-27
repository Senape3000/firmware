#include "rf_scan.h"
#include "core/led_control.h"
#include "core/sd_functions.h"
#include "core/type_convertion.h"
#include "rf_debug.h"
#include "rf_send.h"
#include <globals.h>
#include <sstream>

RFScan::RFScan() { setup(); }

RFScan::~RFScan() {
    rf_rmt_rx_deinit();
    deinitRfModule();
}

void RFScan::setup() {
    if (!initRfModule("rx", bruceConfigPins.rfFreq)) { return; }

    RCSwitch_Enable_Receive();

    if (bruceConfigPins.rfScanRange < 0 || bruceConfigPins.rfScanRange > 3) {
        bruceConfigPins.setRfScanRange(3);
    }
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) { bruceConfigPins.setRfFxdFreq(1); }

    display_info(received, signals, ReadRAW, codesOnly, autoSave, title);

    if (bruceConfigPins.rfFxdFreq) frequency = bruceConfigPins.rfFreq;

    // Clear cache for RAW signal
    returnToMenu = false;
    restartScan = false;

    return loop();
}

void RFScan::loop() {
    while (1) {
        if (check(EscPress) || returnToMenu) return;
        if (check(NextPress)) {
            select_menu_option();
            if (returnToMenu) return;
            return setup();
        }
        if (restartScan) return setup();

        if (bruceConfigPins.rfFxdFreq) frequency = bruceConfigPins.rfFreq;
        if (frequency <= 0) init_freqs();

        while (frequency <= 0) { // FastScan
            if (check(EscPress) || returnToMenu) return;
            if (check(NextPress)) {
                select_menu_option();
                if (returnToMenu) return;
                return setup();
            }

            if (fast_scan()) return setup(); // frequency found, reset
        }

        // Check RAW accumulation timeout: after collecting frames for 500 ms,
        // finalize the accumulated signal and display/save it.
        if (_rawAccumulating && (millis() - _rawAccumStart >= 500)) {
            _rawAccumulating = false;
            RF_DBG_SCAN("RAW accumulation complete: data=%u chars", received.data.length());
            display_info(received, signals, ReadRAW, codesOnly, autoSave, title);
            if (autoSave && (lastSavedKey != received.key || received.key == 0)) save_signal();
        }

        // Poll for RF signal via RMT (non-blocking)
        if (rf_rmt_rx_receive(50)) {
            size_t symCount = rf_rmt_rx_symbol_count();

            // Noise filter: skip captures with too few transitions (e.g. CC1101
            // mode-switch glitch that produces 1-2 symbols of garbage).
            if (symCount < 4) {
                RF_DBG_SCAN("noise: only %u symbols — skipping", (unsigned)symCount);
                rf_rmt_rx_restart();
                continue;
            }

            RF_DBG_SCAN(
                "signal received! mode=%s, symCount=%u", ReadRAW ? "RAW" : "Decode", (unsigned)symCount
            );

            if (!ReadRAW) {
                // === DECODE MODE with deduplication ===
                RfDecodeResult dr = rf_rmt_rx_decode();
                if (dr.valid) {
                    RF_DBG_SCAN(
                        "decoded: proto=%u, value=0x%llX, bits=%u, TE=%u",
                        dr.protocolId,
                        dr.value,
                        dr.bitLength,
                        dr.pulseLength
                    );
                    // Suppress identical signals within cooldown window
                    // (one button press = one signal, not 5-10 repeated frames)
                    unsigned long now = millis();
                    if (dr.value == _lastDecodeValue && dr.protocolId == _lastDecodeProto &&
                        (now - _lastDecodeTime) < DECODE_COOLDOWN_MS) {
                        RF_DBG_SCAN("  suppressed (duplicate within %lu ms cooldown)", DECODE_COOLDOWN_MS);
                    } else {
                        _lastDecodeValue = dr.value;
                        _lastDecodeProto = dr.protocolId;
                        _lastDecodeTime = now;
                        read_rcswitch();
                        if (autoSave && (lastSavedKey != received.key || received.key == 0)) save_signal();
                    }
                }
                // Decode failed — discard in decode-only mode
                else {
                    RF_DBG_SCAN("decode FAILED — discarding (decode-only mode)");
                }
            } else {
                // === RAW MODE with frame accumulation ===
                // Collects all frames from one button press into a single signal.
                // First frame triggers read_raw(); subsequent frames within a 500 ms
                // window are silently appended to received.data with synthetic gaps.
                if (!_rawAccumulating) {
                    // First frame — process normally, start accumulation window
                    int prevSignals = signals;
                    read_raw();
                    if (signals > prevSignals) {
                        _rawAccumStart = millis();
                        _rawAccumulating = true;
                        // Auto-save deferred until accumulation completes
                    }
                } else {
                    // Additional frame — append silently to accumulated data
                    received.data += " -15000 ";
                    received.data += rf_rmt_rx_get_raw_string();
                    RF_DBG_SCAN(
                        "RAW accum: +%u symbols (data=%u chars)", (unsigned)symCount, received.data.length()
                    );
                }
            }
            rf_rmt_rx_restart(); // Re-arm for next frame
        }
    }
}

void RFScan::RCSwitch_Enable_Receive() {
    gpio_num_t rxPin = (bruceConfigPins.rfModule == CC1101_SPI_MODULE)
                           ? gpio_num_t(bruceConfigPins.CC1101_bus.io0)
                           : gpio_num_t(bruceConfigPins.rfRx);
    rf_rmt_rx_init(rxPin);
}

void RFScan::init_freqs() {
    for (int i = 0; i < _MAX_TRIES; i++) {
        _freqs[i].freq = 433.92;
        _freqs[i].rssi = -75;
    }
    _try = 0;
}

bool RFScan::fast_scan() {

    if (idx < range_limits[bruceConfigPins.rfScanRange][0] ||
        idx > range_limits[bruceConfigPins.rfScanRange][1]) {
        idx = range_limits[bruceConfigPins.rfScanRange][0];
    }
    float checkFrequency = subghz_frequency_list[idx];
    setMHZ(checkFrequency);
    tft.drawPixel(0, 0, 0); // To make sure CC1101 shared with TFT works properly
    vTaskDelay(5 / portTICK_PERIOD_MS);
    rssi = ELECHOUSE_cc1101.getRssi();
    if (rssi > rssiThreshold) {
        _freqs[_try].freq = checkFrequency;
        _freqs[_try].rssi = rssi;
        _try++;
        if (_try >= _MAX_TRIES) {
            int max_index = 0;
            for (int i = 1; i < _MAX_TRIES; ++i) {
                if (_freqs[i].rssi > _freqs[max_index].rssi) { max_index = i; }
            }

            bruceConfigPins.setRfFreq(_freqs[max_index].freq, 2); // change to fixed frequency
            frequency = _freqs[max_index].freq;
            setMHZ(frequency);
            RF_DBG_SCAN(
                "freq scan: FOUND %.2f MHz (rssi=%d, attempt=%d)", frequency, _freqs[max_index].rssi, _try
            );
            Serial.println("Frequency Found: " + String(frequency));
            // When changing to fixed frequency, need to restart the module to reset the registers
            // so we get good signal reception at this frequency
            rf_rmt_rx_deinit();
            deinitRfModule();

            return true;
        }
    }
    ++idx;
    return false;
}

void keeloq_identify(RfCodes &instance) {
    RF_DBG_SCAN(
        "keeloq_identify: fix=0x%08lX, encrypted=0x%08lX, serial=0x%07lX, btn=%u",
        instance.fix,
        instance.encrypted,
        instance.serial,
        instance.btn
    );
    FS *fs = NULL;

    if (!getFsStorage(fs)) { return; }

    KeeloqKeystore keystore{fs};

    for (const auto &key : keystore.get_keys()) {
        switch (key.type) {
            case KEELOQ_SIMPLE_LEARNING: {
                uint64_t decrypt = keeloq_decrypt(instance.encrypted, key.key);
                RF_DBG_SCAN(
                    "  trying simple key '%s': decrypt=0x%08lX", key.mf_name.c_str(), (uint32_t)decrypt
                );

                if (instance.keeloq_check_decrypt(decrypt)) {
                    instance.mf_name = key.mf_name;
                    instance.hop = decrypt;

                    return;
                }

                break;
            }

            case KEELOQ_NORMAL_LEARNING: {
                uint64_t man = keeloq_normal_learning(instance.fix, key.key);
                uint64_t decrypt = keeloq_decrypt(instance.encrypted, man);
                RF_DBG_SCAN(
                    "  trying normal key '%s': man=0x%llX, decrypt=0x%08lX",
                    key.mf_name.c_str(),
                    man,
                    (uint32_t)decrypt
                );

                if (instance.mf_name == "Centurion") {
                    if (instance.keeloq_check_decrypt_centurion(decrypt)) {
                        instance.hop = decrypt;

                        return;
                    }
                }

                if (instance.keeloq_check_decrypt(decrypt)) {
                    instance.mf_name = key.mf_name;
                    instance.hop = decrypt;
                    RF_DBG_SCAN("  MATCH! mf='%s' (normal learning)", key.mf_name.c_str());
                    return;
                }

                break;
            }
        }
    }
    RF_DBG_SCAN("  keeloq_identify: no key matched");
}

void RFScan::read_rcswitch() {
    RF_DBG_SCAN("read_rcswitch: decoding...");
    received.fix = 0;
    received.hop = 0;
    received.btn = 0;
    received.cnt = 0;
    received.mf_name = "Unknown";
    received.encrypted = 0;

    // Decode already succeeded before this is called (checked in loop())
    RfDecodeResult decoded = rf_rmt_rx_decode();

    if (decoded.valid && decoded.value) {
        Serial.println("RcSwitch signal captured");
        RF_DBG_SCAN(
            "read_rcswitch: CAPTURED proto=%u, value=0x%llX, bits=%u, TE=%u",
            decoded.protocolId,
            decoded.value,
            decoded.bitLength,
            decoded.pulseLength
        );
        blinkLed();
        ++signals;
        found_freq = frequency;
        received.frequency = long(frequency * 1000000);
        received.key = decoded.value;
        received.preset = String(decoded.protocolId);
        received.protocol = "RcSwitch";
        received.te = decoded.pulseLength;
        received.Bit = decoded.bitLength;
        received.filepath = "signal_" + String(signals);
        received.data = "";

        // Build RAW data string from symbols for potential RAW replay
        received.data = rf_rmt_rx_get_raw_string();
        RF_DBG_SCAN("  raw data length: %u chars", received.data.length());

        if (decoded.protocolId == PRESET_KEELOQ) {
            uint64_t yek = reverse_bits(decoded.value, 64);
            RF_DBG_SCAN("  KeeLoq detected: reversed=0x%llX", yek);

            received.fix = yek >> 32;
            received.btn = received.fix >> 28;
            received.encrypted = yek & 0xFFFFFFFF;
            received.serial = (yek >> 32) & 0xFFFFFFF;
            RF_DBG_SCAN(
                "  KeeLoq fields: fix=0x%08lX, btn=%u, serial=0x%07lX, encrypted=0x%08lX",
                received.fix,
                received.btn,
                received.serial,
                received.encrypted
            );

            keeloq_identify(received);
        }

        frequency = 0;
        display_info(received, signals, ReadRAW, codesOnly, autoSave, title);
    }
}

void RFScan::read_raw() {
    // Add RAW data (& decoded data if any) to the RCCode
    found_freq = frequency;

    // Get raw timings from RMT symbols
    const rmt_symbol_word_t *symbols = rf_rmt_rx_symbols();
    size_t symCount = rf_rmt_rx_symbol_count();
    RF_DBG_SCAN("read_raw: %u symbols from RMT", (unsigned)symCount);
    auto timingsVec = rf_rmt_rx_symbols_to_timings(symbols, symCount);

    // Also try protocol decode
    RfDecodeResult decoded = rf_rmt_rx_decode();

    int transitions = 0;
    String _data = "";
    std::vector<int> durations;
    std::vector<int> indexed_durations;
    uint8_t repetition = 0;

    received.te = 0;

    received.fix = 0;
    received.hop = 0;
    received.btn = 0;
    received.cnt = 0;
    received.mf_name = "Unknown";
    received.encrypted = 0;

    for (transitions = 0; transitions < (int)timingsVec.size(); transitions++) {
        if (transitions > 0) _data += " ";
        int duration = timingsVec[transitions];
        if (duration < -5000 && repetition < 2) { repetition += 1; }
        _data += String(duration);
        if (received.te == 0 && duration > 0) received.te = duration;

        if (!decoded.valid && repetition == 1 && duration >= -5000) {
            int index = find_pulse_index(indexed_durations, duration);
            if (index == -1) {
                indexed_durations.push_back(abs(duration));
                index = indexed_durations.size() - 1;
            }
            durations.push_back(index); // Store indexes for CRC calculation
        }
    }

    received.data = _data;
    received.filepath = "signal_" + String(signals);
    received.frequency = long(frequency * 1000000);

    // if there is a value decoded, show it
    if (decoded.valid && decoded.value) {
        Serial.println("RcSwitch signal captured");
        RF_DBG_SCAN(
            "read_raw: DECODED proto=%u, value=0x%llX, bits=%u, TE=%u, transitions=%d",
            decoded.protocolId,
            decoded.value,
            decoded.bitLength,
            decoded.pulseLength,
            transitions
        );
        blinkLed();
        ++signals;
        received.key = decoded.value;
        received.preset = String(decoded.protocolId);
        received.protocol = "RcSwitch";
        received.indexed_durations = {};
        received.te = decoded.pulseLength;
        received.Bit = decoded.bitLength;

        if (decoded.protocolId == PRESET_KEELOQ) {
            uint64_t yek = reverse_bits(decoded.value, 64);

            received.fix = yek >> 32;
            received.btn = received.fix >> 28;
            received.encrypted = yek & 0xFFFFFFFF;
            received.serial = (yek >> 32) & 0xFFFFFFF;

            keeloq_identify(received);
        }

        frequency = 0;
        display_info(received, signals, ReadRAW, codesOnly, autoSave, title);
    }
    // if there is no value decoded, but we calculated a CRC, show it
    else if (repetition >= 2 && !durations.empty()) {
        Serial.println("Raw signal captured");
        RF_DBG_SCAN(
            "read_raw: RAW with CRC — transitions=%d, repetitions=%u, unique_durations=%u",
            transitions,
            repetition,
            (unsigned)indexed_durations.size()
        );
        blinkLed();
        ++signals;
        received.preset = "0";
        received.protocol = "RAW";
        received.key = crc64_ecma(durations); // Calculate CRC-64
        received.indexed_durations = indexed_durations;
        received.Bit = durations.size();
        frequency = 0;
        display_info(received, signals, ReadRAW, codesOnly, autoSave, title);
    }
    // If there is no decoded value and no CRC calculated, only show the data when specified
    else if (!codesOnly) {
        Serial.println("Raw data captured");
        RF_DBG_SCAN(
            "read_raw: unidentified signal — transitions=%d, repetitions=%u", transitions, repetition
        );
        blinkLed();
        ++signals;
        received.preset = "0";
        received.protocol = "RAW";
        received.key = 0;
        received.indexed_durations = {};
        received.Bit = 0;
        frequency = 0;
        display_info(received, signals, ReadRAW, codesOnly, autoSave, title);
    }
}

void RFScan::select_menu_option() {
    rf_rmt_rx_deinit(); // stop RMT RX before entering menu

    options = {};

    if (received.protocol != "") options.emplace_back("Replay", [this]() { set_option(REPLAY); });
    if (received.data != "" && received.protocol != "RAW")
        options.emplace_back("Replay as RAW", [this]() { set_option(REPLAY_RAW); });

    if (received.protocol != "") options.emplace_back("Save Signal", [this]() { set_option(SAVE); });
    if (received.data != "" && received.protocol != "RAW")
        options.emplace_back("Save as RAW", [this]() { set_option(SAVE_RAW); });

    if (received.protocol != "") options.emplace_back("Reset Signal", [this]() { set_option(RESET); });

    if (bruceConfigPins.rfModule == CC1101_SPI_MODULE)
        options.emplace_back("Range", [this]() { set_option(RANGE); });
    if (bruceConfigPins.rfModule == CC1101_SPI_MODULE && !bruceConfigPins.rfFxdFreq)
        options.emplace_back("Threshold", [this]() { set_option(THRESHOLD); });

    if (ReadRAW)
        options.emplace_back("Mode = RAW", [&]() {
            ReadRAW = false;
            return select_menu_option();
        });
    else
        options.emplace_back("Mode = Decode", [&]() {
            ReadRAW = true;
            return select_menu_option();
        });

    if (ReadRAW && codesOnly)
        options.emplace_back("Filter = Code", [&]() {
            codesOnly = false;
            return select_menu_option();
        });
    else if (ReadRAW)
        options.emplace_back("Filter = All", [&]() {
            codesOnly = true;
            return select_menu_option();
        });

    if (autoSave)
        options.emplace_back("Save = Auto", [&]() {
            autoSave = false;
            return select_menu_option();
        });
    else
        options.emplace_back("Save = Manual", [&]() {
            autoSave = true;
            return select_menu_option();
        });

    options.emplace_back("Close Menu", [this]() { set_option(CLOSE_MENU); });
    options.emplace_back("Main Menu", [this]() { set_option(MAIN_MENU); });

    loopOptions(options);
}

void RFScan::set_option(RFMenuOption option) {
    switch (option) {
        case REPLAY:
        case REPLAY_RAW: replay_signal(option == REPLAY_RAW); break;

        case SAVE:
        case SAVE_RAW: save_signal(option == SAVE_RAW); break;

        case RANGE: rf_range_selection(); break; // using a common function to other features
        case RESET: reset_signals(); break;
        case THRESHOLD: set_threshold(); break;

        case CLOSE_MENU: break;

        case MAIN_MENU: returnToMenu = true; return;
    }

    restartScan = true;
    deinitRfModule();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void RFScan::replay_signal(bool asRaw) {
    String actualProtocol = received.protocol;
    if (asRaw) { received.protocol = "RAW"; }
    displayTextLine("Sending..");
    sendRfCommand(received);
    addToRecentCodes(received);
    received.protocol = actualProtocol;

    if (received.fix != 0 && !asRaw) { received.keeloq_step(1); }
}

void RFScan::save_signal(bool asRaw) {
    asRaw = asRaw || received.protocol == "RAW";
    Serial.println(asRaw ? "RCSwitch_SaveSignal RAW true" : "RCSwitch_SaveSignal RAW false");
    decimalToHexString(received.key, hexString);
    RCSwitch_SaveSignal(found_freq, received, asRaw, hexString, autoSave);
    lastSavedKey = received.key;
}

void RFScan::reset_signals() {
    received.Bit = 0;
    received.data = "";
    received.key = 0;
    received.preset = "";
    received.protocol = "";
    signals = 0;
    received.fix = 0;
    received.hop = 0;
    received.btn = 0;
    received.cnt = 0;
    received.mf_name = "Unknown";
    received.encrypted = 0;
}

void RFScan::set_threshold() {
    options = {
        {"(-55) More Accurate", [&]() { rssiThreshold = -55; }},
        {"(-60)",               [&]() { rssiThreshold = -60; }},
        {"(-65) Default ",      [&]() { rssiThreshold = -65; }},
        {"(-70)",               [&]() { rssiThreshold = -70; }},
        {"(-75)",               [&]() { rssiThreshold = -75; }},
        {"(-80) Less Accurate", [&]() { rssiThreshold = -80; }},
    };
    loopOptions(options);
}
/*
// Using similar function from rf_utils.h
void RFScan::set_range() {
    bool chooseFixedOpt = false;

    options = {
        {String("Fxd [" + String(bruceConfigPins.rfFreq) + "]").c_str(),
         [=]() { bruceConfigPins.setRfScanRange(bruceConfigPins.rfScanRange, 1); } },
        {"Choose Fxd",                                                   [&]() { chooseFixedOpt = true; } },
        {subghz_frequency_ranges[0],                                     [=]() {
bruceConfigPins.setRfScanRange(0); }}, {subghz_frequency_ranges[1],                                     [=]()
{ bruceConfigPins.setRfScanRange(1); }}, {subghz_frequency_ranges[2], [=]() {
bruceConfigPins.setRfScanRange(2); }}, {subghz_frequency_ranges[3],                                     [=]()
{ bruceConfigPins.setRfScanRange(3); }},
    };

    loopOptions(options);

    if (chooseFixedOpt) { // Range
        options.clear();
        int ind = 0;
        int arraySize = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);
        for (int i = 0; i < arraySize; i++) {
            String tmp = String(subghz_frequency_list[i], 2) + "Mhz";
            options.emplace_back(tmp.c_str(), [=]() { bruceConfigPins.rfFreq = subghz_frequency_list[i]; });
            if (int(frequency * 100) == int(subghz_frequency_list[i] * 100)) ind = i;
        }
        loopOptions(options, ind);
        options.clear();
        bruceConfigPins.setRfScanRange(bruceConfigPins.rfScanRange, 1);
    }

    if (bruceConfigPins.rfFxdFreq) displayTextLine("Scan freq set to " + String(bruceConfigPins.rfFreq));
    else displayTextLine("Range set to " + String(subghz_frequency_ranges[bruceConfigPins.rfScanRange]));
}
*/
void display_info(RfCodes received, int signals, bool ReadRAW, bool codesOnly, bool autoSave, String title) {
    if (title != "") drawMainBorderWithTitle(title);
    else drawMainBorder();

    if (received.protocol != "") display_signal_data(received);

    tft.setTextColor(getColorVariation(bruceConfig.priColor), bruceConfig.bgColor);

    if (!ReadRAW) padprintln("Recording: Only RCSwitch codes.");
    else if (codesOnly) padprintln("Recording: RAW with CRC or RCSwitch.");
    else padprintln("Recording: Any RAW signal.");

    if (autoSave) padprintln("Auto save: Enabled");

    if (bruceConfigPins.rfFxdFreq) padprintln("Scanning: " + String(bruceConfigPins.rfFreq) + " MHz");
    else padprintln("Scanning: " + String(subghz_frequency_ranges[bruceConfigPins.rfScanRange]));

    padprintln("Total signals found: " + String(signals));

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    padprintln("");
    padprintln("Press [NEXT] for options.");
}

void display_signal_data(RfCodes received) {
    std::string txt = received.data.c_str();
    std::stringstream ss(txt);
    std::string palavra;
    int transitions = 0;
    char hexString[64];

    while (ss >> palavra) transitions++;

    if (received.preset != "") {
        if (received.fix != 0) {
            padprintln("Protocol: KeeLoq");
        } else padprintln("Protocol: " + String(received.protocol) + "(" + received.preset + ")");
    } else padprintln("Protocol: " + String(received.protocol));

    if (received.key > 0) {
        decimalToHexString(received.key, hexString);
        if (received.protocol == "RAW") {
            padprintln("Length: " + String(received.Bit) + " transitions");
            // tft.setCursor(tft.getCursorX(), tft.getCursorY() + 2);
            padprintln("Record length: " + String(transitions) + " transitions");
        } else {
            if (received.fix == 0) {
                padprintln("Length: " + String(received.Bit) + " bits");
                const char *b = dec2binWzerofill(received.key, min(received.Bit, 40));
                // tft.setCursor(tft.getCursorX(), tft.getCursorY() + 2);
                padprintln("Binary: " + String(b));
            }
        }
    } else {
        strcpy(hexString, "No code identified");
        padprintln("Length: No code identified");
        padprintln("Record length: " + String(transitions) + " transitions");
    }

    if (received.protocol == "RAW") padprintln("CRC: " + String(hexString));
    else {
        if (received.fix != 0) {
            padprintln("Manufacturer: " + received.mf_name);

            decimalToHexString(received.serial, hexString);
            padprintln("Serial: " + String(hexString));

            padprintln("Btn: " + String(received.btn));

            decimalToHexString(received.fix, hexString);
            padprintln("Fix: " + String(hexString));

            if (received.mf_name != "Unknown") {
                decimalToHexString(received.hop, hexString);
                padprintln("Hop: " + String(hexString));

                padprintln("Counter: " + String(received.cnt));
            } else {
                decimalToHexString(received.encrypted, hexString);
                padprintln("Encrypted: " + String(hexString));
            }
        } else {
            padprintln("Key: " + String(hexString));
        }
    }

    // if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
    //     int rssi = ELECHOUSE_cc1101.getRssi();
    //     tft.drawPixel(0, 0, 0);
    //     padprintln("Rssi: " + String(rssi));
    // }

    // if (!received.indexed_durations.empty()) {
    //     padprint("PulseLenghts: ");
    //     for (int i = 0; i < received.indexed_durations.size(); i++) {
    //         if (i < received.indexed_durations.size() - 1)
    //             tft.print(String(received.indexed_durations[i]) + "us, ");
    //         else tft.println(String(received.indexed_durations[i]) + "us");
    //     }
    // } else if (received.te) padprintln("PulseLenght: " + String(received.te) + "us");
    // else padprintln("PulseLenght: unknown");

    // padprintln("Frequency: " + String(received.frequency) + " Hz");
    padprintln("");
}

bool RCSwitch_SaveSignal(float frequency, RfCodes codes, bool raw, char *key, bool autoSave) {
    FS *fs;
    String filename = "";

    if (!getFsStorage(fs)) {
        displayError("No space left on device", true);
        return false;
    }

    if (!codes.key && codes.data == "") {
        Serial.println("Empty signal, it was not saved.");
        return false;
    }

    String subfile_out = "Filetype: Bruce SubGhz File\nVersion 1\n";
    subfile_out += "Frequency: " + String(int(frequency * 1000000)) + "\n";
    if (!raw) {
        subfile_out += "Preset: " + String(codes.preset) + "\n";
        subfile_out += "Protocol: RcSwitch\n";
        subfile_out += "Bit: " + String(codes.Bit) + "\n";
        if (codes.hop != 0) {
            subfile_out += "Manufacturer: " + String(codes.mf_name) + "\n";
            char hexString[64] = {0};

            decimalToHexString(codes.serial, hexString);

            subfile_out += "Serial: " + String(hexString) + "\n";
            subfile_out += "Button: " + String(codes.btn) + "\n";
            subfile_out += "Counter: " + String(codes.cnt) + "\n";
        } else {
            subfile_out += "Key: " + String(key) + "\n";
        }
        subfile_out += "TE: " + String(codes.te) + "\n";
        filename = "rcs.sub";
        // subfile_out += "RAW_Data: " + codes.data;
    } else {
        // save as raw
        if (codes.preset == "1") {
            codes.preset = "FuriHalSubGhzPresetOok270Async";
        } else if (codes.preset == "2") {
            codes.preset = "FuriHalSubGhzPresetOok650Async";
        }

        subfile_out += "Preset: " + String(codes.preset) + "\n";
        subfile_out += "Protocol: RAW\n";
        subfile_out += "RAW_Data: " + codes.data;
        filename = "raw.sub";
    }

    String filepath = "/BruceRF";
    if (autoSave) filepath += "/autoSaved";
    File file = createNewFile(fs, filepath, filename);

    if (file) {
        file.println(subfile_out);
        if (!autoSave) displaySuccess(file.path());
    } else {
        displayError("Error saving file", true);
    }

    file.close();
    return true;
}

String rf_scan(float start_freq, float stop_freq, int max_loops) {
    // derived from https://github.com/mcore1976/cc1101-tool/blob/main/cc1101-tool-esp32.ino#L480

    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("rf scanning is available with CC1101 only", true);
        return ""; // only CC1101 is supported for this
    }
    if (!initRfModule("rx", start_freq)) return "";

    ELECHOUSE_cc1101.setRxBW(256);

    float settingf1 = start_freq;
    float settingf2 = stop_freq;
    float freq = 0;
    long compare_freq = 0;
    float mark_freq;
    int rssi;
    int mark_rssi = -100;
    String out = "";

    while (max_loops || !check(EscPress)) {
        vTaskDelay(1 / portTICK_PERIOD_MS);
        max_loops -= 1;

        setMHZ(freq);

        rssi = ELECHOUSE_cc1101.getRssi();
        if (rssi > -75) {
            if (rssi > mark_rssi) {
                mark_rssi = rssi;
                mark_freq = freq;
            };
        };

        freq += 0.01;

        if (freq > settingf2) {
            freq = settingf1;

            if (mark_rssi > -75) {
                long fr = mark_freq * 100;
                if (fr == compare_freq) {
                    Serial.print(F("\r\nSignal found at  "));
                    Serial.print(F("Freq: "));
                    Serial.print(mark_freq);
                    Serial.print(F(" Rssi: "));
                    Serial.println(mark_rssi);
                    out += String(mark_freq) + ",";
                    mark_rssi = -100;
                    compare_freq = 0;
                    mark_freq = 0;
                } else {
                    compare_freq = mark_freq * 100;
                    freq = mark_freq - 0.10;
                    mark_freq = 0;
                    mark_rssi = -100;
                };
            };
        }; // end of IF freq>stop frequency
    }; // End of While

    deinitRfModule();
    return out;
}

String RCSwitch_Read(float frequency, int max_loops, bool raw, bool headless) {
    RfCodes received;

    if (!frequency) frequency = bruceConfigPins.rfFreq; // default from config

    char hexString[64];

RestartRec:
    if (!headless) {
        drawMainBorder();
        tft.setCursor(10, 28);
        tft.setTextSize(FP);
        tft.println("Waiting for a " + String(frequency) + " MHz " + "signal.");
    }

    // init receive
    if (!initRfModule("rx", frequency)) return "";
    gpio_num_t rxPin = (bruceConfigPins.rfModule == CC1101_SPI_MODULE)
                           ? gpio_num_t(bruceConfigPins.CC1101_bus.io0)
                           : gpio_num_t(bruceConfigPins.rfRx);
    if (!rf_rmt_rx_init(rxPin)) {
        Serial.println("rf_rmt_rx_init failed");
        return "";
    }
    Serial.println("RMT RX enableReceive()");

    while (!check(EscPress)) {
        // Poll for signal (non-blocking with short timeout)
        if (rf_rmt_rx_receive(100)) {
            // Try protocol decode
            RfDecodeResult decoded = rf_rmt_rx_decode();

            if (decoded.valid && decoded.value) {
                received.frequency = long(frequency * 1000000);
                received.key = decoded.value;
                received.protocol = "RcSwitch";
                received.preset = String(decoded.protocolId);
                received.te = decoded.pulseLength;
                received.Bit = decoded.bitLength;
                received.filepath = "unsaved";

                // Build raw data string from symbols
                received.data = rf_rmt_rx_get_raw_string();

                decimalToHexString(received.key, hexString);

                if (!headless) display_info(received, 1, raw);
            }

            if (raw && !decoded.valid) {
                // No protocol match — try as RAW
                auto timingsVec = rf_rmt_rx_symbols_to_timings(rf_rmt_rx_symbols(), rf_rmt_rx_symbol_count());
                int transitions = (int)timingsVec.size();

                if (transitions > 20) {
                    received.data = "";
                    for (int i = 0; i < transitions; i++) {
                        if (i > 0) received.data += " ";
                        received.data += String(timingsVec[i]);
                    }
                    received.frequency = long(frequency * 1000000);
                    received.protocol = "RAW";
                    received.preset = "0";
                    received.filepath = "unsaved";
                    if (!headless) display_info(received, 1, raw);
                } else {
                    received.data = ""; // too few transitions - discard
                }
            }

            rf_rmt_rx_restart();
        }

        if (received.key > 0 ||
            received.data.length() > 20) { // RAW data does not have "key", 20 is more than 5 transitions
            // switch to raw mode if decoding failed
            if (received.preset == 0) {
                Serial.println("signal decoding failed, switching to RAW mode");
                raw = true;
            }
            String subfile_out = "Filetype: Bruce SubGhz File\nVersion 1\n";
            subfile_out += "Frequency: " + String(int(frequency * 1000000)) + "\n";
            if (!raw) {
                subfile_out += "Preset: " + String(received.preset) + "\n";
                subfile_out += "Protocol: RcSwitch\n";
                subfile_out += "Bit: " + String(received.Bit) + "\n";
                subfile_out += "Key: " + String(hexString) + "\n";
                subfile_out += "TE: " + String(received.te) + "\n";
            } else {
                // save as raw
                if (received.preset == "1") received.preset = "FuriHalSubGhzPresetOok270Async";
                else if (received.preset == "2") received.preset = "FuriHalSubGhzPresetOok650Async";
                subfile_out += "Preset: " + String(received.preset) + "\n";
                subfile_out += "Protocol: RAW\n";
                subfile_out += "RAW_Data: " + received.data;
            }
            rf_rmt_rx_deinit();
            // headless mode
            return subfile_out;
        }
        if (max_loops > 0) {
            // headless mode, quit if nothing received after max_loops
            vTaskDelay(1000 / portTICK_PERIOD_MS); // wait first, THEN check
            max_loops -= 1;
            if (max_loops == 0) {
                // Use sentinel -1: loop runs one more iteration to catch signals
                // that arrived during vTaskDelay before giving up
                max_loops = -1;
            }
        } else if (max_loops == -1) {
            // Final check already done in this iteration - truly timed out
            Serial.println("timeout");
            rf_rmt_rx_deinit();
            return "";
        }
    }
Exit:
    vTaskDelay(1 / portTICK_PERIOD_MS);

    rf_rmt_rx_deinit();
    deinitRfModule();

    return "";
}
