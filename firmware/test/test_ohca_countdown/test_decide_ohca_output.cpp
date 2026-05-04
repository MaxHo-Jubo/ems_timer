// EMS DoseSync Pro — Phase A Unit Test: decideOhcaOutput / advanceOhcaPhase
//
// 對應測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.1 / §3.3 phase 邊界
// 執行：pio test -e native -f test_ohca_countdown
//
// 涵蓋：
//   - 5 個 phase 的輸出（WAIT_FIRST_EPI / COUNTDOWN / WARNING / ALARMING / OVERTIME）
//   - phase 邊界（180000/180001、239999/240000、245000/245001）
//   - WARNING / OVERTIME 每 15s 短嗶的邊緣偵測
//   - millis() rollover 安全性
#include <unity.h>
#include <cstring>
#include "ems_ohca_countdown.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  §3.1.1 Phase = WAIT_FIRST_EPI
// ============================================================

/** WAIT_FIRST_EPI 任意 since → 全 false / 0 / nullptr */
static void test_wait_first_epi_all_idle() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WAIT_FIRST_EPI, 0, 0);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_FALSE(out.buzz_alarm_continuous);
    TEST_ASSERT_FALSE(out.led_yellow_slow);
    TEST_ASSERT_FALSE(out.led_red_fast);
    TEST_ASSERT_FALSE(out.led_red_slow);
    TEST_ASSERT_FALSE(out.vibrate);
    TEST_ASSERT_FALSE(out.screen_flash);
    TEST_ASSERT_NULL(out.display_label);
    TEST_ASSERT_EQUAL_UINT32(0, out.display_remaining_ms);
}

/** WAIT_FIRST_EPI 即使 since 很大也保持 idle */
static void test_wait_first_epi_ignores_since() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WAIT_FIRST_EPI, 0, 999999);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_NULL(out.display_label);
    TEST_ASSERT_EQUAL_UINT32(0, out.display_remaining_ms);
}

// ============================================================
//  §3.1.2 Phase = COUNTDOWN
// ============================================================

/** COUNTDOWN since=0 → display_remaining=240000，無蜂鳴 */
static void test_countdown_start_displays_full_240s() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_COUNTDOWN, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(240000, out.display_remaining_ms);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_FALSE(out.led_yellow_slow);
    TEST_ASSERT_NULL(out.display_label);
}

/** COUNTDOWN since=120000 → 剩 120000 */
static void test_countdown_mid() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_COUNTDOWN, 119000, 120000);
    TEST_ASSERT_EQUAL_UINT32(120000, out.display_remaining_ms);
    TEST_ASSERT_FALSE(out.buzz_short);
}

/** COUNTDOWN 邊界 since=180000 → 剩 60000，仍無蜂鳴（≤ 180000 屬 COUNTDOWN） */
static void test_countdown_boundary_180000() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_COUNTDOWN, 179000, 180000);
    TEST_ASSERT_EQUAL_UINT32(60000, out.display_remaining_ms);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_FALSE(out.led_yellow_slow);
}

// ============================================================
//  §3.1.3 Phase = WARNING
// ============================================================

/** WARNING since=180001 → 黃慢閃 + 首次 15s 嗶 + 顯示「請準備給藥」 */
static void test_warning_first_beep_at_180001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 180000, 180001);
    TEST_ASSERT_TRUE(out.buzz_short);
    TEST_ASSERT_TRUE(out.led_yellow_slow);
    TEST_ASSERT_NOT_NULL(out.display_label);
    TEST_ASSERT_EQUAL_STRING("請準備給藥", out.display_label);
    TEST_ASSERT_EQUAL_UINT32(59999, out.display_remaining_ms);
}

/** WARNING 兩次嗶之間 since=185000 → 不嗶但黃燈持續 */
static void test_warning_no_beep_between_markers() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 184000, 185000);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_TRUE(out.led_yellow_slow);
}

/** WARNING since=195001 → 第 2 次 15s 嗶 */
static void test_warning_second_beep_at_195001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 195000, 195001);
    TEST_ASSERT_TRUE(out.buzz_short);
}

/** WARNING since=210001 → 第 3 次 15s 嗶 */
static void test_warning_third_beep_at_210001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 210000, 210001);
    TEST_ASSERT_TRUE(out.buzz_short);
}

/** WARNING since=225001 → 第 4 次 15s 嗶 */
static void test_warning_fourth_beep_at_225001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 225000, 225001);
    TEST_ASSERT_TRUE(out.buzz_short);
}

/** WARNING 跨大區間（185000 → 200000）含一個 marker (195001) → 嗶一次 */
static void test_warning_large_jump_crosses_marker() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 185000, 200000);
    TEST_ASSERT_TRUE(out.buzz_short);
}

