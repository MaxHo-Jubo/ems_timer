// EMS DoseSync Pro — Phase A: 兩段確認模組（純函式）
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §6.2 / §7.2 / §8.2
// 工程規格：docs/pm-dev-spec.md §5
// 測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.2
//
// 用法：每個事件型別（EPI / 電擊 / Amio）各自持有獨立 instance：
//   two_step_confirm_t s = {};
//   twoStepConfirm_init(&s, 5000);                 // 5 秒 timeout
//   bool ok = twoStepConfirm_press(&s, now_ms);    // 按下時呼叫
//   twoStepConfirm_tick(&s, now_ms);               // 主迴圈每 tick 呼叫，逾時清 armed
#pragma once
#include <cstdint>

namespace ems {

constexpr uint32_t TWO_STEP_DEFAULT_TIMEOUT_MS = 5000;

typedef struct {
    uint32_t first_press_ms;  // 第一次按下的 now_ms（armed 期間有效）
    uint32_t timeout_ms;      // 確認逾時 ms（含；測試計劃 §3.2.2）
    bool     armed;           // 是否在等待第二次按
} two_step_confirm_t;

/**
 * 初始化（清 0 + 設定 timeout）
 * @param s          待初始化的 struct
 * @param timeout_ms 兩段確認 timeout（建議 5000；TWO_STEP_DEFAULT_TIMEOUT_MS）
 */
void twoStepConfirm_init(two_step_confirm_t* s, uint32_t timeout_ms);

/**
 * 按下時呼叫
 *
 * @return  true  → 第二段確認成立（armed 已清，可建立紀錄 / 啟動倒數）
 *          false → 第一段（armed 設立，需顯示確認對話框等使用者再按一次）
 *
 * 行為（測試計劃 §3.2）：
 *   未 armed              → armed=true,  first_press_ms=now,            回 false
 *   armed 且 elapsed ≤ to → armed=false,                                回 true
 *   armed 且 elapsed > to → 視為新 cycle：first_press_ms=now, armed 仍 true, 回 false
 */
bool twoStepConfirm_press(two_step_confirm_t* s, uint32_t now_ms);

/**
 * 主迴圈每 tick 呼叫；若 armed 已逾時則自動清除（避免下次 press 誤判）
 */
void twoStepConfirm_tick(two_step_confirm_t* s, uint32_t now_ms);

} // namespace ems
