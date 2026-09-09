// EMS DoseSync Pro — 產測（factory-test）純邏輯實作
// 介面與規則說明見 factory_test_logic.h；測試見 test/test_factory_test_logic/
#include "factory_test_logic.h"

namespace ems {

// 按鍵 TFT 名稱，索引對齊 gpio-allocation.md §1（BTN_PRIMARY..BTN_SHOCK）；限 ASCII
static const char* const BUTTON_LABELS[FT_BUTTON_COUNT] = {
    "PRIMARY", "UP", "DOWN", "POWER", "RECORD", "BACK", "EPI", "SHOCK",
};

// 超出範圍的按鍵索引顯示用佔位字串
static const char* const BUTTON_LABEL_PLACEHOLDER = "?";

// 失敗原因字串，與 docs/vendor-assembly-brief.html §7.5 一致
static const char* const REASON_BUS_STUCK    = "I2C BUS STUCK";
static const char* const REASON_BUS_ERROR    = "I2C BUS ERROR";
static const char* const REASON_NVS_ERROR    = "NVS ERROR";
static const char* const REASON_RTC_MISSING  = "RTC MISSING";
static const char* const REASON_RTC_IO       = "RTC I/O ERROR";
static const char* const REASON_RTC_STUCK    = "RTC NOT TICKING";
static const char* const REASON_RTC_BATTERY  = "RTC BATTERY";
static const char* const REASON_BTN_ORDER    = "BUTTON ORDER";
static const char* const REASON_BTN_SHORT    = "BUTTON SHORT";

// 等待原因字串，與 §7.4 一致
static const char* const PENDING_POWER_CYCLE = "POWER CYCLE";
static const char* const PENDING_RTC         = "RTC";
static const char* const PENDING_BUTTONS     = "BUTTONS";

// DS3231 暫存器位元（datasheet Figure 1. Timekeeping Registers）
static constexpr uint8_t RTC_SECONDS_RESERVED_MASK = 0x80;  // 秒暫存器 bit7 必為 0
static constexpr uint8_t RTC_MINUTES_RESERVED_MASK = 0x80;  // 分暫存器 bit7 必為 0
static constexpr uint8_t RTC_HOURS_12H_FLAG        = 0x40;  // 時暫存器 bit6 = 12 小時制（本專案只用 24 小時制）
static constexpr uint8_t RTC_HOURS_RESERVED_MASK   = 0x80;  // 時暫存器 bit7 必為 0
static constexpr uint8_t RTC_HOURS_VALUE_MASK      = 0x3F;  // 24 小時制下的小時 BCD
static constexpr uint8_t RTC_DOW_RESERVED_MASK     = 0xF8;  // 星期暫存器 bit7~3 必為 0
static constexpr uint8_t RTC_DOW_VALUE_MASK        = 0x07;  // 星期 1~7
static constexpr uint8_t RTC_DATE_RESERVED_MASK    = 0xC0;  // 日暫存器 bit7~6 必為 0
static constexpr uint8_t RTC_DATE_VALUE_MASK       = 0x3F;  // 日 BCD
static constexpr uint8_t RTC_MONTH_RESERVED_MASK   = 0x60;  // 月暫存器 bit6~5 必為 0（bit7 是世紀旗標，允許）
static constexpr uint8_t RTC_MONTH_VALUE_MASK      = 0x1F;  // 月 BCD

// 時間欄位的合法上界
static constexpr uint8_t RTC_MAX_SECOND = 59;
static constexpr uint8_t RTC_MAX_MINUTE = 59;
static constexpr uint8_t RTC_MAX_HOUR   = 23;
static constexpr uint8_t RTC_MAX_DAY    = 31;
static constexpr uint8_t RTC_MAX_MONTH  = 12;
static constexpr uint8_t RTC_MAX_YEAR   = 99;
static constexpr uint8_t RTC_MAX_DOW    = 7;

// epoch 換算
static constexpr uint32_t SECONDS_PER_MINUTE = 60;
static constexpr uint32_t SECONDS_PER_HOUR   = 3600;
static constexpr uint32_t SECONDS_PER_DAY    = 86400;
static constexpr uint32_t RTC_YEAR_BASE      = 2000;    // FtRtcTime.year 0~99 → 2000~2099
static constexpr uint32_t DAYS_PER_ERA       = 146097; // 400 年一個 era（Hinnant days_from_civil）
static constexpr uint32_t YEARS_PER_ERA      = 400;
static constexpr uint32_t DAYS_TO_UNIX_EPOCH = 719468; // 0000-03-01 到 1970-01-01 的天數

// BCD 單一 nibble 的合法上界
static constexpr uint8_t BCD_NIBBLE_MAX = 9;

/**
 * 把一個 BCD byte 轉成二進位，任一 nibble > 9 視為失敗
 * @param bcd 兩位 BCD
 * @param out 轉換結果
 * @return true = 兩個 nibble 都合法
 */
static bool bcdToBinary(uint8_t bcd, uint8_t& out) {
    // STEP 01: 拆高低 nibble 各自驗證
    const uint8_t high = bcd >> 4;    // 十位
    const uint8_t low  = bcd & 0x0F;  // 個位
    if (high > BCD_NIBBLE_MAX || low > BCD_NIBBLE_MAX) {
        return false;
    }

    // STEP 02: 組回十進位
    out = static_cast<uint8_t>(high * 10 + low);
    return true;
}

/**
 * 解碼單一暫存器：先驗保留位元，再驗 BCD，再驗範圍
 * @param raw           暫存器原始值
 * @param reservedMask  必為 0 的位元
 * @param valueMask     BCD 所在位元
 * @param minValue      合法下界
 * @param maxValue      合法上界
 * @param out           解碼結果
 * @return true = 合法
 */
static bool decodeField(uint8_t raw, uint8_t reservedMask, uint8_t valueMask,
                        uint8_t minValue, uint8_t maxValue, uint8_t& out) {
    // STEP 01: 保留位元有 1 就是壞資料（例如 SDA 浮空讀到 0xFF）
    if ((raw & reservedMask) != 0) {
        return false;
    }

    // STEP 02: BCD 與範圍
    uint8_t value = 0;  // BCD 轉出的十進位
    if (!bcdToBinary(raw & valueMask, value)) {
        return false;
    }
    if (value < minValue || value > maxValue) {
        return false;
    }
    out = value;
    return true;
}

FtProbe ft_classify_i2c_probe(uint8_t code) {
    // STEP 01: 三個已知碼各自對應；其餘一律視為 driver 異常
    if (code == FT_I2C_CODE_ACK) {
        return FtProbe::Present;
    }
    if (code == FT_I2C_CODE_ADDR_NACK) {
        return FtProbe::Absent;
    }
    if (code == FT_I2C_CODE_TIMEOUT) {
        return FtProbe::BusStuck;
    }
    return FtProbe::BusError;
}

/**
 * 把單一位址的探測結果併進狀態副本：在線旗標重算，stuck／error 只設不清
 * @param next    正在組裝的新狀態
 * @param code    該位址的回傳碼
 * @param present 該位址的在線旗標欄位
 */
static void applyProbe(FactoryTestState& next, uint8_t code, bool& present) {
    // STEP 01: 分類
    const FtProbe probe = ft_classify_i2c_probe(code);
    present = (probe == FtProbe::Present);

    // STEP 02: 黏性錯誤旗標；error_code 只記第一個
    if (probe == FtProbe::BusStuck) {
        next.i2c_bus_stuck = true;
    }
    if (probe == FtProbe::BusError && !next.i2c_bus_error) {
        next.i2c_bus_error  = true;
        next.i2c_error_code = code;
    }
}

FactoryTestState ft_apply_i2c_scan(const FactoryTestState& s, const FtI2cScan& scan) {
    // STEP 01: 複製後逐位址套用（applyProbe 只改副本）
    FactoryTestState next = s;
    applyProbe(next, scan.rtc_code,    next.rtc_present);
    applyProbe(next, scan.eeprom_code, next.eeprom_present);
    applyProbe(next, scan.gauge_code,  next.gauge_present);

    // STEP 02: RTC 是必要件，任何一輪沒 ACK 就黏住（虛焊恢復也不放行）
    if (!next.rtc_present) {
        next.rtc_missing_seen = true;
    }
    return next;
}

FactoryTestState ft_mark_nvs_error(const FactoryTestState& s) {
    // STEP 01: 黏性旗標
    FactoryTestState next = s;
    next.nvs_error = true;
    return next;
}

FactoryTestState ft_apply_button_presses(const FactoryTestState& s, const uint8_t* indices, uint8_t count) {
    // STEP 01: 黏性錯誤、已完成、沒事件 → 原樣回傳
    if (s.button_error != FtButtonError::None || ft_all_buttons_done(s) || count == 0) {
        return s;
    }
    FactoryTestState next = s;

    // STEP 02: 同一輪兩顆以上 = 短路
    if (count >= 2) {
        next.button_error = FtButtonError::Multiple;
        next.button_err_a = indices[0];
        next.button_err_b = indices[1];
        return next;
    }

    // STEP 03: 單顆：超出範圍忽略、剛驗收過的那顆再按忽略、預期的那顆驗收、其他錯接
    const uint8_t index    = indices[0];       // 本輪唯一的下降緣
    const uint8_t expected = s.buttons_done;   // 下一顆預期索引
    if (index >= FT_BUTTON_COUNT) {
        return s;
    }
    if (expected > 0 && index == expected - 1) {
        return s;
    }
    if (index == expected) {
        next.buttons_done = static_cast<uint8_t>(expected + 1);
        return next;
    }
    next.button_error = FtButtonError::WrongOrder;
    next.button_err_a = expected;
    next.button_err_b = index;
    return next;
}

bool ft_all_buttons_done(const FactoryTestState& s) {
    // STEP 01: 依序驗收數達 8 即完成
    return s.buttons_done >= FT_BUTTON_COUNT;
}

const char* ft_button_label(uint8_t index) {
    // STEP 01: 超出範圍回佔位字串
    if (index >= FT_BUTTON_COUNT) {
        return BUTTON_LABEL_PLACEHOLDER;
    }
    return BUTTON_LABELS[index];
}

/**
 * Gregorian 閏年（2000~2099 範圍內 400 年規則只影響 2000，仍完整實作）
 * @param year 西元年
 * @return true = 閏年
 */
static bool isLeapYear(uint32_t year) {
    // STEP 01: 4 年一閏、100 年不閏、400 年再閏
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * 該年該月的天數
 * @param year  西元年
 * @param month 1~12（呼叫端已驗）
 * @return 28~31
 */
static uint8_t daysInMonth(uint32_t year, uint8_t month) {
    // STEP 01: 查表，2 月依閏年加一天
    static const uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // 平年各月天數
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return DAYS[month - 1];
}

bool ft_rtc_decode(const uint8_t* raw, FtRtcTime& out) {
    // STEP 01: 逐欄位解碼，任一欄失敗整筆作廢
    FtRtcTime t;         // 暫存解碼結果，全部成功才寫回 out
    uint8_t   dow = 0;   // 星期，只驗範圍不輸出
    if (!decodeField(raw[0], RTC_SECONDS_RESERVED_MASK, 0x7F, 0, RTC_MAX_SECOND, t.second)) {
        return false;
    }
    if (!decodeField(raw[1], RTC_MINUTES_RESERVED_MASK, 0x7F, 0, RTC_MAX_MINUTE, t.minute)) {
        return false;
    }
    if (!decodeField(raw[2], static_cast<uint8_t>(RTC_HOURS_RESERVED_MASK | RTC_HOURS_12H_FLAG),
                     RTC_HOURS_VALUE_MASK, 0, RTC_MAX_HOUR, t.hour)) {
        return false;
    }
    if (!decodeField(raw[3], RTC_DOW_RESERVED_MASK, RTC_DOW_VALUE_MASK, 1, RTC_MAX_DOW, dow)) {
        return false;
    }
    if (!decodeField(raw[4], RTC_DATE_RESERVED_MASK, RTC_DATE_VALUE_MASK, 1, RTC_MAX_DAY, t.day)) {
        return false;
    }
    if (!decodeField(raw[5], RTC_MONTH_RESERVED_MASK, RTC_MONTH_VALUE_MASK, 1, RTC_MAX_MONTH, t.month)) {
        return false;
    }
    if (!decodeField(raw[6], 0x00, 0xFF, 0, RTC_MAX_YEAR, t.year)) {
        return false;
    }

    // STEP 02: 日期組合要真的存在（2/30、4/31、平年 2/29 是合法 BCD 但不是日期；
    //          放過會被 epoch 換算正規化成別的日子，損壞資料就能假裝走時）
    if (t.day > daysInMonth(RTC_YEAR_BASE + t.year, t.month)) {
        return false;
    }

    // STEP 03: 全部合法才寫回
    out = t;
    return true;
}

/**
 * proleptic Gregorian 日期 → 1970-01-01 起算的天數（Howard Hinnant days_from_civil）
 * @param year  西元年（2000~2099）
 * @param month 1~12
 * @param day   1~31
 * @return 天數
 */
static uint32_t daysFromCivil(uint32_t year, uint32_t month, uint32_t day) {
    // STEP 01: 以 3 月為年初，2 月的長度問題移到年尾
    const uint32_t y   = year - (month <= 2 ? 1 : 0);
    const uint32_t era = y / YEARS_PER_ERA;
    const uint32_t yoe = y - era * YEARS_PER_ERA;                        // year of era
    const uint32_t mp  = (month + 9) % 12;                               // 3 月 = 0 … 2 月 = 11
    const uint32_t doy = (153 * mp + 2) / 5 + day - 1;                   // day of year（3 月起算）
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // day of era
    return era * DAYS_PER_ERA + doe - DAYS_TO_UNIX_EPOCH;
}

uint32_t ft_rtc_time_to_seconds(const FtRtcTime& t) {
    // STEP 01: 天數 × 86400 + 當日秒數
    const uint32_t days = daysFromCivil(RTC_YEAR_BASE + t.year, t.month, t.day);
    return days * SECONDS_PER_DAY + SECONDS_PER_HOUR * t.hour + SECONDS_PER_MINUTE * t.minute + t.second;
}

FtRtcTick ft_classify_rtc_tick(uint32_t prev_seconds, uint32_t now_seconds, uint32_t elapsed_since_change_ms) {
    // STEP 01: 倒退或一輪跳太多都是亂資料，不是走時
    if (now_seconds < prev_seconds || (now_seconds - prev_seconds) > FT_RTC_MAX_STEP_S) {
        return FtRtcTick::Invalid;
    }

    // STEP 02: 合理前進就是在走，不看經過時間
    if (now_seconds != prev_seconds) {
        return FtRtcTick::Ok;
    }

    // STEP 03: 沒變但還沒達到逾時 → 觀察中；達到 → 停擺
    if (elapsed_since_change_ms < FT_RTC_TICK_TIMEOUT_MS) {
        return FtRtcTick::Unknown;
    }
    return FtRtcTick::Stuck;
}

FactoryTestState ft_apply_rtc_tick(const FactoryTestState& s, FtRtcTick observed) {
    // STEP 01: Invalid 轉成黏性 I/O 錯誤，走時狀態不動
    FactoryTestState next = s;
    if (observed == FtRtcTick::Invalid) {
        next.rtc_io_error = true;
        return next;
    }

    // STEP 02: 已 Stuck 就維持，否則採本輪觀測
    if (s.rtc_tick != FtRtcTick::Stuck) {
        next.rtc_tick = observed;
    }
    return next;
}

FtRtcBackup ft_classify_rtc_backup(bool osf_set, bool seeded_before, bool has_last_seen,
                                   int32_t off_seconds, bool esp_powered_off, FtRtcBackup stored) {
    // STEP 01: OSF 亮是新證據，優先於任何存起來的結論；有過任何基準（seed 過或記錄過）就是 Lost
    if (osf_set) {
        return (seeded_before || has_last_seen) ? FtRtcBackup::Lost : FtRtcBackup::Unverified;
    }

    // STEP 02: 沒有上次記錄就算不出離線時間，要求做一次斷電驗證
    if (!has_last_seen) {
        return FtRtcBackup::Unverified;
    }

    // STEP 03: RTC 說離線夠久 + ESP 說真的斷過電，兩者都成立才算電池撐住；否則沿用存的結論
    if (off_seconds >= FT_POWER_CYCLE_MIN_OFF_S && esp_powered_off) {
        return FtRtcBackup::Ok;
    }
    return stored;
}

FtFailKind ft_fail_kind(const FactoryTestState& s) {
    // STEP 01: bus 層先於裝置層（bus 被拉住時 RTC 必然掃不到，先報根因）
    if (s.i2c_bus_stuck) {
        return FtFailKind::BusStuck;
    }
    if (s.i2c_bus_error) {
        return FtFailKind::BusError;
    }

    // STEP 02: 主控儲存壞了，備援驗證的證據不可信
    if (s.nvs_error) {
        return FtFailKind::NvsError;
    }

    // STEP 03: RTC 必要件：在線（含曾經缺席）→ 可讀 → 走時 → 電池
    if (!s.rtc_present || s.rtc_missing_seen) {
        return FtFailKind::RtcMissing;
    }
    if (s.rtc_io_error) {
        return FtFailKind::RtcIo;
    }
    if (s.rtc_tick == FtRtcTick::Stuck) {
        return FtFailKind::RtcStuck;
    }
    if (s.rtc_backup == FtRtcBackup::Lost) {
        return FtFailKind::RtcBattery;
    }

    // STEP 04: 按鍵
    if (s.button_error == FtButtonError::WrongOrder) {
        return FtFailKind::ButtonOrder;
    }
    if (s.button_error == FtButtonError::Multiple) {
        return FtFailKind::ButtonShort;
    }

    // STEP 05: 沒有失敗項
    return FtFailKind::None;
}

const char* ft_fail_reason_for(FtFailKind kind) {
    // STEP 01: 種類 → 字串，一對一
    switch (kind) {
        case FtFailKind::BusStuck:    return REASON_BUS_STUCK;
        case FtFailKind::BusError:    return REASON_BUS_ERROR;
        case FtFailKind::NvsError:    return REASON_NVS_ERROR;
        case FtFailKind::RtcMissing:  return REASON_RTC_MISSING;
        case FtFailKind::RtcIo:       return REASON_RTC_IO;
        case FtFailKind::RtcStuck:    return REASON_RTC_STUCK;
        case FtFailKind::RtcBattery:  return REASON_RTC_BATTERY;
        case FtFailKind::ButtonOrder: return REASON_BTN_ORDER;
        case FtFailKind::ButtonShort: return REASON_BTN_SHORT;
        default:                      return nullptr;  // None
    }
}

const char* ft_fail_reason(const FactoryTestState& s) {
    // STEP 01: 種類與字串同一來源
    return ft_fail_reason_for(ft_fail_kind(s));
}

const char* ft_pending_reason(const FactoryTestState& s) {
    // STEP 01: 先斷電驗電池（按鍵進度會在斷電時歸零，所以排最前面提示）
    if (s.rtc_backup == FtRtcBackup::Unverified) {
        return PENDING_POWER_CYCLE;
    }

    // STEP 02: 走時觀察中
    if (s.rtc_tick != FtRtcTick::Ok) {
        return PENDING_RTC;
    }

    // STEP 03: 按鍵未按齊
    if (!ft_all_buttons_done(s)) {
        return PENDING_BUTTONS;
    }
    return nullptr;
}

FtVerdict ft_evaluate(const FactoryTestState& s) {
    // STEP 01: 失敗原因與等待原因共用同一份規則，判定不會與顯示字串分歧
    if (ft_fail_reason(s) != nullptr) {
        return FtVerdict::Fail;
    }
    if (ft_pending_reason(s) != nullptr) {
        return FtVerdict::Pending;
    }
    return FtVerdict::Pass;
}

const char* ft_verdict_label(FtVerdict v) {
    // STEP 01: 三種判定各自的顯示字串
    switch (v) {
        case FtVerdict::Pass: return "PASS";
        case FtVerdict::Fail: return "FAIL";
        default:              return "WAITING";  // Pending
    }
}

const char* ft_rtc_backup_label(FtRtcBackup b) {
    // STEP 01: 三種備援狀態各自的顯示字串（與 §7.4 一致）
    switch (b) {
        case FtRtcBackup::Ok:   return "BACKUP OK";
        case FtRtcBackup::Lost: return "BACKUP LOST - CHECK CR2032";
        default:                return "BACKUP: POWER CYCLE TO VERIFY";  // Unverified
    }
}

}  // namespace ems
