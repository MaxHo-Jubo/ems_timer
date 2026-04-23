// Unit test: decideMedCountdownAction
// 執行：pio test -e native -f test_countdown
//
// 對應 PM 規格：docs/pm-dev-spec.md §4.2「4 分鐘給藥倒數 ±50ms」
// 涵蓋：
//   - MED_PHASE 四階段判定（NOT_STARTED / COUNTING / WARNING / ALARMING）
//   - 1 分鐘 WARNING 進入警示
//   - 首次到時 ALARMING 觸發
//   - ALARMING 階段連續 pulse（取代 v1.3 前每 30s 週期）
//   - 邊界值、millis wrap-around、自訂參數
#include <unity.h>
#include "ems_countdown.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ---------- Helpers ----------

/** 判斷所有 fire 旗標都是 false（phase 另外檢查） */
static void assert_no_fire(const MedCountdownAction& a) {
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
}

// =========================================================
// Phase 判定測試（MED_PHASE enum）
// =========================================================

/** 倒數未啟動 → phase = NOT_STARTED，全 false */
static void test_phase_not_started() {
    auto a = decideMedCountdownAction(
        /*now*/ 99999, /*start*/ 0, /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_EQUAL(MED_PHASE_NOT_STARTED, a.phase);
    assert_no_fire(a);
}

/** 倒數剛啟動（剩餘 > 1min） → phase = COUNTING */
static void test_phase_counting_early() {
    // elapsed = 10000, remaining = 230000 > 60000 → COUNTING
    auto a = decideMedCountdownAction(
        /*now*/ 11000, /*start*/ 1000,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_EQUAL(MED_PHASE_COUNTING, a.phase);
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
}

/** 剩餘剛好 60s → phase = WARNING */
static void test_phase_warning_at_boundary() {
    // start=1, now=180001 → elapsed=180000, remaining=60000 → WARNING
    auto a = decideMedCountdownAction(
        /*now*/ 180001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_EQUAL(MED_PHASE_WARNING, a.phase);
}

/** 剩餘 60001ms（差 1ms）→ 仍 COUNTING */
static void test_phase_counting_just_before_warning() {
    // start=1, now=180000 → elapsed=179999, remaining=60001 → COUNTING
    auto a = decideMedCountdownAction(
        /*now*/ 180000, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_EQUAL(MED_PHASE_COUNTING, a.phase);
}

/** reminderActive=true → phase = ALARMING（不論 elapsed） */
static void test_phase_alarming_when_reminder_active() {
    auto a = decideMedCountdownAction(
        /*now*/ 300000, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true, /*lastPulse*/ 299000);
    TEST_ASSERT_EQUAL(MED_PHASE_ALARMING, a.phase);
}

// =========================================================
// WARNING 觸發測試（fireWarn1Min）
// =========================================================

/** 剛啟動 1 秒，距警示還很遠 → COUNTING，不觸發 warn */
static void test_early_countdown_no_fire() {
    auto a = decideMedCountdownAction(
        /*now*/ 11000, /*start*/ 10000, /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_EQUAL(MED_PHASE_COUNTING, a.phase);
    assert_no_fire(a);
}

/** elapsed=0 剛好（warn 的 elapsed > 0 防護） → 全 false */
static void test_elapsed_zero_guard() {
    auto a = decideMedCountdownAction(
        /*now*/ 10000, /*start*/ 10000, /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    assert_no_fire(a);
}

/** 剩餘剛好 60s + 未觸發過 → fireWarn1Min + WARNING phase */
static void test_warn_1min_at_boundary() {
    auto a = decideMedCountdownAction(
        /*now*/ 180001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_TRUE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
    TEST_ASSERT_EQUAL(MED_PHASE_WARNING, a.phase);
}

/** 剩餘 60001ms（差 1ms）→ 不觸發 warn */
static void test_warn_1min_just_before_threshold() {
    auto a = decideMedCountdownAction(
        /*now*/ 180000, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    assert_no_fire(a);
    TEST_ASSERT_EQUAL(MED_PHASE_COUNTING, a.phase);
}

/** warn 已觸發過（w1T=true）→ 不重複觸發（冪等性） */
static void test_warn_1min_not_retriggered() {
    auto a = decideMedCountdownAction(
        /*now*/ 180001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ true, /*lastPulse*/ 0);
    assert_no_fire(a);
    TEST_ASSERT_EQUAL(MED_PHASE_WARNING, a.phase);
}

// =========================================================
// ALARMING 首次觸發測試（fireReminderStart）
// =========================================================

/** 歸零剛到（w1T=false, rA=false）→ warn + reminderStart 同 cycle 觸發 */
static void test_first_expire_with_warn() {
    auto a = decideMedCountdownAction(
        /*now*/ 240001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_TRUE(a.fireWarn1Min);       // remaining=0 ≤ 60000
    TEST_ASSERT_TRUE(a.fireReminderStart);  // elapsed >= TOTAL
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
    // 此 cycle rA=false，phase 仍是 WARNING（剩餘=0）；下次呼叫 rA=true 時才變 ALARMING
    TEST_ASSERT_EQUAL(MED_PHASE_WARNING, a.phase);
}

/** 歸零（warn 已觸發過，rA=false）→ 只 fireReminderStart */
static void test_first_expire_without_warn() {
    auto a = decideMedCountdownAction(
        /*now*/ 240001, /*start*/ 1,
        /*rA*/ false, /*w1T*/ true, /*lastPulse*/ 0);
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_TRUE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
}

/** 歸零前 1ms（elapsed=TOTAL-1）→ 只 fireWarn1Min */
static void test_just_before_expire() {
    auto a = decideMedCountdownAction(
        /*now*/ 240000, /*start*/ 1,  // elapsed=239999
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0);
    TEST_ASSERT_TRUE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
}

// =========================================================
// ALARMING 連續發報測試（fireAlarmingPulse，取代 v1.3 前的 30s 週期）
// =========================================================

/** ALARMING 中，距上次 pulse 剛好 1499ms（差 1ms）→ 不 pulse */
static void test_alarming_pulse_just_before_interval() {
    auto a = decideMedCountdownAction(
        /*now*/ 251499, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true,
        /*lastPulse*/ 250000);  // now - lastPulse = 1499 < 1500
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
    TEST_ASSERT_EQUAL(MED_PHASE_ALARMING, a.phase);
}

/** ALARMING 中，距上次剛好 1500ms → fireAlarmingPulse */
static void test_alarming_pulse_at_interval() {
    auto a = decideMedCountdownAction(
        /*now*/ 251500, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true,
        /*lastPulse*/ 250000);  // now - lastPulse = 1500
    TEST_ASSERT_FALSE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_TRUE(a.fireAlarmingPulse);
    TEST_ASSERT_EQUAL(MED_PHASE_ALARMING, a.phase);
}

/** ALARMING 中，距上次超過 2 秒 → fireAlarmingPulse（連續發報語意） */
static void test_alarming_pulse_continuous() {
    auto a = decideMedCountdownAction(
        /*now*/ 252000, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true,
        /*lastPulse*/ 250000);  // diff = 2000
    TEST_ASSERT_TRUE(a.fireAlarmingPulse);
}

// =========================================================
// millis() wrap-around（uint32_t 49.7 天週期）
// =========================================================

/** start 接近 UINT32_MAX，now 已 wrap → elapsed 仍正確（uint32 減法） */
static void test_millis_wraparound_expire() {
    uint32_t now = 500U;
    uint32_t expectedElapsed = 240001U;
    uint32_t computedStart = now - expectedElapsed;  // uint32 wrap
    auto a = decideMedCountdownAction(
        now, computedStart,
        /*rA*/ false, /*w1T*/ true, /*lastPulse*/ 0);
    TEST_ASSERT_TRUE(a.fireReminderStart);
    TEST_ASSERT_FALSE(a.fireAlarmingPulse);
}

/** ALARMING pulse 計算也適用 wrap-around：lastPulse 近 UINT32_MAX，now wrap */
static void test_alarming_pulse_wraparound() {
    uint32_t lastPulse = 0xFFFFFFFFUL - 500;  // UINT32_MAX - 500
    uint32_t now = 1000;                      // wrap 後 now - lastPulse = 1501（uint32 wrap）
    auto a = decideMedCountdownAction(
        now, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true, lastPulse);
    TEST_ASSERT_TRUE(a.fireAlarmingPulse);
}

// =========================================================
// 自訂參數
// =========================================================

/** 自訂 totalMs / warningMs / alarmPulseMs 參數：驗證非預設值正確 */
static void test_custom_timing_params() {
    // 短倒數：10 秒總 / 2 秒警示
    auto a = decideMedCountdownAction(
        /*now*/ 8001, /*start*/ 1,  // elapsed=8000, remaining=2000 ≤ 2000
        /*rA*/ false, /*w1T*/ false, /*lastPulse*/ 0,
        /*total*/ 10000, /*warn*/ 2000, /*alarmPulse*/ 500);
    TEST_ASSERT_TRUE(a.fireWarn1Min);
    TEST_ASSERT_FALSE(a.fireReminderStart);
    TEST_ASSERT_EQUAL(MED_PHASE_WARNING, a.phase);
}

/** 自訂 alarmPulseMs=500：ALARMING 中 distance=500 → fire */
static void test_custom_alarm_pulse_interval() {
    auto a = decideMedCountdownAction(
        /*now*/ 10500, /*start*/ 1,
        /*rA*/ true, /*w1T*/ true, /*lastPulse*/ 10000,
        /*total*/ 5000, /*warn*/ 1000, /*alarmPulse*/ 500);
    TEST_ASSERT_TRUE(a.fireAlarmingPulse);
    TEST_ASSERT_EQUAL(MED_PHASE_ALARMING, a.phase);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    // Phase 判定
    RUN_TEST(test_phase_not_started);
    RUN_TEST(test_phase_counting_early);
    RUN_TEST(test_phase_warning_at_boundary);
    RUN_TEST(test_phase_counting_just_before_warning);
    RUN_TEST(test_phase_alarming_when_reminder_active);
    // WARNING 觸發
    RUN_TEST(test_early_countdown_no_fire);
    RUN_TEST(test_elapsed_zero_guard);
    RUN_TEST(test_warn_1min_at_boundary);
    RUN_TEST(test_warn_1min_just_before_threshold);
    RUN_TEST(test_warn_1min_not_retriggered);
    // ALARMING 首次觸發
    RUN_TEST(test_first_expire_with_warn);
    RUN_TEST(test_first_expire_without_warn);
    RUN_TEST(test_just_before_expire);
    // ALARMING 連續 pulse
    RUN_TEST(test_alarming_pulse_just_before_interval);
    RUN_TEST(test_alarming_pulse_at_interval);
    RUN_TEST(test_alarming_pulse_continuous);
    // wraparound
    RUN_TEST(test_millis_wraparound_expire);
    RUN_TEST(test_alarming_pulse_wraparound);
    // 自訂參數
    RUN_TEST(test_custom_timing_params);
    RUN_TEST(test_custom_alarm_pulse_interval);
    return UNITY_END();
}
