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
 * OHCA 案件「進行中」phase 判定。
 * @param s 待判定的 state
 * @return true 若 s ∈ { WAIT_FIRST_EPI, COUNTDOWN, WARNING, ALARMING, OVERTIME }
 *
 * 用於 SoT V1 §10.1 主鍵長按 3s 入口判斷與 END_CANCEL 還原合法 source 過濾。
 */
inline bool isOhcaInProgress(ohca_state_t s) {
    return s == OHCA_STATE_WAIT_FIRST_EPI ||
           s == OHCA_STATE_COUNTDOWN      ||
           s == OHCA_STATE_WARNING        ||
           s == OHCA_STATE_ALARMING       ||
           s == OHCA_STATE_OVERTIME;
}

/**
 * State machine transition（純函式）
 *
 * @param current           當前 state
 * @param event             輸入事件
 * @param since_last_epi_ms 自上次 EPI 確認以來的 ms（僅 TIMER_TICK 使用）
 * @param end_check_source  END_CHECK 的來源 state；END_CANCEL 時 fallback 用
 *                          caller 在進 END_CHECK 前應記錄當時的 ohcaState
 *                          僅當值為合法進行中 phase
 *                          (WAIT_FIRST_EPI/COUNTDOWN/WARNING/ALARMING/OVERTIME)
 *                          才會回到該 state；否則 fallback 至 OVERTIME
 * @return 下一個 state（無 transition 時回傳 current）
 *
 * 行為摘要（SoT V1 §10.1 / 測試計劃 §3.3）：
 *   MAIN_MENU + MAIN_BTN_SHORT          → START_FLASH
 *   START_FLASH + FLASH_TIMEOUT         → WAIT_FIRST_EPI
 *   WAIT_FIRST_EPI + EPI_CONFIRMED      → COUNTDOWN
 *   COUNTDOWN/WARNING/ALARMING/OVERTIME + EPI_CONFIRMED → COUNTDOWN（重啟）
 *   COUNTDOWN/WARNING/ALARMING/OVERTIME + TIMER_TICK    → 依 since 推進 phase
 *   WAIT_FIRST_EPI/COUNTDOWN/WARNING/ALARMING/OVERTIME + MAIN_BTN_LONG_3S
 *                                       → END_CHECK（SoT V1 §10.1：正式 OHCA 中皆可）
 *   END_CHECK + END_CONFIRM             → LOCKED
 *   END_CHECK + END_CANCEL              → end_check_source（還原到原 phase）
 *   LOCKED + TO_SUMMARY                 → SUMMARY
 *   LOCKED + 其他事件                   → noop（資料不可修改）
 *   ALARMING/OVERTIME + MAIN_BTN_SHORT  → noop（消音由外部處理；不建紀錄）
 *   電擊 / Amio confirm                 → noop（不重啟倒數，§23.2）
 */
ohca_state_t nextOhcaState(ohca_state_t current,
                           ohca_event_t event,
                           uint32_t     since_last_epi_ms,
                           ohca_state_t end_check_source = OHCA_STATE_OVERTIME);

/** OHCA 6 秒給氣 overlay 的執行期狀態（純資料鏡像，供 case-end 停止判定用） */
struct OhcaVentState {
    bool     overlay_enabled;  // overlay 是否啟用
    bool     paused;           // 是否暫停
    uint32_t start_ms;         // overlay 起始 millis（0 = 未跑）
};

/**
 * 依 OHCA state 轉移決定 6 秒給氣 overlay 是否該停（V1 §14.12：首次進 LOCKED =
 * 案件結束 → 自動停止）。純函式無 side effect；caller 把 3 個 vent global 打包
 * 傳入、拿回更新後的值寫回。
 *
 * @param prev     轉移前 state
 * @param next     轉移後 state
 * @param current  目前 vent overlay 執行期狀態
 * @return 首次進 LOCKED（prev != LOCKED && next == LOCKED）→ 全清 {false,false,0}；
 *         其他轉移（含 LOCKED→LOCKED / LOCKED→SUMMARY）→ 原值不變
 */
OhcaVentState ohcaVentAfterTransition(ohca_state_t prev,
                                      ohca_state_t next,
                                      OhcaVentState current);

} // namespace ems
