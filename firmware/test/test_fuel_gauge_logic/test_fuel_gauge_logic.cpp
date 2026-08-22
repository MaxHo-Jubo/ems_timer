// EMS DoseSync Pro — Impl-Phase H：燃料計純邏輯測試
//
// 黃金值來自 2026-08-22 實機驗收（docs/power-module-purchase.md §10.8）：
//   VCELL raw=0xC030 → 3843.75mV，函式回傳 uint16_t 取整（無條件捨去）為 3843
//   SOC   raw=0x366A → 54.41%，函式回傳 uint8_t 取整（無條件捨去）為 54
#include <unity.h>
#include "fuel_gauge_logic.h"

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
    return UNITY_END();
}
