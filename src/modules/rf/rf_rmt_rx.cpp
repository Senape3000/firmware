/**
 * @file rf_rmt_rx.cpp
 * @brief RMT-based RF receiver & decoder implementation.
 *
 * Replaces all RCSwitch RX functions (enableReceive, available, getReceivedValue,
 * getRAWReceivedRawdata, etc.) with ESP-IDF RMT peripheral + software decode.
 *
 * Decode algorithm is a faithful port of RCSwitch::receiveProtocol() and
 * RCSwitch::handleInterrupt() operating on RMT symbols instead of ISR timings.
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 2
 */

#include "rf_rmt_rx.h"
#include "rf_debug.h"
#include <cmath>
#include <cstring>
#include <esp_log.h>

static const char *TAG = "rf_rmt_rx";

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static rmt_channel_handle_t s_rx_channel = nullptr;
static QueueHandle_t s_rx_queue = nullptr;
static gpio_num_t s_rx_pin = GPIO_NUM_NC;

/// Double-buffer: the RMT peripheral writes here, we read after queue event.
static rmt_symbol_word_t s_rx_buf[RF_RMT_RX_BUF_SYMBOLS];

/// Last received data (copied from event callback data)
static rmt_symbol_word_t s_last_symbols[RF_RMT_RX_BUF_SYMBOLS];
static size_t s_last_count = 0;

// ---------------------------------------------------------------------------
// RMT callback
// ---------------------------------------------------------------------------
static bool rx_done_callback(rmt_channel_t *channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool rf_rmt_rx_init(gpio_num_t pin) {
    RF_DBG_RX("init: GPIO=%d (current=%d)", (int)pin, (int)s_rx_pin);
    if (s_rx_channel && pin == s_rx_pin) {
        RF_DBG_RX("init: already active on same pin, reusing");
        return true;
    }

    if (s_rx_channel) rf_rmt_rx_deinit();

    // Create RMT RX channel
    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num = pin;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = 1 * 1000 * 1000; // 1 µs
    cfg.mem_block_symbols = 128;         // increased from 64 for complex protocols
    cfg.intr_priority = 0;
    cfg.flags.invert_in = false;
    cfg.flags.with_dma = false;
    cfg.flags.allow_pd = false;
    cfg.flags.io_loop_back = false;

    esp_err_t err = rmt_new_rx_channel(&cfg, &s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(err));
        s_rx_channel = nullptr;
        return false;
    }

    // Create queue
    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "Failed to create receive queue");
        rmt_del_channel(s_rx_channel);
        s_rx_channel = nullptr;
        return false;
    }

    // Register callback
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rx_done_callback,
    };
    err = rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_rx_queue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register callbacks failed: %s", esp_err_to_name(err));
        vQueueDelete(s_rx_queue);
        rmt_del_channel(s_rx_channel);
        s_rx_queue = nullptr;
        s_rx_channel = nullptr;
        return false;
    }

    // Enable
    err = rmt_enable(s_rx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        vQueueDelete(s_rx_queue);
        rmt_del_channel(s_rx_channel);
        s_rx_queue = nullptr;
        s_rx_channel = nullptr;
        return false;
    }

    s_rx_pin = pin;
    s_last_count = 0;

    // Start first receive immediately so the hardware is listening
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = RF_RMT_RX_SIGNAL_MIN_NS,
        .signal_range_max_ns = RF_RMT_RX_SIGNAL_MAX_NS,
    };
    err = rmt_receive(s_rx_channel, s_rx_buf, sizeof(s_rx_buf), &rx_cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Initial rmt_receive failed: %s", esp_err_to_name(err)); }

    ESP_LOGI(TAG, "RX channel ready on GPIO %d", (int)pin);
    RF_DBG_RX(
        "init: SUCCESS — RMT RX channel ready, GPIO=%d, buf=%d symbols, min_ns=%d, max_ns=%d",
        (int)pin,
        RF_RMT_RX_BUF_SYMBOLS,
        RF_RMT_RX_SIGNAL_MIN_NS,
        RF_RMT_RX_SIGNAL_MAX_NS
    );
    return true;
}

