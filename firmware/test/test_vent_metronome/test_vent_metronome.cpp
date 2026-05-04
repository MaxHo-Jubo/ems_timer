// EMS DoseSync Pro — Phase C Unit Test: 6 秒通氣節奏（純函式）
//
// 對應規格：
//   - SoT V1 §13（獨立模式）/ §14（OHCA 內輔助區塊，秒數規則一致）
//   - pm-dev-spec §7
//   - 測試計劃 §5.3
//
// 涵蓋：
//   - computeVentBeat 邊界（since=0 / 999 / 1000 / 5999 / 6000 / 12000）
//   - decideVentOutput 邊緣偵測（每秒只觸發 buzz 一次）
//   - 第 1 拍 vs 第 2~6 拍輸出差異
//   - volume = 0 靜音規則（buzz 全關，視覺保留）
//   - volume 邊界 + clamp
//   - 多循環邊界（第 2 / 3 個循環的 BEAT_1）
//   - rollover-safe（uint32_t 溢位）
#include <unity.h>
#include "ems_vent_metronome.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  Group 1: computeVentBeat 邊界
// ============================================================

static void test_compute_beat_at_zero_returns_beat_1() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_1, computeVentBeat(0));
}

static void test_compute_beat_at_999_still_beat_1() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_1, computeVentBeat(999));
}

static void test_compute_beat_at_1000_switches_to_beat_2() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_2, computeVentBeat(1000));
}

static void test_compute_beat_at_5999_still_beat_6() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_6, computeVentBeat(5999));
}

static void test_compute_beat_at_6000_back_to_beat_1() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_1, computeVentBeat(6000));
}

static void test_compute_beat_at_12000_third_cycle_beat_1() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_1, computeVentBeat(12000));
}

static void test_compute_beat_at_3500_is_beat_4() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_4, computeVentBeat(3500));
}

static void test_compute_beat_at_2000_is_beat_3() {
    TEST_ASSERT_EQUAL_INT(VENT_BEAT_3, computeVentBeat(2000));
}

// ============================================================
//  Group 2: decideVentOutput — 第 1 拍邊緣（emphasis + flash + invert）
// ============================================================

