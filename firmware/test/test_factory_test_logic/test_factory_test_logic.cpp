// EMS DoseSync Pro — 產測（factory-test）純邏輯測試
//
// 對應 lib：firmware/lib/ems_factory_test/factory_test_logic.{h,cpp}
// 硬體端：firmware/src_factory_test/main.cpp 只做 I/O，PASS／FAIL 判定全部走這裡的純函式。
//
// 命名與門檻依 docs/vendor-assembly-brief.html §7 的判讀表：
//   - RTC 必要（在線／可讀／走時／電池），EEPROM（0x57）與電量計（0x36）可有可無
//   - I2C bus 被拉住或回異常碼一律 FAIL，優先於其他狀態
//   - 8 顆按鍵必須依序驗收，接反或短路直接 FAIL
//   - 失敗狀態黏性：一旦成立後續正常觀測不會把它清掉
//
// expected 側一律寫字面值，不拿 lib 常數比自己（feedback_test_assert_not_same_symbol）。
#include <unity.h>
#include <cstring>
#include "factory_test_logic.h"

/** Unity 每個測試前的準備：本檔全用純函式，無共用狀態 */
void setUp() {}
/** Unity 每個測試後的清理：無 */
void tearDown() {}

// ============================================================
//  共用 fixture
// ============================================================

/**
 * 建一份「全部合格」的狀態當基準，各測試從這裡拿掉一項看判定是否正確翻轉
 * @return bus 正常、RTC 在線／可讀／走時／電池 OK、8 鍵依序驗收完、選配裝置都不在（本次留排針）
 */
static ems::FactoryTestState allGoodState() {
    ems::FactoryTestState s;  // 從預設值建構，只覆寫「合格」所需欄位
    s.rtc_present  = true;
    s.rtc_tick     = ems::FtRtcTick::Ok;
    s.rtc_backup   = ems::FtRtcBackup::Ok;
    s.buttons_done = 8;
    return s;
}

/**
 * 從空狀態依序套用給定按鍵索引，每次一顆
 * @param order   按鍵索引序列
 * @param count   序列長度
 * @param base    起始狀態
 * @return 套用後狀態
 */
static ems::FactoryTestState pressSequence(const uint8_t* order, uint8_t count, ems::FactoryTestState base) {
    ems::FactoryTestState s = base;  // 逐步更新的工作副本
    for (uint8_t i = 0; i < count; i++) {
        s = ems::ft_apply_button_presses(s, &order[i], 1);
    }
    return s;
}

// ============================================================
//  Group 1: I2C 探測與掃描
// ============================================================

/** endTransmission 四類回傳碼各自對應到正確的 FtProbe */
static void test_probe_classification() {
    TEST_ASSERT_EQUAL(ems::FtProbe::Present,  ems::ft_classify_i2c_probe(0));
    TEST_ASSERT_EQUAL(ems::FtProbe::Absent,   ems::ft_classify_i2c_probe(2));
    TEST_ASSERT_EQUAL(ems::FtProbe::BusStuck, ems::ft_classify_i2c_probe(5));
    TEST_ASSERT_EQUAL(ems::FtProbe::BusError, ems::ft_classify_i2c_probe(1));
    TEST_ASSERT_EQUAL(ems::FtProbe::BusError, ems::ft_classify_i2c_probe(3));
    TEST_ASSERT_EQUAL(ems::FtProbe::BusError, ems::ft_classify_i2c_probe(4));
}

/** 三個位址都 ACK → 三個在線旗標為真，且無任何錯誤旗標 */
static void test_scan_all_ack_sets_present_flags() {
    const ems::FtI2cScan scan(0, 0, 0);  // 全部 ACK
    const ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), scan);
    TEST_ASSERT_TRUE(s.rtc_present);
    TEST_ASSERT_TRUE(s.eeprom_present);
    TEST_ASSERT_TRUE(s.gauge_present);
    TEST_ASSERT_FALSE(s.i2c_bus_stuck);
    TEST_ASSERT_FALSE(s.i2c_bus_error);
}

/** 選配位址 NACK 只是不在線，不是錯誤（本次電量計留排針就是這個情境） */
static void test_scan_nack_is_absent_not_error() {
    const ems::FtI2cScan scan(0, 2, 2);  // RTC ACK，EEPROM／電量計 NACK
    const ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), scan);
    TEST_ASSERT_TRUE(s.rtc_present);
    TEST_ASSERT_FALSE(s.eeprom_present);
    TEST_ASSERT_FALSE(s.gauge_present);
    TEST_ASSERT_FALSE(s.i2c_bus_stuck);
    TEST_ASSERT_FALSE(s.i2c_bus_error);
}

/** 任一位址逾時 → bus_stuck，且其他位址的在線旗標照自己的碼算 */
static void test_scan_timeout_sets_bus_stuck() {
    const ems::FtI2cScan scan(0, 5, 2);  // EEPROM 逾時
    const ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), scan);
    TEST_ASSERT_TRUE(s.i2c_bus_stuck);
    TEST_ASSERT_TRUE(s.rtc_present);
    TEST_ASSERT_FALSE(s.eeprom_present);
}