void rf_rmt_rx_deinit() {
    RF_DBG_RX("deinit: pin=%d, had_channel=%d", (int)s_rx_pin, s_rx_channel != nullptr);
    if (s_rx_channel) {
        rmt_disable(s_rx_channel);
        rmt_del_channel(s_rx_channel);
        s_rx_channel = nullptr;
    }
    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = nullptr;
    }
    s_rx_pin = GPIO_NUM_NC;
    s_last_count = 0;
    RF_DBG_RX("deinit: complete");
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------

bool rf_rmt_rx_receive(uint32_t timeout_ms) {
    if (!s_rx_channel || !s_rx_queue) {
        RF_DBG_RX("receive: SKIP — channel=%p queue=%p", s_rx_channel, s_rx_queue);
        return false;
    }

    // Poll the queue — the hardware is already listening from init or last restart.
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return false; // timeout — no signal (intentionally silent to avoid spam)
    }

    // Copy to persistent buffer
    s_last_count = rx_data.num_symbols;
    if (s_last_count > RF_RMT_RX_BUF_SYMBOLS) s_last_count = RF_RMT_RX_BUF_SYMBOLS;
    memcpy(s_last_symbols, rx_data.received_symbols, s_last_count * sizeof(rmt_symbol_word_t));

    RF_DBG_RX("receive: GOT %u symbols (timeout was %lu ms)", (unsigned)s_last_count, timeout_ms);
    rf_dbg_dump_symbols("RX received", s_last_symbols, s_last_count);
    rf_dbg_signal_quality(s_last_symbols, s_last_count);

    return s_last_count > 0;
}

void rf_rmt_rx_restart() {
    if (!s_rx_channel) return;

    RF_DBG_RX("restart: re-arming RMT RX on GPIO=%d", (int)s_rx_pin);
    // Re-arm the RMT hardware for the next frame
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = RF_RMT_RX_SIGNAL_MIN_NS,
        .signal_range_max_ns = RF_RMT_RX_SIGNAL_MAX_NS,
    };
    esp_err_t err = rmt_receive(s_rx_channel, s_rx_buf, sizeof(s_rx_buf), &rx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rf_rmt_rx_restart: rmt_receive failed: %s", esp_err_to_name(err));
    } else {
        RF_DBG_RX("restart: re-armed OK");
    }
}

const rmt_symbol_word_t *rf_rmt_rx_symbols() { return s_last_symbols; }

size_t rf_rmt_rx_symbol_count() { return s_last_count; }

// ---------------------------------------------------------------------------
// Conversion: RMT symbols → flat timing array
// ---------------------------------------------------------------------------

/**
 * Convert rmt_symbol_word_t array to a flat uint32_t timing array.
 * Each symbol produces 2 entries: duration0 and duration1 (in µs).
 * Returns the number of valid entries written.
 *
 * This mirrors how RCSwitch::timings[] is populated by handleInterrupt().
 */
static size_t
symbols_to_flat_timings(const rmt_symbol_word_t *symbols, size_t count, uint32_t *out, size_t out_capacity) {
    size_t idx = 0;
    for (size_t i = 0; i < count && idx + 1 < out_capacity; ++i) {
        out[idx++] = symbols[i].duration0;
        // Skip trailing zero-durations at the very end
        if (symbols[i].duration1 > 0 || i + 1 < count) { out[idx++] = symbols[i].duration1; }
    }
    return idx;
}

// ---------------------------------------------------------------------------
// Protocol matching  (faithful port of RCSwitch::receiveProtocol)
// ---------------------------------------------------------------------------

static inline uint32_t abs_diff(uint32_t a, uint32_t b) { return (a > b) ? (a - b) : (b - a); }

/**
 * Try to decode flat timings against a single protocol definition.
 *
 * @param timings    Flat timing array (interleaved HIGH/LOW durations in µs)
 * @param count      Number of entries in timings[]
 * @param proto      Protocol to test
 * @param tolerance  Tolerance in percent
 * @param result     [out] filled on success
 * @return true if the protocol matched
 *
 * Algorithm (identical to RCSwitch):
 *   - timings[0] is assumed to be the sync/gap pulse (the longest one,
 *     which terminates the previous frame).
 *   - TE is derived from: timings[0] / max(syncFactor.high, syncFactor.low)
 *   - Each pair of (timings[i], timings[i+1]) is tested against zero/one HighLow.
 */
