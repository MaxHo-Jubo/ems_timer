// Unit test: ems_rtc — Wave 1（NullBackend 純邏輯）
// 執行：pio test -e native -f test_rtc
//
// 對齊 plan：docs/ds3231-integration-plan.md §3.1 / §3.2 / §8 Wave 1
// 涵蓋：
//   - NullRtcBackend::is_present() 永遠 false
//   - NullRtcBackend::now_epoch_ms() 永遠 0（fallback 到 spec §4.1「未對時」慣例）
//   - NullRtcBackend::set_epoch_ms() 永遠回 false（no-op，不可寫入）
//   - RtcBackend* 多型呼叫驗證 v-table 正常

#include <unity.h>

#include "ems_rtc.h"
#include "null_backend.h"

using namespace ems;

void setUp() {}
void tearDown() {}

// ============================================================
// NullRtcBackend 基本契約
// ============================================================

static void test_null_backend_is_not_present() {
    NullRtcBackend backend;
    TEST_ASSERT_FALSE(backend.is_present());
}

static void test_null_backend_now_returns_zero() {
    NullRtcBackend backend;
    TEST_ASSERT_EQUAL_UINT64(0, backend.now_epoch_ms());
}

static void test_null_backend_set_epoch_returns_false() {
    NullRtcBackend backend;
    TEST_ASSERT_FALSE(backend.set_epoch_ms(1704067200000ULL));  // 2024-01-01
    // 寫入應為 no-op：now_epoch_ms 仍回 0
    TEST_ASSERT_EQUAL_UINT64(0, backend.now_epoch_ms());
}

// ============================================================
// 多型驗證（caller 拿 RtcBackend* 不分支即可工作）
// ============================================================

static void test_null_backend_polymorphic_via_base_pointer() {
    NullRtcBackend impl;
    RtcBackend* backend = &impl;

    TEST_ASSERT_FALSE(backend->is_present());
    TEST_ASSERT_EQUAL_UINT64(0, backend->now_epoch_ms());
    TEST_ASSERT_FALSE(backend->set_epoch_ms(9999999999999ULL));
}

// ============================================================
// main
// ============================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_null_backend_is_not_present);
    RUN_TEST(test_null_backend_now_returns_zero);
    RUN_TEST(test_null_backend_set_epoch_returns_false);
    RUN_TEST(test_null_backend_polymorphic_via_base_pointer);
    return UNITY_END();
}
