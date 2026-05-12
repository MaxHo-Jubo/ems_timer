// EMS DoseSync Pro — Phase C: 6 秒通氣節奏實作
//
// 對應 header: ems_vent_metronome.h
#include "ems_vent_metronome.h"

namespace ems {

// ============================================================
//  computeVentBeat
// ============================================================

vent_beat_t computeVentBeat(uint32_t since_start_ms) {
    uint32_t beat_idx = (since_start_ms / VENT_BEAT_INTERVAL_MS) % 6;
    return (vent_beat_t)beat_idx;
}

// ============================================================
//  decideVentOutput（純函式 + 邊緣偵測）
// ============================================================

vent_output_t decideVentOutput(uint32_t prev_since_ms,
                               uint32_t since_start_ms,
                               uint8_t  volume) {
    vent_output_t out;
    out.buzz_short        = false;
    out.buzz_emphasis     = false;
    out.led_red_flash     = false;
    out.screen_invert_red = false;

    // STEP 01: 計算當前 beat（display_number 由 beat 決定，不需預設值）
    vent_beat_t cur = computeVentBeat(since_start_ms);
    out.display_number = (uint8_t)cur + 1;

    // STEP 02: BEAT_1 視覺輸出（不受 volume 影響）
    if (cur == VENT_BEAT_1) {
        out.led_red_flash     = true;
        out.screen_invert_red = true;
    }

    // STEP 03: 邊緣偵測 — 跨 beat 邊界才觸發 buzz
    //   special case: prev=0 && since=0 視為剛啟動，無 buzz（避免初始化誤觸發）
    bool init_tick = (prev_since_ms == 0 && since_start_ms == 0);
    if (!init_tick) {
        vent_beat_t prev = computeVentBeat(prev_since_ms);
        bool crossed = (prev != cur) || (since_start_ms - prev_since_ms >= VENT_CYCLE_MS);

        if (crossed && volume > 0) {
            if (cur == VENT_BEAT_1) {
                out.buzz_emphasis = true;
            } else {
                out.buzz_short = true;
            }
        }
    }

    return out;
}

// ============================================================
//  clampVentVolume
// ============================================================

uint8_t clampVentVolume(int16_t v) {
    if (v < (int16_t)VENT_VOLUME_MIN) return VENT_VOLUME_MIN;
    if (v > (int16_t)VENT_VOLUME_MAX) return VENT_VOLUME_MAX;
    return (uint8_t)v;
}

} // namespace ems
