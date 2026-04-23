// Unit test: decideVentTickAction
// 執行：pio test -e native -f test_vent
//
// 對應 PM 規格：docs/pm-dev-spec.md §4.2「6 秒 CPR 節拍 ±50ms（僅通氣模式啟用）」
// 涵蓋：首次啟動 / 週期邊界 / ±50ms 精度 / millis wraparound / 自訂週期
#include <unity.h>
#include "ems_vent.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// =========================================================
// 基本行為：首次啟動 + 週期觸發
// =========================================================

/** 節拍器尚未啟動（lastTickMs=0） → 立即 fire 第一次 tick */
static void test_not_started_fires_immediately() {
    auto a = decideVentTickAction(/*now*/ 12345, /*last*/ 0);
    TEST_ASSERT_TRUE(a.fireBeep);
}

/** 剛 fire 完 1ms → 不 fire */
static void test_right_after_fire_no_op() {
    auto a = decideVentTickAction(/*now*/ 5001, /*last*/ 5000);
    TEST_ASSERT_FALSE(a.fireBeep);
}

/** 剛 fire 完 1 秒 → 不 fire */
static void test_one_second_after_fire_no_op() {
    auto a = decideVentTickAction(/*now*/ 6000, /*last*/ 5000);
    TEST_ASSERT_FALSE(a.fireBeep);
}

/** 距上次 5999ms（差 1ms 未到週期） → 不 fire */
static void test_just_before_interval_no_op() {
    auto a = decideVentTickAction(/*now*/ 6999, /*last*/ 1000);
    TEST_ASSERT_FALSE(a.fireBeep);
}

/** 剛好 6000ms → fire（邊界） */
static void test_exactly_at_interval_fires() {
    auto a = decideVentTickAction(/*now*/ 7000, /*last*/ 1000);
    TEST_ASSERT_TRUE(a.fireBeep);
}

/** 超過週期 500ms → fire */
static void test_after_interval_fires() {
    auto a = decideVentTickAction(/*now*/ 7500, /*last*/ 1000);
    TEST_ASSERT_TRUE(a.fireBeep);
}

// =========================================================
// ±50ms 精度邊界（PM §4.2 精度需求）
// =========================================================

/** 精度下界：距上次 5950ms（-50ms 邊界）→ 不 fire（尚在容忍期內） */
static void test_accuracy_50ms_under_no_op() {
    auto a = decideVentTickAction(/*now*/ 6950, /*last*/ 1000);
    TEST_ASSERT_FALSE(a.fireBeep);
}

/** 精度上界：距上次 6050ms（+50ms 邊界）→ fire */
static void test_accuracy_50ms_over_fires() {
    auto a = decideVentTickAction(/*now*/ 7050, /*last*/ 1000);
    TEST_ASSERT_TRUE(a.fireBeep);
}

// =========================================================
// millis() wraparound（uint32_t 49.7 天週期）
// =========================================================

/** millis wrap：last=UINT32_MAX-1000, now=5000 → uint32 差 = 6001 → fire */
static void test_millis_wraparound_fires() {
    uint32_t last = 0xFFFFFFFFUL - 1000;  // UINT32_MAX - 1000
    uint32_t now  = 5000;                  // wrap 後重新計數
    // now - last (uint32) = 5000 - (UINT32_MAX - 1000) = 6001 (unsigned wrap)
    auto a = decideVentTickAction(now, last);
    TEST_ASSERT_TRUE(a.fireBeep);
}

/** millis wrap 未到週期：last=UINT32_MAX-2000, now=3000 → diff=5001 → 不 fire */
static void test_millis_wraparound_before_interval_no_op() {
    uint32_t last = 0xFFFFFFFFUL - 2000;
    uint32_t now  = 3000;
    // now - last (uint32) = 3000 - (UINT32_MAX - 2000) = 5001
    auto a = decideVentTickAction(now, last);
    TEST_ASSERT_FALSE(a.fireBeep);
}

// =========================================================
// 自訂週期參數
// =========================================================

/** 自訂 1000ms 週期：last=500, now=1500 → diff=1000 → fire */
static void test_custom_interval_fires() {
    auto a = decideVentTickAction(/*now*/ 1500, /*last*/ 500, /*interval*/ 1000);
    TEST_ASSERT_TRUE(a.fireBeep);
}

/** 自訂 1000ms 週期：last=500, now=1499 → diff=999 → 不 fire */
static void test_custom_interval_before_no_op() {
    auto a = decideVentTickAction(/*now*/ 1499, /*last*/ 500, /*interval*/ 1000);
    TEST_ASSERT_FALSE(a.fireBeep);
}

// =========================================================
// Unity 主程式
// =========================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_not_started_fires_immediately);
    RUN_TEST(test_right_after_fire_no_op);
    RUN_TEST(test_one_second_after_fire_no_op);
    RUN_TEST(test_just_before_interval_no_op);
    RUN_TEST(test_exactly_at_interval_fires);
    RUN_TEST(test_after_interval_fires);
    RUN_TEST(test_accuracy_50ms_under_no_op);
    RUN_TEST(test_accuracy_50ms_over_fires);
    RUN_TEST(test_millis_wraparound_fires);
    RUN_TEST(test_millis_wraparound_before_interval_no_op);
    RUN_TEST(test_custom_interval_fires);
    RUN_TEST(test_custom_interval_before_no_op);
    return UNITY_END();
}
