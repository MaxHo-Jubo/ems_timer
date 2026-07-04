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

/**
 * RTC 讀取結果。取代舊 now_epoch_ms() 用 0 同時表示「無有效時間」與
 * 「合法 1970-01-01 epoch」的 sentinel conflation。
 *
 * - valid=false：backend 不在線，或在線但從未設過有效時間。epoch_ms 此時無意義，
 *   caller 應走「未對時」fallback（spec §4.1），絕不可把 epoch_ms 當合法時戳。
 * - valid=true：epoch_ms 為 backend 回報的當下 epoch ms（仍可能越界，
 *   caller 需另做 floor/ceiling 範圍檢查——valid 只保證「有時間」，不保證「時間合理」）。
 */
struct RtcReading {
    bool     valid;     // 是否有有效時間；false 時 epoch_ms 不可當時戳
    uint64_t epoch_ms;  // valid=true 時的當下 epoch ms
};

/**
 * set_epoch_ms 寫回結果。取代舊 bool，區分三種語意讓 caller log 能精準分類
 * （NullBackend no-op 與實體寫入失敗不再混為 false）。
 *
 * - Ok：已送出寫入序列（成功）
 * - NotPresent：backend 不在線，寫入為 no-op（正常降級，非錯誤）
 * - IoError：backend 在線但寫入失敗（I2C NACK 等）。註：DS3231 走 RTClib
 *   adjust()（void 無回傳）目前偵測不到 NACK，故實務上不會回此值，保留給
 *   未來加 read-back verify 時使用（對齊 tasks/todo.md R3 配套）。
 */
enum class SetResult : uint8_t {
    Ok         = 0,
    NotPresent = 1,
    IoError    = 2,
};

class RtcBackend {
public:
    virtual ~RtcBackend() = default;

    /** 是否實體 RTC 在線（NullBackend 永遠 false） */
    virtual bool is_present() const = 0;

    /**
     * 讀取當前時間。回 RtcReading，valid=false 表無有效時間
     * （caller 走未對時 fallback，不要把 epoch_ms 當合法時戳）。
     */
    virtual RtcReading now() const = 0;

    /**
     * 設定 RTC 時間（用於 BLE time_sync 後反向寫回 DS3231）。
     * NullBackend 為 no-op 回 NotPresent。
     * @return SetResult — Ok（已送出）/ NotPresent（no-op）/ IoError（寫入失敗）
     */
    virtual SetResult set_epoch_ms(uint64_t epoch) = 0;
};

}  // namespace ems
