#pragma once

#include <stdint.h>

// Lightweight protocol definition for RF brute force - no heap, no vtable, no std::map
struct BruteProtocol {
    const char *name;
    int bits;
    int zero[2];  // {duration1, duration2} for bit '0'
    int one[2];   // {duration1, duration2} for bit '1'
    int pilot[2]; // {duration1, duration2} for pilot/sync (0,0 = none)
    int stop[2];  // {duration1, duration2} for stop bit  (0,0 = none)
};

// All protocols defined as constexpr data - zero RAM cost, lives in flash
// Timing convention: positive = HIGH, negative = LOW
// Verified against Flipper Zero firmware (dev/lib/subghz/protocols/)
static constexpr BruteProtocol brute_protocols[] = {
    // name               bits  zero              one              pilot              stop
    {"Came 12bit",      12, {-320, 640},  {-640, 320},  {-11520, 320},  {0, 0}     }, // Flipper ✓
    {"Nice 12bit",      12, {-700, 1400}, {-1400, 700}, {-25200, 700},  {0, 0}     }, // Flipper ✓
    {"Ansonic 12bit",   12, {-1111, 555}, {-555, 1111}, {-19425, 555},  {0, 0}     }, // Flipper ✓
    {"Holtek 12bit",    12, {-870, 430},  {-430, 870},  {-15480, 430},  {0, 0}     }, // Flipper ✓
    {"Linear 10bit",    10, {500, -1500}, {1500, -500}, {0, 0},         {1, -21000}}, // Flipper ✓
    {"Hormann 12bit",   12, {500, -1000}, {1000, -500}, {12000, -500},  {0, 0}     }, // Flipper ✓
    {"PhoenixV2 12bit", 12, {-427, 853},  {-853, 427},  {-25620, 2562}, {0, 0}     }, // Flipper ✓
    {"Doitrand 12bit",  12, {-400, 1100}, {-1100, 400}, {-24800, 800},  {0, 0}     }, // Flipper ✓
};

static constexpr int BRUTE_PROTOCOL_COUNT = sizeof(brute_protocols) / sizeof(brute_protocols[0]);

void rf_bruteforce();