/** 非 ACK/NACK/逾時的碼 → bus_error 並記下該碼（選配位址也算，不可被「缺席」吞掉） */
static void test_scan_other_code_sets_bus_error_with_code() {
    const ems::FtI2cScan scan(0, 2, 4);  // 電量計回 4
    const ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), scan);
    TEST_ASSERT_TRUE(s.i2c_bus_error);
    TEST_ASSERT_EQUAL_UINT8(4, s.i2c_error_code);
    TEST_ASSERT_FALSE(s.i2c_bus_stuck);
}

/** stuck／error 黏性：後續一輪全部 ACK 也不會被清掉，error_code 保留第一個 */
static void test_scan_errors_are_sticky() {
    // STEP 01: 先製造逾時與錯誤碼
    ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), ems::FtI2cScan(5, 3, 2));
    TEST_ASSERT_TRUE(s.i2c_bus_stuck);
    TEST_ASSERT_EQUAL_UINT8(3, s.i2c_error_code);

    // STEP 02: 再來一輪全 ACK（含另一個錯誤碼 4 在電量計）→ 旗標仍在、碼不被覆蓋
    s = ems::ft_apply_i2c_scan(s, ems::FtI2cScan(0, 0, 4));
    TEST_ASSERT_TRUE(s.i2c_bus_stuck);
    TEST_ASSERT_TRUE(s.i2c_bus_error);
    TEST_ASSERT_EQUAL_UINT8(3, s.i2c_error_code);
    TEST_ASSERT_TRUE(s.rtc_present);
}

/** RTC 缺席黏性：某一輪 NACK 後即使下一輪 ACK，仍是 FAIL: RTC MISSING（虛焊恢復不放行） */
static void test_scan_rtc_absent_is_sticky() {
    // STEP 01: ACK → NACK → ACK
    ems::FactoryTestState s = ems::ft_apply_i2c_scan(ems::FactoryTestState(), ems::FtI2cScan(0, 2, 2));
    s = ems::ft_apply_i2c_scan(s, ems::FtI2cScan(2, 2, 2));
    s = ems::ft_apply_i2c_scan(s, ems::FtI2cScan(0, 2, 2));

    // STEP 02: 畫面用的 present 是真，但失敗黏住
    TEST_ASSERT_TRUE(s.rtc_present);
    TEST_ASSERT_TRUE(s.rtc_missing_seen);
    s.rtc_tick = ems::FtRtcTick::Ok; s.rtc_backup = ems::FtRtcBackup::Ok; s.buttons_done = 8;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Fail, ems::ft_evaluate(s));
    TEST_ASSERT_EQUAL_STRING("RTC MISSING", ems::ft_fail_reason(s));
}

/** 不可變：套用掃描不會改到傳入的原狀態 */
static void test_scan_does_not_mutate_input() {
    const ems::FactoryTestState original;  // 預設：全部不在線
    const ems::FactoryTestState updated = ems::ft_apply_i2c_scan(original, ems::FtI2cScan(0, 0, 0));
    TEST_ASSERT_FALSE(original.rtc_present);
    TEST_ASSERT_TRUE(updated.rtc_present);
}

// ============================================================
//  Group 2: 按鍵依序驗收
// ============================================================

/** 依 0~7 順序各按一次 → 8 顆驗收完、無錯誤 */
static void test_buttons_in_order_completes() {
    const uint8_t order[8] = {0, 1, 2, 3, 4, 5, 6, 7};  // 正確順序
    const ems::FactoryTestState s = pressSequence(order, 8, ems::FactoryTestState());
    TEST_ASSERT_EQUAL_UINT8(8, s.buttons_done);
    TEST_ASSERT_EQUAL(ems::FtButtonError::None, s.button_error);
    TEST_ASSERT_TRUE(ems::ft_all_buttons_done(s));
}