static bool try_decode_protocol(
    const uint32_t *timings, size_t count, const RfProtocolDef &proto, int tolerance, RfDecodeResult &result
) {
    // Need at least sync + a few changes
    if (count < RF_RMT_RX_MIN_TRANSITIONS) return false;
    if (proto.syncFactor.high == 0 && proto.syncFactor.low == 0) return false;

    // Determine TE from the sync pulse (timings[0] = the long gap)
    const uint8_t syncMax =
        (proto.syncFactor.low > proto.syncFactor.high) ? proto.syncFactor.low : proto.syncFactor.high;
    if (syncMax == 0) return false;

    const uint32_t delay = timings[0] / syncMax;
    if (delay == 0) return false;

    RF_DBG_DECODE(
        "try proto %d (%s): sync[0]=%lu, syncMax=%lu, TE=%lu, tol=%d%%",
        proto.id,
        proto.name,
        timings[0],
        syncMax,
        delay,
        tolerance
    );

    const uint32_t delayTolerance = delay * tolerance / 100;

    // For inverted protocols, data starts at index 2 (skip the first HIGH that
    // belongs to the sync); for normal protocols, data starts at index 1.
    const size_t firstData = proto.invertedSignal ? 2 : 1;

    uint64_t code = 0;
    size_t bitCount = 0;

    for (size_t i = firstData; i + 1 < count; i += 2) {
        code <<= 1;

        // Check if this pair matches bit '0'
        if (abs_diff(timings[i], delay * proto.zero.high) < delayTolerance &&
            abs_diff(timings[i + 1], delay * proto.zero.low) < delayTolerance) {
            // bit 0 — code already shifted, LSB is 0
        }
        // Check if this pair matches bit '1'
        else if (abs_diff(timings[i], delay * proto.one.high) < delayTolerance &&
                 abs_diff(timings[i + 1], delay * proto.one.low) < delayTolerance) {
            code |= 1ULL;
        } else {
            // Mismatch
            return false;
        }
        ++bitCount;
    }

    if (bitCount < 4) {
        RF_DBG_DECODE("  proto %d: only %u bits decoded — too few, rejecting", proto.id, (unsigned)bitCount);
        return false; // too few bits — likely noise
    }

    result.value = code;
    result.bitLength = static_cast<uint8_t>(bitCount);
    result.protocolId = proto.id;
    result.pulseLength = static_cast<uint16_t>(delay);
    result.invertedSignal = proto.invertedSignal;
    result.valid = true;
    RF_DBG_DECODE(
        "  proto %d MATCHED! value=0x%llX, bits=%u, TE=%lu µs, inverted=%d",
        proto.id,
        code,
        (unsigned)bitCount,
        delay,
        proto.invertedSignal
    );
    return true;
}

// ---------------------------------------------------------------------------
// Syncless protocol decoder — Step 2 fallback   (Phase 11)
// ---------------------------------------------------------------------------

/**
 * Try to decode flat timings against a protocol WITHOUT relying on a sync pulse.
 *
 * For RMT captures that do NOT include the inter-frame sync/gap
 * (because it exceeded signal_range_max_ns and terminated the capture),
 * this decoder tries matching timing pairs directly as data bits from
 * the start of the capture, estimating TE from protocol patterns.
 *
 * This fixes protocols like CAME, FAAC, NICE where the inter-frame
 * gap (~15 ms) exceeds the RMT signal_range_max_ns (12 ms), so each
 * frame is captured individually without the leading sync pulse.
 *
 * Algorithm:
 *   1. Generate TE candidates from protocol definition + first timing pair
 *   2. Try multiple starting indices (0, 1) to handle lead-in variations
 *   3. For each (TE, startIdx) combination, match all timing pairs as data
 *   4. Return the first match with >= 4 decoded bits
 */
