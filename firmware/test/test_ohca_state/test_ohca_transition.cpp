// EMS DoseSync Pro — Phase A Unit Test: nextOhcaState（OHCA 子狀態機）
//
// 對應測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.3
// 執行：pio test -e native -f test_ohca_state
//
// 涵蓋：
//   - 正常流程：MAIN_MENU → START_FLASH → WAIT_FIRST_EPI → COUNTDOWN → ... → LOCKED → SUMMARY
//   - 啟動倒數白名單（§23.2）：僅 EPI_CONFIRMED 可重啟倒數
//   - LOCKED 拒絕（除 TO_SUMMARY）
//   - ALARMING / OVERTIME 主鍵短按 noop
//   - END_CHECK confirm / cancel
//   - TIMER_TICK delegate 至 advanceOhcaPhase
#include <unity.h>
#include "ems_ohca_state.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  正常流程
// ============================================================

/** MAIN_MENU + 主鍵短按 → START_FLASH */
static void test_main_menu_short_press_to_start_flash() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_START_FLASH,
        nextOhcaState(OHCA_STATE_MAIN_MENU, OHCA_EVT_MAIN_BTN_SHORT, 0));
}

/** START_FLASH + FLASH_TIMEOUT → WAIT_FIRST_EPI */
static void test_start_flash_timeout_to_wait_first_epi() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WAIT_FIRST_EPI,
        nextOhcaState(OHCA_STATE_START_FLASH, OHCA_EVT_FLASH_TIMEOUT, 0));
}

/** WAIT_FIRST_EPI + EPI_CONFIRMED → COUNTDOWN */
static void test_wait_first_epi_confirmed_to_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_WAIT_FIRST_EPI, OHCA_EVT_EPI_CONFIRMED, 0));
}

/** COUNTDOWN + TIMER_TICK (since=180001) → WARNING */
static void test_countdown_tick_180001_to_warning() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WARNING,
        nextOhcaState(OHCA_STATE_COUNTDOWN, OHCA_EVT_TIMER_TICK, 180001));
}

/** WARNING + TIMER_TICK (since=240000) → ALARMING */
static void test_warning_tick_240000_to_alarming() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,
        nextOhcaState(OHCA_STATE_WARNING, OHCA_EVT_TIMER_TICK, 240000));
}

/** ALARMING + TIMER_TICK (since=245001) → OVERTIME */
static void test_alarming_tick_245001_to_overtime() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_TIMER_TICK, 245001));
}

/** OVERTIME + 主鍵長按 3s → END_CHECK */
static void test_overtime_long_press_to_end_check() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_END_CHECK,
        nextOhcaState(OHCA_STATE_OVERTIME, OHCA_EVT_MAIN_BTN_LONG_3S, 300000));
}

/** END_CHECK + END_CONFIRM → LOCKED */
static void test_end_check_confirm_to_locked() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_END_CHECK, OHCA_EVT_END_CONFIRM, 300000));
}

/** LOCKED + TO_SUMMARY → SUMMARY */
static void test_locked_to_summary() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_SUMMARY,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_TO_SUMMARY, 0));
}

// ============================================================
//  EPI_CONFIRMED 重啟倒數（§23.2 唯一可重啟路徑）
// ============================================================

/** COUNTDOWN + EPI_CONFIRMED → COUNTDOWN（caller 自行 reset since） */
static void test_countdown_epi_confirmed_resets() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_COUNTDOWN, OHCA_EVT_EPI_CONFIRMED, 100000));
}

/** WARNING + EPI_CONFIRMED → COUNTDOWN */
static void test_warning_epi_confirmed_resets_to_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_WARNING, OHCA_EVT_EPI_CONFIRMED, 200000));
}

/** ALARMING + EPI_CONFIRMED → COUNTDOWN（重啟） */
static void test_alarming_epi_confirmed_resets_to_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_EPI_CONFIRMED, 243000));
}

/** OVERTIME + EPI_CONFIRMED → COUNTDOWN（第二次 EPI 重啟） */
static void test_overtime_epi_confirmed_resets_to_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_OVERTIME, OHCA_EVT_EPI_CONFIRMED, 300000));
}