/** WARNING 邊界 since=239999 → 仍 WARNING 顯示，剩 1ms */
static void test_warning_boundary_239999() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_WARNING, 239998, 239999);
    TEST_ASSERT_EQUAL_UINT32(1, out.display_remaining_ms);
    TEST_ASSERT_TRUE(out.led_yellow_slow);
    TEST_ASSERT_FALSE(out.buzz_alarm_continuous);
}

// ============================================================
//  §3.1.4 Phase = ALARMING
// ============================================================

/** ALARMING since=240000 → 連續發報 + 紅快閃 + 震動 + 畫面閃 + 「請給藥」 */
static void test_alarming_full_alarm_at_240000() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_ALARMING, 239999, 240000);
    TEST_ASSERT_TRUE(out.buzz_alarm_continuous);
    TEST_ASSERT_TRUE(out.led_red_fast);
    TEST_ASSERT_TRUE(out.vibrate);
    TEST_ASSERT_TRUE(out.screen_flash);
    TEST_ASSERT_NOT_NULL(out.display_label);
    TEST_ASSERT_EQUAL_STRING("請給藥", out.display_label);
    TEST_ASSERT_EQUAL_UINT32(0, out.display_remaining_ms);
}

/** ALARMING 中 since=243000 → 警報持續，無短嗶 */
static void test_alarming_no_short_beep() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_ALARMING, 242000, 243000);
    TEST_ASSERT_TRUE(out.buzz_alarm_continuous);
    TEST_ASSERT_FALSE(out.buzz_short);
}

/** ALARMING 邊界 since=245000 → 仍 ALARMING（≤ 245000） */
static void test_alarming_boundary_245000() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_ALARMING, 244999, 245000);
    TEST_ASSERT_TRUE(out.buzz_alarm_continuous);
    TEST_ASSERT_TRUE(out.led_red_fast);
}

// ============================================================
//  §3.1.5 Phase = OVERTIME
// ============================================================

/** OVERTIME since=245001 → 紅慢閃 + 首次超時嗶 + 顯示累計時間（label=NULL） */
static void test_overtime_first_beep_at_245001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 245000, 245001);
    TEST_ASSERT_TRUE(out.buzz_short);
    TEST_ASSERT_TRUE(out.led_red_slow);
    TEST_ASSERT_NULL(out.display_label);             // 畫面顯示累計時間
    TEST_ASSERT_EQUAL_UINT32(245001, out.display_remaining_ms);
    TEST_ASSERT_FALSE(out.buzz_alarm_continuous);    // 已停止連發
}

/** OVERTIME since=260001 → 第 2 次超時嗶（每 15s） */
static void test_overtime_second_beep_at_260001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 260000, 260001);
    TEST_ASSERT_TRUE(out.buzz_short);
    TEST_ASSERT_EQUAL_UINT32(260001, out.display_remaining_ms);
}

/** OVERTIME 兩次嗶之間 since=250000 → 不嗶 */
static void test_overtime_no_beep_between_markers() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 249000, 250000);
    TEST_ASSERT_FALSE(out.buzz_short);
    TEST_ASSERT_TRUE(out.led_red_slow);
}

/** OVERTIME since=275001 → 第 3 次超時嗶 */
static void test_overtime_third_beep_at_275001() {
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 275000, 275001);
    TEST_ASSERT_TRUE(out.buzz_short);
}

/** OVERTIME 長時間（OVERTIME 入後 ~5.75 分鐘）仍每 15s 嗶（測試持續性） */
static void test_overtime_long_term_still_beeps() {
    // 第 24 個 marker：245001 + 24*15000 = 605001
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 605000, 605001);
    TEST_ASSERT_TRUE(out.buzz_short);
    // 兩 marker 之間 600000（marker 23 為 590001，marker 24 為 605001）→ 不嗶
    out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 599000, 600000);
    TEST_ASSERT_FALSE(out.buzz_short);
}

// ============================================================
//  §3.1.6 millis() rollover（uint32 wrap-around）
// ============================================================

/** 邊緣偵測在 uint32 rollover 下不誤觸（prev=UINT32_MAX, curr=0） */
static void test_edge_detection_handles_rollover() {
    // OVERTIME 中 prev 接近 UINT32_MAX，curr 已 wrap 到 0
    // 經 wrap 後 curr (0) < first_marker (245001) → 不觸發 buzz
    ohca_output_t out = decideOhcaOutput(OHCA_PHASE_OVERTIME, 0xFFFFFFFFu, 0);
    TEST_ASSERT_FALSE(out.buzz_short);
}

// ============================================================
//  advanceOhcaPhase（§3.3 phase 自動推進）
// ============================================================

