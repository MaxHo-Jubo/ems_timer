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
    return UNITY_END();
}
