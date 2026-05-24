// EMS DoseSync Pro — Dev-Phase 3: RTC 抽象介面
//
// 對應 plan：docs/ds3231-integration-plan.md §3.1
// 測試入口：firmware/test/test_rtc/test_main.cpp
//
// 用途：
//   同一份韌體支援有/無 DS3231 兩種硬體配置。caller 拿 RtcBackend*
//   不分支處理，下游邏輯遇 now_epoch_ms()==0 自動 fallback 到既有
//   「未對時 timestamp = 0」慣例（spec §4.1）。
//
// 兩種具體實作：
//   - DS3231Backend ：實體 DS3231 I2C 模組（Wave 4，硬體相依）
//   - NullRtcBackend：無 RTC 硬體時的降級實作（Wave 1，本檔同 lib）

#pragma once

#include <cstdint>

namespace ems {

class RtcBackend {
public:
    virtual ~RtcBackend() = default;

    /** 是否實體 RTC 在線（NullBackend 永遠 false） */
    virtual bool is_present() const = 0;

    /**
     * 取目前 epoch milliseconds。NullBackend 或 DS3231 尚未設過時回 0。
     * 下游 caller 看到 0 應 fallback 到既有「未對時」邏輯（不要當合法時戳）。
     */
    virtual uint64_t now_epoch_ms() const = 0;

    /**
     * 設定 RTC 時間（用於 BLE time_sync 後反向寫回 DS3231）。
     * NullBackend 為 no-op 回 false。
     * @return true 成功寫回；false 表 backend 不支援或寫入失敗
     */
    virtual bool set_epoch_ms(uint64_t epoch) = 0;
};

}  // namespace ems
