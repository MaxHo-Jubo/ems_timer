#include "ems_countdown.h"

namespace ems {

MedCountdownAction decideMedCountdownAction(
    uint32_t now,
    uint32_t countdownStartMs,
    bool     reminderActive,
    bool     warn1MinTriggered,
    uint32_t lastReminderBeepMs,
    uint32_t countdownTotalMs,
    uint32_t warning1MinMs,
    uint32_t reminderRepeatMs) {

    MedCountdownAction action = { false, false, false };

    // STEP 01: 倒數未啟動 → 無動作
    if (countdownStartMs == 0) {
        return action;
    }

    // STEP 02: 計算 elapsed（uint32_t 自動處理 millis() wrap-around）
    uint32_t elapsed = now - countdownStartMs;

    // STEP 03: 1 分鐘中途警示（一次性，只在未進入強提醒前觸發）
    //   條件：尚未進入 reminderActive + 尚未觸發過 warn1Min + elapsed > 0
    //         且剩餘時間 <= warning1MinMs
    if (!reminderActive && !warn1MinTriggered && elapsed > 0) {
        uint32_t remaining = (elapsed >= countdownTotalMs)
                             ? 0
                             : (countdownTotalMs - elapsed);
        if (remaining <= warning1MinMs) {
            action.fireWarn1Min = true;
        }
    }

    // STEP 04: 尚未到時 + 未在強提醒 → 回傳（可能攜帶 warn1Min）
    if (elapsed < countdownTotalMs && !reminderActive) {
        return action;
    }

    // STEP 05: 首次到時 → 觸發首次強提醒（不繼續檢查 repeat）
    //   注意：若本 cycle 同時觸發 warn1Min 與 reminderStart，保留兩者
    if (!reminderActive && elapsed >= countdownTotalMs) {
        action.fireReminderStart = true;
        return action;
    }

    // STEP 06: 已在強提醒狀態 → 檢查重複提醒週期
    if (reminderActive && (now - lastReminderBeepMs >= reminderRepeatMs)) {
        action.fireReminderRepeat = true;
    }

    return action;
}

} // namespace ems
