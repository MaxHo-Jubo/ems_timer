// EMS Timer — L2 regression: DisplaySnapshot 欄位/旗標覆蓋
//
// 動機：Phase E 歷史頁面按 UP/DOWN 無重繪 bug 的根因是 DisplaySnapshot 漏掉
// historyCursor 欄位 → memcmp 看不到變化 → updateDisplay 早 return。
// 這層 native test 在新增 visible state 忘記同步到 snapshot 時必須擋下來。
//
// 對應規格：
//   - lib/ems_display_snapshot/ems_display_snapshot.h
//   - tasks/testing-infrastructure.md #2
//   - docs/EMS_DoseSync_Pro_Test_Plan_V1.md §1 測試金字塔 L2
//
// 覆蓋：
//   - baseline：全相同 input → snapshotsEqual = true
//   - 每個 1:1 欄位獨立改變 → snapshotsEqual = false
//   - 每個 bool flag 對應唯一 bit mask
//   - Phase E 回溯：historyCursor / historyScrollOffset 改變 → 觸發重繪
#include <unity.h>
#include "ems_display_snapshot.h"

void setUp()    {}
void tearDown() {}

// ============================================================
//  Group 1: baseline — 全相同 input 必相等
// ============================================================

static void test_baseline_identical_inputs_produce_equal_snapshots() {
    DisplaySnapshotInputs in;  // 全預設值
    DisplaySnapshot a = captureSnapshot(in);
    DisplaySnapshot b = captureSnapshot(in);
    TEST_ASSERT_TRUE(snapshotsEqual(a, b));
}

static void test_default_snapshot_flags_are_zero() {
    DisplaySnapshotInputs in;
    DisplaySnapshot s = captureSnapshot(in);
    TEST_ASSERT_EQUAL_UINT16(0, s.flags);
}

// ============================================================
//  Group 2: 每個 1:1 欄位變化 → snapshot 必跟著變
//  （regression：Phase E history UI bug 的類別 — 漏掉欄位 → 重繪不觸發）
// ============================================================

#define ASSERT_FIELD_TRIGGERS_CHANGE(field, value)                  \
    do {                                                            \
        DisplaySnapshotInputs base;                                 \
        DisplaySnapshotInputs mut = base;                           \
        mut.field = (value);                                        \
        DisplaySnapshot a = captureSnapshot(base);                  \
        DisplaySnapshot b = captureSnapshot(mut);                   \
        TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(a, b),             \
            "snapshot 未反映 input." #field " 變化");                \
    } while (0)

static void test_global_state_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(globalState, 3);
}

static void test_ohca_state_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(ohcaState, 5);
}

static void test_ohca_sub_state_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(ohcaSubState, 2);
}

static void test_sync_state_change_triggers_redraw() {
    // Phase F MVP2：sync 狀態機 6 個 state 切換必須觸發重繪
    ASSERT_FIELD_TRIGGERS_CHANGE(syncState, 3);
}

static void test_main_menu_cursor_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(mainMenuCursor, 1);
}

static void test_backfill_cursor_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(backfillCursor, 4);
}

static void test_vent_volume_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(ventVolume, 7);
}

static void test_vent_paused_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(ventPaused, true);
}

static void test_countdown_sec_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(countdownSec, 42);
}

static void test_vent_beat_change_triggers_redraw() {
    ASSERT_FIELD_TRIGGERS_CHANGE(ventBeat, 3);
}

// Phase E 回溯：history UI bug 真正類別
static void test_history_cursor_change_triggers_redraw_phase_e_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(historyCursor, 5);
}

static void test_history_scroll_offset_change_triggers_redraw_phase_e_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(historyScrollOffset, 10);
}

// Phase F MVP2-Followup 回溯：OHCA 案件總覽 sub-menu cursor 移動 bug 同類別
static void test_summary_submenu_cursor_change_triggers_redraw_phase_f_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(summarySubmenuCursor, 1);
}

// B4 回溯：OHCA_END_CHECK cursor 移動 bug 同類別（漏 endCheckCursor 導致第一次 UP 無重繪）
static void test_end_check_cursor_change_triggers_redraw_b4_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(endCheckCursor, 1);
}

// W3 回溯：Training Setup cursor 移動 bug 同類別（漏 trainingSetupCursor 導致第一次 UP 無重繪）
static void test_training_setup_cursor_change_triggers_redraw_w3_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(trainingSetupCursor, 1);
}

// W6 回溯：歷史分類層 cursor 移動 bug 同類別（漏 historyTypeCursor 導致無重繪）
static void test_history_type_cursor_change_triggers_redraw_w6_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(historyTypeCursor, 1);
}

// W7 回溯：Training 歷史操作選單 cursor 移動 bug 同類別
static void test_training_history_options_cursor_change_triggers_redraw_w7_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(trainingHistoryOptionsCursor, 1);
}

// W5 回溯：Training 保存/不保存 cursor 移動 bug 同類別（漏 trainingSaveCursor 導致保存畫面高亮凍結）
static void test_training_save_cursor_change_triggers_redraw_w5_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(trainingSaveCursor, 1);
}