/** END_CHECK 中 EPI_CONFIRMED 仍可重啟倒數（不應被 END_CHECK 卡住） */
static void test_end_check_epi_confirmed_resets() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_END_CHECK, OHCA_EVT_EPI_CONFIRMED, 300000));
}

// ============================================================
//  §23.2 啟動倒數白名單：電擊 / Amio 不重啟倒數
// ============================================================

/** WAIT_FIRST_EPI + SHOCK_CONFIRMED → 不轉 COUNTDOWN */
static void test_wait_first_epi_shock_does_not_start_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WAIT_FIRST_EPI,
        nextOhcaState(OHCA_STATE_WAIT_FIRST_EPI, OHCA_EVT_SHOCK_CONFIRMED, 0));
}

/** WAIT_FIRST_EPI + AMIO_CONFIRMED → 不轉 COUNTDOWN */
static void test_wait_first_epi_amio_does_not_start_countdown() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WAIT_FIRST_EPI,
        nextOhcaState(OHCA_STATE_WAIT_FIRST_EPI, OHCA_EVT_AMIO_CONFIRMED, 0));
}

/** COUNTDOWN + SHOCK_CONFIRMED → COUNTDOWN（state 不變，不重啟倒數） */
static void test_countdown_shock_does_not_reset() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_COUNTDOWN, OHCA_EVT_SHOCK_CONFIRMED, 100000));
}

/** WARNING + AMIO_CONFIRMED → WARNING（不重啟） */
static void test_warning_amio_does_not_reset() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WARNING,
        nextOhcaState(OHCA_STATE_WARNING, OHCA_EVT_AMIO_CONFIRMED, 200000));
}

/** ALARMING + AMIO_CONFIRMED → ALARMING（Amio 確認成立但 ALARMING 持續） */
static void test_alarming_amio_keeps_alarming() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_AMIO_CONFIRMED, 243000));
}

/** OVERTIME + SHOCK_CONFIRMED → OVERTIME（電擊建紀錄但不重啟倒數） */
static void test_overtime_shock_keeps_overtime() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,
        nextOhcaState(OHCA_STATE_OVERTIME, OHCA_EVT_SHOCK_CONFIRMED, 300000));
}

// ============================================================
//  ALARMING / OVERTIME 主鍵短按 noop（§6.7 消音）
// ============================================================

/** ALARMING + MAIN_BTN_SHORT → ALARMING（state 不轉；消音由外部處理） */
static void test_alarming_main_short_press_noop() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_MAIN_BTN_SHORT, 243000));
}

/** OVERTIME + MAIN_BTN_SHORT → OVERTIME（不建立紀錄、不重啟倒數） */
static void test_overtime_main_short_press_noop() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,
        nextOhcaState(OHCA_STATE_OVERTIME, OHCA_EVT_MAIN_BTN_SHORT, 300000));
}

// ============================================================
//  LOCKED 拒絕所有事件（除 TO_SUMMARY）
// ============================================================

/** LOCKED + EPI_CONFIRMED → LOCKED（拒絕） */
static void test_locked_rejects_epi() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_EPI_CONFIRMED, 0));
}

/** LOCKED + SHOCK_CONFIRMED → LOCKED */
static void test_locked_rejects_shock() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_SHOCK_CONFIRMED, 0));
}

/** LOCKED + AMIO_CONFIRMED → LOCKED */
static void test_locked_rejects_amio() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_AMIO_CONFIRMED, 0));
}

/** LOCKED + MAIN_BTN_SHORT → LOCKED */
static void test_locked_rejects_main_short() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_MAIN_BTN_SHORT, 0));
}

/** LOCKED + MAIN_BTN_LONG_3S → LOCKED */
static void test_locked_rejects_main_long() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_MAIN_BTN_LONG_3S, 0));
}

/** LOCKED + TIMER_TICK → LOCKED（不再推進） */
static void test_locked_rejects_timer_tick() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_LOCKED,
        nextOhcaState(OHCA_STATE_LOCKED, OHCA_EVT_TIMER_TICK, 999999));
}

