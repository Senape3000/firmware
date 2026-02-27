/**
 * @file rf_protocols.h
 * @brief Unified RF protocol definitions for the RMT-based RF subsystem.
 *
 * This file replaces the RCSwitch library's internal `proto[]` array and the
 * OOP-based `protocols/*.h` class hierarchy with a single constexpr table that
 * lives entirely in flash (zero RAM cost).
 *
 * Protocol timings are expressed as **multipliers** of the base pulse length
 * (TE), exactly matching the RCSwitch convention.  At runtime the actual
 * microsecond durations are: `duration_us = multiplier * pulseLength`.
 *
 * The protocol IDs (1-22) are 1:1 with the original RCSwitch `proto[]` array
 * so that .sub files written with `Preset: <N>` remain backward-compatible.
 * Protocol 23 is reserved for KeeLoq (handled by a dedicated decoder).
 *
 * Extracted from: bmorcelli/rc-switch  (RCSwitch.cpp, commit as of 2026-02)
 * Cross-referenced: Flipper Zero firmware (dev/lib/subghz/protocols/)
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 1
 */

#ifndef RF_PROTOCOLS_H
#define RF_PROTOCOLS_H

#include <cstdint>

// ---------------------------------------------------------------------------
// Encoding types
// ---------------------------------------------------------------------------
enum class RfEncodingType : uint8_t {
    PulseDistance, ///< Fixed mark, variable space (most common OOK)
    PulseWidth,    ///< Variable mark, fixed space (e.g. Hormann)
    Manchester,    ///< Biphase encoding (e.g. Marantec in Flipper)
    TriState,      ///< 3-symbol (Princeton / SMC5326 — uses tri[])
    KeeLoq,        ///< 66-bit KeeLoq preamble + PulseDistance body
};

// ---------------------------------------------------------------------------
// HighLow — multiplier pair (same semantics as RCSwitch::HighLow)
// ---------------------------------------------------------------------------
struct RfHighLow {
    uint8_t high; ///< HIGH duration = high * pulseLength µs
    uint8_t low;  ///< LOW  duration = low  * pulseLength µs
};

// ---------------------------------------------------------------------------
// RfProtocolDef — one row of the protocol table   (28 bytes, flash-resident)
// ---------------------------------------------------------------------------
/**
 * Describes a single RCSwitch-compatible OOK protocol.
 *
 * Timing semantics (identical to RCSwitch):
 *   - When `invertedSignal == false`:
 *       A HighLow {H, L} means: HIGH for H*TE, then LOW for L*TE.
 *   - When `invertedSignal == true`:
 *       A HighLow {H, L} means: LOW for H*TE, then HIGH for L*TE.
 *
 * The sync (syncFactor) is transmitted **after** the data bits in RCSwitch
 * convention (it acts as a long gap / separator between repetitions).
 */
struct RfProtocolDef {
    const char *name;        ///< Human-readable label (flash string)
    uint8_t id;              ///< 1-based ID matching RCSwitch proto[] index
    RfEncodingType encoding; ///< Encoding family
    uint16_t pulseLength;    ///< Base timing element (TE) in µs
    RfHighLow syncFactor;    ///< Sync/gap pulse (multipliers of TE)
    RfHighLow zero;          ///< Bit '0' waveform (multipliers of TE)
    RfHighLow one;           ///< Bit '1' waveform (multipliers of TE)
    bool invertedSignal;     ///< Swap HIGH/LOW logic levels
};

