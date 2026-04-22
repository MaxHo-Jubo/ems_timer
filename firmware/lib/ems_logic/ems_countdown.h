// EMS Timer — 給藥倒數決策純函式
// 不依賴 Arduino.h / millis()，可在 native 測試環境直接編譯
//
// 對應 PM 規格：docs/pm-dev-spec.md §4.2 節律提醒引擎
//   - 4 分鐘給藥高提醒（誤差 < ±50ms）
//
// 設計：把原 updateMedCountdown() 的純邏輯抽出，主函式只負責執行 side effect
//       （triggerBeep / triggerOledFlash / recordEvent / Serial.print）
#pragma once
#include <cstdint>

namespace ems {

/** 給藥倒數預設時長（PM 規格：4 分鐘） */
constexpr uint32_t DEFAULT_MED_COUNTDOWN_MS       = 240000UL;
/** 剩餘多少 ms 觸發中途警示（PM 規格：1 分鐘前） */
constexpr uint32_t DEFAULT_MED_WARN_1MIN_MS       = 60000UL;
/** 到時後重複提醒間隔 */
constexpr uint32_t DEFAULT_MED_REMINDER_REPEAT_MS = 30000UL;

/**
 * 倒數決策輸出：告訴呼叫方這個 loop cycle 要觸發什麼
 * 三個旗標可能同時為 true（例如 elapsed 剛好跨過 TOTAL 邊界）
 */
struct MedCountdownAction {
    bool fireWarn1Min;       // 觸發 1 分鐘中途警示嗶聲（2 短嗶）
    bool fireReminderStart;  // 觸發首次強提醒（3 嗶 + OLED 反色 + recordEvent）
    bool fireReminderRepeat; // 觸發每 30s 重複提醒（3 嗶 + OLED 反色）
};

/**
 * 給藥倒數決策：根據當前時間與狀態，決定本 cycle 要觸發什麼行為
 * 純函式：無副作用、不呼叫 millis()、不印 Serial；完全由參數決定輸出
 *
 * @param now                  目前時間戳（ms）
 * @param countdownStartMs     倒數起點時間戳；0 代表倒數尚未啟動
 * @param reminderActive       是否已進入「歸零後強提醒」狀態
 * @param warn1MinTriggered    1 分鐘中途警示是否已觸發過（一次性）
 * @param lastReminderBeepMs   上次重複提醒嗶聲的時間戳（用於週期判斷）
 * @param countdownTotalMs     倒數總時長（預設 240000）
 * @param warning1MinMs        剩餘多少 ms 觸發中途警示（預設 60000）
 * @param reminderRepeatMs     重複提醒週期（預設 30000）
 * @return MedCountdownAction 三旗標
 */
MedCountdownAction decideMedCountdownAction(
    uint32_t now,
    uint32_t countdownStartMs,
    bool     reminderActive,
    bool     warn1MinTriggered,
    uint32_t lastReminderBeepMs,
    uint32_t countdownTotalMs = DEFAULT_MED_COUNTDOWN_MS,
    uint32_t warning1MinMs    = DEFAULT_MED_WARN_1MIN_MS,
    uint32_t reminderRepeatMs = DEFAULT_MED_REMINDER_REPEAT_MS);

} // namespace ems
