#include "ems_time.h"

namespace ems {

uint32_t computeTaskElapsedMs(uint32_t now,
                              uint32_t taskStartMs,
                              uint32_t pauseStartMs,
                              uint32_t totalPausedMs,
                              uint8_t  state) {
    // STEP 01: 任務尚未開始 → 0
    if (taskStartMs == 0) {
        return 0;
    }
    // STEP 02: 暫停中 → 以暫停起點為終點，排除累計暫停時間
    //          uint32_t 減法在 UINT32_MAX wrap 時仍得出正確的時間差
    if (state == STATE_PAUSE) {
        return (pauseStartMs - taskStartMs) - totalPausedMs;
    }
    // STEP 03: 執行中或其他非暫停狀態 → 以當下時間計算
    return (now - taskStartMs) - totalPausedMs;
}

} // namespace ems
