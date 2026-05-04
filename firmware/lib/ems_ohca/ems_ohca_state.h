// EMS DoseSync Pro — Phase A: OHCA 子狀態機（純函式 transition table）
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §5–§11
// 工程規格：docs/pm-dev-spec.md §3
// 測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.3
//
// 設計原則：
//   - 純函式 transition table：input (current_state, event, since_ms) → next_state
//   - 不持有 timer / 不做 side effect；外層 caller 處理計時與 I/O
//   - 計時驅動的 phase 推進交給 ems_ohca_countdown::advanceOhcaPhase
//     此處 OHCA_EVT_TIMER_TICK 會 delegate 給該函式
//
// 重要不變式（測試計劃 §3.3）：
//   - 啟動倒數白名單：僅本機 EPI 確認可進 / 重啟 OHCA_STATE_COUNTDOWN
//     電擊 / Amio / 接手前 EPI / 純補登 EPI 都不重啟倒數
//   - LOCKED：所有事件 noop
//   - ALARMING / OVERTIME 主鍵短按：不轉 state（消音由外部處理）
#pragma once
#include <cstdint>
#include "ems_ohca_countdown.h"

namespace ems {

// ===== State 列舉 =====
typedef enum {
    OHCA_STATE_MAIN_MENU       = 0,
    OHCA_STATE_START_FLASH     = 1,  // 「案件開始 OHCA」1s 提示
    OHCA_STATE_WAIT_FIRST_EPI  = 2,
    OHCA_STATE_COUNTDOWN       = 3,
    OHCA_STATE_WARNING         = 4,
    OHCA_STATE_ALARMING        = 5,
    OHCA_STATE_OVERTIME        = 6,
    OHCA_STATE_END_CHECK       = 7,
    OHCA_STATE_LOCKED          = 8,
    OHCA_STATE_SUMMARY         = 9,
} ohca_state_t;

// ===== Event 列舉 =====
typedef enum {
    OHCA_EVT_MAIN_BTN_SHORT      = 0,   // 主鍵短按
    OHCA_EVT_MAIN_BTN_LONG_3S    = 1,   // 主鍵長按 3s（觸發結束前檢查）
    OHCA_EVT_EPI_CONFIRMED       = 2,   // 本機 EPI 兩段確認成立
    OHCA_EVT_SHOCK_CONFIRMED     = 3,   // 本機電擊兩段確認成立（不重啟倒數）
    OHCA_EVT_AMIO_CONFIRMED      = 4,   // Amio 兩段確認成立（不重啟倒數）
    OHCA_EVT_END_CONFIRM         = 5,   // END_CHECK 中選「完成並結束」
    OHCA_EVT_END_CANCEL          = 6,   // END_CHECK 中選「返回案件」
    OHCA_EVT_FLASH_TIMEOUT       = 7,   // START_FLASH 1s 結束（自動轉 WAIT_FIRST_EPI）
    OHCA_EVT_TIMER_TICK          = 8,   // 計時推進；delegate 至 advanceOhcaPhase
    OHCA_EVT_TO_SUMMARY          = 9,   // LOCKED → SUMMARY（外部翻頁）
} ohca_event_t;

// ===== State→phase 映射（給 TIMER_TICK 用） =====
ohca_phase_t mapStateToPhase(ohca_state_t s);
ohca_state_t mapPhaseToState(ohca_phase_t p);  // 反向：advance 完轉回 state

/**
 * State machine transition（純函式）
 *
 * @param current           當前 state
 * @param event             輸入事件
 * @param since_last_epi_ms 自上次 EPI 確認以來的 ms（僅 TIMER_TICK 使用）
 * @return 下一個 state（無 transition 時回傳 current）
 *
 * 行為摘要（測試計劃 §3.3）：
 *   MAIN_MENU + MAIN_BTN_SHORT          → START_FLASH
 *   START_FLASH + FLASH_TIMEOUT         → WAIT_FIRST_EPI
 *   WAIT_FIRST_EPI + EPI_CONFIRMED      → COUNTDOWN
 *   COUNTDOWN/WARNING/ALARMING/OVERTIME + EPI_CONFIRMED → COUNTDOWN（重啟）
 *   COUNTDOWN/WARNING/ALARMING/OVERTIME + TIMER_TICK    → 依 since 推進 phase
 *   OVERTIME + MAIN_BTN_LONG_3S         → END_CHECK
 *   END_CHECK + END_CONFIRM             → LOCKED
 *   END_CHECK + END_CANCEL              → OVERTIME（簡化：END_CHECK 只能來自 OVERTIME）
 *   LOCKED + TO_SUMMARY                 → SUMMARY
 *   LOCKED + 其他事件                   → noop（資料不可修改）
 *   ALARMING/OVERTIME + MAIN_BTN_SHORT  → noop（消音由外部處理；不建紀錄）
 *   電擊 / Amio confirm                 → noop（不重啟倒數，§23.2）
 */
ohca_state_t nextOhcaState(ohca_state_t current,
                           ohca_event_t event,
                           uint32_t     since_last_epi_ms);

} // namespace ems
