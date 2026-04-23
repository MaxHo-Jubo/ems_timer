#include "ems_countdown.h"

namespace ems {

/**
 * 判定當前倒數階段（不依賴副作用，純由輸入決定）
 *
 * @param countdownStartMs  倒數起點；0 = NOT_STARTED
 * @param reminderActive    是否已進入 ALARMING（呼叫方設定）
 * @param elapsed           now - countdownStartMs（uint32_t 自動 wrap）
 * @param countdownTotalMs  倒數總時長
 * @param warning1MinMs     進入 WARNING 的剩餘門檻
 */
static MedPhase computePhase(
    uint32_t countdownStartMs,
    bool     reminderActive,
    uint32_t elapsed,
    uint32_t countdownTotalMs,
    uint32_t warning1MinMs) {

    // STEP 01: 未啟動 → NOT_STARTED
    if (countdownStartMs == 0) {
        return MED_PHASE_NOT_STARTED;
    }

    // STEP 02: ALARMING 由呼叫方 reminderActive 旗標決定
    //   （呼叫方在本 cycle 看到 fireReminderStart 後會把 reminderActive 設 true，
    //     下次呼叫此函式時 phase 才會判定為 ALARMING）
    if (reminderActive) {
        return MED_PHASE_ALARMING;
    }

    // STEP 03: 依剩餘時間區分 COUNTING / WARNING
    uint32_t remaining = (elapsed >= countdownTotalMs)
                         ? 0
                         : (countdownTotalMs - elapsed);
    if (remaining <= warning1MinMs) {
        return MED_PHASE_WARNING;
    }
    return MED_PHASE_COUNTING;
}

MedCountdownAction decideMedCountdownAction(
    uint32_t now,
    uint32_t countdownStartMs,
    bool     reminderActive,
    bool     warn1MinTriggered,
    uint32_t lastAlarmPulseMs,
    uint32_t countdownTotalMs,
    uint32_t warning1MinMs,
    uint32_t alarmPulseMs) {

    MedCountdownAction action = { MED_PHASE_NOT_STARTED, false, false, false };

    // STEP 01: 倒數未啟動 → 無動作
    if (countdownStartMs == 0) {
        action.phase = MED_PHASE_NOT_STARTED;
        return action;
    }

    // STEP 02: 計算 elapsed（uint32_t 自動處理 millis() wrap-around）
    uint32_t elapsed = now - countdownStartMs;

    // STEP 03: 判定 phase（純由輸入決定）
    action.phase = computePhase(
        countdownStartMs, reminderActive, elapsed,
        countdownTotalMs, warning1MinMs);

    // STEP 04: 1 分鐘 WARNING 進入警示（一次性，只在未進入 ALARMING 前觸發）
    //   條件：尚未 reminderActive + 尚未觸發過 warn1Min + elapsed > 0
    //         且剩餘時間 <= warning1MinMs
    if (!reminderActive && !warn1MinTriggered && elapsed > 0) {
        uint32_t remaining = (elapsed >= countdownTotalMs)
                             ? 0
                             : (countdownTotalMs - elapsed);
        if (remaining <= warning1MinMs) {
            action.fireWarn1Min = true;
        }
    }

    // STEP 05: 尚未到時 + 未在 ALARMING → 回傳（可能攜帶 warn1Min）
    if (elapsed < countdownTotalMs && !reminderActive) {
        return action;
    }

    // STEP 06: 首次到時 → 觸發首次 ALARMING（不繼續檢查 pulse）
    //   若本 cycle 同時觸發 warn1Min 與 reminderStart，保留兩者
    if (!reminderActive && elapsed >= countdownTotalMs) {
        action.fireReminderStart = true;
        return action;
    }

    // STEP 07: 已在 ALARMING → 檢查連續發報 pulse 週期
    //   PM 規格 v1.4：連續發報直到按主鍵解除
    //   實作：每 alarmPulseMs 觸發一次 pulse（預設 1500ms，配合 3-pulse 序列）
    if (reminderActive && (now - lastAlarmPulseMs >= alarmPulseMs)) {
        action.fireAlarmingPulse = true;
    }

    return action;
}

} // namespace ems
