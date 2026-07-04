// EMS DoseSync Pro — Dev-Phase 3: RTC 無硬體降級實作
//
// 對應 plan：docs/ds3231-integration-plan.md §3.2
// 測試入口：firmware/test/test_rtc/test_main.cpp
//
// 用途：
//   I2C probe 沒偵測到 DS3231 時掛上 NullRtcBackend，讓上層 caller
//   無條件呼叫 backend 不需分支。所有 method 回降級值：
//     is_present()   → false
//     now()          → {valid=false, 0}（fallback 到 spec §4.1「未對時」）
//     set_epoch_ms() → NotPresent（no-op，不可寫入）

#pragma once

#include "ems_rtc.h"

namespace ems {

class NullRtcBackend : public RtcBackend {
public:
    bool is_present() const override { return false; }
    RtcReading now() const override { return {false, 0}; }
    SetResult set_epoch_ms(uint64_t /*epoch*/) override { return SetResult::NotPresent; }
};

}  // namespace ems
