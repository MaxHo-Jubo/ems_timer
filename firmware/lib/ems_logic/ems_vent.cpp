// EMS Timer — 通氣節拍器決策純函式實作
// 見 ems_vent.h 註解
#include "ems_vent.h"

namespace ems {

VentTickAction decideVentTickAction(
    uint32_t now,
    uint32_t lastTickMs,
    uint32_t tickIntervalMs)
{
    VentTickAction action = { false };

    // STEP 01: 節拍器尚未啟動（lastTickMs == 0）→ 立即 fire 第一次 tick
    //   caller 隨後應把 lastTickMs 更新為 now，後續依週期判斷
    if (lastTickMs == 0) {
        action.fireBeep = true;
        return action;
    }

    // STEP 02: 到達週期 → fire
    //   uint32_t 減法會自動處理 millis() wraparound（49.7 天週期無縫銜接）
    if (now - lastTickMs >= tickIntervalMs) {
        action.fireBeep = true;
    }

    return action;
}

} // namespace ems