/** 第一顆就按到索引 1（例如 GPIO 4／5 接反）→ WrongOrder，記下預期 0、實際 1 */
static void test_buttons_wrong_first_press_is_order_error() {
    const uint8_t wrong = 1;  // 預期 0 卻收到 1
    const ems::FactoryTestState s = ems::ft_apply_button_presses(ems::FactoryTestState(), &wrong, 1);
    TEST_ASSERT_EQUAL(ems::FtButtonError::WrongOrder, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(0, s.button_err_a);
    TEST_ASSERT_EQUAL_UINT8(1, s.button_err_b);
    TEST_ASSERT_EQUAL_UINT8(0, s.buttons_done);
}

/** 中途跳號（按完 0、1 後按 3）→ WrongOrder，預期 2、實際 3，進度停在 2 */
static void test_buttons_skip_is_order_error() {
    const uint8_t order[3] = {0, 1, 3};  // 跳過 2
    const ems::FactoryTestState s = pressSequence(order, 3, ems::FactoryTestState());
    TEST_ASSERT_EQUAL(ems::FtButtonError::WrongOrder, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(2, s.button_err_a);
    TEST_ASSERT_EQUAL_UINT8(3, s.button_err_b);
    TEST_ASSERT_EQUAL_UINT8(2, s.buttons_done);
}

/** 剛驗收過的那顆再按一次（手抖／重複按）→ 忽略，不算錯也不前進 */
static void test_buttons_repeat_of_last_accepted_is_ignored() {
    const uint8_t order[3] = {0, 0, 1};  // 0 按兩次
    const ems::FactoryTestState s = pressSequence(order, 3, ems::FactoryTestState());
    TEST_ASSERT_EQUAL(ems::FtButtonError::None, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(2, s.buttons_done);
}

/** 回頭按更早的那顆（按完 0、1、2 再按 0）→ 不是「剛驗收過」，算錯接 */
static void test_buttons_older_repeat_is_order_error() {
    const uint8_t order[4] = {0, 1, 2, 0};  // 回頭按 0
    const ems::FactoryTestState s = pressSequence(order, 4, ems::FactoryTestState());
    TEST_ASSERT_EQUAL(ems::FtButtonError::WrongOrder, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(3, s.button_err_a);
    TEST_ASSERT_EQUAL_UINT8(0, s.button_err_b);
}

/** 同一輪兩顆下降緣（兩條按鍵線短路）→ Multiple，記下兩顆索引 */
static void test_buttons_two_at_once_is_short() {
    const uint8_t both[2] = {0, 1};  // 同輪同時觸發
    const ems::FactoryTestState s = ems::ft_apply_button_presses(ems::FactoryTestState(), both, 2);
    TEST_ASSERT_EQUAL(ems::FtButtonError::Multiple, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(0, s.button_err_a);
    TEST_ASSERT_EQUAL_UINT8(1, s.button_err_b);
}

/** 最後兩顆短路（按 6 時 6、7 同時觸發）也要被抓到，不能因為 7 剛好是下一顆就放行 */
static void test_buttons_last_pair_short_is_caught() {
    const uint8_t order[6] = {0, 1, 2, 3, 4, 5};   // 前六顆正常
    const uint8_t pair[2]  = {6, 7};               // 第七顆按下時兩顆一起來
    ems::FactoryTestState s = pressSequence(order, 6, ems::FactoryTestState());
    s = ems::ft_apply_button_presses(s, pair, 2);
    TEST_ASSERT_EQUAL(ems::FtButtonError::Multiple, s.button_error);
    TEST_ASSERT_FALSE(ems::ft_all_buttons_done(s));
}

/** 錯誤黏性：出錯後再按正確的按鍵也不會前進、不會清錯 */
static void test_buttons_error_is_sticky() {
    const uint8_t order[3] = {1, 0, 1};  // 先錯接，再按對的
    const ems::FactoryTestState s = pressSequence(order, 3, ems::FactoryTestState());
    TEST_ASSERT_EQUAL(ems::FtButtonError::WrongOrder, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(0, s.buttons_done);
}

/** 8 顆驗收完之後的任何按鍵都忽略（PASS 後多按不會翻成 FAIL） */
static void test_buttons_after_complete_are_ignored() {
    const uint8_t order[10] = {0, 1, 2, 3, 4, 5, 6, 7, 2, 5};  // 完成後亂按
    const ems::FactoryTestState s = pressSequence(order, 10, ems::FactoryTestState());
    TEST_ASSERT_EQUAL(ems::FtButtonError::None, s.button_error);
    TEST_ASSERT_EQUAL_UINT8(8, s.buttons_done);
}

/** 超出範圍的索引與 count 0 都原樣回傳 */
static void test_buttons_out_of_range_and_empty_are_ignored() {
    const uint8_t bad = 8;  // 不存在的第 9 顆
    const ems::FactoryTestState base;
    const ems::FactoryTestState afterBad   = ems::ft_apply_button_presses(base, &bad, 1);
    const ems::FactoryTestState afterEmpty = ems::ft_apply_button_presses(base, &bad, 0);
    TEST_ASSERT_EQUAL(ems::FtButtonError::None, afterBad.button_error);
    TEST_ASSERT_EQUAL_UINT8(0, afterBad.buttons_done);
    TEST_ASSERT_EQUAL_UINT8(0, afterEmpty.buttons_done);
}

/** TFT 端只用內建 ASCII 字型，label 不可含非 ASCII；8 個 label 兩兩不同 */
static void test_button_labels_are_ascii_and_distinct() {
    for (uint8_t i = 0; i < 8; i++) {
        const char* label = ems::ft_button_label(i);  // 受測 label
        TEST_ASSERT_NOT_NULL(label);
        TEST_ASSERT_TRUE(strlen(label) > 0);
        for (const char* c = label; *c != '\0'; c++) {
            TEST_ASSERT_TRUE_MESSAGE((static_cast<unsigned char>(*c)) < 0x80, "label 含非 ASCII 字元");
        }
        for (uint8_t j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(label, ems::ft_button_label(j)));
        }
    }
}

/** 順序對齊 gpio-allocation.md §1（BTN_PRIMARY..BTN_SHOCK），廠商文件 §4.3 同序 */
static void test_button_label_matches_sot_order() {
    TEST_ASSERT_EQUAL_STRING("PRIMARY", ems::ft_button_label(0));
    TEST_ASSERT_EQUAL_STRING("UP",      ems::ft_button_label(1));
    TEST_ASSERT_EQUAL_STRING("DOWN",    ems::ft_button_label(2));
    TEST_ASSERT_EQUAL_STRING("POWER",   ems::ft_button_label(3));
    TEST_ASSERT_EQUAL_STRING("RECORD",  ems::ft_button_label(4));
    TEST_ASSERT_EQUAL_STRING("BACK",    ems::ft_button_label(5));
    TEST_ASSERT_EQUAL_STRING("EPI",     ems::ft_button_label(6));
    TEST_ASSERT_EQUAL_STRING("SHOCK",   ems::ft_button_label(7));
}

/** 超出範圍的索引回佔位字串 */
static void test_button_label_out_of_range_is_placeholder() {
    TEST_ASSERT_EQUAL_STRING("?", ems::ft_button_label(8));
}

// ============================================================
//  Group 3: RTC 暫存器解碼
// ============================================================

/** 黃金值：2026-09-09（三）14:03:27 的 DS3231 暫存器 → 各欄位與壓縮秒數 */
static void test_rtc_decode_golden() {
    const uint8_t raw[7] = {0x27, 0x03, 0x14, 0x03, 0x09, 0x09, 0x26};  // 秒/分/時/星期/日/月/年
    ems::FtRtcTime t;
    TEST_ASSERT_TRUE(ems::ft_rtc_decode(raw, t));
    TEST_ASSERT_EQUAL_UINT8(27, t.second);
    TEST_ASSERT_EQUAL_UINT8(3,  t.minute);
    TEST_ASSERT_EQUAL_UINT8(14, t.hour);
    TEST_ASSERT_EQUAL_UINT8(9,  t.day);
    TEST_ASSERT_EQUAL_UINT8(9,  t.month);
    TEST_ASSERT_EQUAL_UINT8(26, t.year);
    // Unix epoch（UTC）：python calendar.timegm(2026-09-09 14:03:27) = 1788962607
    TEST_ASSERT_EQUAL_UINT32(1788962607u, ems::ft_rtc_time_to_seconds(t));
}

/** 世紀旗標（月暫存器 bit7）是合法位元，不影響月份 */
static void test_rtc_decode_century_bit_is_allowed() {
    const uint8_t raw[7] = {0x00, 0x00, 0x00, 0x01, 0x01, 0x89, 0x00};  // 月 = 0x09 | century
    ems::FtRtcTime t;
    TEST_ASSERT_TRUE(ems::ft_rtc_decode(raw, t));
    TEST_ASSERT_EQUAL_UINT8(9, t.month);
}

/** SDA 浮空讀到全 0xFF → 拒絕（nibble F 不是 BCD、保留位元非 0） */
static void test_rtc_decode_all_ff_is_invalid() {
    const uint8_t raw[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ems::FtRtcTime t;
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(raw, t));
}

/** 全 0x00（日=0、月=0、星期=0）→ 拒絕；未初始化 buffer 常是這個樣子 */
static void test_rtc_decode_all_zero_is_invalid() {
    const uint8_t raw[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ems::FtRtcTime t;
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(raw, t));
}

/** 不存在的日期組合要擋：平年 2/29、2/30、4/31；閏年 2/29 要收 */
static void test_rtc_decode_rejects_impossible_dates() {
    ems::FtRtcTime t;
    const uint8_t feb29_2026[7] = {0x00, 0x00, 0x00, 0x01, 0x29, 0x02, 0x26};  // 2026 平年
    const uint8_t feb30_2028[7] = {0x00, 0x00, 0x00, 0x01, 0x30, 0x02, 0x28};  // 閏年也沒有 2/30
    const uint8_t apr31_2026[7] = {0x00, 0x00, 0x00, 0x01, 0x31, 0x04, 0x26};
    const uint8_t feb29_2028[7] = {0x00, 0x00, 0x00, 0x01, 0x29, 0x02, 0x28};  // 2028 閏年
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(feb29_2026, t));
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(feb30_2028, t));
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(apr31_2026, t));
    TEST_ASSERT_TRUE(ems::ft_rtc_decode(feb29_2028, t));
    TEST_ASSERT_EQUAL_UINT8(29, t.day);
}

/** 各欄位單獨越界／非法各自被擋：秒 nibble A、12 小時制旗標、月 13、日 32、星期 0 */
static void test_rtc_decode_rejects_each_invalid_field() {
    const uint8_t good[7] = {0x27, 0x03, 0x14, 0x03, 0x09, 0x09, 0x26};  // 黃金值當底
    ems::FtRtcTime t;

    uint8_t badSecond[7]; memcpy(badSecond, good, 7); badSecond[0] = 0x2A;  // 個位 nibble A
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(badSecond, t));

    uint8_t bad12h[7]; memcpy(bad12h, good, 7); bad12h[2] = 0x54;  // bit6 = 12 小時制
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(bad12h, t));

    uint8_t badMonth[7]; memcpy(badMonth, good, 7); badMonth[5] = 0x13;  // 月 13
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(badMonth, t));

    uint8_t badDay[7]; memcpy(badDay, good, 7); badDay[4] = 0x32;  // 日 32
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(badDay, t));

    uint8_t badDow[7]; memcpy(badDow, good, 7); badDow[3] = 0x00;  // 星期 0
    TEST_ASSERT_FALSE(ems::ft_rtc_decode(badDow, t));
}

