// EMS DoseSync Pro — Dev-Phase 3: DS3231 實體 RTC 包裝
//
// 對應 plan：docs/ds3231-integration-plan.md §3.3
// 硬體：DS3231 I2C 模組 @ 0x68，掛在 SDA=42 / SCL=41（gpio-allocation.md §5.4）
//
// 設計：
//   - 整個 class 用 #ifdef ARDUINO 守護，避免 native env 拉 RTClib.h 失敗
//   - native env 仍可 #include 本檔（編譯為空 class declaration），但不應實際 new 物件
//   - 純邏輯由 NullRtcBackend / MockRtcBackend 涵蓋；DS3231Backend 本身依賴實機驗證
//
// 注意（plan §3.3）：DS3231Backend.cpp 不能進 native test（硬體相依）

#pragma once

#include "ems_rtc.h"

#ifdef ARDUINO
#include <RTClib.h>
#include <Wire.h>

namespace ems {

class DS3231Backend : public RtcBackend {
public:
    /**
     * 嘗試啟動 DS3231。caller 已先呼叫 Wire.begin(SDA, SCL)。
     * @return true 偵測到 0x68 設備；false 表 caller 應改掛 NullRtcBackend
     */
    bool begin(TwoWire& wire);

    bool is_present() const override { return present_; }
    RtcReading now() const override;
    SetResult set_epoch_ms(uint64_t epoch) override;

private:
    // RTClib 的 now() 非 const（內部走 I2C 讀），但本介面語意上是「讀取」。
    // 用 mutable 標記讓 const override 成立。
    mutable RTC_DS3231 rtc_{};
    bool present_ = false;
};

}  // namespace ems

#endif  // ARDUINO