/** SUMMARY 為終端狀態 */
static void test_summary_is_terminal() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_SUMMARY,
        nextOhcaState(OHCA_STATE_SUMMARY, OHCA_EVT_MAIN_BTN_SHORT, 0));
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_SUMMARY,
        nextOhcaState(OHCA_STATE_SUMMARY, OHCA_EVT_EPI_CONFIRMED, 0));
}

// ============================================================
//  END_CHECK 行為
// ============================================================

/** END_CHECK + END_CANCEL → OVERTIME（V1 簡化路徑） */
static void test_end_check_cancel_returns_to_overtime() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,
        nextOhcaState(OHCA_STATE_END_CHECK, OHCA_EVT_END_CANCEL, 300000));
}

/** END_CHECK 中其他無關事件 → 不變 */
static void test_end_check_ignores_unrelated_events() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_END_CHECK,
        nextOhcaState(OHCA_STATE_END_CHECK, OHCA_EVT_TIMER_TICK, 999999));
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_END_CHECK,
        nextOhcaState(OHCA_STATE_END_CHECK, OHCA_EVT_MAIN_BTN_SHORT, 0));
}

// ============================================================
//  END_CHECK 限制：僅 OVERTIME 長按 3s 可進入
// ============================================================

/** COUNTDOWN + 主鍵長按 3s → 不轉 END_CHECK */
static void test_countdown_long_press_does_not_enter_end_check() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_COUNTDOWN, OHCA_EVT_MAIN_BTN_LONG_3S, 100000));
}

/** WARNING + 主鍵長按 3s → 不轉 END_CHECK */
static void test_warning_long_press_does_not_enter_end_check() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WARNING,
        nextOhcaState(OHCA_STATE_WARNING, OHCA_EVT_MAIN_BTN_LONG_3S, 200000));
}

/** ALARMING + 主鍵長按 3s → 不轉 END_CHECK（必須先進 OVERTIME） */
static void test_alarming_long_press_does_not_enter_end_check() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_MAIN_BTN_LONG_3S, 243000));
}

// ============================================================
//  TIMER_TICK 推進邏輯
// ============================================================

/** WAIT_FIRST_EPI + TIMER_TICK → 不推進（不轉 COUNTDOWN） */
static void test_wait_first_epi_timer_tick_does_not_advance() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WAIT_FIRST_EPI,
        nextOhcaState(OHCA_STATE_WAIT_FIRST_EPI, OHCA_EVT_TIMER_TICK, 999999));
}

/** COUNTDOWN + TIMER_TICK (since=180000) → 仍 COUNTDOWN（≤ 180000 邊界） */
static void test_countdown_tick_at_boundary_180000() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN,
        nextOhcaState(OHCA_STATE_COUNTDOWN, OHCA_EVT_TIMER_TICK, 180000));
}

/** WARNING + TIMER_TICK (since=239999) → 仍 WARNING */
static void test_warning_tick_at_boundary_239999() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WARNING,
        nextOhcaState(OHCA_STATE_WARNING, OHCA_EVT_TIMER_TICK, 239999));
}

/** ALARMING + TIMER_TICK (since=245000) → 仍 ALARMING */
static void test_alarming_tick_at_boundary_245000() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,
        nextOhcaState(OHCA_STATE_ALARMING, OHCA_EVT_TIMER_TICK, 245000));
}

/** OVERTIME + TIMER_TICK 任意 since → OVERTIME */
static void test_overtime_tick_persists() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,
        nextOhcaState(OHCA_STATE_OVERTIME, OHCA_EVT_TIMER_TICK, 999999));
}

// ============================================================
//  Mapper 測試
// ============================================================