static bool try_decode_protocol_syncless(
    const uint32_t *timings, size_t count, const RfProtocolDef &proto, int tolerance, RfDecodeResult &result
) {
    // Need at least 8 transitions (4 bit-pairs)
    if (count < RF_RMT_RX_MIN_TRANSITIONS) return false;

    // --- Build TE candidates from protocol definition + first timings ---
    static constexpr int MAX_TE_CANDIDATES = 6;
    uint32_t te_cands[MAX_TE_CANDIDATES];
    int n_te = 0;

    auto add_te = [&](uint32_t te) {
        if (te < 50 || te > 2000 || n_te >= MAX_TE_CANDIDATES) return;
        // Deduplicate: skip if within ~12% of an existing candidate
        for (int i = 0; i < n_te; i++) {
            if (abs_diff(te_cands[i], te) <= te / 8) return;
        }
        te_cands[n_te++] = te;
    };

    // Candidate 1: protocol's nominal pulse length
    add_te(proto.pulseLength);

    // Candidates 2-5: derived from the first timing pair
    if (count >= 2) {
        // Assuming first pair is a zero bit
        if (proto.zero.high > 0) add_te(timings[0] / proto.zero.high);
        if (proto.zero.low > 0) add_te(timings[1] / proto.zero.low);
        // Assuming first pair is a one bit
        if (proto.one.high > 0) add_te(timings[0] / proto.one.high);
        if (proto.one.low > 0) add_te(timings[1] / proto.one.low);
    }

    RF_DBG_DECODE(
        "  syncless try proto %d (%s): %d TE candidates, first=[%lu,%lu]",
        proto.id,
        proto.name,
        n_te,
        count >= 1 ? (unsigned long)timings[0] : 0,
        count >= 2 ? (unsigned long)timings[1] : 0
    );

    // --- Try each TE candidate × starting index ---
    for (int c = 0; c < n_te; c++) {
        const uint32_t te = te_cands[c];
        const uint32_t teTol = te * tolerance / 100;

        // Try starting from index 0 (first timing is data) and index 1
        // (in case there's a leading half-bit from inverted protocol or partial sync)
        for (size_t start = 0; start <= 1 && start + RF_RMT_RX_MIN_TRANSITIONS <= count; start++) {
            uint64_t code = 0;
            size_t bitCount = 0;
            bool failed = false;

            for (size_t i = start; i + 1 < count; i += 2) {
                const bool isZero =
                    abs_diff(timings[i], static_cast<uint32_t>(proto.zero.high) * te) <= teTol &&
                    abs_diff(timings[i + 1], static_cast<uint32_t>(proto.zero.low) * te) <= teTol;
                const bool isOne =
                    abs_diff(timings[i], static_cast<uint32_t>(proto.one.high) * te) <= teTol &&
                    abs_diff(timings[i + 1], static_cast<uint32_t>(proto.one.low) * te) <= teTol;

                if (isZero) {
                    code <<= 1;
                    ++bitCount;
                } else if (isOne) {
                    code <<= 1;
                    code |= 1ULL;
                    ++bitCount;
                } else {
                    // Allow the very last pair to mismatch (trailing sync remnant)
                    if (i + 3 < count) { failed = true; }
                    break;
                }
            }

            if (failed || bitCount < 4) continue;

            RF_DBG_DECODE(
                "  syncless proto %d MATCHED! TE=%lu, start=%u, bits=%u, value=0x%llX",
                proto.id,
                (unsigned long)te,
                (unsigned)start,
                (unsigned)bitCount,
                code
            );

            result.value = code;
            result.bitLength = static_cast<uint8_t>(bitCount);
            result.protocolId = proto.id;
            result.pulseLength = static_cast<uint16_t>(te);
            result.invertedSignal = proto.invertedSignal;
            result.valid = true;
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// KeeLoq dedicated decoder  (Phase 7)
// ---------------------------------------------------------------------------

/**
 * Try to decode a KeeLoq signal from flat timings.
 *
 * KeeLoq (HCS300/301) physical-layer encoding:
 *   Preamble : series of (TE HIGH, TE LOW) pairs — receiver sync
 *   Guard    : TE HIGH + 10×TE LOW — frame start marker
 *   Data bits: 64 meaningful bits (32 encrypted + 28 serial + 4 button)
 *              + 2 status bits (ignored — don't fit in uint64_t)
 *   Bit 0    : TE HIGH  + 2×TE LOW   (constant 3×TE per bit)
 *   Bit 1    : 2×TE HIGH +  TE LOW   (constant 3×TE per bit)
 *   TE ≈ 400 µs (varies by manufacturer, typically 300–500 µs)
 *   Transmission order: LSB first per HCS301 datasheet.
 *
 * The decoder accumulates bits with left-shift (MSB-first storage).
 * Callers use reverse_bits(value, 64) to restore native byte order.
 *
 * @param timings    Flat timing array (alternating durations in µs)
 * @param count      Number of entries in timings[]
 * @param tolerance  Tolerance in percent of TE
 * @param result     [out] filled on success
 * @return true if a valid KeeLoq frame was decoded
 */
static bool try_decode_keeloq(const uint32_t *timings, size_t count, int tolerance, RfDecodeResult &result) {
    // --- Tuning constants ------------------------------------------------
    static constexpr uint32_t TE_MIN_US = 250;       // smallest acceptable TE
    static constexpr uint32_t TE_MAX_US = 600;       // largest  acceptable TE
    static constexpr uint32_t GUARD_FACTOR_MIN = 5;  // guard LOW ≥ 5 × TE
    static constexpr uint32_t GUARD_FACTOR_MAX = 25; // guard LOW ≤ 25 × TE
    static constexpr size_t MIN_PREAMBLE_PAIRS = 3;  // at least 3 (TE,TE) pairs
    static constexpr size_t KEELOQ_DATA_BITS = 64;   // 32 hop + 28 serial + 4 btn

    // Need: preamble (≥6) + guard (2) + 64 bits (128) = ≥136 entries
    if (count < MIN_PREAMBLE_PAIRS * 2 + 2 + KEELOQ_DATA_BITS * 2) {
        RF_DBG_KEELOQ(
            "skip: too few timings (%u < %u minimum)",
            (unsigned)count,
            (unsigned)(MIN_PREAMBLE_PAIRS * 2 + 2 + KEELOQ_DATA_BITS * 2)
        );
        return false;
    }

    RF_DBG_KEELOQ(
        "scanning %u timings for guard pulse (TE range %lu-%lu µs)",
        (unsigned)count,
        (uint32_t)TE_MIN_US,
        (uint32_t)TE_MAX_US
    );

    // --- Scan for guard pulse: short HIGH (≈TE) + long LOW (≥5×TE) -------
    for (size_t gi = MIN_PREAMBLE_PAIRS * 2; gi + 1 + KEELOQ_DATA_BITS * 2 < count; ++gi) {
        const uint32_t g_short = timings[gi];
        const uint32_t g_long = timings[gi + 1];

        if (g_short < TE_MIN_US || g_short > TE_MAX_US) continue;
        if (g_long < g_short * GUARD_FACTOR_MIN) continue;
        if (g_long > g_short * GUARD_FACTOR_MAX) continue;

        // Candidate guard found.  Refine TE by averaging with preamble.
        RF_DBG_KEELOQ(
            "  guard candidate at [%u]: short=%lu µs, long=%lu µs (ratio=%.1f)",
            (unsigned)gi,
            g_short,
            g_long,
            (float)g_long / g_short
        );
        uint32_t te_sum = g_short;
        uint32_t te_n = 1;
        for (size_t pi = gi - MIN_PREAMBLE_PAIRS * 2; pi < gi; ++pi) {
            te_sum += timings[pi];
            te_n++;
        }
        const uint32_t te = te_sum / te_n;
        if (te < TE_MIN_US || te > TE_MAX_US) {
            RF_DBG_KEELOQ(
                "  guard rejected: TE=%lu µs out of range [%lu-%lu]",
                te,
                (uint32_t)TE_MIN_US,
                (uint32_t)TE_MAX_US
            );
            continue;
        }

        RF_DBG_KEELOQ(
            "  TE estimated: %lu µs (avg of %lu samples, tol=%lu µs)", te, te_n, te * tolerance / 100
        );

        const uint32_t te_tol = te * tolerance / 100;

        // --- Verify preamble: each entry ≈ TE ---------------------------
        bool preamble_ok = true;
        for (size_t pi = gi - MIN_PREAMBLE_PAIRS * 2; pi < gi; ++pi) {
            if (abs_diff(timings[pi], te) > te_tol) {
                preamble_ok = false;
                break;
            }
        }
        if (!preamble_ok) {
            RF_DBG_KEELOQ("  preamble check FAILED at guard index %u", (unsigned)gi);
            continue;
        }

        RF_DBG_KEELOQ(
            "  preamble OK, decoding %u data bits from index %u",
            (unsigned)KEELOQ_DATA_BITS,
            (unsigned)(gi + 2)
        );

        // --- Decode data bits after the guard ---------------------------
        const size_t data_start = gi + 2;
        if (data_start + KEELOQ_DATA_BITS * 2 > count) continue;

        const uint32_t te2 = te * 2;
        uint64_t code = 0;
        bool ok = true;

        for (size_t b = 0; b < KEELOQ_DATA_BITS; ++b) {
            const size_t idx = data_start + b * 2;
            const uint32_t hi = timings[idx];
            const uint32_t lo = timings[idx + 1];

            code <<= 1;

            // Bit 0: short HIGH (≈TE) + long LOW (≈2×TE)
            if (abs_diff(hi, te) <= te_tol && abs_diff(lo, te2) <= te_tol) {
                // bit 0 — LSB already 0
            }
            // Bit 1: long HIGH (≈2×TE) + short LOW (≈TE)
            else if (abs_diff(hi, te2) <= te_tol && abs_diff(lo, te) <= te_tol) {
                code |= 1ULL;
            } else {
                RF_DBG_KEELOQ(
                    "  bit %u MISMATCH: hi=%lu lo=%lu (expected TE=%lu or 2xTE=%lu)",
                    (unsigned)b,
                    hi,
                    lo,
                    te,
                    te2
                );
                ok = false;
                break;
            }
        }

        if (!ok) {
            RF_DBG_KEELOQ("  decode FAILED at guard index %u", (unsigned)gi);
            continue;
        }

        // --- Success! ---------------------------------------------------
        result.value = code;
        result.bitLength = KEELOQ_DATA_BITS;
        result.protocolId = 23; // PRESET_KEELOQ
        result.pulseLength = static_cast<uint16_t>(te);
        result.invertedSignal = false;
        result.valid = true;

        RF_DBG_KEELOQ(
            "DECODED! value=0x%llX, bits=%u, TE=%lu µs, guard_idx=%u",
            code,
            (unsigned)KEELOQ_DATA_BITS,
            te,
            (unsigned)gi
        );
        ESP_LOGI(
            TAG,
            "KeeLoq decoded: value=0x%llX, bits=%d, TE=%d",
            result.value,
            result.bitLength,
            result.pulseLength
        );
        return true;
    }

    RF_DBG_KEELOQ("no valid KeeLoq frame found in %u timings", (unsigned)count);
    return false;
}

// ---------------------------------------------------------------------------
// Public decode API
// ---------------------------------------------------------------------------

RfDecodeResult rf_rmt_rx_decode_symbols(const rmt_symbol_word_t *symbols, size_t count, int tolerance) {
    RfDecodeResult result;
    result.clear();

    if (!symbols || count == 0) {
        RF_DBG_DECODE("decode_symbols: no data (symbols=%p, count=%u)", symbols, (unsigned)count);
        return result;
    }

    RF_DBG_DECODE("decode_symbols: %u symbols, tolerance=%d%%", (unsigned)count, tolerance);

    // Convert to flat timings
    // Maximum flat entries = count * 2
    const size_t maxFlat = count * 2;
    std::vector<uint32_t> flat(maxFlat);
    size_t flatCount = symbols_to_flat_timings(symbols, count, flat.data(), maxFlat);

    RF_DBG_DECODE("  converted to %u flat timings", (unsigned)flatCount);
    rf_dbg_dump_flat_timings("flat timings", flat.data(), flatCount);

    if (flatCount < RF_RMT_RX_MIN_TRANSITIONS) {
        RF_DBG_DECODE(
            "  too few transitions (%u < %d) — skipping decode",
            (unsigned)flatCount,
            RF_RMT_RX_MIN_TRANSITIONS
        );
        return result;
    }

    // Try every protocol
    RF_DBG_DECODE("  testing %d standard protocols...", RF_PROTOCOL_COUNT - 1);
    for (int p = 0; p < RF_PROTOCOL_COUNT; ++p) {
        const RfProtocolDef &proto = rf_protocols[p];
        // Skip KeeLoq — it uses a dedicated decoder
        if (proto.encoding == RfEncodingType::KeeLoq) continue;

        if (try_decode_protocol(flat.data(), flatCount, proto, tolerance, result)) {
            ESP_LOGI(
                TAG,
                "Matched protocol %d (%s): value=0x%llX, bits=%d, TE=%d",
                proto.id,
                proto.name,
                result.value,
                result.bitLength,
                result.pulseLength
            );
            return result;
        }
    }

    // --- Step 2: Syncless decode (for captures without inter-frame sync/gap) ---
    // Handles protocols like CAME/FAAC/NICE where the gap exceeds signal_range_max_ns
    // and is stripped from the capture, leaving only data bits starting at timings[0].
    RF_DBG_DECODE("  standard protos failed, trying syncless multi-step decode...");
    for (int p = 0; p < RF_PROTOCOL_COUNT; ++p) {
        const RfProtocolDef &proto = rf_protocols[p];
        if (proto.encoding == RfEncodingType::KeeLoq) continue;

        if (try_decode_protocol_syncless(flat.data(), flatCount, proto, tolerance, result)) {
            ESP_LOGI(
                TAG,
                "Syncless matched protocol %d (%s): value=0x%llX, bits=%d, TE=%d",
                proto.id,
                proto.name,
                result.value,
                result.bitLength,
                result.pulseLength
            );
            return result;
        }
    }

    // --- Step 3: Try dedicated KeeLoq decoder (Phase 7) ---
    RF_DBG_DECODE("  syncless and standard protos failed, trying KeeLoq decoder...");
    if (try_decode_keeloq(flat.data(), flatCount, tolerance, result)) { return result; }

    // No protocol matched
    RF_DBG_DECODE(
        "  NO PROTOCOL MATCHED for %u symbols / %u flat timings", (unsigned)count, (unsigned)flatCount
    );
    result.clear();
    return result;
}

RfDecodeResult rf_rmt_rx_decode(int tolerance) {
    return rf_rmt_rx_decode_symbols(s_last_symbols, s_last_count, tolerance);
}

// ---------------------------------------------------------------------------
// RAW data extraction
// ---------------------------------------------------------------------------

std::vector<int> rf_rmt_rx_symbols_to_timings(const rmt_symbol_word_t *symbols, size_t count) {
    std::vector<int> timings;
    timings.reserve(count * 2);

    for (size_t i = 0; i < count; ++i) {
        // duration0 with level0
        if (symbols[i].duration0 > 0) {
            int sign = symbols[i].level0 ? 1 : -1;
            timings.push_back(sign * static_cast<int>(symbols[i].duration0));
        }
        // duration1 with level1
        if (symbols[i].duration1 > 0) {
            int sign = symbols[i].level1 ? 1 : -1;
            timings.push_back(sign * static_cast<int>(symbols[i].duration1));
        }
    }

    return timings;
}

String rf_rmt_rx_symbols_to_raw_string(const rmt_symbol_word_t *symbols, size_t count) {
    auto timings = rf_rmt_rx_symbols_to_timings(symbols, count);

    String out;
    out.reserve(timings.size() * 6); // rough estimate

    for (size_t i = 0; i < timings.size(); ++i) {
        if (i > 0) out += ' ';
        out += String(timings[i]);
    }

    return out;
}

String rf_rmt_rx_get_raw_string() {
    if (s_last_count == 0) return "";
    return rf_rmt_rx_symbols_to_raw_string(s_last_symbols, s_last_count);
}
