// MockRtcBackend — test-only RtcBackend 實作
//
// 對齊 plan：docs/ds3231-integration-plan.md §3.4
//
// 用途：
//   integration test 注入可控 RTC 狀態，模擬「DS3231 在線 / 不在線 / 已對時 / 未對時 / 後續走時」。
//   不依賴硬體，純記憶體 fixture。
//
// 契約（對齊 NullRtcBackend）：
//   - is_present()：回 set_present() 設定值（預設 true）
//   - now_epoch_ms()：回 set_epoch_ms / advance_ms 累積的值（預設 0）
//   - set_epoch_ms()：present 時寫入 now_ms_ + 回 true；absent 時 no-op + 回 false（與 Null 一致）
//   - advance_ms(d)：模擬 RTC 走時 d 毫秒（test 用，非 RtcBackend 介面）

#pragma once

#include "ems_rtc.h"

namespace ems {

class MockRtcBackend : public RtcBackend {
public:
    bool is_present() const override { return present_; }
    uint64_t now_epoch_ms() const override { return now_ms_; }

    bool set_epoch_ms(uint64_t epoch) override {
        if (!present_) {
            return false;  // 對齊 NullRtcBackend：absent 不寫入
        }
        now_ms_ = epoch;
        return true;
    }

    // Test fixture helpers（非介面）
    void set_present(bool p) { present_ = p; }
    void advance_ms(uint64_t d) { now_ms_ += d; }

private:
    bool present_ = true;
    uint64_t now_ms_ = 0;
};

}  // namespace ems
