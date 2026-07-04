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
//   - now()：present 時回 reading_（預設 {valid=false,0}=present 但未設有效時間）；
//            absent 時回 {false,0}
//   - set_epoch_ms()：present 時寫入 reading_={true,epoch} + 回 Ok；absent 時 no-op + 回 NotPresent
//   - advance_ms(d)：模擬 RTC 走時 d 毫秒（test 用，非 RtcBackend 介面）
//   - set_reading(valid,epoch)：直接注入讀值（模擬 present 但未設 / 已設 / 越界；test 用）

#pragma once

#include "ems_rtc.h"

namespace ems {

class MockRtcBackend : public RtcBackend {
public:
    bool is_present() const override { return present_; }

    RtcReading now() const override {
        if (!present_) {
            return {false, 0};  // 不在線 → 無有效時間
        }
        return reading_;
    }

    SetResult set_epoch_ms(uint64_t epoch) override {
        if (!present_) {
            return SetResult::NotPresent;  // 對齊 NullRtcBackend：absent 不寫入
        }
        reading_ = {true, epoch};  // 寫入即成為有效時間
        return SetResult::Ok;
    }

    // Test fixture helpers（非介面）
    void set_present(bool p) { present_ = p; }
    void advance_ms(uint64_t d) { reading_.epoch_ms += d; }
    // 直接注入讀值：模擬 present 但未設（valid=false）/ 已設 / 越界（valid=true 但超範圍）
    void set_reading(bool valid, uint64_t epoch) { reading_ = {valid, epoch}; }

private:
    bool present_ = true;
    RtcReading reading_ = {false, 0};  // 預設：present 但未設有效時間
};

}  // namespace ems