// ============================================================
//  Group 3: 每個 bool flag → 唯一 bit mask 對應
//  （regression：新增 flag 但忘記分配 bit 或 bit 撞號）
// ============================================================

static void test_flag_epi_armed_sets_bit_0x0001() {
    DisplaySnapshotInputs in;
    in.showEpiArmedPrompt = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_EPI_ARMED, captureSnapshot(in).flags);
}

static void test_flag_shock_armed_sets_bit_0x0002() {
    DisplaySnapshotInputs in;
    in.showShockArmedPrompt = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_SHOCK_ARMED, captureSnapshot(in).flags);
}

static void test_flag_amio_armed_sets_bit_0x0004() {
    DisplaySnapshotInputs in;
    in.showAmioArmedPrompt = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_AMIO_ARMED, captureSnapshot(in).flags);
}

static void test_flag_ohca_vent_sets_bit_0x0008() {
    DisplaySnapshotInputs in;
    in.ohcaVentOverlayEnabled = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_OHCA_VENT, captureSnapshot(in).flags);
}

static void test_flag_vent_end_check_sets_bit_0x0010() {
    DisplaySnapshotInputs in;
    in.ventEndCheckShown = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_VENT_END_CHECK, captureSnapshot(in).flags);
}

static void test_flag_alarm_muted_sets_bit_0x0020() {
    DisplaySnapshotInputs in;
    in.alarmMuted = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_ALARM_MUTED, captureSnapshot(in).flags);
}

static void test_flag_vent_back_hint_sets_bit_0x0040() {
    DisplaySnapshotInputs in;
    in.ventBackHintShown = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_VENT_BACK_HINT, captureSnapshot(in).flags);
}

static void test_flag_alarming_flash_sets_bit_0x0080() {
    DisplaySnapshotInputs in;
    in.alarmingFlashOn = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_ALARMING_FLASH, captureSnapshot(in).flags);
}

static void test_flag_end_confirm_sets_bit_0x0100() {
    DisplaySnapshotInputs in;
    in.endConfirmShown = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_END_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_flash_active_sets_bit_0x0200() {
    DisplaySnapshotInputs in;
    in.flashStateActive = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_FLASH_ACTIVE, captureSnapshot(in).flags);
}

static void test_flag_vent_pre_sets_bit_0x0400() {
    DisplaySnapshotInputs in;
    in.ventPreShown = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_VENT_PRE, captureSnapshot(in).flags);
}

static void test_flag_history_summary_sets_bit_0x0800() {
    DisplaySnapshotInputs in;
    in.historySummaryMode = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_HISTORY_SUMMARY, captureSnapshot(in).flags);
}

static void test_flag_ble_connected_sets_bit_0x1000() {
    DisplaySnapshotInputs in;
    in.bleConnected = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_BLE_CONNECTED, captureSnapshot(in).flags);
}