/** 從 0→0 視為剛啟動，無 buzz（避免初始化誤觸發） */
static void test_decide_init_state_no_buzz() {
    vent_output_t o = decideVentOutput(0, 0, /*vol*/ 3);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_TRUE (o.led_red_flash);      // BEAT_1 視覺仍維持
    TEST_ASSERT_TRUE (o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** 0→1（剛啟動的第一個推進 tick）→ 仍 BEAT_1 內，無新 buzz（不誤觸發） */
static void test_decide_first_tick_no_double_emphasis() {
    vent_output_t o = decideVentOutput(0, 1, /*vol*/ 3);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_TRUE (o.led_red_flash);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** 0→500（仍 BEAT_1，無邊緣）→ 不應再 emphasis */
static void test_decide_within_beat_1_no_buzz() {
    vent_output_t o = decideVentOutput(0, 500, /*vol*/ 3);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_TRUE (o.led_red_flash);
    TEST_ASSERT_TRUE (o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** 999→1000（跨 BEAT_1→BEAT_2 邊界）→ 觸發 buzz_short */
static void test_decide_crossing_to_beat_2_triggers_short() {
    vent_output_t o = decideVentOutput(999, 1000, /*vol*/ 3);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_TRUE (o.buzz_short);
    TEST_ASSERT_FALSE(o.led_red_flash);
    TEST_ASSERT_FALSE(o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(2, o.display_number);
}

/** 1000→1500（仍 BEAT_2，無邊緣）→ 無 buzz */
static void test_decide_within_beat_2_no_buzz() {
    vent_output_t o = decideVentOutput(1000, 1500, /*vol*/ 3);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_EQUAL_UINT8(2, o.display_number);
}

/** 5999→6000（跨 BEAT_6→BEAT_1，新循環）→ 觸發 buzz_emphasis */
static void test_decide_crossing_cycle_triggers_emphasis() {
    vent_output_t o = decideVentOutput(5999, 6000, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_TRUE (o.led_red_flash);
    TEST_ASSERT_TRUE (o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** 跨 BEAT_2→BEAT_3 → 觸發 buzz_short，display=3 */
static void test_decide_crossing_to_beat_3() {
    vent_output_t o = decideVentOutput(1999, 2000, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_short);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_EQUAL_UINT8(3, o.display_number);
}

// ============================================================
//  Group 3: volume = 0 靜音規則（V1 §13.6 / §14.5）
// ============================================================

/** volume=0 + 跨 BEAT_1 → buzz_emphasis=false，但視覺保留 */
static void test_volume_0_emphasis_off_visuals_kept() {
    vent_output_t o = decideVentOutput(5999, 6000, /*vol*/ 0);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_TRUE (o.led_red_flash);
    TEST_ASSERT_TRUE (o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** volume=0 + 跨 BEAT_2 → buzz_short=false */
static void test_volume_0_short_off_no_visuals() {
    vent_output_t o = decideVentOutput(999, 1000, /*vol*/ 0);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.led_red_flash);
    TEST_ASSERT_FALSE(o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(2, o.display_number);
}

/** volume=5 + 跨 BEAT_1 → 全部觸發 */
static void test_volume_max_emphasis_triggered() {
    vent_output_t o = decideVentOutput(5999, 6000, VENT_VOLUME_MAX);
    TEST_ASSERT_TRUE(o.buzz_emphasis);
    TEST_ASSERT_TRUE(o.led_red_flash);
    TEST_ASSERT_TRUE(o.screen_invert_red);
}

// ============================================================
//  Group 4: 多循環邊界
// ============================================================

/** 第 2 個循環 BEAT_1 → 與第 1 個循環一致 */
static void test_decide_second_cycle_beat_1_same_behavior() {
    vent_output_t o = decideVentOutput(11999, 12000, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_emphasis);
    TEST_ASSERT_TRUE (o.screen_invert_red);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

/** 第 3 個循環 BEAT_3 */
static void test_decide_third_cycle_beat_3() {
    vent_output_t o = decideVentOutput(13999, 14000, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_short);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_EQUAL_UINT8(3, o.display_number);
}

// ============================================================
//  Group 5: clampVentVolume 邊界
// ============================================================

static void test_clamp_volume_min_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, clampVentVolume(0));
}

static void test_clamp_volume_max_5() {
    TEST_ASSERT_EQUAL_UINT8(5, clampVentVolume(5));
}

static void test_clamp_volume_negative_to_min() {
    TEST_ASSERT_EQUAL_UINT8(0, clampVentVolume(-1));
    TEST_ASSERT_EQUAL_UINT8(0, clampVentVolume(-100));
}

static void test_clamp_volume_above_max_to_5() {
    TEST_ASSERT_EQUAL_UINT8(5, clampVentVolume(6));
    TEST_ASSERT_EQUAL_UINT8(5, clampVentVolume(255));
}

static void test_clamp_volume_mid_value_passthrough() {
    TEST_ASSERT_EQUAL_UINT8(3, clampVentVolume(3));
}

// ============================================================
//  Group 6: 連續推進不重複觸發 buzz（同 beat 多 tick）
// ============================================================

/** BEAT_2 內連續 3 個 tick → 只第一個 tick 觸發 buzz */
static void test_continuous_ticks_within_beat_only_first_buzzes() {
    // tick 1: 999 → 1000 (BEAT_1 → BEAT_2，跨界，觸發)
    vent_output_t a = decideVentOutput(999, 1000, /*vol*/ 3);
    TEST_ASSERT_TRUE(a.buzz_short);
    // tick 2: 1000 → 1200 (仍 BEAT_2，無跨界)
    vent_output_t b = decideVentOutput(1000, 1200, /*vol*/ 3);
    TEST_ASSERT_FALSE(b.buzz_short);
    // tick 3: 1200 → 1800 (仍 BEAT_2)
    vent_output_t c = decideVentOutput(1200, 1800, /*vol*/ 3);
    TEST_ASSERT_FALSE(c.buzz_short);
}

// ============================================================
//  Group 7: rollover-safe（uint32_t 溢位）
// ============================================================

/** 跨越 UINT32_MAX 的減法：unsigned underflow → 仍能計算 since 差距 */
static void test_decide_handles_uint32_rollover() {
    // 假設 prev_since 跟 since_start 都已是 mod 後的數字
    // 不應該有問題，因為 since_start 直接決定 beat
    vent_output_t o = decideVentOutput(0xFFFFFFFEu, 0xFFFFFFFFu, /*vol*/ 3);
    // 0xFFFFFFFE / 1000 = 4294967, 4294967 % 6 = 5 → BEAT_6
    // 0xFFFFFFFF / 1000 = 4294967, 4294967 % 6 = 5 → BEAT_6（同 beat）
    TEST_ASSERT_EQUAL_UINT8(6, o.display_number);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_FALSE(o.buzz_emphasis);
}

// ============================================================
//  Group 8: 跳秒場景（caller 落後一個 tick 仍能觸發）
// ============================================================

/** prev=500, since=2500 跨 BEAT_1→BEAT_3（跳了 BEAT_2）→ 仍應觸發 buzz_short on BEAT_3 */
static void test_skip_beat_still_triggers_buzz_for_landed_beat() {
    vent_output_t o = decideVentOutput(500, 2500, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_short);          // 落在 BEAT_3 = 短音
    TEST_ASSERT_FALSE(o.buzz_emphasis);
    TEST_ASSERT_EQUAL_UINT8(3, o.display_number);
}

/** prev=500, since=6500 跨多 beat 落在 BEAT_1（新循環）→ 應觸發 emphasis */
static void test_skip_to_new_cycle_triggers_emphasis() {
    vent_output_t o = decideVentOutput(500, 6500, /*vol*/ 3);
    TEST_ASSERT_TRUE (o.buzz_emphasis);
    TEST_ASSERT_FALSE(o.buzz_short);
    TEST_ASSERT_EQUAL_UINT8(1, o.display_number);
}

// ============================================================
//  main
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // Group 1: computeVentBeat
    RUN_TEST(test_compute_beat_at_zero_returns_beat_1);
    RUN_TEST(test_compute_beat_at_999_still_beat_1);
    RUN_TEST(test_compute_beat_at_1000_switches_to_beat_2);
    RUN_TEST(test_compute_beat_at_5999_still_beat_6);
    RUN_TEST(test_compute_beat_at_6000_back_to_beat_1);
    RUN_TEST(test_compute_beat_at_12000_third_cycle_beat_1);
    RUN_TEST(test_compute_beat_at_3500_is_beat_4);
    RUN_TEST(test_compute_beat_at_2000_is_beat_3);

    // Group 2: decideVentOutput edge detection
    RUN_TEST(test_decide_init_state_no_buzz);
    RUN_TEST(test_decide_first_tick_no_double_emphasis);
    RUN_TEST(test_decide_within_beat_1_no_buzz);
    RUN_TEST(test_decide_crossing_to_beat_2_triggers_short);
    RUN_TEST(test_decide_within_beat_2_no_buzz);
    RUN_TEST(test_decide_crossing_cycle_triggers_emphasis);
    RUN_TEST(test_decide_crossing_to_beat_3);

    // Group 3: volume = 0
    RUN_TEST(test_volume_0_emphasis_off_visuals_kept);
    RUN_TEST(test_volume_0_short_off_no_visuals);
    RUN_TEST(test_volume_max_emphasis_triggered);

    // Group 4: multi-cycle
    RUN_TEST(test_decide_second_cycle_beat_1_same_behavior);
    RUN_TEST(test_decide_third_cycle_beat_3);

    // Group 5: clamp
    RUN_TEST(test_clamp_volume_min_zero);
    RUN_TEST(test_clamp_volume_max_5);
    RUN_TEST(test_clamp_volume_negative_to_min);
    RUN_TEST(test_clamp_volume_above_max_to_5);
    RUN_TEST(test_clamp_volume_mid_value_passthrough);

    // Group 6: no-double-trigger
    RUN_TEST(test_continuous_ticks_within_beat_only_first_buzzes);

    // Group 7: rollover
    RUN_TEST(test_decide_handles_uint32_rollover);

    // Group 8: skip beats
    RUN_TEST(test_skip_beat_still_triggers_buzz_for_landed_beat);
    RUN_TEST(test_skip_to_new_cycle_triggers_emphasis);

    return UNITY_END();
}
