/**
 * @file rf_rmt_tx.cpp
 * @brief RMT-based RF transmitter implementation.
 *
 * Replaces RCSwitch_send(), RCSwitch_RAW_send(), RCSwitch_RAW_Bit_send()
 * with hardware-timed transmission via the ESP-IDF RMT peripheral.
 *
 * Key difference from IR: RF OOK/ASK uses **no carrier modulation**.
 * The RMT channel directly drives the GPIO HIGH/LOW.
 *
 * Migration: RF_RMT_MIGRATION_STUDY.md — Phase 3
 */

#include "rf_rmt_tx.h"
#include "rf_debug.h"
#include <cstring>
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "rf_rmt_tx";

// ---------------------------------------------------------------------------
// Module state (lazy-init, single channel at a time)
// ---------------------------------------------------------------------------
static rmt_channel_handle_t s_tx_channel = nullptr;
static rmt_encoder_handle_t s_copy_encoder = nullptr;
static gpio_num_t s_tx_pin = GPIO_NUM_NC;

// Maximum RMT symbol duration at 1 MHz resolution = 32767 µs (15-bit field).
// Longer pulses must be split into multiple symbols.
static constexpr uint16_t RMT_MAX_DURATION = 32767;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * Create one rmt_symbol_word_t from a HIGH/LOW duration pair (µs).
 * If either duration exceeds RMT_MAX_DURATION it is clamped — the caller
 * must handle splitting if exact long durations are required.
 */
static inline rmt_symbol_word_t make_symbol(uint16_t high_us, uint16_t low_us) {
    rmt_symbol_word_t sym = {};
    sym.duration0 = (high_us > RMT_MAX_DURATION) ? RMT_MAX_DURATION : high_us;
    sym.level0 = 1;
    sym.duration1 = (low_us > RMT_MAX_DURATION) ? RMT_MAX_DURATION : low_us;
    sym.level1 = 0;
    return sym;
}

/**
 * Push a long HIGH+LOW pulse that may exceed the 15-bit RMT limit.
 * Splits into multiple symbols as needed, keeping the logic level correct.
 */