/**
 * 建 FtRtcTime 的小工具（year 為 0~99）
 * @return 填好的時間
 */
static ems::FtRtcTime rtcTime(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    ems::FtRtcTime t;  // 回傳值
    t.year = year; t.month = month; t.day = day; t.hour = hour; t.minute = minute; t.second = second;
    return t;
}

/** epoch 在月底、閏年 2/29、年底的相鄰秒差都恰為 1（離線秒數靠差值，跨月不可跳 86401） */
static void test_rtc_seconds_boundaries_are_exactly_one_second_apart() {
    // 2026-01-31 23:59:59 = 1769903999 → 2026-02-01 00:00:00 = 1769904000
    TEST_ASSERT_EQUAL_UINT32(1769903999u, ems::ft_rtc_time_to_seconds(rtcTime(26, 1, 31, 23, 59, 59)));
    TEST_ASSERT_EQUAL_UINT32(1769904000u, ems::ft_rtc_time_to_seconds(rtcTime(26, 2, 1, 0, 0, 0)));
    // 閏年：2028-02-28 23:59:59 = 1835395199 → 2028-02-29 00:00:00 = 1835395200
    TEST_ASSERT_EQUAL_UINT32(1835395199u, ems::ft_rtc_time_to_seconds(rtcTime(28, 2, 28, 23, 59, 59)));
    TEST_ASSERT_EQUAL_UINT32(1835395200u, ems::ft_rtc_time_to_seconds(rtcTime(28, 2, 29, 0, 0, 0)));
    // 年底：2026-12-31 23:59:59 = 1798761599 → 2027-01-01 = 1798761600
    TEST_ASSERT_EQUAL_UINT32(1798761599u, ems::ft_rtc_time_to_seconds(rtcTime(26, 12, 31, 23, 59, 59)));
    TEST_ASSERT_EQUAL_UINT32(1798761600u, ems::ft_rtc_time_to_seconds(rtcTime(27, 1, 1, 0, 0, 0)));
}

