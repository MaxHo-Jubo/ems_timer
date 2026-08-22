// EMS DoseSync Pro — Impl-Phase H：燃料計純邏輯測試
//
// 黃金值來自 2026-08-22 實機驗收（docs/power-module-purchase.md §10.8）：
//   VCELL raw=0xC030 → 3843.75mV，函式回傳 uint16_t 取整（無條件捨去）為 3843
//   SOC   raw=0x366A → 54.41%，函式回傳 uint8_t 取整（無條件捨去）為 54
#include <unity.h>
#include "fuel_gauge_logic.h"
#include "null_fuel_gauge.h"

void setUp()    {}
void tearDown() {}

static void test_vcell_golden_value_from_hardware_acceptance() {
    // 驗證實機值：0xC030 轉換為 3843mV，對應實測 3.844V（補表與四捨五入差異）
    // 0xC030 >> 4 = 3075 counts；3075 * 1.25mV = 3843.75mV → 取整 3843
    TEST_ASSERT_EQUAL_UINT16(3843, ems::vcell_raw_to_mv(0xC030));
}

static void test_vcell_zero_is_zero() {
    // 邊界值驗證：零輸入應回傳零
    TEST_ASSERT_EQUAL_UINT16(0, ems::vcell_raw_to_mv(0x0000));
}

static void test_vcell_uses_high_12_bits_only() {
    // 位元應用驗證：低 4 bit 是無效位元，改變它們不可影響結果
    TEST_ASSERT_EQUAL_UINT16(ems::vcell_raw_to_mv(0xC030), ems::vcell_raw_to_mv(0xC03F));
}

static void test_vcell_max_raw_returns_unclamped_value() {
    // 極端值行為鎖定：0xFFFF >> 4 = 4095 counts × 1.25 = 5118.75 → 5118mV
    // 單節 LiPo 物理上不可能到此值，但本層不做合理性上界——「讀值不可信」判定屬 FuelReading.valid 層
    // 本測試鎖住當前行為，避免日後加上界時成為無聲 regression
    TEST_ASSERT_EQUAL_UINT16(5118, ems::vcell_raw_to_mv(0xFFFF));
}

static void test_soc_golden_value_from_hardware_acceptance() {
    // 驗證實機值：0x366A 轉換為 54%，對應實測 54.4%（補表差異）
    // 0x36 = 54 整數；0x6A = 106 → 106/256 = 0.41 → 54.41% → 取整 54
    TEST_ASSERT_EQUAL_UINT8(54, ems::soc_raw_to_percent(0x366A));
}

static void test_soc_clamps_above_100() {
    // 安全約束驗證：燃料計剛充飽時可能回報 >100%，UI 不該顯示 103%
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0x6700));
}

static void test_soc_zero_is_zero() {
    // 邊界值驗證：零輸入應回傳零
    TEST_ASSERT_EQUAL_UINT8(0, ems::soc_raw_to_percent(0x0000));
}

static void test_soc_max_raw_clamps_to_100() {
    // 極端值行為鎖定：垃圾讀值（0xFFFF = 255.99%）與正常超衝一樣夾到 100
    // 本層無法區分兩者，「讀值不可信」判定屬 FuelReading.valid 層
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0xFFFF));
}

static void test_soc_exactly_100_hits_clamp_boundary() {
    // 邊界測試：0x6400 = 整數 100、小數 0 —— 恰好落在 `>= SOC_PERCENT_MAX` 的邊界上
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0x6400));
}

// ============================================================
//  Group 2: 充電狀態趨勢推導
// ============================================================

static void test_trend_before_window_full_is_unknown() {
    // 硬體無充電狀態訊號，開機後窗未滿時必須誠實回 Unknown，不可猜 Discharging
    ems::ChargeTrendTracker t;
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
}

static void test_trend_rising_beyond_deadband_is_charging() {
    // 窗內電壓上升超過死區判定為充電中（3810 - 3800 = +10mV > 3mV）
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);  // 窗滿：3810 - 3800 = +10mV > 死區
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
}