/** mapStateToPhase 對倒數 group 正確映射 */
static void test_map_state_to_phase_countdown_group() {
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_COUNTDOWN, mapStateToPhase(OHCA_STATE_COUNTDOWN));
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_WARNING,   mapStateToPhase(OHCA_STATE_WARNING));
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_ALARMING,  mapStateToPhase(OHCA_STATE_ALARMING));
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_OVERTIME,  mapStateToPhase(OHCA_STATE_OVERTIME));
    TEST_ASSERT_EQUAL_INT(OHCA_PHASE_WAIT_FIRST_EPI, mapStateToPhase(OHCA_STATE_WAIT_FIRST_EPI));
}

/** mapPhaseToState 反向映射 */
static void test_map_phase_to_state() {
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_COUNTDOWN, mapPhaseToState(OHCA_PHASE_COUNTDOWN));
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_WARNING,   mapPhaseToState(OHCA_PHASE_WARNING));
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_ALARMING,  mapPhaseToState(OHCA_PHASE_ALARMING));
    TEST_ASSERT_EQUAL_INT(OHCA_STATE_OVERTIME,  mapPhaseToState(OHCA_PHASE_OVERTIME));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // 正常流程
    RUN_TEST(test_main_menu_short_press_to_start_flash);
    RUN_TEST(test_start_flash_timeout_to_wait_first_epi);
    RUN_TEST(test_wait_first_epi_confirmed_to_countdown);
    RUN_TEST(test_countdown_tick_180001_to_warning);
    RUN_TEST(test_warning_tick_240000_to_alarming);
    RUN_TEST(test_alarming_tick_245001_to_overtime);
    RUN_TEST(test_overtime_long_press_to_end_check);
    RUN_TEST(test_end_check_confirm_to_locked);
    RUN_TEST(test_locked_to_summary);

    // EPI_CONFIRMED 重啟倒數
    RUN_TEST(test_countdown_epi_confirmed_resets);
    RUN_TEST(test_warning_epi_confirmed_resets_to_countdown);
    RUN_TEST(test_alarming_epi_confirmed_resets_to_countdown);
    RUN_TEST(test_overtime_epi_confirmed_resets_to_countdown);
    RUN_TEST(test_end_check_epi_confirmed_resets);

    // §23.2 啟動倒數白名單
    RUN_TEST(test_wait_first_epi_shock_does_not_start_countdown);
    RUN_TEST(test_wait_first_epi_amio_does_not_start_countdown);
    RUN_TEST(test_countdown_shock_does_not_reset);
    RUN_TEST(test_warning_amio_does_not_reset);
    RUN_TEST(test_alarming_amio_keeps_alarming);
    RUN_TEST(test_overtime_shock_keeps_overtime);

    // §6.7 ALARMING / OVERTIME 主鍵短按 noop
    RUN_TEST(test_alarming_main_short_press_noop);
    RUN_TEST(test_overtime_main_short_press_noop);

    // LOCKED 拒絕
    RUN_TEST(test_locked_rejects_epi);
    RUN_TEST(test_locked_rejects_shock);
    RUN_TEST(test_locked_rejects_amio);
    RUN_TEST(test_locked_rejects_main_short);
    RUN_TEST(test_locked_rejects_main_long);
    RUN_TEST(test_locked_rejects_timer_tick);
    RUN_TEST(test_summary_is_terminal);

    // END_CHECK
    RUN_TEST(test_end_check_cancel_returns_to_overtime);
    RUN_TEST(test_end_check_ignores_unrelated_events);
    RUN_TEST(test_countdown_long_press_does_not_enter_end_check);
    RUN_TEST(test_warning_long_press_does_not_enter_end_check);
    RUN_TEST(test_alarming_long_press_does_not_enter_end_check);

    // TIMER_TICK 推進
    RUN_TEST(test_wait_first_epi_timer_tick_does_not_advance);
    RUN_TEST(test_countdown_tick_at_boundary_180000);
    RUN_TEST(test_warning_tick_at_boundary_239999);
    RUN_TEST(test_alarming_tick_at_boundary_245000);
    RUN_TEST(test_overtime_tick_persists);

    // Mapper
    RUN_TEST(test_map_state_to_phase_countdown_group);
    RUN_TEST(test_map_phase_to_state);

    return UNITY_END();
}