/** epoch 值域兩端：seed 時間 2026-01-01 與 2099-12-31 23:59:59 都算得出且不溢位 */
static void test_rtc_seconds_range_endpoints() {
    TEST_ASSERT_EQUAL_UINT32(1767225600u, ems::ft_rtc_time_to_seconds(rtcTime(26, 1, 1, 0, 0, 0)));
    TEST_ASSERT_EQUAL_UINT32(4102444799u, ems::ft_rtc_time_to_seconds(rtcTime(99, 12, 31, 23, 59, 59)));
}

// ============================================================
//  Group 4: RTC 走時與備援電池
// ============================================================

/** 合理向前變化 → 晶振在走，不看經過時間；一輪最多允許前進 10 秒 */
static void test_rtc_tick_seconds_changed_is_ok() {
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Ok, ems::ft_classify_rtc_tick(1000, 1001, 0));
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Ok, ems::ft_classify_rtc_tick(1000, 1001, 999999));
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Ok, ems::ft_classify_rtc_tick(1000, 1010, 0));
}

/** 倒退或一輪跳超過 10 秒 → Invalid；套進狀態變成黏性 RTC I/O ERROR，之後正常走時也不清 */
static void test_rtc_tick_backward_or_jump_is_invalid_and_sticky() {
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Invalid, ems::ft_classify_rtc_tick(1001, 1000, 0));
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Invalid, ems::ft_classify_rtc_tick(1000, 1011, 0));
    ems::FactoryTestState s = allGoodState();
    s = ems::ft_apply_rtc_tick(s, ems::FtRtcTick::Invalid);
    TEST_ASSERT_TRUE(s.rtc_io_error);
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Ok, s.rtc_tick);  // 走時狀態不動，失敗走 io_error
    s = ems::ft_apply_rtc_tick(s, ems::FtRtcTick::Ok);
    TEST_ASSERT_TRUE(s.rtc_io_error);
    TEST_ASSERT_EQUAL_STRING("RTC I/O ERROR", ems::ft_fail_reason(s));
}

/** 讀值相同但未達 3000ms → 觀察中（1 秒輪詢，第一輪必然相同） */
static void test_rtc_tick_same_within_timeout_is_unknown() {
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Unknown, ems::ft_classify_rtc_tick(1000, 1000, 0));
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Unknown, ems::ft_classify_rtc_tick(1000, 1000, 2999));
}

/** 邊界 3000ms 含：達到 3 秒秒數沒動就是晶振停擺 */
static void test_rtc_tick_same_at_or_beyond_timeout_is_stuck() {
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Stuck, ems::ft_classify_rtc_tick(1000, 1000, 3000));
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Stuck, ems::ft_classify_rtc_tick(1000, 1000, 60000));
}

/** 走時 Stuck 黏性：停振過一次，之後又走起來也不能翻回 Ok；未 Stuck 時照本輪觀測 */
static void test_rtc_tick_stuck_is_sticky() {
    ems::FactoryTestState s = allGoodState();
    s = ems::ft_apply_rtc_tick(s, ems::FtRtcTick::Unknown);
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Unknown, s.rtc_tick);
    s = ems::ft_apply_rtc_tick(s, ems::FtRtcTick::Stuck);
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Stuck, s.rtc_tick);
    s = ems::ft_apply_rtc_tick(s, ems::FtRtcTick::Ok);
    TEST_ASSERT_EQUAL(ems::FtRtcTick::Stuck, s.rtc_tick);
    TEST_ASSERT_EQUAL_STRING("RTC NOT TICKING", ems::ft_fail_reason(s));
}