/** WAIT_FIRST_EPI 不因計時自動推進 */
static void test_advance_wait_first_epi_stays() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_WAIT_FIRST_EPI,
        advanceOhcaPhase(OHCA_PHASE_WAIT_FIRST_EPI, 999999));
}

/** COUNTDOWN since=0 → COUNTDOWN */
static void test_advance_countdown_at_zero() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_COUNTDOWN,
        advanceOhcaPhase(OHCA_PHASE_COUNTDOWN, 0));
}

/** 邊界 since=180000 → COUNTDOWN（仍未轉 WARNING） */
static void test_advance_boundary_180000_still_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_COUNTDOWN,
        advanceOhcaPhase(OHCA_PHASE_COUNTDOWN, 180000));
}

/** 邊界 since=180001 → WARNING */
static void test_advance_boundary_180001_to_warning() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_WARNING,
        advanceOhcaPhase(OHCA_PHASE_COUNTDOWN, 180001));
}

/** 邊界 since=239999 → 仍 WARNING */
static void test_advance_boundary_239999_still_warning() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_WARNING,
        advanceOhcaPhase(OHCA_PHASE_WARNING, 239999));
}

/** 邊界 since=240000 → ALARMING */
static void test_advance_boundary_240000_to_alarming() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_ALARMING,
        advanceOhcaPhase(OHCA_PHASE_WARNING, 240000));
}

/** 邊界 since=245000 → 仍 ALARMING */
static void test_advance_boundary_245000_still_alarming() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_ALARMING,
        advanceOhcaPhase(OHCA_PHASE_ALARMING, 245000));
}

/** 邊界 since=245001 → OVERTIME */
static void test_advance_boundary_245001_to_overtime() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_OVERTIME,
        advanceOhcaPhase(OHCA_PHASE_ALARMING, 245001));
}

/** OVERTIME 持續 → 仍 OVERTIME */
static void test_advance_overtime_persists() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_OVERTIME,
        advanceOhcaPhase(OHCA_PHASE_OVERTIME, 999999));
}

/** advanceOhcaPhase 無記憶：since 回到小值仍依 since 推導（caller 重置責任） */
static void test_advance_back_to_countdown_when_since_resets() {
    // 即使從 OVERTIME 呼叫，若 since=0（caller 已重置）→ 回 COUNTDOWN
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_COUNTDOWN,
        advanceOhcaPhase(OHCA_PHASE_OVERTIME, 0));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // §3.1.1 WAIT_FIRST_EPI
    RUN_TEST(test_wait_first_epi_all_idle);
    RUN_TEST(test_wait_first_epi_ignores_since);

    // §3.1.2 COUNTDOWN
    RUN_TEST(test_countdown_start_displays_full_240s);
    RUN_TEST(test_countdown_mid);
    RUN_TEST(test_countdown_boundary_180000);

    // §3.1.3 WARNING
    RUN_TEST(test_warning_first_beep_at_180001);
    RUN_TEST(test_warning_no_beep_between_markers);
    RUN_TEST(test_warning_second_beep_at_195001);
    RUN_TEST(test_warning_third_beep_at_210001);
    RUN_TEST(test_warning_fourth_beep_at_225001);
    RUN_TEST(test_warning_large_jump_crosses_marker);
    RUN_TEST(test_warning_boundary_239999);

    // §3.1.4 ALARMING
    RUN_TEST(test_alarming_full_alarm_at_240000);
    RUN_TEST(test_alarming_no_short_beep);
    RUN_TEST(test_alarming_boundary_245000);

    // §3.1.5 OVERTIME
    RUN_TEST(test_overtime_first_beep_at_245001);
    RUN_TEST(test_overtime_second_beep_at_260001);
    RUN_TEST(test_overtime_no_beep_between_markers);
    RUN_TEST(test_overtime_third_beep_at_275001);
    RUN_TEST(test_overtime_long_term_still_beeps);

    // §3.1.6 rollover
    RUN_TEST(test_edge_detection_handles_rollover);

    // advanceOhcaPhase（phase 自動推進邊界）
    RUN_TEST(test_advance_wait_first_epi_stays);
    RUN_TEST(test_advance_countdown_at_zero);
    RUN_TEST(test_advance_boundary_180000_still_countdown);
    RUN_TEST(test_advance_boundary_180001_to_warning);
    RUN_TEST(test_advance_boundary_239999_still_warning);
    RUN_TEST(test_advance_boundary_240000_to_alarming);
    RUN_TEST(test_advance_boundary_245000_still_alarming);
    RUN_TEST(test_advance_boundary_245001_to_overtime);
    RUN_TEST(test_advance_overtime_persists);
    RUN_TEST(test_advance_back_to_countdown_when_since_resets);

    return UNITY_END();
}
