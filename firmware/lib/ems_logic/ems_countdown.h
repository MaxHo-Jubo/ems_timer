// EMS Timer — 給藥倒數決策純函式
// 不依賴 Arduino.h / millis()，可在 native 測試環境直接編譯
//
// 對應 PM 規格：docs/pm-dev-spec.md §4.2 節律提醒引擎
//   - 4 分鐘給藥倒數 ±50ms
//   - 三階段內部狀態：COUNTING / WARNING / ALARMING
//   - ALARMING 階段連續發報直到按主鍵解除（取代每 30 秒週期提醒）
//
// 設計：把原 updateMedCountdown() 的純邏輯抽出，主函式只負責執行 side effect
//       （triggerBeep / triggerOledFlash / recordEvent / Serial.print）
#pragma once
#include <cstdint>

namespace ems {

/** 給藥倒數預設時長（PM 規格：4 分鐘） */
constexpr uint32_t DEFAULT_MED_COUNTDOWN_MS = 240000UL;
/** 剩餘多少 ms 觸發中途警示（PM 規格：1 分鐘前進 WARNING 階段） */
constexpr uint32_t DEFAULT_MED_WARN_1MIN_MS = 60000UL;
/**
 * ALARMING 階段連續發報的 pulse 間隔（ms）
 *
 * PM 規格（pm-flow-spec v1.4 §2）：ALARMING 階段「連續發報直到按主鍵解除」，
 * 取代 v1.3 前的「每 30 秒週期提醒」設計。
 *
 * 預設 1500ms 對應主韌體 3-pulse beep 序列（200ms on + 200ms off × 3 = 1.4s
 * + 0.1s 間隙），從使用者感知連續，不中斷硬體蜂鳴器的 pulse 序列。
 */
constexpr uint32_t DEFAULT_MED_ALARM_PULSE_MS = 1500UL;

/**
 * 給藥倒數內部階段（對應 PM 規格 pm-dev-spec §4.2 MED_PHASE enum）
 *
 * 注意：此 enum 不影響頂層狀態機（IDLE/RUNNING/PAUSE/END），僅表示倒數模組
 * 的內部狀態，供主韌體判斷 UI 呈現與按鍵行為（ALARMING 時短按主鍵 = 重置倒數；
 * 非 ALARMING 時短按主鍵 = 進入藥物選單）。
 */
enum MedPhase : uint8_t {
    MED_PHASE_NOT_STARTED = 0, ///< 倒數尚未啟動（countdownStartMs == 0）
    MED_PHASE_COUNTING    = 1, ///< 倒數中（剩餘 > 1 分鐘）
    MED_PHASE_WARNING     = 2, ///< 預警（剩餘 ≤ 1 分鐘，尚未歸零）
    MED_PHASE_ALARMING    = 3, ///< 發報（倒數歸零後，直到按主鍵解除）
};

/**
 * 倒數決策輸出：告訴呼叫方這個 loop cycle 要觸發什麼
 *
 * 旗標可能同時為 true（例如 elapsed 剛好跨過 TOTAL 邊界時同時觸發
 * warn1Min + reminderStart）
 */
struct MedCountdownAction {
    MedPhase phase;             ///< 本 cycle 判定的倒數階段
    bool fireWarn1Min;          ///< 觸發 1 分鐘 WARNING 進入警示（一次性）
    bool fireReminderStart;     ///< 觸發首次 ALARMING（3 嗶 + OLED 反色 + recordEvent）
    bool fireAlarmingPulse;     ///< ALARMING 連續發報的本 cycle pulse（間隔 alarmPulseMs）
};

/**
 * 給藥倒數決策：根據當前時間與狀態，決定本 cycle 要觸發什麼行為
 * 純函式：無副作用、不呼叫 millis()、不印 Serial；完全由參數決定輸出
 *
 * @param now                  目前時間戳（ms）
 * @param countdownStartMs     倒數起點時間戳；0 代表倒數尚未啟動
 * @param reminderActive       是否已進入 ALARMING 階段
 * @param warn1MinTriggered    1 分鐘 WARNING 是否已觸發過（一次性）
 * @param lastAlarmPulseMs     上次 ALARMING pulse 的時間戳（週期判斷用）
 * @param countdownTotalMs     倒數總時長（預設 240000）
 * @param warning1MinMs        剩餘多少 ms 進入 WARNING（預設 60000）
 * @param alarmPulseMs         ALARMING 連續發報 pulse 間隔（預設 1500）
 * @return MedCountdownAction  階段與三旗標
 */
MedCountdownAction decideMedCountdownAction(
    uint32_t now,
    uint32_t countdownStartMs,
    bool     reminderActive,
    bool     warn1MinTriggered,
    uint32_t lastAlarmPulseMs,
    uint32_t countdownTotalMs = DEFAULT_MED_COUNTDOWN_MS,
    uint32_t warning1MinMs    = DEFAULT_MED_WARN_1MIN_MS,
    uint32_t alarmPulseMs     = DEFAULT_MED_ALARM_PULSE_MS);

} // namespace ems