/** 第一次上電 OSF 亮、沒有任何基準（沒 seed 過也沒記錄）→ Unverified（要求斷電驗證） */
static void test_rtc_backup_first_power_on_is_unverified() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Unverified,
                      ems::ft_classify_rtc_backup(true, false, false, 600, true, ems::FtRtcBackup::Ok));
}

/** 有過基準（seed 過或記錄過）又失憶 → Lost，即使存的結論是 Ok、即使只是按 RST（OSF 是新證據） */
static void test_rtc_backup_osf_after_baseline_is_lost() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Lost,
                      ems::ft_classify_rtc_backup(true, true, true, 0, false, ems::FtRtcBackup::Ok));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Lost,
                      ems::ft_classify_rtc_backup(true, true, false, 0, false, ems::FtRtcBackup::Unverified));
    // 出廠已在走時所以沒 seed 過，但斷電驗證期間電池鬆脫：只有 last_seen 記錄也算有基準
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Lost,
                      ems::ft_classify_rtc_backup(true, false, true, 0, true, ems::FtRtcBackup::Unverified));
}

/** 沒有上次記錄（NVS 空）就算不出離線時間 → Unverified，模組出廠已在走時也一樣要斷電驗證 */
static void test_rtc_backup_without_last_seen_is_unverified() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Unverified,
                      ems::ft_classify_rtc_backup(false, false, false, 600, true, ems::FtRtcBackup::Ok));
}

/** 離線 ≥ 5 秒且 ESP 真的斷過電 → Ok，從 Unverified 或 Lost 都能轉正 */
static void test_rtc_backup_long_off_time_with_power_off_is_ok() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Ok, ems::ft_classify_rtc_backup(false, true, true, 5,  true, ems::FtRtcBackup::Unverified));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Ok, ems::ft_classify_rtc_backup(false, true, true, 15, true, ems::FtRtcBackup::Lost));
}

/** 離線夠久但 ESP 沒斷電證據（按住 RST 超過 5 秒、RTC 仍由 VCC 供電）→ 沿用存的結論，不給 Ok */
static void test_rtc_backup_requires_esp_power_off_evidence() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Unverified, ems::ft_classify_rtc_backup(false, true, true, 600, false, ems::FtRtcBackup::Unverified));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Lost,       ems::ft_classify_rtc_backup(false, true, true, 600, false, ems::FtRtcBackup::Lost));
}

/** 離線 < 5 秒（按 RST）或負值（RTC 被重設）沒有新證據 → 沿用存的結論，即使 ESP 說斷過電 */
static void test_rtc_backup_short_or_negative_off_time_keeps_stored() {
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Unverified, ems::ft_classify_rtc_backup(false, true, true, 4,   true, ems::FtRtcBackup::Unverified));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Ok,         ems::ft_classify_rtc_backup(false, true, true, 2,   true, ems::FtRtcBackup::Ok));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Lost,       ems::ft_classify_rtc_backup(false, true, true, 0,   true, ems::FtRtcBackup::Lost));
    TEST_ASSERT_EQUAL(ems::FtRtcBackup::Unverified, ems::ft_classify_rtc_backup(false, true, true, -30, true, ems::FtRtcBackup::Unverified));
}

// ============================================================
//  Group 5: 總判定、失敗與等待原因
// ============================================================

/** 全部合格 → Pass，且兩種原因都是 nullptr */
static void test_evaluate_all_good_passes() {
    const ems::FactoryTestState s = allGoodState();
    TEST_ASSERT_EQUAL(ems::FtVerdict::Pass, ems::ft_evaluate(s));
    TEST_ASSERT_NULL(ems::ft_fail_reason(s));
    TEST_ASSERT_NULL(ems::ft_pending_reason(s));
}

/** 選配裝置在不在都不影響 Pass */
static void test_evaluate_optional_devices_present_still_passes() {
    ems::FactoryTestState s = allGoodState();
    s.eeprom_present = true;
    s.gauge_present  = true;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Pass, ems::ft_evaluate(s));
}

/** 預設狀態（尚未掃到任何裝置）→ Fail，原因 RTC MISSING；鎖住「預設值不可意外等於 PASS」 */
static void test_default_state_fails_as_rtc_missing() {
    const ems::FactoryTestState s;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Fail, ems::ft_evaluate(s));
    TEST_ASSERT_EQUAL_STRING("RTC MISSING", ems::ft_fail_reason(s));
}

