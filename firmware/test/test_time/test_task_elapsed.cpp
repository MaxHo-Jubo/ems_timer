// Unit test: computeTaskElapsedMs
// 執行：pio test -e native -f test_time
#include <unity.h>
#include "ems_time.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

/** RUNNING 狀態：taskStart=1000, now=5000 → elapsed=4000 */
static void test_running_happy_path() {
    TEST_ASSERT_EQUAL_UINT32(4000,
        computeTaskElapsedMs(5000, 1000, 0, 0, STATE_RUNNING));
}

/** PAUSE 狀態（第一次暫停）：pauseStart=3000, taskStart=1000 → elapsed=2000 */
static void test_paused_first_time() {
    TEST_ASSERT_EQUAL_UINT32(2000,
        computeTaskElapsedMs(9999, 1000, 3000, 0, STATE_PAUSE));
}

/** PAUSE 狀態（含歷史暫停）：taskStart=1000, pauseStart=10000, prior=2000 → elapsed=7000 */
static void test_paused_after_prior_pauses() {
    TEST_ASSERT_EQUAL_UINT32(7000,
        computeTaskElapsedMs(99999, 1000, 10000, 2000, STATE_PAUSE));
}

/** 任務尚未開始：taskStart=0 → 永遠回 0 */
static void test_not_started_returns_zero() {
    TEST_ASSERT_EQUAL_UINT32(0,
        computeTaskElapsedMs(9999, 0, 0, 0, STATE_RUNNING));
    TEST_ASSERT_EQUAL_UINT32(0,
        computeTaskElapsedMs(9999, 0, 5000, 1000, STATE_PAUSE));
}

/** RUNNING 狀態含歷史暫停：taskStart=1000, now=10000, prior=2000 → elapsed=7000 */
static void test_running_after_one_pause() {
    TEST_ASSERT_EQUAL_UINT32(7000,
        computeTaskElapsedMs(10000, 1000, 3000, 2000, STATE_RUNNING));
}

/** millis() wrap-around 邊界：taskStart 接近 UINT32_MAX、now 已溢位 */
static void test_millis_wraparound() {
    // taskStart at UINT32_MAX - 0x100, now wrapped to 0x100
    // uint32_t 減法：0x100 - (UINT32_MAX - 0x100) = 0x201（含 +1 wrap）
    uint32_t taskStart = 0xFFFFFEFF;  // UINT32_MAX - 0x100
    uint32_t now       = 0x00000100;
    uint32_t expected  = now - taskStart;  // uint32_t wrap = 0x201
    TEST_ASSERT_EQUAL_UINT32(expected,
        computeTaskElapsedMs(now, taskStart, 0, 0, STATE_RUNNING));
}

/** STATE_IDLE 走 running 分支（同公式）：elapsed=4000 */
static void test_idle_state_same_formula_as_running() {
    TEST_ASSERT_EQUAL_UINT32(4000,
        computeTaskElapsedMs(5000, 1000, 0, 0, STATE_IDLE));
}

/** STATE_END 走 running 分支：elapsed=5000 */
static void test_end_state_same_formula_as_running() {
    TEST_ASSERT_EQUAL_UINT32(5000,
        computeTaskElapsedMs(6000, 1000, 0, 0, STATE_END));
}

/** 多段暫停累積驗證：task 從 t=1 起跑（避開 taskStartMs=0 的 early return）
 *  情境：跑 10s → 暫停 5s → 跑 10s → 暫停 3s → 跑 5s → 暫停 */
static void test_multiple_pause_segments() {
    // 第一次 PAUSE：task=1, pauseStart=10001 (跑了 10000ms), totalPaused=0
    //   預期 elapsed = 10001 - 1 - 0 = 10000
    TEST_ASSERT_EQUAL_UINT32(10000,
        computeTaskElapsedMs(99999, 1, 10001, 0, STATE_PAUSE));

    // 第二次 PAUSE：task=1, pauseStart=25001 (前跑 10s + 暫停 5s + 再跑 10s), totalPaused=5000
    //   預期 elapsed = 25001 - 1 - 5000 = 20000（累計兩段運行時間 10+10）
    TEST_ASSERT_EQUAL_UINT32(20000,
        computeTaskElapsedMs(99999, 1, 25001, 5000, STATE_PAUSE));

    // 第三次 PAUSE：task=1, pauseStart=33001 (再暫停 3s + 跑 5s), totalPaused=8000 (5000+3000)
    //   預期 elapsed = 33001 - 1 - 8000 = 25000（累計運行 10+10+5）
    TEST_ASSERT_EQUAL_UINT32(25000,
        computeTaskElapsedMs(99999, 1, 33001, 8000, STATE_PAUSE));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_running_happy_path);
    RUN_TEST(test_paused_first_time);
    RUN_TEST(test_paused_after_prior_pauses);
    RUN_TEST(test_not_started_returns_zero);
    RUN_TEST(test_running_after_one_pause);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_idle_state_same_formula_as_running);
    RUN_TEST(test_end_state_same_formula_as_running);
    RUN_TEST(test_multiple_pause_segments);
    return UNITY_END();
}
