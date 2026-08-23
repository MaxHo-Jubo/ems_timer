#include "fuel_gauge_logic.h"

namespace ems {

uint16_t vcell_raw_to_mv(uint16_t raw) {
    // STEP 01: 取高 12 bit 的 A/D counts，乘上每 LSB 的電壓後取整
    const uint32_t counts = static_cast<uint32_t>(raw >> VCELL_SHIFT_BITS);  // A/D 轉換的計數值（0~4095）
    return static_cast<uint16_t>(static_cast<float>(counts) * VCELL_LSB_MV);
}

uint8_t soc_raw_to_percent(uint16_t raw) {
    // STEP 01: 高位元組為整數部分，低位元組為 1/256 % 的小數部分
    const float whole    = static_cast<float>(raw >> 8);               // 整數百分比（0~255）
    const float fraction = static_cast<float>(raw & 0xFF) * SOC_LSB_PERCENT;  // 小數百分比（0~0.996）
    const float percent  = whole + fraction;                           // 合併後的百分比值

    // STEP 02: 充飽時可能超過 100%，夾到上限避免 UI 顯示 103%
    if (percent >= static_cast<float>(SOC_PERCENT_MAX)) {
        return SOC_PERCENT_MAX;
    }

    return static_cast<uint8_t>(percent);
}

bool is_plausible_vcell_mv(uint16_t mv) {
    // 檢查電壓是否落在合理上界以內
    return mv <= PLAUSIBLE_MAX_MV;
}

bool is_plausible_soc_raw(uint16_t raw_soc) {
    // STEP 01: 檢查 SOC 暫存器的整數部分（高位元組）是否超界
    // 注意：必須在 raw 層檢查，因為 soc_raw_to_percent() 會把 >=100 的值夾到 100，
    // 垃圾值換算完後就看不出來了
    const uint8_t whole = static_cast<uint8_t>(raw_soc >> 8);  // SOC 暫存器整數部分（高位元組），即電量百分比整數值
    return whole <= PLAUSIBLE_SOC_WHOLE_MAX;
}

bool compute_low_battery_blink_on(uint8_t percent, bool low, uint32_t now_ms) {
    // STEP 01: 燃料計不在線時圖示本來就不畫，仍翻轉相位只會造成無效的永久性全螢幕重繪
    if (is_battery_absent(percent)) {
        return false;
    }

    // STEP 02: 非低電量不閃爍
    if (!low) {
        return false;
    }

    // STEP 03: 低電量且在線 → 依時間戳計算閃爍相位（BATTERY_BLINK_HALF_PERIOD_MS 半週期）
    return (now_ms / BATTERY_BLINK_HALF_PERIOD_MS) % 2 == 0;
}

void ChargeTrendTracker::push(uint16_t millivolts) {
    // STEP 01: 寫入環形緩衝並前進 head
    samples_[head_] = millivolts;
    head_ = static_cast<uint8_t>((head_ + 1) % ChargeTrendTracker::TREND_WINDOW_SAMPLES);

    // STEP 02: count_ 只累加到窗滿為止，避免長時間執行後溢位
    if (count_ < ChargeTrendTracker::TREND_WINDOW_SAMPLES) {
        count_++;
    }
}

ChargeState ChargeTrendTracker::state() const {
    // STEP 01: 窗未滿時無從判斷趨勢，誠實回 Unknown
    if (count_ < ChargeTrendTracker::TREND_WINDOW_SAMPLES) {
        return ChargeState::Unknown;
    }

    // STEP 02: head_ 指向下一次寫入位置，也就是環形緩衝中最舊的那筆
    const uint16_t oldest = samples_[head_];
    const uint16_t newest = samples_[(head_ + ChargeTrendTracker::TREND_WINDOW_SAMPLES - 1) % ChargeTrendTracker::TREND_WINDOW_SAMPLES];
    const int32_t  delta  = static_cast<int32_t>(newest) - static_cast<int32_t>(oldest);

    // STEP 03: 依 delta 與死區判定 Charging／Discharging／Idle
    if (delta > static_cast<int32_t>(ChargeTrendTracker::TREND_DEADBAND_MV)) {
        return ChargeState::Charging;
    }
    if (delta < -static_cast<int32_t>(ChargeTrendTracker::TREND_DEADBAND_MV)) {
        return ChargeState::Discharging;
    }

    return ChargeState::Idle;
}

void ChargeTrendTracker::reset() {
    // STEP 01: 清掉計數與位置即可讓 state() 回到 Unknown，樣本值不需清零
    count_ = 0;
    head_  = 0;
}

void LowBatteryLatch::update(uint8_t percent) {
    // STEP 01: 契約防呆——超出 0~100 的值一律視為不可信讀值（例如 255 = 燃料計不在線），
    //          直接跳過不更新任何狀態。不可 clamp 後續走正常邏輯：那會把「讀不到」
    //          偽裝成「電量 100%」，反而讓已鎖存的低電量警示被誤清。
    if (percent > SOC_PERCENT_MAX) {
        return;
    }

    // STEP 02: 已在低電量且回升到解除門檻 → 脫離遲滯
    if (is_low_ && percent >= LowBatteryLatch::LOW_BATTERY_EXIT_PERCENT) {
        is_low_        = false;
        entry_pending_ = false;
        return;
    }

    // STEP 03: 已在低電量但未達解除門檻 → 維持現狀
    if (is_low_) {
        return;
    }

    // STEP 04: 未在低電量，跌到進入門檻即觸發（開機首次取樣即低於門檻也走這條）
    if (percent <= LowBatteryLatch::LOW_BATTERY_ENTER_PERCENT) {
        is_low_        = true;
        entry_pending_ = true;
    }
}

bool LowBatteryLatch::consume_first_entry() {
    // STEP 01: 沒有待消費事件就回 false（提示只顯示一次）
    if (!entry_pending_) {
        return false;
    }

    // STEP 02: 消費掉事件，下次呼叫回 false，直到解除後再次跨入才重新掛起
    entry_pending_ = false;
    return true;
}

FuelReading make_reading(uint16_t raw_vcell, uint16_t raw_soc) {
    // STEP 01: SOC 合理性必須判定 raw_soc 本身，不可判定 soc_raw_to_percent() 轉換／夾值
    //          後的結果——夾值後 >=100 的垃圾值與剛充飽的合法超衝已無法區分。
    //          （本函式恰好先判 SOC 再判 VCELL，但兩者順序不影響正確性；
    //            真正不可違反的是判定對象必須是未轉換的原始值。）
    if (!is_plausible_soc_raw(raw_soc)) {
        return FuelReading();
    }

    // STEP 02: 換算電壓並判上界
    const uint16_t mv = vcell_raw_to_mv(raw_vcell);  // 換算後的電池電壓（mV）
    if (!is_plausible_vcell_mv(mv)) {
        return FuelReading();
    }

    // STEP 03: 兩項都通過才算可信讀數
    return FuelReading(true, mv, soc_raw_to_percent(raw_soc));
}

uint8_t to_display_percent(const FuelReading& reading) {
    // STEP 01: 讀值不可信一律回哨兵——不可回 0，0% 是合法讀數
    if (!reading.valid) {
        return BATTERY_PERCENT_ABSENT;
    }

    // STEP 02: 有效讀值直接透傳。0~100 的範圍由目前唯一的生產路徑 make_reading() 保證，
    //          非型別層級保證——FuelReading 的 constructor 沒有 range check，呼叫端仍可能
    //          自行建構出 percent > 100 的實例，只是目前 call graph 上沒有人這樣做
    return reading.percent;
}

BatteryPollOutcome apply_fuel_reading(const FuelReading& reading,
                                      ChargeTrendTracker& trend,
                                      LowBatteryLatch& latch) {
    // STEP 01: 顯示百分比只透過 to_display_percent() 這唯一出口轉譯，成功/失敗共用同一守衛
    const uint8_t percent = to_display_percent(reading);

    // STEP 02: 讀取失敗——不沿用上一筆舊值假裝還讀得到，鎖存狀態只讀不寫
    if (!reading.valid) {
        // STEP 02.01: 趨勢窗必須重置，確保讀取恢復後從乾淨的窗重新累積（見 reset() 的 doc）
        trend.reset();

        // STEP 02.02: 鎖存狀態只讀 is_low()，絕不呼叫 latch.update(reading.percent)——
        //             無效讀值的 reading.percent 是建構子預設值 0，而 0 <= SOC_PERCENT_MAX
        //             會通過 LowBatteryLatch::update() 的 guard；若當下電量正常，一次
        //             暫時性 I2C 失敗就會被誤判成跌破 20% 門檻，憑空觸發低電量警示。
        //             此路徑由 test_apply_fuel_reading_failure_does_not_fabricate_low_battery
        //             鎖住（把這裡改成呼叫 latch.update(reading.percent) 該測試必須變紅）。
        return {percent, 0, ChargeState::Unknown, latch.is_low()};
    }

    // STEP 03: 讀取成功——推進趨勢與低電量閂鎖
    //          趨勢用電壓（mV）而非百分比——百分比的量化粒度大於 30 秒窗內的真實
    //          充電訊號，會同時造成漏報與誤報（spec §3.4.1）
    trend.push(reading.millivolts);
    latch.update(reading.percent);
    return {percent, reading.millivolts, trend.state(), latch.is_low()};
}

}  // namespace ems