/** 八種失敗各自的原因字串與 Fail 判定 */
static void test_each_failure_reason() {
    ems::FactoryTestState s;

    s = allGoodState(); s.i2c_bus_stuck = true;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Fail, ems::ft_evaluate(s));
    TEST_ASSERT_EQUAL_STRING("I2C BUS STUCK", ems::ft_fail_reason(s));

    s = allGoodState(); s.i2c_bus_error = true; s.i2c_error_code = 4;
    TEST_ASSERT_EQUAL_STRING("I2C BUS ERROR", ems::ft_fail_reason(s));

    s = allGoodState(); s.rtc_present = false;
    TEST_ASSERT_EQUAL_STRING("RTC MISSING", ems::ft_fail_reason(s));

    s = allGoodState(); s.rtc_io_error = true;
    TEST_ASSERT_EQUAL_STRING("RTC I/O ERROR", ems::ft_fail_reason(s));

    s = allGoodState(); s.rtc_tick = ems::FtRtcTick::Stuck;
    TEST_ASSERT_EQUAL_STRING("RTC NOT TICKING", ems::ft_fail_reason(s));

    s = allGoodState(); s.rtc_backup = ems::FtRtcBackup::Lost;
    TEST_ASSERT_EQUAL_STRING("RTC BATTERY", ems::ft_fail_reason(s));

    s = allGoodState(); s.button_error = ems::FtButtonError::WrongOrder;
    TEST_ASSERT_EQUAL_STRING("BUTTON ORDER", ems::ft_fail_reason(s));

    s = allGoodState(); s.button_error = ems::FtButtonError::Multiple;
    TEST_ASSERT_EQUAL_STRING("BUTTON SHORT", ems::ft_fail_reason(s));
    TEST_ASSERT_EQUAL(ems::FtVerdict::Fail, ems::ft_evaluate(s));
}

/** 原因優先序：bus 被拉住 > NVS > RTC 缺席 > RTC I/O > 按鍵（顯示根因，不顯示連帶症狀） */
static void test_fail_reason_priority() {
    ems::FactoryTestState s = allGoodState();
    s.button_error  = ems::FtButtonError::WrongOrder;
    TEST_ASSERT_EQUAL(ems::FtFailKind::ButtonOrder, ems::ft_fail_kind(s));
    s.rtc_io_error  = true;
    TEST_ASSERT_EQUAL_STRING("RTC I/O ERROR", ems::ft_fail_reason(s));
    s.rtc_present = false;
    TEST_ASSERT_EQUAL_STRING("RTC MISSING", ems::ft_fail_reason(s));
    s = ems::ft_mark_nvs_error(s);
    TEST_ASSERT_EQUAL_STRING("NVS ERROR", ems::ft_fail_reason(s));
    TEST_ASSERT_EQUAL(ems::FtFailKind::NvsError, ems::ft_fail_kind(s));
    s.i2c_bus_stuck = true;
    TEST_ASSERT_EQUAL_STRING("I2C BUS STUCK", ems::ft_fail_reason(s));
}

/** 種類 ↔ 字串一對一，且 ft_fail_reason 與 ft_fail_kind 永遠同源 */
static void test_fail_kind_reason_mapping() {
    TEST_ASSERT_NULL(ems::ft_fail_reason_for(ems::FtFailKind::None));
    TEST_ASSERT_EQUAL_STRING("I2C BUS STUCK",   ems::ft_fail_reason_for(ems::FtFailKind::BusStuck));
    TEST_ASSERT_EQUAL_STRING("I2C BUS ERROR",   ems::ft_fail_reason_for(ems::FtFailKind::BusError));
    TEST_ASSERT_EQUAL_STRING("NVS ERROR",       ems::ft_fail_reason_for(ems::FtFailKind::NvsError));
    TEST_ASSERT_EQUAL_STRING("RTC MISSING",     ems::ft_fail_reason_for(ems::FtFailKind::RtcMissing));
    TEST_ASSERT_EQUAL_STRING("RTC I/O ERROR",   ems::ft_fail_reason_for(ems::FtFailKind::RtcIo));
    TEST_ASSERT_EQUAL_STRING("RTC NOT TICKING", ems::ft_fail_reason_for(ems::FtFailKind::RtcStuck));
    TEST_ASSERT_EQUAL_STRING("RTC BATTERY",     ems::ft_fail_reason_for(ems::FtFailKind::RtcBattery));
    TEST_ASSERT_EQUAL_STRING("BUTTON ORDER",    ems::ft_fail_reason_for(ems::FtFailKind::ButtonOrder));
    TEST_ASSERT_EQUAL_STRING("BUTTON SHORT",    ems::ft_fail_reason_for(ems::FtFailKind::ButtonShort));
    TEST_ASSERT_EQUAL(ems::FtFailKind::None, ems::ft_fail_kind(allGoodState()));
}

/** 等待原因優先序：POWER CYCLE > RTC > BUTTONS（斷電會清按鍵進度，所以先提示斷電） */
static void test_pending_reason_priority() {
    ems::FactoryTestState s = allGoodState();
    s.buttons_done = 3;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Pending, ems::ft_evaluate(s));
    TEST_ASSERT_EQUAL_STRING("BUTTONS", ems::ft_pending_reason(s));
    s.rtc_tick = ems::FtRtcTick::Unknown;
    TEST_ASSERT_EQUAL_STRING("RTC", ems::ft_pending_reason(s));
    s.rtc_backup = ems::FtRtcBackup::Unverified;
    TEST_ASSERT_EQUAL_STRING("POWER CYCLE", ems::ft_pending_reason(s));
    TEST_ASSERT_NULL(ems::ft_fail_reason(s));
}

/** 備援未驗證時即使按鍵全按完也不 Pass */
static void test_backup_unverified_blocks_pass() {
    ems::FactoryTestState s = allGoodState();
    s.rtc_backup = ems::FtRtcBackup::Unverified;
    TEST_ASSERT_EQUAL(ems::FtVerdict::Pending, ems::ft_evaluate(s));
}