static void test_trend_falling_beyond_deadband_is_discharging() {
    // 窗內電壓下降超過死區判定為放電中（3800 - 3810 = -10mV < -3mV）
    ems::ChargeTrendTracker t;
    t.push(3810);
    t.push(3805);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_within_deadband_is_idle() {
    // 靜置時電壓會微幅抖動，死區內不可在充放電之間跳
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3800);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_slides_window_and_updates() {
    // 先判定充電，之後持續下降應翻成放電（窗會滑動，不是只看最初三筆）
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
    t.push(3805);
    t.push(3800);
    t.push(3795);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_reset_returns_to_unknown() {
    // reset() 清除取樣窗，狀態回到 Unknown；新資料應反映新趨勢，不受舊值殘留影響
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
    t.reset();
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    // reset 後推入相反方向的值，應明確判定為下降（驗證樣本值不會殘留）
    t.push(3810);
    t.push(3805);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_ignores_middle_sample_only_endpoints_matter() {
    // 非單調數列：中間值飆高但頭尾相同 → 應為 Idle。
    // 若 newest 索引誤寫成 (head_+1)%N（讀到中間值），會誤判 Charging，本測試即失敗。
    // 現有的單調數列測試無法區分這兩種實作——中間樣本永遠站在正確方向上。
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3900);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_delta_equal_to_deadband_stays_idle() {
    // 死區邊界：變化剛好等於死區值（3mV）→ 不觸發，仍為 Idle
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3801);
    t.push(3803);   // delta = +3，等於死區，不應觸發
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_delta_just_over_deadband_charges() {
    // 死區邊界：變化剛好超過死區值 → 觸發 Charging。
    // 與上一個測試成對，鎖住死區常數——若 TREND_DEADBAND_MV 被誤改，兩者至少一個會 fail。
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3801);
    t.push(3804);   // delta = +4，超過死區
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
}

// ============================================================
//  Group 3: 低電量遲滯與一次性提示
// ============================================================

static void test_low_battery_not_triggered_above_threshold() {
    // ENTER 門檻的寬鬆方向：21% 以上都不觸發（擋住門檻升到 21%）
    ems::LowBatteryLatch latch;
    latch.update(30);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_low_battery_triggers_at_or_below_20() {
    // ENTER 門檻的嚴格方向：20% 以下都觸發（擋住門檻降到 20%）
    ems::LowBatteryLatch latch;
    latch.update(20);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_low_battery_stays_low_between_thresholds() {
    // 遲滯：進入 20% 後，回升到 22% 仍算低電量（未達 25% 解除門檻）
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(22);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_low_battery_clears_at_or_above_25() {
    // EXIT 門檻的嚴格方向：25% 以上才解除（擋住門檻升到 25%）
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(25);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_first_entry_consumed_only_once() {
    // §13.16：執行中只顯示一次提示
    ems::LowBatteryLatch latch;
    latch.update(18);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_first_entry_rearms_after_recovery() {
    // 充完電拔掉再掉到 20%，應該重新提醒一次
    ems::LowBatteryLatch latch;
    latch.update(18);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    latch.update(30);  // 充電回升，解除
    latch.update(18);  // 再次掉落
    TEST_ASSERT_TRUE(latch.consume_first_entry());
}

static void test_boot_already_low_counts_as_entry() {
    // 開機當下就低於 20%，沒有「上一次在門檻上」的紀錄，仍必須算一次跨越
    ems::LowBatteryLatch latch;
    latch.update(15);
    TEST_ASSERT_TRUE(latch.is_low());
    TEST_ASSERT_TRUE(latch.consume_first_entry());
}

static void test_hysteresis_does_not_retrigger_while_oscillating() {
    // 先跌破 20% 觸發並消費首次事件，之後在 20~25% 之間來回抖動，不可重複觸發提示
    ems::LowBatteryLatch latch;
    latch.update(19);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    latch.update(23);
    latch.update(21);
    latch.update(24);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_first_entry_cleared_on_recovery_without_consume() {
    // 進入低電量後未消費即回升——解除時必須一併清掉待消費事件，
    // 否則 consume_first_entry() 會在非低電量狀態下回 true（違反契約）。
    // 這條清除線目前無任何測試守住，刪掉它 26 個測試全綠。
    ems::LowBatteryLatch latch;
    latch.update(18);   // 進入，掛起事件，但不消費
    latch.update(30);   // 回升解除
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_low_battery_not_triggered_at_21_percent() {
    // ENTER 門檻的寬鬆方向：21% 不應觸發。
    // 現有測試只驗 20% 會觸發（擋住門檻降到 19），沒有測試擋住門檻升到 21。
    ems::LowBatteryLatch latch;
    latch.update(21);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_low_battery_does_not_clear_at_24_percent() {
    // EXIT 門檻的寬鬆方向：24% 不應解除。
    // 現有測試只驗 25% 會解除（擋住門檻升到 26），沒有測試擋住門檻降到 24。
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(24);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_offline_sentinel_does_not_clear_low_battery() {
    // 哨兵值不得清除已鎖存的低電量狀態。
    // 255 = 燃料計不在線（spec §4.4）；若沒有契約防呆，255 >= 25 會誤判為電量回升。
    ems::LowBatteryLatch latch;
    latch.update(18);   // 進入低電量
    latch.update(255);  // 感測器離線
    TEST_ASSERT_TRUE(latch.is_low());              // 鎖存不得被清
    TEST_ASSERT_TRUE(latch.consume_first_entry()); // 待消費事件也不得被清
}

// ============================================================
//  Group 4: Null backend 降級
// ============================================================

static void test_null_backend_not_present() {
    // NullFuelGauge 表示無實體硬體在線；上層無條件呼叫不需分支
    ems::NullFuelGauge nb;
    TEST_ASSERT_FALSE(nb.is_present());
}

static void test_null_backend_read_is_invalid() {
    // 不在線時 valid=false；caller 不可把 millivolts / percent 當合法讀數
    ems::NullFuelGauge nb;
    const ems::FuelReading r = nb.read();
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(0, r.millivolts);  // 讀值無效時電壓須鎖住 0
    TEST_ASSERT_EQUAL_UINT8(0, r.percent);      // 讀值無效時百分比須鎖住 0
}

// ============================================================
//  Group：合理性判定（VCELL 上界與 SOC 原始值檢查）
// ============================================================

static void test_vcell_at_upper_bound_is_plausible() {
    // 4400 是界內，不是界外——邊界值本身必須可信，否則正常充飽會被判成異常
    TEST_ASSERT_TRUE(ems::is_plausible_vcell_mv(ems::PLAUSIBLE_MAX_MV));
}

static void test_vcell_above_upper_bound_is_implausible() {
    // 超過上界代表 I2C 位元翻轉或 EMI 干擾，讀值不可信
    TEST_ASSERT_FALSE(ems::is_plausible_vcell_mv(ems::PLAUSIBLE_MAX_MV + 1));
}

static void test_soc_raw_at_upper_bound_is_plausible() {
    // 高位元組 = PLAUSIBLE_SOC_WHOLE_MAX（界內）；低位元組帶非零值確認只看整數部分。
    // 不硬編字面值：PLAUSIBLE_SOC_WHOLE_MAX 註解明寫「需上機實測校正」，常數改動後
    // 測試須自動跟著新邊界走，否則會靜默測到已被校正掉的舊邊界
    const uint16_t raw = static_cast<uint16_t>(ems::PLAUSIBLE_SOC_WHOLE_MAX) << 8 | 0x80;
    TEST_ASSERT_TRUE(ems::is_plausible_soc_raw(raw));
}

static void test_soc_raw_above_upper_bound_is_implausible() {
    // 緊鄰上界外一格：高位元組 = PLAUSIBLE_SOC_WHOLE_MAX + 1，必須判為不可信。
    // 與 VCELL 側 test_vcell_above_upper_bound_is_implausible 成對，補上 SOC 側原本
    // 缺失的鑑別力——沒有這條，把 `<=` 鬆動一格（PLAUSIBLE_SOC_WHOLE_MAX + 1）
    // 不會被任何既有測試發現
    const uint16_t raw = static_cast<uint16_t>(ems::PLAUSIBLE_SOC_WHOLE_MAX + 1) << 8 | 0x80;
    TEST_ASSERT_FALSE(ems::is_plausible_soc_raw(raw));
}

static void test_soc_raw_garbage_is_implausible() {
    // 0xFFFF 是 I2C 匯流排故障的典型訊號：高位元組 255 遠超任何真實電量。
    // 若沒有這個檢查，soc_raw_to_percent() 會把它夾成 100 並標記 valid=true，
    // 把感測器故障偽裝成「滿電、一切正常」
    TEST_ASSERT_FALSE(ems::is_plausible_soc_raw(0xFFFF));
}

// ============================================================
//  Group：FuelReading constructor 映射正確性
// ============================================================

static void test_fuel_reading_constructor_maps_fields_correctly() {
    // 用三個互不相同的值，任何一組參數互換都會讓某條斷言變紅
    const ems::FuelReading r(true, 3843, 54);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(3843, r.millivolts);
    TEST_ASSERT_EQUAL_UINT8(54, r.percent);
}

// ============================================================
//  Group：make_reading() 統合判定測試
// ============================================================

static void test_make_reading_accepts_real_hardware_values() {
    // 2026-08-22 實機驗收值：VCELL raw=0xC030 → 3843mV、SOC raw=0x366A → 54%
    const ems::FuelReading r = ems::make_reading(0xC030, 0x366A);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(3843, r.millivolts);
    TEST_ASSERT_EQUAL_UINT8(54, r.percent);
}

static void test_make_reading_rejects_garbage_soc() {
    // VCELL 正常但 SOC 是垃圾：整筆必須無效。
    // 少了這條守衛，soc_raw_to_percent() 會把 0xFFFF 夾成 100 並標記 valid=true，
    // 把感測器故障偽裝成「滿電、一切正常」——低電量警示最不該有的失效方向
    const ems::FuelReading r = ems::make_reading(0xC030, 0xFFFF);
    TEST_ASSERT_FALSE(r.valid);
}

static void test_make_reading_rejects_implausible_vcell() {
    // SOC 正常但電壓超出單節 LiPo 的物理上界：整筆必須無效。
    // 0xFFF0 >> 4 = 4095 counts；4095 * 1.25 = 5118mV > 4400，超界
    const ems::FuelReading r = ems::make_reading(0xFFF0, 0x366A);
    TEST_ASSERT_FALSE(r.valid);
}

static void test_make_reading_zero_percent_is_valid() {
    // 0% 是合法讀數（電池真的沒電），不可被當成異常濾掉——
    // 這正是整個 valid 旗標設計要防的混淆
    // 0x9600 >> 4 = 2400 counts；2400 * 1.25 = 3000mV（界內）
    const ems::FuelReading r = ems::make_reading(0x9600, 0x0000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT8(0, r.percent);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_vcell_golden_value_from_hardware_acceptance);
    RUN_TEST(test_vcell_zero_is_zero);
    RUN_TEST(test_vcell_uses_high_12_bits_only);
    RUN_TEST(test_vcell_max_raw_returns_unclamped_value);
    RUN_TEST(test_soc_golden_value_from_hardware_acceptance);
    RUN_TEST(test_soc_clamps_above_100);
    RUN_TEST(test_soc_zero_is_zero);
    RUN_TEST(test_soc_max_raw_clamps_to_100);
    RUN_TEST(test_soc_exactly_100_hits_clamp_boundary);
    RUN_TEST(test_trend_before_window_full_is_unknown);
    RUN_TEST(test_trend_rising_beyond_deadband_is_charging);
    RUN_TEST(test_trend_falling_beyond_deadband_is_discharging);
    RUN_TEST(test_trend_within_deadband_is_idle);
    RUN_TEST(test_trend_slides_window_and_updates);
    RUN_TEST(test_trend_reset_returns_to_unknown);
    RUN_TEST(test_trend_ignores_middle_sample_only_endpoints_matter);
    RUN_TEST(test_trend_delta_equal_to_deadband_stays_idle);
    RUN_TEST(test_trend_delta_just_over_deadband_charges);
    RUN_TEST(test_low_battery_not_triggered_above_threshold);
    RUN_TEST(test_low_battery_triggers_at_or_below_20);
    RUN_TEST(test_low_battery_stays_low_between_thresholds);
    RUN_TEST(test_low_battery_clears_at_or_above_25);
    RUN_TEST(test_first_entry_consumed_only_once);
    RUN_TEST(test_first_entry_rearms_after_recovery);
    RUN_TEST(test_boot_already_low_counts_as_entry);
    RUN_TEST(test_hysteresis_does_not_retrigger_while_oscillating);
    RUN_TEST(test_first_entry_cleared_on_recovery_without_consume);
    RUN_TEST(test_low_battery_not_triggered_at_21_percent);
    RUN_TEST(test_low_battery_does_not_clear_at_24_percent);
    RUN_TEST(test_offline_sentinel_does_not_clear_low_battery);
    RUN_TEST(test_null_backend_not_present);
    RUN_TEST(test_null_backend_read_is_invalid);
    RUN_TEST(test_vcell_at_upper_bound_is_plausible);
    RUN_TEST(test_vcell_above_upper_bound_is_implausible);
    RUN_TEST(test_soc_raw_at_upper_bound_is_plausible);
    RUN_TEST(test_soc_raw_above_upper_bound_is_implausible);
    RUN_TEST(test_soc_raw_garbage_is_implausible);
    RUN_TEST(test_fuel_reading_constructor_maps_fields_correctly);
    RUN_TEST(test_make_reading_accepts_real_hardware_values);
    RUN_TEST(test_make_reading_rejects_garbage_soc);
    RUN_TEST(test_make_reading_rejects_implausible_vcell);
    RUN_TEST(test_make_reading_zero_percent_is_valid);
    return UNITY_END();
}
