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

}  // namespace ems