/** 顯示字串：三種判定與三種備援狀態都是 ASCII 且兩兩不同 */
static void test_labels_are_ascii_and_distinct() {
    TEST_ASSERT_EQUAL_STRING("PASS",    ems::ft_verdict_label(ems::FtVerdict::Pass));
    TEST_ASSERT_EQUAL_STRING("FAIL",    ems::ft_verdict_label(ems::FtVerdict::Fail));
    TEST_ASSERT_EQUAL_STRING("WAITING", ems::ft_verdict_label(ems::FtVerdict::Pending));
    TEST_ASSERT_EQUAL_STRING("BACKUP OK", ems::ft_rtc_backup_label(ems::FtRtcBackup::Ok));
    const char* labels[2] = {  // 非 Ok 的兩個備援字串，驗 ASCII 與相異
        ems::ft_rtc_backup_label(ems::FtRtcBackup::Unverified),
        ems::ft_rtc_backup_label(ems::FtRtcBackup::Lost),
    };
    TEST_ASSERT_NOT_EQUAL(0, strcmp(labels[0], labels[1]));
    for (uint8_t i = 0; i < 2; i++) {
        for (const char* c = labels[i]; *c != '\0'; c++) {
            TEST_ASSERT_TRUE_MESSAGE((static_cast<unsigned char>(*c)) < 0x80, "備援字串含非 ASCII 字元");
        }
    }
}

/**
 * Unity 進入點
 * @param argc 未使用
 * @param argv 未使用
 * @return Unity 測試結束碼（0 = 全數通過）
 */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_probe_classification);
    RUN_TEST(test_scan_all_ack_sets_present_flags);
    RUN_TEST(test_scan_nack_is_absent_not_error);
    RUN_TEST(test_scan_timeout_sets_bus_stuck);
    RUN_TEST(test_scan_other_code_sets_bus_error_with_code);
    RUN_TEST(test_scan_errors_are_sticky);
    RUN_TEST(test_scan_rtc_absent_is_sticky);
    RUN_TEST(test_scan_does_not_mutate_input);
    RUN_TEST(test_buttons_in_order_completes);
    RUN_TEST(test_buttons_wrong_first_press_is_order_error);
    RUN_TEST(test_buttons_skip_is_order_error);
    RUN_TEST(test_buttons_repeat_of_last_accepted_is_ignored);
    RUN_TEST(test_buttons_older_repeat_is_order_error);
    RUN_TEST(test_buttons_two_at_once_is_short);
    RUN_TEST(test_buttons_last_pair_short_is_caught);
    RUN_TEST(test_buttons_error_is_sticky);
    RUN_TEST(test_buttons_after_complete_are_ignored);
    RUN_TEST(test_buttons_out_of_range_and_empty_are_ignored);
    RUN_TEST(test_button_labels_are_ascii_and_distinct);
    RUN_TEST(test_button_label_matches_sot_order);
    RUN_TEST(test_button_label_out_of_range_is_placeholder);
    RUN_TEST(test_rtc_decode_golden);
    RUN_TEST(test_rtc_decode_century_bit_is_allowed);
    RUN_TEST(test_rtc_decode_all_ff_is_invalid);
    RUN_TEST(test_rtc_decode_all_zero_is_invalid);
    RUN_TEST(test_rtc_decode_rejects_impossible_dates);
    RUN_TEST(test_rtc_decode_rejects_each_invalid_field);
    RUN_TEST(test_rtc_seconds_boundaries_are_exactly_one_second_apart);
    RUN_TEST(test_rtc_seconds_range_endpoints);
    RUN_TEST(test_rtc_tick_seconds_changed_is_ok);
    RUN_TEST(test_rtc_tick_backward_or_jump_is_invalid_and_sticky);
    RUN_TEST(test_rtc_tick_same_within_timeout_is_unknown);
    RUN_TEST(test_rtc_tick_same_at_or_beyond_timeout_is_stuck);
    RUN_TEST(test_rtc_tick_stuck_is_sticky);
    RUN_TEST(test_rtc_backup_first_power_on_is_unverified);
    RUN_TEST(test_rtc_backup_osf_after_baseline_is_lost);
    RUN_TEST(test_rtc_backup_without_last_seen_is_unverified);
    RUN_TEST(test_rtc_backup_long_off_time_with_power_off_is_ok);
    RUN_TEST(test_rtc_backup_requires_esp_power_off_evidence);
    RUN_TEST(test_rtc_backup_short_or_negative_off_time_keeps_stored);
    RUN_TEST(test_evaluate_all_good_passes);
    RUN_TEST(test_evaluate_optional_devices_present_still_passes);
    RUN_TEST(test_default_state_fails_as_rtc_missing);
    RUN_TEST(test_each_failure_reason);
    RUN_TEST(test_fail_reason_priority);
    RUN_TEST(test_fail_kind_reason_mapping);
    RUN_TEST(test_pending_reason_priority);
    RUN_TEST(test_backup_unverified_blocks_pass);
    RUN_TEST(test_labels_are_ascii_and_distinct);
    return UNITY_END();
}
