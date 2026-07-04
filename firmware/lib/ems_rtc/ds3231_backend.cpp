// EMS DoseSync Pro — DS3231 backend 實作（硬體相依，僅 ARDUINO env 編譯）
// 對應 plan：docs/ds3231-integration-plan.md §3.3

#ifdef ARDUINO

#include "ds3231_backend.h"

namespace ems {

bool DS3231Backend::begin(TwoWire& wire) {
    // STEP 01: 嘗試與 0x68 握手；RTClib 內部會驗 OSF flag 與通訊
    //   注意 RTClib begin() 只回單一 bool，CR2032 失效（OSF=1）/I2C NACK/接線錯
    //   三種故障都收斂成 false。實機 OSF=1 的狀況上層會看到 now_epoch_ms()=0
    //   的 fallback，main.cpp 的「present but time not set」log 即承載這個信號
    present_ = rtc_.begin(&wire);
    return present_;
}

RtcReading DS3231Backend::now() const {
    // STEP 01: 沒偵測到硬體 → valid=false，caller fallback 到「未對時」慣例（spec §4.1）
    if (!present_) {
        return {false, 0};
    }
    // STEP 02: DS3231 只有秒解析度，ms 部位永遠是 0；32-bit unixtime 覆蓋到 2106 年。
    //   valid=true 只保證「讀到時間」；爛資料（年份溢位）由 caller 的範圍檢查擋。
    return {true, static_cast<uint64_t>(rtc_.now().unixtime()) * 1000ULL};
}

SetResult DS3231Backend::set_epoch_ms(uint64_t epoch) {
    // STEP 01: 不在線就 no-op 回 NotPresent（呼應 plan §5.1 write-back 的降級分支）
    if (!present_) {
        return SetResult::NotPresent;
    }
    // STEP 02: 截掉 sub-second（DS3231 沒地方存），轉成 Unix 秒餵 DateTime ctor
    //   限制：RTClib adjust() 是 void，I2C NACK 無法偵測。回 Ok 表「已送出 I2C
    //   write 序列」而非「對方確認寫入」。如需強驗證（回 IoError）需在 caller 端做
    //   read-back 比對（讀回 now() 與本次 epoch 在 ±2s 內），不在此 lib 強制以免
    //   每筆 BLE write-back 多一次 I2C 往返
    const uint32_t unix_seconds = static_cast<uint32_t>(epoch / 1000ULL);
    rtc_.adjust(DateTime(unix_seconds));
    return SetResult::Ok;
}

}  // namespace ems

#endif  // ARDUINO