static void push_long_pulse(std::vector<rmt_symbol_word_t> &out, uint32_t high_us, uint32_t low_us) {
    // --- HIGH portion ---
    while (high_us > RMT_MAX_DURATION) {
        rmt_symbol_word_t sym = {};
        sym.duration0 = RMT_MAX_DURATION;
        sym.level0 = 1;
        sym.duration1 = 0;
        sym.level1 = 1; // keep HIGH for continuation
        out.push_back(sym);
        high_us -= RMT_MAX_DURATION;
    }
    // --- LOW portion ---
    // First symbol carries the remaining HIGH + start of LOW
    if (low_us <= RMT_MAX_DURATION) {
        out.push_back(make_symbol(static_cast<uint16_t>(high_us), static_cast<uint16_t>(low_us)));
        return;
    }
    // LOW also needs splitting
    rmt_symbol_word_t sym = {};
    sym.duration0 = static_cast<uint16_t>(high_us);
    sym.level0 = 1;
    sym.duration1 = RMT_MAX_DURATION;
    sym.level1 = 0;
    out.push_back(sym);
    low_us -= RMT_MAX_DURATION;

    while (low_us > RMT_MAX_DURATION) {
        rmt_symbol_word_t s = {};
        s.duration0 = RMT_MAX_DURATION;
        s.level0 = 0;
        s.duration1 = 0;
        s.level1 = 0;
        out.push_back(s);
        low_us -= RMT_MAX_DURATION;
    }
    if (low_us > 0) {
        rmt_symbol_word_t s = {};
        s.duration0 = static_cast<uint16_t>(low_us);
        s.level0 = 0;
        s.duration1 = 0;
        s.level1 = 0;
        out.push_back(s);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool rf_rmt_tx_init(gpio_num_t pin) {
    RF_DBG_TX("init: GPIO=%d (current=%d)", (int)pin, (int)s_tx_pin);
    // Already initialised on the same pin? Reuse.
    if (s_tx_channel && pin == s_tx_pin) {
        RF_DBG_TX("init: already active on same pin, reusing");
        return true;
    }

    // Different pin — tear down first.
    if (s_tx_channel) rf_rmt_tx_deinit();

    // --- Create TX channel ---
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num = pin;
    tx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz = 1 * 1000 * 1000; // 1 µs tick
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.trans_queue_depth = 1;
    tx_cfg.flags.invert_out = false;
    tx_cfg.flags.with_dma = false;
    tx_cfg.flags.io_loop_back = false;
    tx_cfg.flags.io_od_mode = false;

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
        s_tx_channel = nullptr;
        return false;
    }

    // --- Create copy encoder (sends raw symbol arrays as-is) ---
    rmt_copy_encoder_config_t enc_cfg = {};
    err = rmt_new_copy_encoder(&enc_cfg, &s_copy_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        rmt_del_channel(s_tx_channel);
        s_tx_channel = nullptr;
        return false;
    }

    // --- Enable ---
    err = rmt_enable(s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
        rmt_del_encoder(s_copy_encoder);
        rmt_del_channel(s_tx_channel);
        s_copy_encoder = nullptr;
        s_tx_channel = nullptr;
        return false;
    }

    s_tx_pin = pin;
    ESP_LOGI(TAG, "TX channel ready on GPIO %d", (int)pin);
    RF_DBG_TX("init: SUCCESS — TX channel ready, GPIO=%d, mem_block=64 symbols, resolution=1MHz", (int)pin);
    return true;
}

void rf_rmt_tx_deinit() {
    RF_DBG_TX("deinit: pin=%d, had_channel=%d", (int)s_tx_pin, s_tx_channel != nullptr);
    if (s_tx_channel) {
        rmt_disable(s_tx_channel);
        if (s_copy_encoder) {
            rmt_del_encoder(s_copy_encoder);
            s_copy_encoder = nullptr;
        }
        rmt_del_channel(s_tx_channel);
        s_tx_channel = nullptr;
    }
    s_tx_pin = GPIO_NUM_NC;
    RF_DBG_TX("deinit: complete");
}

// ---------------------------------------------------------------------------
// Symbol encoding
// ---------------------------------------------------------------------------

std::vector<rmt_symbol_word_t>
rf_rmt_encode_protocol(uint64_t data, uint8_t bits, const RfProtocolDef &proto, uint16_t te) {
    std::vector<rmt_symbol_word_t> symbols;
    symbols.reserve(bits + 2); // data bits + sync

    RF_DBG_TX(
        "encode: proto=%d (%s), data=0x%llX, bits=%u, TE=%u µs, inv=%d",
        proto.id,
        proto.name,
        data,
        bits,
        te,
        proto.invertedSignal
    );

    const bool inv = proto.invertedSignal;

    // Encode each data bit (MSB first, same as RCSwitch)
    for (int i = bits - 1; i >= 0; --i) {
        const RfHighLow &hl = (data & (1ULL << i)) ? proto.one : proto.zero;
        uint32_t first_us = static_cast<uint32_t>(hl.high) * te;
        uint32_t second_us = static_cast<uint32_t>(hl.low) * te;

        if (!inv) {
            // Normal: first = HIGH, second = LOW
            push_long_pulse(symbols, first_us, second_us);
        } else {
            // Inverted: first = LOW, second = HIGH  → swap for RMT
            // RMT symbol: level0=HIGH always comes first,
            // so we emit: HIGH=second_us, LOW=first_us
            push_long_pulse(symbols, second_us, first_us);
        }
    }

    // Sync / gap pulse (acts as frame separator)
    if (proto.syncFactor.high != 0 || proto.syncFactor.low != 0) {
        uint32_t sync_first = static_cast<uint32_t>(proto.syncFactor.high) * te;
        uint32_t sync_second = static_cast<uint32_t>(proto.syncFactor.low) * te;

        if (!inv) {
            push_long_pulse(symbols, sync_first, sync_second);
        } else {
            push_long_pulse(symbols, sync_second, sync_first);
        }
    }

    RF_DBG_TX("encode: produced %u RMT symbols (data + sync)", (unsigned)symbols.size());
    rf_dbg_dump_symbols("TX encoded frame", symbols.data(), symbols.size());
    return symbols;
}

std::vector<rmt_symbol_word_t> rf_rmt_timings_to_symbols(const int *timings, size_t count) {
    std::vector<rmt_symbol_word_t> symbols;
    symbols.reserve((count + 1) / 2);

    size_t i = 0;
    while (i < count) {
        int t0 = timings[i];
        int t1 = (i + 1 < count) ? timings[i + 1] : 0;

        // Convention: positive = HIGH, negative = LOW
        // We need to pair them into (HIGH, LOW) symbols.
        // If t0 is positive → HIGH duration, t1 should be negative → LOW duration
        // If t0 is negative → LOW duration, need to emit (0-HIGH, |t0|-LOW)

        if (t0 >= 0 && t1 <= 0) {
            // Normal pair: HIGH then LOW
            push_long_pulse(symbols, static_cast<uint32_t>(t0), static_cast<uint32_t>(-t1));
            i += 2;
        } else if (t0 >= 0 && t1 > 0) {
            // Two consecutive HIGHs — emit first as HIGH-only
            push_long_pulse(symbols, static_cast<uint32_t>(t0), 0);
            i += 1;
        } else if (t0 < 0 && t1 >= 0) {
            // Starts with LOW — emit as (0-HIGH, |t0|-LOW)
            push_long_pulse(symbols, 0, static_cast<uint32_t>(-t0));
            i += 1;
        } else {
            // Two consecutive LOWs — emit first as LOW-only
            push_long_pulse(symbols, 0, static_cast<uint32_t>(-t0));
            i += 1;
        }
    }

    return symbols;
}

// ---------------------------------------------------------------------------
// Transmit helpers
// ---------------------------------------------------------------------------

/**
 * Send a pre-built symbol array via the RMT channel.
 * Blocks until transmission is complete.
 */
static bool rmt_send_symbols(const rmt_symbol_word_t *syms, size_t num_symbols) {
    if (!s_tx_channel || !s_copy_encoder || num_symbols == 0) {
        RF_DBG_TX(
            "send_symbols: SKIP — channel=%p encoder=%p syms=%u",
            s_tx_channel,
            s_copy_encoder,
            (unsigned)num_symbols
        );
        return false;
    }

    RF_DBG_TX(
        "send_symbols: transmitting %u symbols (%u bytes)",
        (unsigned)num_symbols,
        (unsigned)(num_symbols * sizeof(rmt_symbol_word_t))
    );

    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0; // no hardware loop — we handle repeats ourselves

    esp_err_t err =
        rmt_transmit(s_tx_channel, s_copy_encoder, syms, num_symbols * sizeof(rmt_symbol_word_t), &tx_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
        return false;
    }

    err = rmt_tx_wait_all_done(s_tx_channel, pdMS_TO_TICKS(5000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_tx_wait_all_done failed: %s", esp_err_to_name(err));
        return false;
    }

    RF_DBG_TX("send_symbols: transmission complete (%u symbols sent)", (unsigned)num_symbols);
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool rf_rmt_tx_protocol(
    uint64_t data, uint8_t bits, uint8_t protocolId, uint16_t te_override, uint8_t repeats
) {
    RF_DBG_TX(
        "tx_protocol: data=0x%llX, bits=%u, proto=%u, te_override=%u, repeats=%u",
        data,
        bits,
        protocolId,
        te_override,
        repeats
    );
    const RfProtocolDef *proto = rf_protocol_by_id(protocolId);
    if (!proto) {
        ESP_LOGE(TAG, "Unknown protocol ID %d", protocolId);
        return false;
    }

    uint16_t te = te_override ? te_override : proto->pulseLength;

    // Build one frame
    auto symbols = rf_rmt_encode_protocol(data, bits, *proto, te);
    if (symbols.empty()) {
        ESP_LOGE(TAG, "Encoding produced empty symbol array");
        return false;
    }

    // Transmit with repeats
    RF_DBG_TX(
        "tx_protocol: sending %u repeats of %u symbols (TE=%u µs)", repeats, (unsigned)symbols.size(), te
    );
    for (uint8_t r = 0; r < repeats; ++r) {
        if (!rmt_send_symbols(symbols.data(), symbols.size())) {
            RF_DBG_TX("tx_protocol: FAILED at repeat %u/%u", r + 1, repeats);
            return false;
        }
    }

    // Ensure line goes LOW after transmission
    // (RMT should already end LOW for non-inverted protocols,
    //  but add a tiny LOW pulse just in case)
    gpio_set_level(s_tx_pin, 0);

    RF_DBG_TX("tx_protocol: SUCCESS — sent %u repeats of proto %u", repeats, protocolId);
    return true;
}

bool rf_rmt_tx_raw(const int *timings, size_t count) {
    if (!timings) return false;

    // Auto-detect count if not provided
    if (count == 0) {
        while (timings[count] != 0) ++count;
    }
    if (count == 0) {
        RF_DBG_TX("tx_raw: empty timing array");
        return false;
    }

    RF_DBG_TX("tx_raw: %u timings", (unsigned)count);
    rf_dbg_dump_signed_timings("TX raw input", timings, count);

    auto symbols = rf_rmt_timings_to_symbols(timings, count);
    if (symbols.empty()) return false;

    bool ok = rmt_send_symbols(symbols.data(), symbols.size());

    gpio_set_level(s_tx_pin, 0);
    RF_DBG_TX(
        "tx_raw: %s (%u timings → %u symbols)",
        ok ? "SUCCESS" : "FAILED",
        (unsigned)count,
        (unsigned)symbols.size()
    );
    return ok;
}

bool rf_rmt_tx_binraw(const char *bitString, uint16_t te) {
    if (!bitString || te == 0) return false;

    size_t len = strlen(bitString);
    if (len == 0) return false;

    RF_DBG_TX("tx_binraw: %u bits, TE=%u µs, first 32 chars: %.32s", (unsigned)len, te, bitString);

    // Build symbols: each bit becomes a (level, te) half-symbol.
    // We pair consecutive bits into full symbols.
    std::vector<rmt_symbol_word_t> symbols;
    symbols.reserve((len + 1) / 2);

    for (size_t i = 0; i < len; i += 2) {
        rmt_symbol_word_t sym = {};
        sym.level0 = (bitString[i] == '1') ? 1 : 0;
        sym.duration0 = te;

        if (i + 1 < len) {
            sym.level1 = (bitString[i + 1] == '1') ? 1 : 0;
            sym.duration1 = te;
        } else {
            // Odd number of bits — final half
            sym.level1 = 0;
            sym.duration1 = 0;
        }
        symbols.push_back(sym);
    }

    bool ok = rmt_send_symbols(symbols.data(), symbols.size());
    gpio_set_level(s_tx_pin, 0);
    RF_DBG_TX(
        "tx_binraw: %s (%u bits → %u symbols)",
        ok ? "SUCCESS" : "FAILED",
        (unsigned)len,
        (unsigned)symbols.size()
    );
    return ok;
}