// ---------------------------------------------------------------------------
// Protocol table  — exact copy of bmorcelli/rc-switch proto[] (22 entries)
// plus KeeLoq placeholder (id=23).
//
// IMPORTANT: the indices here are 0-based; the `id` field is 1-based.
// ---------------------------------------------------------------------------
static constexpr RfProtocolDef rf_protocols[] = {
    // -----------------------------------------------------------------------
    // id  1 — SC5262 / HX2262 / PT2262 / EV1527 / RT1527 / FP1527 / HS2303
    // -----------------------------------------------------------------------
    {"Proto1(PT2262)",        1,  RfEncodingType::PulseDistance, 350, {1, 31},  {1, 3},  {3, 1},  false},

    // -----------------------------------------------------------------------
    // id  2 — SC5272
    // -----------------------------------------------------------------------
    {"Proto2(SC5272)",        2,  RfEncodingType::PulseDistance, 650, {1, 10},  {1, 2},  {2, 1},  false},

    // -----------------------------------------------------------------------
    // id  3 — Intertechno
    // -----------------------------------------------------------------------
    {"Proto3(Intertechno)",   3,  RfEncodingType::PulseDistance, 100, {30, 71}, {4, 11}, {9, 6},  false},

    // -----------------------------------------------------------------------
    // id  4 — EV1527 variant
    // -----------------------------------------------------------------------
    {"Proto4(EV1527)",        4,  RfEncodingType::PulseDistance, 380, {1, 6},   {1, 3},  {3, 1},  false},

    // -----------------------------------------------------------------------
    // id  5
    // -----------------------------------------------------------------------
    {"Proto5",                5,  RfEncodingType::PulseDistance, 500, {6, 14},  {1, 2},  {2, 1},  false},

    // -----------------------------------------------------------------------
    // id  6 — HT6P20B (inverted)
    // -----------------------------------------------------------------------
    {"Proto6(HT6P20B)",       6,  RfEncodingType::PulseDistance, 450, {23, 1},  {1, 2},  {2, 1},  true },

    // -----------------------------------------------------------------------
    // id  7 — HS2303-PT / AUKEY Remote
    // -----------------------------------------------------------------------
    {"Proto7(HS2303)",        7,  RfEncodingType::PulseDistance, 150, {2, 62},  {1, 6},  {6, 1},  false},

    // -----------------------------------------------------------------------
    // id  8 — Conrad RS-200 RX
    // -----------------------------------------------------------------------
    {"Proto8(RS200RX)",       8,  RfEncodingType::PulseDistance, 200, {3, 130}, {7, 16}, {3, 16}, false},

    // -----------------------------------------------------------------------
    // id  9 — Conrad RS-200 TX (inverted)
    // -----------------------------------------------------------------------
    {"Proto9(RS200TX)",       9,  RfEncodingType::PulseDistance, 200, {130, 7}, {16, 7}, {16, 3}, true },

    // -----------------------------------------------------------------------
    // id 10 — 1ByOne Doorbell (inverted)
    // -----------------------------------------------------------------------
    {"Proto10(1ByOne)",       10, RfEncodingType::PulseDistance, 365, {18, 1},  {3, 1},  {1, 3},  true },

    // -----------------------------------------------------------------------
    // id 11 — HT12E (inverted)
    // -----------------------------------------------------------------------
    {"Proto11(HT12E)",        11, RfEncodingType::PulseDistance, 270, {36, 1},  {1, 2},  {2, 1},  true },

    // -----------------------------------------------------------------------
    // id 12 — SM5212 (inverted)
    // -----------------------------------------------------------------------
    {"Proto12(SM5212)",       12, RfEncodingType::PulseDistance, 320, {36, 1},  {1, 2},  {2, 1},  true },

    // -----------------------------------------------------------------------
    // id 13 — Mumbi RC-10
    // -----------------------------------------------------------------------
    {"Proto13(MumbiRC10)",    13, RfEncodingType::PulseDistance, 100, {3, 100}, {3, 8},  {8, 3},  false},

    // -----------------------------------------------------------------------
    // id 14 — Blyss Doorbell Ref. DC6-FR-WH 656185
    // -----------------------------------------------------------------------
    {"Proto14(Blyss)",        14, RfEncodingType::PulseDistance, 500, {1, 14},  {1, 3},  {3, 1},  false},

    // -----------------------------------------------------------------------
    // id 15 — SC2260R4
    // -----------------------------------------------------------------------
    {"Proto15(SC2260R4)",     15, RfEncodingType::PulseDistance, 415, {1, 30},  {1, 3},  {4, 1},  false},

    // -----------------------------------------------------------------------
    // id 16 — Home NetWerks Bathroom Fan 6201-500
    // -----------------------------------------------------------------------
    {"Proto16(HomeNetWerks)", 16, RfEncodingType::PulseDistance, 250, {20, 10}, {1, 1},  {3, 1},  false},

    // -----------------------------------------------------------------------
    // id 17 — ORNO OR-GB-417GD
    // -----------------------------------------------------------------------
    {"Proto17(ORNO)",         17, RfEncodingType::PulseDistance, 80,  {3, 25},  {3, 13}, {11, 5}, false},

    // -----------------------------------------------------------------------
    // id 18 — CLARUS BHC993BF-3
    // -----------------------------------------------------------------------
    {"Proto18(CLARUS)",       18, RfEncodingType::PulseDistance, 82,  {2, 65},  {3, 5},  {7, 1},  false},

    // -----------------------------------------------------------------------
    // id 19 — NEC (IR-style protocol over RF)
    // -----------------------------------------------------------------------
    {"Proto19(NEC)",          19, RfEncodingType::PulseDistance, 560, {16, 8},  {1, 1},  {1, 3},  false},

    // -----------------------------------------------------------------------
    // id 20 — CAME 12-bit (RCSwitch fork addition)
    // -----------------------------------------------------------------------
    {"Proto20(CAME)",         20, RfEncodingType::PulseDistance, 250, {1, 3},   {2, 1},  {1, 2},  false},

    // -----------------------------------------------------------------------
    // id 21 — FAAC 12-bit (RCSwitch fork addition)
    // -----------------------------------------------------------------------
    {"Proto21(FAAC)",         21, RfEncodingType::PulseDistance, 330, {1, 34},  {2, 1},  {1, 2},  false},

    // -----------------------------------------------------------------------
    // id 22 — NICE 12-bit (RCSwitch fork addition)
    // -----------------------------------------------------------------------
    {"Proto22(NICE)",         22, RfEncodingType::PulseDistance, 700, {1, 36},  {2, 1},  {1, 2},  false},

    // -----------------------------------------------------------------------
    // id 23 — KeeLoq (HCS300/301, 64 data bits + 2 status).
    // Actual decode is handled by the dedicated KeeLoq decoder
    // (try_decode_keeloq) in rf_rmt_rx.cpp.  The entry here is kept for
    // protocol-table completeness and .sub file backward compatibility.
    //
    // KeeLoq physical encoding (per HCS301 datasheet):
    //   TE ≈ 400 µs (300–500 µs depending on manufacturer)
    //   Preamble: series of (TE HIGH, TE LOW) pairs
    //   Guard:    TE HIGH + 10×TE LOW (frame start marker)
    //   Bit 0:    TE HIGH  + 2×TE LOW   (constant 3×TE per bit)
    //   Bit 1:    2×TE HIGH +  TE LOW   (constant 3×TE per bit)
    //   Transmission: LSB first, 64 data bits decoded
    //               (32 encrypted + 28 serial + 4 button)
    // -----------------------------------------------------------------------
    {"KeeLoq",                23, RfEncodingType::KeeLoq,        400, {0, 0},   {1, 2},  {2, 1},  false},
};

