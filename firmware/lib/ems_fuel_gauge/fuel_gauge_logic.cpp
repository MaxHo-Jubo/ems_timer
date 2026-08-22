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

}  // namespace ems
