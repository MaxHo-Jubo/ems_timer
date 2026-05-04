// EMS DoseSync Pro — Phase A Unit Test: twoStepConfirm
//
// 對應測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.2
// 執行：pio test -e native -f test_two_step_confirm
//
// 涵蓋：
//   - happy path：第一段 armed → 第二段在 timeout 內成立
//   - 邊界 5000ms 整：成立（含 timeout）
//   - 邊界 5001ms：視為新 cycle
//   - tick() 自動清 armed
//   - 多 cycle 連續按
//   - 跨型別 instance 獨立
#include <unity.h>
#include "ems_two_step_confirm.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  Happy path
// ============================================================

/** 第一次按 → armed=true, first_press=now, 回 false */
static void test_first_press_arms_returns_false() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    bool ok = twoStepConfirm_press(&s, 1000);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_UINT32(1000, s.first_press_ms);
}

/** armed 中第二次按（在 timeout 內）→ 成立, armed 清, 回 true */
static void test_second_press_within_timeout_succeeds() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);                 // first
    bool ok = twoStepConfirm_press(&s, 4000);       // 3000ms later
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.armed);
}

// ============================================================
//  Boundary：5000ms 整 vs 5001ms
// ============================================================

/** 邊界：第二次按剛好 5000ms（含）→ 成立 */
static void test_boundary_5000ms_inclusive_succeeds() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);
    bool ok = twoStepConfirm_press(&s, 6000);       // 5000ms 後
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(s.armed);
}

/** 邊界：第二次按 5001ms → 視為新 cycle，armed 重置為新 first_press */
static void test_boundary_5001ms_re_arms_new_cycle() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);
    bool ok = twoStepConfirm_press(&s, 6001);       // 5001ms 後
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_TRUE(s.armed);                       // 仍 armed（新 cycle）
    TEST_ASSERT_EQUAL_UINT32(6001, s.first_press_ms);
}

// ============================================================
//  tick() 自動清 armed
// ============================================================

/** tick 在 timeout 內 → armed 不清 */
static void test_tick_within_timeout_keeps_armed() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);
    twoStepConfirm_tick(&s, 4000);                   // 3000ms 後
    TEST_ASSERT_TRUE(s.armed);
}

/** tick 在 timeout 邊界 5000ms → armed 不清（≤ timeout 仍視為 armed 期內） */
static void test_tick_at_boundary_5000ms_keeps_armed() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);
    twoStepConfirm_tick(&s, 6000);                   // 剛好 5000ms
    TEST_ASSERT_TRUE(s.armed);
}

/** tick 超過 timeout → armed 自動清 */
static void test_tick_after_timeout_clears_armed() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);
    twoStepConfirm_tick(&s, 6001);                   // 5001ms
    TEST_ASSERT_FALSE(s.armed);
}

/** tick 在未 armed 狀態 → 無副作用 */
static void test_tick_when_not_armed_is_noop() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_tick(&s, 99999);
    TEST_ASSERT_FALSE(s.armed);
}

// ============================================================
//  多 cycle 連續按
// ============================================================

/** 連續 3 個確認 cycle 都正常運作 */
static void test_multiple_consecutive_cycles() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);

    // Cycle 1
    TEST_ASSERT_FALSE(twoStepConfirm_press(&s, 1000));
    TEST_ASSERT_TRUE (twoStepConfirm_press(&s, 3000));

    // Cycle 2
    TEST_ASSERT_FALSE(twoStepConfirm_press(&s, 10000));
    TEST_ASSERT_TRUE (twoStepConfirm_press(&s, 12000));

    // Cycle 3
    TEST_ASSERT_FALSE(twoStepConfirm_press(&s, 20000));
    TEST_ASSERT_TRUE (twoStepConfirm_press(&s, 21000));
}

/** 逾時後立即第二次按 → 視為 cycle 2 的第一段 */
static void test_press_after_timeout_becomes_new_first_press() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 1000);                  // cycle 1 first
    twoStepConfirm_press(&s, 7000);                  // 逾時 → 變成 cycle 2 first
    bool ok = twoStepConfirm_press(&s, 8000);        // cycle 2 second（1000ms 內）
    TEST_ASSERT_TRUE(ok);
}

