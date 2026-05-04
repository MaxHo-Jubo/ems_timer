// EMS DoseSync Pro — Phase A: 兩段確認模組實作
// 對應 header：ems_two_step_confirm.h
#include "ems_two_step_confirm.h"

namespace ems {

/**
 * 計算「現在」距離「第一次按」的時間差（rollover-safe uint32 減法）
 */
static uint32_t elapsedSinceFirst(const two_step_confirm_t* s, uint32_t now_ms) {
    return now_ms - s->first_press_ms;
}

void twoStepConfirm_init(two_step_confirm_t* s, uint32_t timeout_ms) {
    // STEP 01: 清 0 並套用 timeout
    s->first_press_ms = 0;
    s->armed          = false;
    s->timeout_ms     = timeout_ms;
}

bool twoStepConfirm_press(two_step_confirm_t* s, uint32_t now_ms) {
    // STEP 01: 未 armed → 進入 armed，紀錄首次時間
    if (!s->armed) {
        s->armed          = true;
        s->first_press_ms = now_ms;
        return false;
    }

    // STEP 02: armed 中 → 計算自首次按下經過時間
    uint32_t elapsed = elapsedSinceFirst(s, now_ms);

    // STEP 02.01: 在 timeout 內（含邊界）→ 確認成立，清 armed
    if (elapsed <= s->timeout_ms) {
        s->armed = false;
        return true;
    }

    // STEP 02.02: 已逾時 → 視為全新 cycle（重設 first_press），仍 armed，回 false
    s->first_press_ms = now_ms;
    return false;
}

void twoStepConfirm_tick(two_step_confirm_t* s, uint32_t now_ms) {
    // STEP 01: 未 armed 時不需處理
    if (!s->armed) {
        return;
    }
    // STEP 02: armed 但已逾時 → 清 armed
    if (elapsedSinceFirst(s, now_ms) > s->timeout_ms) {
        s->armed = false;
    }
}

} // namespace ems
