/**
 * @file rf_rmt_rx.h
 * @brief RMT-based RF receiver & decoder — replaces RCSwitch RX functions.
 *
 * Uses the ESP-IDF 5.x RMT peripheral (1 µs resolution) to capture edges,
 * then applies software protocol matching identical to RCSwitch's algorithm
 * but operating on `rmt_symbol_word_t` arrays instead of ISR-captured timings.
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 2
 */

#ifndef RF_RMT_RX_H
#define RF_RMT_RX_H

#include "rf_protocols.h"
#include <Arduino.h>
#include <cstdint>
#include <driver/rmt_rx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

/// Maximum number of RMT symbols per receive buffer.
/// 256 symbols covers 128 bit-pairs — enough for KeeLoq 66-bit + preamble.
#define RF_RMT_RX_BUF_SYMBOLS 256

/// Default tolerance for protocol matching (percent of expected duration).
/// RCSwitch uses 60% by default.
#define RF_RMT_RX_TOLERANCE_PERCENT 60

/// Minimum number of edge transitions to consider a valid signal
/// (same as RCSwitch: changeCount > 7 → at least 4 bits)
#define RF_RMT_RX_MIN_TRANSITIONS 8

/// Minimum signal duration (ns) — filters glitches/noise
#define RF_RMT_RX_SIGNAL_MIN_NS 3000

/// Maximum gap (ns) between edges within a frame.
/// 12 ms matches the working record.cpp / rf_spectrum() implementations.
/// Covers sync pulses up to ~12 ms (protocols 1-7, 9-22, KeeLoq).
#define RF_RMT_RX_SIGNAL_MAX_NS 12000000

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Create and enable an RMT RX channel on the given GPIO.
 * Also creates the internal FreeRTOS queue for async receive.
 *
 * @param pin  GPIO number for the RF receiver
 * @return true on success
 */
bool rf_rmt_rx_init(gpio_num_t pin);

/**
 * Tear down the RMT RX channel and free resources.
 * Safe to call even if not initialised.
 */
void rf_rmt_rx_deinit();

// ---------------------------------------------------------------------------
// Receive (blocking)
// ---------------------------------------------------------------------------

/**
 * Start an RMT receive and wait up to `timeout_ms` for a complete frame.
 *
 * On success, the received symbols are stored internally and can be
 * accessed via `rf_rmt_rx_symbols()` / `rf_rmt_rx_symbol_count()`.
 *
 * @param timeout_ms  Maximum wait time in milliseconds (0 = non-blocking poll)
 * @return true if symbols were received
 */
bool rf_rmt_rx_receive(uint32_t timeout_ms = 1000);

/**
 * Restart the receiver for the next frame (must be called after each
 * successful `rf_rmt_rx_receive()` before calling it again).
 */
void rf_rmt_rx_restart();

// ---------------------------------------------------------------------------
// Access raw symbols
// ---------------------------------------------------------------------------

/// Pointer to the last received symbol buffer (valid until next receive/deinit).
const rmt_symbol_word_t *rf_rmt_rx_symbols();

/// Number of symbols in the last receive.
size_t rf_rmt_rx_symbol_count();

// ---------------------------------------------------------------------------
// Protocol decode
// ---------------------------------------------------------------------------

/**
 * Try to decode the last received symbol buffer against all protocols
 * in rf_protocols[].
 *
 * Algorithm (mirrors RCSwitch::receiveProtocol):
 *   1. Convert RMT symbols to flat timing array [d0, d1, d2, ...]
 *   2. Assume timings[0] is the sync pulse (longest gap)
 *   3. For each protocol: compute TE from sync, then match bit timings
 *
 * @param tolerance  Tolerance in percent (default = RF_RMT_RX_TOLERANCE_PERCENT)
 * @return RfDecodeResult with .valid == true on success
 */
RfDecodeResult rf_rmt_rx_decode(int tolerance = RF_RMT_RX_TOLERANCE_PERCENT);

/**
 * Decode a caller-provided symbol array (does not use internal state).
 *
 * @param symbols    Pointer to RMT symbols
 * @param count      Number of symbols
 * @param tolerance  Matching tolerance (percent)
 * @return Decode result
 */
RfDecodeResult rf_rmt_rx_decode_symbols(
    const rmt_symbol_word_t *symbols, size_t count, int tolerance = RF_RMT_RX_TOLERANCE_PERCENT
);

// ---------------------------------------------------------------------------
// RAW data extraction
// ---------------------------------------------------------------------------

/**
 * Convert the last received symbols into a signed-int timing string
 * compatible with the Flipper .sub RAW_Data format.
 *
 * Output: "350 -1050 350 -1050 ..." (positive = HIGH, negative = LOW).
 *
 * @return Timing string, or empty string if no data.
 */
String rf_rmt_rx_get_raw_string();

/**
 * Convert a caller-provided symbol array to raw timing string.
 */
String rf_rmt_rx_symbols_to_raw_string(const rmt_symbol_word_t *symbols, size_t count);

/**
 * Convert symbols to a vector of signed-int timings.
 * Positive = HIGH duration (µs), Negative = LOW duration (µs).
 */
std::vector<int> rf_rmt_rx_symbols_to_timings(const rmt_symbol_word_t *symbols, size_t count);

#endif // RF_RMT_RX_H