/// Number of protocols in rf_protocols[] (compile-time constant)
static constexpr int RF_PROTOCOL_COUNT = sizeof(rf_protocols) / sizeof(rf_protocols[0]);

// ---------------------------------------------------------------------------
// Decode result
// ---------------------------------------------------------------------------

/**
 * Result of decoding an RMT symbol stream against the protocol table.
 */
struct RfDecodeResult {
    uint64_t value;       ///< Decoded integer value (MSB-first)
    uint8_t bitLength;    ///< Number of decoded bits
    uint8_t protocolId;   ///< Matched protocol ID (1-based), 0 = no match
    uint16_t pulseLength; ///< Detected base timing element (TE) in µs
    bool invertedSignal;  ///< Whether the matched protocol uses inverted logic
    bool valid;           ///< True if a protocol matched successfully

    /** Reset to invalid/empty state */
    void clear() {
        value = 0;
        bitLength = 0;
        protocolId = 0;
        pulseLength = 0;
        invertedSignal = false;
        valid = false;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Find protocol definition by 1-based ID.
 * @return Pointer to the matching entry, or nullptr if not found.
 */
inline const RfProtocolDef *rf_protocol_by_id(uint8_t id) {
    for (int i = 0; i < RF_PROTOCOL_COUNT; ++i) {
        if (rf_protocols[i].id == id) return &rf_protocols[i];
    }
    return nullptr;
}

/**
 * Compute absolute microsecond duration for a HighLow multiplier pair.
 *
 * @param hl   HighLow multiplier struct
 * @param te   Base pulse length in µs
 * @return     {high_us, low_us}
 */
inline constexpr uint32_t rf_hl_high_us(const RfHighLow &hl, uint16_t te) {
    return static_cast<uint32_t>(hl.high) * te;
}
inline constexpr uint32_t rf_hl_low_us(const RfHighLow &hl, uint16_t te) {
    return static_cast<uint32_t>(hl.low) * te;
}

#endif // RF_PROTOCOLS_H
