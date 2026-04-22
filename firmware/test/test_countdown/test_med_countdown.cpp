// Unit test: decideMedCountdownAction
// 執行：pio test -e native -f test_countdown
//
// 對應 PM 規格：docs/pm-dev-spec.md §4.2「4 分鐘給藥高提醒 ±50ms」
// 涵蓋 1 分鐘中途警示、首次到時強提醒、每 30s 重複提醒、邊界值、millis wrap-around
#include <unity.h>
#include "ems_countdown.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// Helper: 判斷所有旗標都是 false
static void assert_no_op(const MedCountdownAction& a) {
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireReminderRepeat);
}

/** 倒數未啟動（countdownStartMs=0） → 全 false */
static void test_not_started_no_op() {
    auto a = decideMedCountdownAction(
        /*now*/ 99999, /*start*/ 0, /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0);
    assert_no_op(a);
}

/** 剛啟動 1 秒（elapsed=1000），距警示還很遠 → 全 false */
static void test_early_countdown_no_op() {
    auto a = decideMedCountdownAction(
        /*now*/ 11000, /*start*/ 10000, /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0);
    assert_no_op(a);
}

/** elapsed=0 剛好（warn1Min 的 elapsed > 0 防護） → 全 false */
static void test_elapsed_zero_guard() {
    auto a = decideMedCountdownAction(
        /*now*/ 10000, /*start*/ 10000, /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0);
    assert_no_op(a);
}

/** 剩餘剛好 60 秒（elapsed=180000）、warn1Min 未觸發 → fireWarn1Min */
static void test_warn_1min_at_boundary() {
    // start=0, now=180000, elapsed=180000, remaining=60000（正好等於 WARN_1MIN_MS）
    auto a = decideMedCountdownAction(
        /*now*/ 180000, /*start*/ 0 + 1, /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0);
    // elapsed = 180000 - 1 = 179999, remaining = 240000 - 179999 = 60001（不符 ≤60000）
    // 這個 case 驗證剛好 1 秒後才會觸發 warn，證明是 ≤ 而非 <
    // 為了精準驗「=60000 時觸發」需 start=0 但被 guard 擋（elapsed=start=0 回 noop）
    // 用 start=1 可行：now=180001, elapsed=180000, remaining=60000 → warn=true
    auto a2 = decideMedCountdownAction(
        /*now*/ 180001, /*start*/ 1, /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0);
    TEST_ASSERT_TRUE(a2.fireWarn1Min);
    TEST_ASSERT_FALSE(a2.fireReminderStart);
    TEST_ASSERT_FALSE(a2.fireReminderRepeat);

    // 對比：a 的 remaining=60001，尚未達門檻
    assert_no_op(a);
}

/** warn1Min 已觸發過 → 不重複觸發（幂等性） */
static void test_warn_1min_not_retriggered() {
    auto a = decideMedCountdownAction(
        /*now*/ 180001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ true,  // 已觸發過
        /*lastRep*/ 0);
    assert_no_op(a);
}

/** 歸零剛到（elapsed=TOTAL, reminderActive=false, warn1T=false）
 *  原程式碼：STEP 02 觸發 warn1Min，STEP 04 不 return（因 elapsed>=TOTAL），
 *           STEP 05 觸發 reminderStart → 同一 cycle 兩旗標都 true */
static void test_first_expire_with_warn() {
    auto a = decideMedCountdownAction(
        /*now*/ 240001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false,
        /*lastRep*/ 0);
    TEST_ASSERT_TRUE(a.fireWarn1Min);       // remaining=0 ≤ 60000
    TEST_ASSERT_TRUE(a.fireReminderStart);  // elapsed >= TOTAL
    TEST_ASSERT_FALSE(a.fireReminderRepeat);
}

/** 歸零（warn1Min 已觸發過，reminderActive=false） → 只 fireReminderStart */
static void test_first_expire_without_warn() {
    auto a = decideMedCountdownAction(
        /*now*/ 240001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ true,  // warn 已觸發
        /*lastRep*/ 0);
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_TRUE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireReminderRepeat);
}

/** 歸零前 1ms（elapsed=TOTAL-1）→ 只 fireWarn1Min（不 fireReminderStart） */
static void test_just_before_expire() {
    auto a = decideMedCountdownAction(
        /*now*/ 240000, /*start*/ 1,  // elapsed=239999
        /*rA*/ false, /*w1T*/ false,
        /*lastRep*/ 0);
    TEST_ASSERT_TRUE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireReminderRepeat);
}

/** reminderActive=true, 距上次重複提醒剛好 29999ms → 未達週期，no-op */
static void test_repeat_just_before_interval() {
    auto a = decideMedCountdownAction(
        /*now*/ 40000, /*start*/ 1,  // elapsed >= TOTAL 只影響 start 分支，此處 reminderActive=true
        /*rA*/ true, /*w1T*/ true,
        /*lastRep*/ 10001);  // now - lastRep = 29999 < 30000
    assert_no_op(a);
}

/** reminderActive=true, 距上次剛好 30000ms → fireReminderRepeat */
static void test_repeat_at_interval() {
    auto a = decideMedCountdownAction(
        /*now*/ 40000, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true,
        /*lastRep*/ 10000);  // now - lastRep = 30000 ≥ 30000
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_TRUE(a.fireReminderRepeat);
}

/** millis wrap-around：countdownStart 接近 UINT32_MAX，now 已溢位
 *  uint32_t 減法自動 wrap，elapsed 仍正確 */
static void test_millis_wraparound() {
    // start = UINT32_MAX - 1000 = 0xFFFFFC17, now wrap 過 0 到 239000
    // elapsed = now - start = 239000 - 0xFFFFFC17 = (uint32_t wrap) 240000
    uint32_t start = 0xFFFFFFFFU - 1000U + 1U;  // 簡化：start 讓 elapsed 剛好 = 240001
    // start = 0xFFFFFC18, now = 240000 → elapsed = 240000 - 0xFFFFFC18 = (wrap) 241000
    // 為精準驗證，直接算：要 elapsed = TOTAL + 1 = 240001，取 start = now - 240001 (wrap)
    uint32_t now   = 500U;
    uint32_t expectedElapsed = 240001U;
    uint32_t computedStart = now - expectedElapsed;  // uint32_t wrap
    auto a = decideMedCountdownAction(
        now, computedStart,
        /*rA*/ false, /*w1T*/ true,
        /*lastRep*/ 0);
    TEST_ASSERT_TRUE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireReminderRepeat);
}

/** 自訂 totalMs / warningMs 參數：驗證非預設值仍正確 */
static void test_custom_timing_params() {
    // 短倒數：10 秒總 / 2 秒警示
    auto a = decideMedCountdownAction(
        /*now*/ 8001, /*start*/ 1,  // elapsed = 8000, remaining = 10000-8000 = 2000 ≤ 2000
        /*rA*/ false, /*w1T*/ false, /*lastRep*/ 0,
        /*total*/ 10000, /*warn*/ 2000, /*repeat*/ 5000);
    TEST_ASSERT_TRUE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_not_started_no_op);
    RUN_TEST(test_early_countdown_no_op);
    RUN_TEST(test_elapsed_zero_guard);
    RUN_TEST(test_warn_1min_at_boundary);
    RUN_TEST(test_warn_1min_not_retriggered);
    RUN_TEST(test_first_expire_with_warn);
    RUN_TEST(test_first_expire_without_warn);
    RUN_TEST(test_just_before_expire);
    RUN_TEST(test_repeat_just_before_interval);
    RUN_TEST(test_repeat_at_interval);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_custom_timing_params);
    return UNITY_END();
}
