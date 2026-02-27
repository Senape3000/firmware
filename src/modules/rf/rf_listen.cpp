#include "rf_listen.h"

#include "../others/audio.h"

volatile unsigned long lastMicros = 0;
volatile unsigned long pulseMicros = 0;
volatile float ___frequency = 0;
volatile unsigned long pulseDuration = 0;
volatile bool newPulse = false;

void IRAM_ATTR onPulse() {
    static bool wasHigh = false;
    unsigned long now = micros();

    if (digitalRead(bruceConfigPins.CC1101_bus.io0)) {
        pulseDuration = now - lastMicros;
        ___frequency = 1000000.0 / pulseDuration;
        newPulse = true;
        wasHigh = true;
    } else if (wasHigh) {
        lastMicros = now;
        wasHigh = false;
    }
}

void rf_listen() {
    float freq = 433.92;
    float last_freq = -1;
    bool redraw = true;
    while (!check(SelPress) && !check(EscPress)) {
        if (check(PrevPress)) { freq -= 0.1f; }
        if (check(NextPress)) { freq += 0.1f; }

        freq = constrain(freq, 300.0f, 928.0f);
        if (freq != last_freq) {
            redraw = true;
            last_freq = freq;
        } else {
            redraw = false;
        }

        if (redraw) {
            String text = String("Frequency: ") + String(freq, 2) + String("MHz");
            displayRedStripe(text, getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor);
        }

        delay(50);
    }

    if (check(EscPress)) return;

    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Listener needs a CC1101!", true);
        return;
    }
    if (!initRfModule("rx", freq)) {
        displayError("CC1101 not found!", true);
        return;
    }

    ELECHOUSE_cc1101.setRxBW(58);
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setDcFilterOff(true);
    attachInterrupt(digitalPinToInterrupt(bruceConfigPins.CC1101_bus.io0), onPulse, CHANGE);
    displayRedStripe("Listening...", getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor);

    unsigned long lastPulseTime = millis();
    bool pulseActive = false;
    bool showingWaiting = false;
    bool showingNoSignal = false;

    // Wait for any previous button press to be released
    delay(300);

    while (!check(EscPress) && !check(SelPress)) {
        if (newPulse) {
            newPulse = false;
            lastPulseTime = millis();
            pulseActive = true;
            showingWaiting = false;
            showingNoSignal = false;
            String pulseText = String("Freq: ") + String(___frequency, 2) + String(" Hz");
            displayRedStripe(pulseText, getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor);
#if defined(BUZZ_PIN)
            tone(BUZZ_PIN, (unsigned int)___frequency, pulseDuration);
#elif defined(HAS_NS4168_SPKR)
            playTone(___frequency, pulseDuration, 0);
#endif
        } else if (pulseActive && millis() - lastPulseTime > 3000) {
            pulseActive = false;
            if (!showingNoSignal) {
                showingNoSignal = true;
                showingWaiting = false;
                displayRedStripe(
                    "No signal", getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor
                );
            }
        } else if (!pulseActive && !showingWaiting) {
            showingWaiting = true;
            displayRedStripe(
                "Waiting for a pulse", getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor
            );
        }

        delay(20);
    }

    detachInterrupt(digitalPinToInterrupt(bruceConfigPins.CC1101_bus.io0));
    deinitRfModule();
}
