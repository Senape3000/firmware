/**
 * @file rf_debug.h
 * @brief Compile-time debug logging for the RF subsystem.
 *
 * When RF_DEBUG is defined to 1, all RF_DBG_*() macros expand to
 * Serial.printf() calls that provide detailed insight into:
 *   - RX: signal reception, symbol counts, timing arrays, noise filtering
 *   - TX: frame encoding, symbol counts, repeat counts, raw timings
 *   - DECODE: protocol matching attempts, TE calculation, bit extraction
 *   - KEELOQ: preamble/guard detection, TE estimation, bit-level decode
 *   - SAVE: file creation, data integrity, timing counts
 *   - SCAN: frequency scanning, signal identification, CRC computation
 *
 * When RF_DEBUG is 0 (default), ALL macros expand to nothing — zero flash,
 * zero RAM, zero CPU overhead.  The compiler fully eliminates dead code.
 *
 * Usage:
 *   #define RF_DEBUG 1   // <-- set to 1 to enable, 0 to disable
 *   #include "rf_debug.h"
 *
 * Or simply edit the default below before building.
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 10 (Debug Instrumentation)
 */

#ifndef RF_DEBUG_H
#define RF_DEBUG_H

// ===========================================================================
// Master switch — set to 1 to enable all RF debug output, 0 to disable.
// When 0, the preprocessor removes ALL debug code: no flash, no RAM cost.
// ===========================================================================
#ifndef RF_DEBUG
#define RF_DEBUG 1
#endif

#if RF_DEBUG

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Core logging macros (category-tagged, timestamped)
// ---------------------------------------------------------------------------

/// General RF subsystem debug
#define RF_DBG(fmt, ...) Serial.printf("[RF_DBG %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// RX (receive) path debug
#define RF_DBG_RX(fmt, ...) Serial.printf("[RF_RX  %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// TX (transmit) path debug
#define RF_DBG_TX(fmt, ...) Serial.printf("[RF_TX  %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// Protocol decode debug
#define RF_DBG_DECODE(fmt, ...) Serial.printf("[RF_DEC %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// KeeLoq-specific decode debug
#define RF_DBG_KEELOQ(fmt, ...) Serial.printf("[RF_KLQ %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// Save/Load file operations debug
#define RF_DBG_SAVE(fmt, ...) Serial.printf("[RF_SAV %lu] " fmt "\n", millis(), ##__VA_ARGS__)

/// Scan/identification debug
#define RF_DBG_SCAN(fmt, ...) Serial.printf("[RF_SCN %lu] " fmt "\n", millis(), ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Helper: dump first N symbols of an rmt_symbol_word_t array
// ---------------------------------------------------------------------------
#include <driver/rmt_rx.h>

static inline void
rf_dbg_dump_symbols(const char *label, const rmt_symbol_word_t *syms, size_t count, size_t max_dump = 16) {
    Serial.printf("[RF_DBG %lu] %s: %u symbols", millis(), label, (unsigned)count);
    if (count == 0) {
        Serial.println();
        return;
    }
    size_t n = (count < max_dump) ? count : max_dump;
    Serial.println(":");
    for (size_t i = 0; i < n; ++i) {
        Serial.printf(
            "  [%3u] L%d:%5u | L%d:%5u\n",
            (unsigned)i,
            syms[i].level0,
            syms[i].duration0,
            syms[i].level1,
            syms[i].duration1
        );
    }
    if (count > max_dump) {
        Serial.printf("  ... (%u more symbols omitted)\n", (unsigned)(count - max_dump));
    }
}

/// Dump first N flat timings (uint32_t array)
static inline void
rf_dbg_dump_flat_timings(const char *label, const uint32_t *timings, size_t count, size_t max_dump = 32) {
    Serial.printf("[RF_DBG %lu] %s: %u flat timings", millis(), label, (unsigned)count);
    if (count == 0) {
        Serial.println();
        return;
    }
    size_t n = (count < max_dump) ? count : max_dump;
    Serial.print(": ");
    for (size_t i = 0; i < n; ++i) { Serial.printf("%lu ", timings[i]); }
    if (count > max_dump) { Serial.printf("... (%u more)", (unsigned)(count - max_dump)); }
    Serial.println();
}

