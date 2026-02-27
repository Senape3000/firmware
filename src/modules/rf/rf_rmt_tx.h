/**
 * @file rf_rmt_tx.h
 * @brief RMT-based RF transmitter — replaces RCSwitch TX functions.
 *
 * Provides hardware-timed OOK/ASK transmission via ESP-IDF 5.x RMT peripheral.
 * No carrier modulation is used (pure GPIO level toggling at 1 µs resolution).
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 3
 */

#ifndef RF_RMT_TX_H
#define RF_RMT_TX_H

#include "rf_protocols.h"
#include <cstdint>
#include <driver/rmt_tx.h>
#include <vector>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Initialise the RMT TX channel on the given GPIO.
 * Safe to call multiple times — re-creates only if the pin changed.
 *
 * @param pin  GPIO number for the RF transmitter
 * @return true on success
 */
bool rf_rmt_tx_init(gpio_num_t pin);

/**
 * Release the RMT TX channel and encoder.
 * Safe to call even if not initialised.
 */
void rf_rmt_tx_deinit();

// ---------------------------------------------------------------------------
// Protocol-encoded transmission   (replaces RCSwitch_send)
// ---------------------------------------------------------------------------

/**
 * Transmit an RF code using a protocol from the rf_protocols[] table.
 *
 * Builds the correct pulse sequence from the protocol timing multipliers,
 * then sends it via the RMT peripheral for hardware-accurate timing.
 *
 * @param data        Bit pattern to send (MSB-first, right-aligned)
 * @param bits        Number of bits to send
 * @param protocolId  1-based protocol ID (matches RCSwitch/sub-file Preset)
 * @param te_override If non-zero, override the protocol's default TE (µs)
 * @param repeats     Number of times to repeat the frame (default 10)
 * @return true on success
 */
bool rf_rmt_tx_protocol(
    uint64_t data, uint8_t bits, uint8_t protocolId, uint16_t te_override = 0, uint8_t repeats = 10
);

// ---------------------------------------------------------------------------
// RAW transmission   (replaces RCSwitch_RAW_send)
// ---------------------------------------------------------------------------

/**
 * Transmit a raw timing sequence.
 *
 * Timing array uses the Flipper / .sub convention:
 *   positive value = HIGH duration in µs
 *   negative value = LOW  duration in µs
 * Terminated by a 0 entry.
 *
 * @param timings   Signed-int array (0-terminated)
 * @param count     Number of elements (excluding terminator). Pass 0 to
 *                  auto-detect via 0-terminator scanning.
 * @return true on success
 */
bool rf_rmt_tx_raw(const int *timings, size_t count = 0);

// ---------------------------------------------------------------------------
// BinRAW transmission   (replaces RCSwitch_RAW_Bit_send)
// ---------------------------------------------------------------------------

/**
 * Transmit a BinRAW signal: each character in `bitString` is '0' or '1',
 * each bit lasts `te` µs.  '1' → HIGH, '0' → LOW.
 *
 * @param bitString  NUL-terminated string of '0'/'1' characters
 * @param te         Duration per bit in µs
 * @return true on success
 */
bool rf_rmt_tx_binraw(const char *bitString, uint16_t te);

// ---------------------------------------------------------------------------
// Utility: build symbol arrays (useful for unit-test / inspection)
// ---------------------------------------------------------------------------

/**
 * Encode a protocol frame into an rmt_symbol_word_t vector.
 * Does NOT include repeat — returns a single frame (data + sync).
 *
 * @param data        Bit pattern (MSB-first)
 * @param bits        Number of bits
 * @param proto       Protocol definition
 * @param te          Actual TE to use (µs)
 * @return            Vector of RMT symbols
 */
std::vector<rmt_symbol_word_t>
rf_rmt_encode_protocol(uint64_t data, uint8_t bits, const RfProtocolDef &proto, uint16_t te);

/**
 * Convert a signed-int timing array into rmt_symbol_word_t pairs.
 *
 * @param timings  Signed-int array (positive=HIGH, negative=LOW)
 * @param count    Number of elements
 * @return         Vector of RMT symbols
 */
std::vector<rmt_symbol_word_t> rf_rmt_timings_to_symbols(const int *timings, size_t count);

#endif // RF_RMT_TX_H
