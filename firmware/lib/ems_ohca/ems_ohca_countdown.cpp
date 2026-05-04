// EMS DoseSync Pro — Phase A: OHCA EPI 倒數引擎實作
// 對應 header：ems_ohca_countdown.h
#include "ems_ohca_countdown.h"

namespace ems {

/**
 * 邊緣偵測輔助：判斷在 (prev, curr] 區間內是否跨越任何 marker
 *
 * Markers 為 first_marker, first_marker+interval, first_marker+2*interval, ...
 *
 * 規則：
 *   curr < first_marker        → 還沒到首個 marker，回 false
 *   prev < first_marker ≤ curr → 跨越首個 marker，回 true
 *   prev, curr 都 ≥ first_marker → 比較 marker index
 */
static bool crossedBeepMarker(uint32_t prev_since_ms,
                              uint32_t curr_since_ms,
                              uint32_t first_marker_ms,
                              uint32_t interval_ms) {
    // STEP 01: 還沒到首個 marker → 不觸發
    if (curr_since_ms < first_marker_ms) {
        return false;
    }
    // STEP 02: 首次跨越 first_marker（prev 在 marker 前）
    if (prev_since_ms < first_marker_ms) {
        return true;
    }
    // STEP 03: prev / curr 都已過首個 marker → 比較 marker index
    uint32_t prev_idx = (prev_since_ms - first_marker_ms) / interval_ms;
    uint32_t curr_idx = (curr_since_ms - first_marker_ms) / interval_ms;
    return curr_idx > prev_idx;
}

/**
 * 計算顯示倒數剩餘 ms：EPI_CYCLE_MS - since（clamp 到 0）
 */
static uint32_t computeRemainingMs(uint32_t since_last_epi_ms) {
    if (since_last_epi_ms >= EPI_CYCLE_MS) {
        return 0;
    }
    return EPI_CYCLE_MS - since_last_epi_ms;
}

ohca_phase_t advanceOhcaPhase(ohca_phase_t current,
                              uint32_t since_last_epi_ms) {
    // STEP 01: WAIT_FIRST_EPI 必須由外部 EPI confirm 轉換，計時不影響
    if (current == OHCA_PHASE_WAIT_FIRST_EPI) {
        return OHCA_PHASE_WAIT_FIRST_EPI;
    }
    // STEP 02: 依測試計劃 §3.1 邊界推導 phase
    if (since_last_epi_ms <= OHCA_COUNTDOWN_END_MS) {
        return OHCA_PHASE_COUNTDOWN;
    }
    if (since_last_epi_ms < EPI_CYCLE_MS) {
        return OHCA_PHASE_WARNING;
    }
    if (since_last_epi_ms <= OHCA_ALARMING_END_MS) {
        return OHCA_PHASE_ALARMING;
    }
    return OHCA_PHASE_OVERTIME;
}

ohca_output_t decideOhcaOutput(ohca_phase_t phase,
                               uint32_t prev_since_ms,
                               uint32_t since_last_epi_ms) {
    // STEP 01: 預設輸出全部關閉 / 0 / nullptr
    ohca_output_t out = {};
    out.display_label = nullptr;

    switch (phase) {
        // STEP 02.01: WAIT_FIRST_EPI — 待本機 EPI，全部 idle
        case OHCA_PHASE_WAIT_FIRST_EPI:
            // 所有欄位維持預設
            break;

        // STEP 02.02: COUNTDOWN — 倒數中，僅顯示剩餘時間
        case OHCA_PHASE_COUNTDOWN:
            out.display_remaining_ms = computeRemainingMs(since_last_epi_ms);
            break;

        // STEP 02.03: WARNING — 1 分鐘預警，黃慢閃 + 每 15s 短嗶
        case OHCA_PHASE_WARNING:
            out.display_label        = "請準備給藥";
            out.led_yellow_slow      = true;
            out.display_remaining_ms = computeRemainingMs(since_last_epi_ms);
            // WARNING 首個 marker：since=180001（COUNTDOWN_END+1）
            out.buzz_short = crossedBeepMarker(
                prev_since_ms, since_last_epi_ms,
                OHCA_COUNTDOWN_END_MS + 1,
                EPI_WARN_BEEP_INTERVAL_MS);
            break;

        // STEP 02.04: ALARMING — 連續發報，紅快閃 + 震動 + 畫面閃
        case OHCA_PHASE_ALARMING:
            out.display_label         = "請給藥";
            out.buzz_alarm_continuous = true;
            out.led_red_fast          = true;
            out.vibrate               = true;
            out.screen_flash          = true;
            out.display_remaining_ms  = 0;
            break;

        // STEP 02.05: OVERTIME — 累計時間，紅慢閃 + 每 15s 短嗶
        case OHCA_PHASE_OVERTIME:
            out.led_red_slow         = true;
            out.display_label        = nullptr;            // 畫面顯示累計時間
            out.display_remaining_ms = since_last_epi_ms;  // 累計值（不是剩餘）
            // OVERTIME 首個 marker：since=245001（ALARMING_END+1）
            out.buzz_short = crossedBeepMarker(
                prev_since_ms, since_last_epi_ms,
                OHCA_ALARMING_END_MS + 1,
                EPI_ALARM_REMIND_INTERVAL_MS);
            break;
    }
    return out;
}

} // namespace ems