/// Dump first N signed timings (int array, Flipper-style +/- format)
static inline void
rf_dbg_dump_signed_timings(const char *label, const int *timings, size_t count, size_t max_dump = 32) {
    Serial.printf("[RF_DBG %lu] %s: %u signed timings", millis(), label, (unsigned)count);
    if (count == 0) {
        Serial.println();
        return;
    }
    size_t n = (count < max_dump) ? count : max_dump;
    Serial.print(": ");
    for (size_t i = 0; i < n; ++i) { Serial.printf("%d ", timings[i]); }
    if (count > max_dump) { Serial.printf("... (%u more)", (unsigned)(count - max_dump)); }
    Serial.println();
}

// ---------------------------------------------------------------------------
// Signal integrity analysis helper
// ---------------------------------------------------------------------------

/// Analyze a received symbol buffer for common signal quality issues
static inline void rf_dbg_signal_quality(const rmt_symbol_word_t *syms, size_t count) {
    if (count == 0) {
        RF_DBG("Signal quality: EMPTY (no symbols)");
        return;
    }

    uint32_t min_dur = UINT32_MAX, max_dur = 0;
    uint64_t total_dur = 0;
    size_t zero_count = 0;
    size_t very_short = 0; // < 50 µs — likely noise
    size_t very_long = 0;  // > 10000 µs — likely gap/sync

    for (size_t i = 0; i < count; ++i) {
        uint32_t d0 = syms[i].duration0;
        uint32_t d1 = syms[i].duration1;
        if (d0 == 0 && d1 == 0) {
            zero_count++;
            continue;
        }

        if (d0 > 0) {
            if (d0 < min_dur) min_dur = d0;
            if (d0 > max_dur) max_dur = d0;
            total_dur += d0;
            if (d0 < 50) very_short++;
            if (d0 > 10000) very_long++;
        }
        if (d1 > 0) {
            if (d1 < min_dur) min_dur = d1;
            if (d1 > max_dur) max_dur = d1;
            total_dur += d1;
            if (d1 < 50) very_short++;
            if (d1 > 10000) very_long++;
        }
    }

    uint32_t avg_dur = (count > zero_count) ? (uint32_t)(total_dur / ((count - zero_count) * 2)) : 0;

    Serial.printf(
        "[RF_DBG %lu] Signal quality: %u symbols, min=%lu µs, max=%lu µs, avg=%lu µs, "
        "total=%llu µs (%.1f ms), zero_syms=%u, noise(<50µs)=%u, gaps(>10ms)=%u\n",
        millis(),
        (unsigned)count,
        min_dur,
        max_dur,
        avg_dur,
        total_dur,
        total_dur / 1000.0,
        (unsigned)zero_count,
        (unsigned)very_short,
        (unsigned)very_long
    );

    // Warn about potential issues
    if (very_short > count / 4) {
        Serial.printf("[RF_DBG %lu] WARNING: >25%% noise pulses (<50µs) — signal may be dirty!\n", millis());
    }
    if (zero_count > 2) {
        Serial.printf(
            "[RF_DBG %lu] WARNING: %u zero-duration symbols — possible buffer overflow or truncation\n",
            millis(),
            (unsigned)zero_count
        );
    }
    if (max_dur > 0 && min_dur > 0 && max_dur / min_dur > 200) {
        Serial.printf(
            "[RF_DBG %lu] WARNING: extreme duration ratio (max/min=%lu) — mixed protocols or noise?\n",
            millis(),
            max_dur / min_dur
        );
    }
}

#else // RF_DEBUG == 0

// ---------------------------------------------------------------------------
// All macros expand to nothing — zero cost
// ---------------------------------------------------------------------------
#define RF_DBG(fmt, ...) ((void)0)
#define RF_DBG_RX(fmt, ...) ((void)0)
#define RF_DBG_TX(fmt, ...) ((void)0)
#define RF_DBG_DECODE(fmt, ...) ((void)0)
#define RF_DBG_KEELOQ(fmt, ...) ((void)0)
#define RF_DBG_SAVE(fmt, ...) ((void)0)
#define RF_DBG_SCAN(fmt, ...) ((void)0)

#define rf_dbg_dump_symbols(label, syms, count, ...) ((void)0)
#define rf_dbg_dump_flat_timings(label, timings, count, ...) ((void)0)
#define rf_dbg_dump_signed_timings(label, timings, count, ...) ((void)0)
#define rf_dbg_signal_quality(syms, count) ((void)0)

#endif // RF_DEBUG

#endif // RF_DEBUG_H