static void test_flag_resync_confirm_sets_bit_0x2000() {
    DisplaySnapshotInputs in;
    in.resyncConfirmShown = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_RESYNC_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_delete_confirm_sets_bit_0x4000() {
    DisplaySnapshotInputs in;
    in.trainingDeleteConfirm = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_DELETE_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_reset_confirm_sets_bit_0x8000() {
    DisplaySnapshotInputs in;
    in.trainingResetConfirm = true;
    TEST_ASSERT_EQUAL_UINT16(SNAP_FLAG_RESET_CONFIRM, captureSnapshot(in).flags);
}

// ============================================================
//  Group 4: 所有 flag 同時開 → 16 個 bit OR 起來
// ============================================================

static void test_all_flags_on_combine_all_bits() {
    DisplaySnapshotInputs in;
    in.showEpiArmedPrompt    = true;
    in.showShockArmedPrompt  = true;
    in.showAmioArmedPrompt   = true;
    in.ohcaVentOverlayEnabled = true;
    in.ventEndCheckShown     = true;
    in.alarmMuted            = true;
    in.ventBackHintShown     = true;
    in.alarmingFlashOn       = true;
    in.endConfirmShown       = true;
    in.flashStateActive      = true;
    in.ventPreShown          = true;
    in.historySummaryMode    = true;
    in.bleConnected          = true;
    in.resyncConfirmShown    = true;
    in.trainingDeleteConfirm = true;
    in.trainingResetConfirm  = true;

    const uint16_t expected = SNAP_FLAG_EPI_ARMED
                            | SNAP_FLAG_SHOCK_ARMED
                            | SNAP_FLAG_AMIO_ARMED
                            | SNAP_FLAG_OHCA_VENT
                            | SNAP_FLAG_VENT_END_CHECK
                            | SNAP_FLAG_ALARM_MUTED
                            | SNAP_FLAG_VENT_BACK_HINT
                            | SNAP_FLAG_ALARMING_FLASH
                            | SNAP_FLAG_END_CONFIRM
                            | SNAP_FLAG_FLASH_ACTIVE
                            | SNAP_FLAG_VENT_PRE
                            | SNAP_FLAG_HISTORY_SUMMARY
                            | SNAP_FLAG_BLE_CONNECTED
                            | SNAP_FLAG_RESYNC_CONFIRM
                            | SNAP_FLAG_DELETE_CONFIRM
                            | SNAP_FLAG_RESET_CONFIRM;
    TEST_ASSERT_EQUAL_UINT16(expected, captureSnapshot(in).flags);
}

static void test_all_flags_bit_masks_are_unique() {
    // 確保 16 個 mask 沒有撞號（OR 全部應等於 set bit count = 16；uint16_t 已用滿）
    const uint16_t all = SNAP_FLAG_EPI_ARMED | SNAP_FLAG_SHOCK_ARMED
                       | SNAP_FLAG_AMIO_ARMED | SNAP_FLAG_OHCA_VENT
                       | SNAP_FLAG_VENT_END_CHECK | SNAP_FLAG_ALARM_MUTED
                       | SNAP_FLAG_VENT_BACK_HINT | SNAP_FLAG_ALARMING_FLASH
                       | SNAP_FLAG_END_CONFIRM | SNAP_FLAG_FLASH_ACTIVE
                       | SNAP_FLAG_VENT_PRE | SNAP_FLAG_HISTORY_SUMMARY
                       | SNAP_FLAG_BLE_CONNECTED | SNAP_FLAG_RESYNC_CONFIRM
                       | SNAP_FLAG_DELETE_CONFIRM | SNAP_FLAG_RESET_CONFIRM;
    // popcount
    int bits = 0;
    for (uint16_t m = all; m; m >>= 1) {
        if (m & 1) {
            bits++;
        }
    }
    TEST_ASSERT_EQUAL_INT(16, bits);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // Group 1: baseline
    RUN_TEST(test_baseline_identical_inputs_produce_equal_snapshots);
    RUN_TEST(test_default_snapshot_flags_are_zero);

    // Group 2: 1:1 field mapping
    RUN_TEST(test_global_state_change_triggers_redraw);
    RUN_TEST(test_ohca_state_change_triggers_redraw);
    RUN_TEST(test_ohca_sub_state_change_triggers_redraw);
    RUN_TEST(test_sync_state_change_triggers_redraw);
    RUN_TEST(test_main_menu_cursor_change_triggers_redraw);
    RUN_TEST(test_backfill_cursor_change_triggers_redraw);
    RUN_TEST(test_vent_volume_change_triggers_redraw);
    RUN_TEST(test_vent_paused_change_triggers_redraw);
    RUN_TEST(test_countdown_sec_change_triggers_redraw);
    RUN_TEST(test_vent_beat_change_triggers_redraw);
    RUN_TEST(test_history_cursor_change_triggers_redraw_phase_e_regression);
    RUN_TEST(test_history_scroll_offset_change_triggers_redraw_phase_e_regression);
    RUN_TEST(test_summary_submenu_cursor_change_triggers_redraw_phase_f_regression);
    RUN_TEST(test_end_check_cursor_change_triggers_redraw_b4_regression);

    // W3 回溯：Training Setup cursor 移動 bug 同類別
    RUN_TEST(test_training_setup_cursor_change_triggers_redraw_w3_regression);
    // W5/W6/W7 回溯：Training 保存/歷史分類/歷史操作 cursor 同類別
    RUN_TEST(test_history_type_cursor_change_triggers_redraw_w6_regression);
    RUN_TEST(test_training_history_options_cursor_change_triggers_redraw_w7_regression);
    RUN_TEST(test_training_save_cursor_change_triggers_redraw_w5_regression);

    // Group 3: bool flag → bit mask
    RUN_TEST(test_flag_epi_armed_sets_bit_0x0001);
    RUN_TEST(test_flag_shock_armed_sets_bit_0x0002);
    RUN_TEST(test_flag_amio_armed_sets_bit_0x0004);
    RUN_TEST(test_flag_ohca_vent_sets_bit_0x0008);
    RUN_TEST(test_flag_vent_end_check_sets_bit_0x0010);
    RUN_TEST(test_flag_alarm_muted_sets_bit_0x0020);
    RUN_TEST(test_flag_vent_back_hint_sets_bit_0x0040);
    RUN_TEST(test_flag_alarming_flash_sets_bit_0x0080);
    RUN_TEST(test_flag_end_confirm_sets_bit_0x0100);
    RUN_TEST(test_flag_flash_active_sets_bit_0x0200);
    RUN_TEST(test_flag_vent_pre_sets_bit_0x0400);
    RUN_TEST(test_flag_history_summary_sets_bit_0x0800);
    RUN_TEST(test_flag_ble_connected_sets_bit_0x1000);
    RUN_TEST(test_flag_resync_confirm_sets_bit_0x2000);
    RUN_TEST(test_flag_delete_confirm_sets_bit_0x4000);
    RUN_TEST(test_flag_reset_confirm_sets_bit_0x8000);

    // Group 4: combine + uniqueness
    RUN_TEST(test_all_flags_on_combine_all_bits);
    RUN_TEST(test_all_flags_bit_masks_are_unique);

    return UNITY_END();
}