// ============================================================
//  跨型別 instance 獨立（EPI / 電擊 / Amio 不互相影響）
// ============================================================

/** 兩個獨立 instance 互不干擾 */
static void test_two_instances_are_independent() {
    two_step_confirm_t epi, amio;
    twoStepConfirm_init(&epi,  5000);
    twoStepConfirm_init(&amio, 5000);

    // EPI armed
    twoStepConfirm_press(&epi, 1000);
    TEST_ASSERT_TRUE(epi.armed);
    TEST_ASSERT_FALSE(amio.armed);

    // Amio armed（不影響 EPI）
    twoStepConfirm_press(&amio, 2000);
    TEST_ASSERT_TRUE(epi.armed);
    TEST_ASSERT_TRUE(amio.armed);

    // Amio 確認成立（EPI 仍 armed）
    bool amio_ok = twoStepConfirm_press(&amio, 3000);
    TEST_ASSERT_TRUE(amio_ok);
    TEST_ASSERT_FALSE(amio.armed);
    TEST_ASSERT_TRUE(epi.armed);

    // EPI 確認成立
    bool epi_ok = twoStepConfirm_press(&epi, 5000);
    TEST_ASSERT_TRUE(epi_ok);
}

// ============================================================
//  rollover 安全性（uint32 wrap-around）
// ============================================================

/** first_press 接近 UINT32_MAX，第二次按時 now 已 wrap → uint32 減法仍正確 */
static void test_press_handles_millis_rollover() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 5000);
    twoStepConfirm_press(&s, 0xFFFFFE0Cu);            // 距 UINT32_MAX = 500ms
    // 500ms wrap 後 now = 0xFFFFFE0C + 2000 (含 wrap) = 0x000007DB
    bool ok = twoStepConfirm_press(&s, 0x000007DBu);  // 經過 ~2000ms 後 wrap
    TEST_ASSERT_TRUE(ok);
}

// ============================================================
//  Init 行為
// ============================================================

/** init 必清 armed 並設 timeout */
static void test_init_clears_state_and_sets_timeout() {
    two_step_confirm_t s;
    s.armed          = true;       // 髒值
    s.first_press_ms = 9999;
    twoStepConfirm_init(&s, 3000);
    TEST_ASSERT_FALSE(s.armed);
    TEST_ASSERT_EQUAL_UINT32(0, s.first_press_ms);
    TEST_ASSERT_EQUAL_UINT32(3000, s.timeout_ms);
}

/** 不同 timeout 配置：3000ms timeout 在 3001 後 re-arm */
static void test_custom_timeout_3000ms() {
    two_step_confirm_t s;
    twoStepConfirm_init(&s, 3000);
    twoStepConfirm_press(&s, 1000);
    TEST_ASSERT_TRUE (twoStepConfirm_press(&s, 4000));   // 3000ms 整成立
    twoStepConfirm_press(&s, 10000);                      // cycle 2 first
    TEST_ASSERT_FALSE(twoStepConfirm_press(&s, 13001));   // 3001ms 視為新 cycle
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_first_press_arms_returns_false);
    RUN_TEST(test_second_press_within_timeout_succeeds);

    RUN_TEST(test_boundary_5000ms_inclusive_succeeds);
    RUN_TEST(test_boundary_5001ms_re_arms_new_cycle);

    RUN_TEST(test_tick_within_timeout_keeps_armed);
    RUN_TEST(test_tick_at_boundary_5000ms_keeps_armed);
    RUN_TEST(test_tick_after_timeout_clears_armed);
    RUN_TEST(test_tick_when_not_armed_is_noop);

    RUN_TEST(test_multiple_consecutive_cycles);
    RUN_TEST(test_press_after_timeout_becomes_new_first_press);

    RUN_TEST(test_two_instances_are_independent);

    RUN_TEST(test_press_handles_millis_rollover);

    RUN_TEST(test_init_clears_state_and_sets_timeout);
    RUN_TEST(test_custom_timeout_3000ms);

    return UNITY_END();
}
