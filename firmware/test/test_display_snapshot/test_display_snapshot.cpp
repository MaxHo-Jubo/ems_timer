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
    TEST_ASSERT_EQUAL_UINT32(0, s.flags);
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

// Phase G 回溯：系統設定選單 cursor 移動無重繪（本類別第 4 次重演，見
// docs/phase-g-system-settings-plan.md §二、零、一 #3）
static void test_settings_cursor_change_triggers_redraw_phase_g_regression() {
    ASSERT_FIELD_TRIGGERS_CHANGE(settingsCursor, 2);
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
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_EPI_ARMED, captureSnapshot(in).flags);
}

static void test_flag_shock_armed_sets_bit_0x0002() {
    DisplaySnapshotInputs in;
    in.showShockArmedPrompt = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_SHOCK_ARMED, captureSnapshot(in).flags);
}

static void test_flag_amio_armed_sets_bit_0x0004() {
    DisplaySnapshotInputs in;
    in.showAmioArmedPrompt = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_AMIO_ARMED, captureSnapshot(in).flags);
}

static void test_flag_ohca_vent_sets_bit_0x0008() {
    DisplaySnapshotInputs in;
    in.ohcaVentOverlayEnabled = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_OHCA_VENT, captureSnapshot(in).flags);
}

static void test_flag_vent_end_check_sets_bit_0x0010() {
    DisplaySnapshotInputs in;
    in.ventEndCheckShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_VENT_END_CHECK, captureSnapshot(in).flags);
}

static void test_flag_alarm_muted_sets_bit_0x0020() {
    DisplaySnapshotInputs in;
    in.alarmMuted = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_ALARM_MUTED, captureSnapshot(in).flags);
}

static void test_flag_vent_back_hint_sets_bit_0x0040() {
    DisplaySnapshotInputs in;
    in.ventBackHintShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_VENT_BACK_HINT, captureSnapshot(in).flags);
}

static void test_flag_alarming_flash_sets_bit_0x0080() {
    DisplaySnapshotInputs in;
    in.alarmingFlashOn = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_ALARMING_FLASH, captureSnapshot(in).flags);
}

static void test_flag_end_confirm_sets_bit_0x0100() {
    DisplaySnapshotInputs in;
    in.endConfirmShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_END_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_flash_active_sets_bit_0x0200() {
    DisplaySnapshotInputs in;
    in.flashStateActive = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_FLASH_ACTIVE, captureSnapshot(in).flags);
}

static void test_flag_vent_pre_sets_bit_0x0400() {
    DisplaySnapshotInputs in;
    in.ventPreShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_VENT_PRE, captureSnapshot(in).flags);
}

static void test_flag_history_summary_sets_bit_0x0800() {
    DisplaySnapshotInputs in;
    in.historySummaryMode = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_HISTORY_SUMMARY, captureSnapshot(in).flags);
}

static void test_flag_ble_connected_sets_bit_0x1000() {
    DisplaySnapshotInputs in;
    in.bleConnected = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_BLE_CONNECTED, captureSnapshot(in).flags);
}

static void test_flag_resync_confirm_sets_bit_0x2000() {
    DisplaySnapshotInputs in;
    in.resyncConfirmShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_RESYNC_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_delete_confirm_sets_bit_0x4000() {
    DisplaySnapshotInputs in;
    in.trainingDeleteConfirm = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_DELETE_CONFIRM, captureSnapshot(in).flags);
}

static void test_flag_reset_confirm_sets_bit_0x8000() {
    DisplaySnapshotInputs in;
    in.trainingResetConfirm = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_RESET_CONFIRM, captureSnapshot(in).flags);
}

// Phase G：uint16_t 的 16 個 bit 於 W8 用罄，flags 擴為 uint32_t 後的前兩個新 bit
static void test_flag_settings_editor_sets_bit_0x10000() {
    DisplaySnapshotInputs in;
    in.settingsEditorMode = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_SETTINGS_EDITOR, captureSnapshot(in).flags);
}

static void test_flag_settings_restore_confirm_sets_bit_0x20000() {
    DisplaySnapshotInputs in;
    in.settingsRestoreConfirm = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_SETTINGS_RESTORE_CONFIRM, captureSnapshot(in).flags);
}

// Phase H Task 13：電池資訊子畫面顯示中的 flag bit
static void test_flag_settings_battery_info_sets_bit_0x200000() {
    // 只開 settingsBatteryInfo 這一個旗標的測試輸入，其餘全預設 false
    DisplaySnapshotInputs in;
    in.settingsBatteryInfo = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_SETTINGS_BATTERY_INFO, captureSnapshot(in).flags);
}

// ============================================================
//  Group 4: 所有 flag 同時開 → 22 個 bit OR 起來
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
    in.settingsEditorMode     = true;
    in.settingsRestoreConfirm = true;
    in.settingsBatteryInfo    = true;
    in.batteryLowBlinkOn      = true;
    in.lowBatteryNoticeVisible = true;
    in.lowBatteryStartConfirmShown = true;

    const uint32_t expected = SNAP_FLAG_EPI_ARMED
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
                            | SNAP_FLAG_RESET_CONFIRM
                            | SNAP_FLAG_SETTINGS_EDITOR
                            | SNAP_FLAG_SETTINGS_RESTORE_CONFIRM
                            | SNAP_FLAG_SETTINGS_BATTERY_INFO
                            | SNAP_FLAG_BATTERY_LOW_BLINK
                            | SNAP_FLAG_LOW_BATTERY_NOTICE
                            | SNAP_FLAG_LOW_BATTERY_START_CONFIRM;
    TEST_ASSERT_EQUAL_UINT32(expected, captureSnapshot(in).flags);
}

/// 截至 Task 13 的 DisplaySnapshot flag bit 總數（不是「Task 7-13 新增了幾個」——
/// 是全案自 flags 欄位存在以來累計的總數），改動時同步更新（新增/移除 flag 記得改這裡）
constexpr uint32_t EXPECTED_DISPLAY_SNAPSHOT_FLAG_COUNT = 22;

static void test_all_flags_bit_masks_are_unique() {
    // 確保 EXPECTED_DISPLAY_SNAPSHOT_FLAG_COUNT 個 mask 沒有撞號（OR 全部應等於 set bit count）
    // Phase G：原 uint16_t 的 16 bit 於 W8 用罄，flags 擴為 uint32_t 容納設定 UI 兩個新 flag
    // Phase H：新增電池低電量閃爍 flag（第 19 個 bit）、§13.16 低電量提示 flag（第 20 個 bit）、
    //          §20.3 低電量開案確認框 flag（第 21 個 bit）與 Task 13 電池資訊子畫面 flag（第 22 個 bit）
    const uint32_t all = SNAP_FLAG_EPI_ARMED | SNAP_FLAG_SHOCK_ARMED
                       | SNAP_FLAG_AMIO_ARMED | SNAP_FLAG_OHCA_VENT
                       | SNAP_FLAG_VENT_END_CHECK | SNAP_FLAG_ALARM_MUTED
                       | SNAP_FLAG_VENT_BACK_HINT | SNAP_FLAG_ALARMING_FLASH
                       | SNAP_FLAG_END_CONFIRM | SNAP_FLAG_FLASH_ACTIVE
                       | SNAP_FLAG_VENT_PRE | SNAP_FLAG_HISTORY_SUMMARY
                       | SNAP_FLAG_BLE_CONNECTED | SNAP_FLAG_RESYNC_CONFIRM
                       | SNAP_FLAG_DELETE_CONFIRM | SNAP_FLAG_RESET_CONFIRM
                       | SNAP_FLAG_SETTINGS_EDITOR | SNAP_FLAG_SETTINGS_RESTORE_CONFIRM
                       | SNAP_FLAG_SETTINGS_BATTERY_INFO
                       | SNAP_FLAG_BATTERY_LOW_BLINK | SNAP_FLAG_LOW_BATTERY_NOTICE
                       | SNAP_FLAG_LOW_BATTERY_START_CONFIRM;
    // popcount
    int bits = 0;
    for (uint32_t m = all; m; m >>= 1) {
        if (m & 1) {
            bits++;
        }
    }
    TEST_ASSERT_EQUAL_INT(EXPECTED_DISPLAY_SNAPSHOT_FLAG_COUNT, bits);
}

// ============================================================
//  Group 5: Impl-Phase H 電池欄位
// ============================================================

static void test_battery_percent_change_triggers_redraw() {
    // 待機畫面電量從 55% 掉到 54% 也必須觸發重繪，否則圖示會停格
    DisplaySnapshotInputs a;   ///< 基準：電量 55%
    a.batteryPercent = 55;
    DisplaySnapshotInputs b;   ///< 對照：電量 54%（僅差 1，仍須觸發重繪）
    b.batteryPercent = 54;
    TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(captureSnapshot(a), captureSnapshot(b)),
        "snapshot 未反映 input.batteryPercent 變化");
    // 值必須落在 batteryPercent 這個具名欄位上：只驗「兩份不等」的話，
    // 實作把值寫到別的欄位（或與 batteryChargeState 互換）照樣會過。
    TEST_ASSERT_EQUAL_UINT8(55, captureSnapshot(a).batteryPercent);
    TEST_ASSERT_EQUAL_UINT8(54, captureSnapshot(b).batteryPercent);
}

static void test_battery_charge_state_change_triggers_redraw() {
    DisplaySnapshotInputs a;   ///< 基準：ChargeState::Charging(1)
    a.batteryChargeState = 1;  // Charging
    DisplaySnapshotInputs b;   ///< 對照：ChargeState::Discharging(2)
    b.batteryChargeState = 2;  // Discharging
    TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(captureSnapshot(a), captureSnapshot(b)),
        "snapshot 未反映 input.batteryChargeState 變化");
    // 同上：確認值落在 batteryChargeState 而非鄰接欄位
    TEST_ASSERT_EQUAL_UINT8(1, captureSnapshot(a).batteryChargeState);
    TEST_ASSERT_EQUAL_UINT8(2, captureSnapshot(b).batteryChargeState);
}

/**
 * Task 13 fix round 1 CRITICAL：電壓連續變化，percent／charge state 常常不動——
 * 若 batteryMillivolts 沒進 DisplaySnapshot，電池資訊畫面會顯示過期電壓且沒有任何
 * 錯誤訊號（這是 project 第 5 次踩「新 UI state 未同步進 snapshot」同型 bug）。
 */
static void test_battery_millivolts_change_triggers_redraw() {
    DisplaySnapshotInputs a;   ///< 基準：3700mV，percent／charge state 保持預設不變
    a.batteryMillivolts = 3700;
    DisplaySnapshotInputs b;   ///< 對照：3712mV（僅電壓變化，percent/charge state 相同）
    b.batteryMillivolts = 3712;
    TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(captureSnapshot(a), captureSnapshot(b)),
        "snapshot 未反映 input.batteryMillivolts 變化（percent/charge state 不變時電壓變化被吞掉）");
    // 值必須落在 batteryMillivolts 這個具名欄位上，同上兩條的驗法
    TEST_ASSERT_EQUAL_UINT16(3700, captureSnapshot(a).batteryMillivolts);
    TEST_ASSERT_EQUAL_UINT16(3712, captureSnapshot(b).batteryMillivolts);
}

static void test_battery_absent_differs_from_zero_percent() {
    // 255（不在線）與 0%（真的沒電）必須是不同狀態
    DisplaySnapshotInputs absent;   ///< 燃料計不在線（哨兵 255）
    absent.batteryPercent = 255;
    DisplaySnapshotInputs empty;    ///< 真的沒電（合法讀數 0%）
    empty.batteryPercent = 0;
    TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(captureSnapshot(absent), captureSnapshot(empty)),
        "不在線(255) 與 0% 被壓成同一個 snapshot");
    TEST_ASSERT_EQUAL_UINT8(255, captureSnapshot(absent).batteryPercent);
    TEST_ASSERT_EQUAL_UINT8(0, captureSnapshot(empty).batteryPercent);
}

/**
 * 未設定電池欄位時，snapshot 的預設值必須是「燃料計不在線」而非 0%。
 *
 * 預設值若退化成 0，畫面會把「還沒讀到電量」畫成「電量耗盡」。
 */
static void test_default_battery_percent_is_absent_sentinel() {
    DisplaySnapshotInputs in;   ///< 全預設輸入（未碰任何電池欄位）
    const DisplaySnapshot s = captureSnapshot(in);
    TEST_ASSERT_EQUAL_UINT8(255, s.batteryPercent);
    TEST_ASSERT_EQUAL_UINT8(0, s.batteryChargeState);  // ChargeState::Unknown
}

static void test_flag_battery_low_blink_sets_bit_0x40000() {
    DisplaySnapshotInputs in;
    in.batteryLowBlinkOn = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_BATTERY_LOW_BLINK, captureSnapshot(in).flags);
}

static void test_flag_low_battery_notice_sets_bit_0x80000() {
    // §13.16：低電量提示顯示中的第 20 個 flag bit
    DisplaySnapshotInputs in;
    in.lowBatteryNoticeVisible = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_LOW_BATTERY_NOTICE, captureSnapshot(in).flags);
}

/**
 * §20.3：低電量開案確認框顯示中必須設起 SNAP_FLAG_LOW_BATTERY_START_CONFIRM
 * （第 21 個 flag bit），且不得連帶設起任何其他 bit。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_flag_low_battery_start_confirm_sets_bit_0x100000() {
    // STEP 01: 只開 lowBatteryStartConfirmShown 這一個旗標的測試輸入，其餘全預設 false
    DisplaySnapshotInputs in;
    in.lowBatteryStartConfirmShown = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_LOW_BATTERY_START_CONFIRM, captureSnapshot(in).flags);
}

// ============================================================
//  Group 6: partial update 判斷（snapshotsEqualExceptCountdown）
//
//  updateDisplay() 在 OHCA 倒數畫面只重繪時間區塊時，靠這個判斷確認
//  「其餘欄位都沒變」。判斷若漏掉某個欄位，該欄位的變化會被 partial
//  路徑連同 lastDisplaySnapshot 一起吞掉，畫面永遠等不到那次重繪。
// ============================================================

/// 相鄰兩秒的倒數值：partial update 只在「僅 countdownSec 前進」時才成立
#define COUNTDOWN_SEC_PREV 30
#define COUNTDOWN_SEC_NEXT 29

/**
 * 只有 countdownSec 不同 → 可走 partial update。
 */
static void test_only_countdown_differs_allows_partial_update() {
    DisplaySnapshotInputs a;   ///< 上一秒的輸入
    a.countdownSec = COUNTDOWN_SEC_PREV;
    DisplaySnapshotInputs b;   ///< 這一秒的輸入：僅倒數前進，其餘不變
    b.countdownSec = COUNTDOWN_SEC_NEXT;
    TEST_ASSERT_TRUE_MESSAGE(
        snapshotsEqualExceptCountdown(captureSnapshot(b), captureSnapshot(a)),
        "僅 countdownSec 變化卻被判為需完整重繪");
}

/**
 * 斷言「countdownSec 與 field 在同一 frame 一起變化」必須被判為需完整重繪。
 *
 * 漏比任一欄位都會讓該欄位的變化被 partial 路徑連同 lastDisplaySnapshot 吞掉，
 * 因此每個會與倒數同時變動的欄位都要有一條。
 *
 * @param field 與 countdownSec 同時變化的 DisplaySnapshotInputs 欄位名
 * @param prev  該欄位在上一秒的值
 * @param next  該欄位在這一秒的值
 */
#define ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(field, prev, next)   \
    do {                                                                   \
        DisplaySnapshotInputs a;                                           \
        a.countdownSec = COUNTDOWN_SEC_PREV;                               \
        a.field = (prev);                                                  \
        DisplaySnapshotInputs b;                                           \
        b.countdownSec = COUNTDOWN_SEC_NEXT;                               \
        b.field = (next);                                                  \
        TEST_ASSERT_FALSE_MESSAGE(                                         \
            snapshotsEqualExceptCountdown(captureSnapshot(b),              \
                                          captureSnapshot(a)),             \
            "input." #field " 與 countdownSec 同時變化時被誤判為可 partial"); \
    } while (0)

/**
 * countdownSec 與 batteryPercent 同一 frame 變化 → 必須走完整重繪。
 *
 * 回溯測試：10 秒電池輪詢與每秒倒數 tick 必然週期性重合，
 * 走 partial 會把該次電量變化寫進 lastDisplaySnapshot 卻不重繪圖示，永久遺失。
 */
static void test_countdown_plus_battery_percent_forces_full_redraw() {
    ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(batteryPercent, 55, 54);
}

/**
 * countdownSec 與 batteryChargeState 同一 frame 變化 → 必須走完整重繪。
 */
static void test_countdown_plus_charge_state_forces_full_redraw() {
    // 1 = ChargeState::Charging / 2 = ChargeState::Discharging
    ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(batteryChargeState, 1, 2);
}

/**
 * countdownSec 與 batteryMillivolts 同一 frame 變化 → 必須走完整重繪。
 *
 * Task 13 fix round 1 CRITICAL 的同一根因：電壓輪詢與每秒倒數 tick 必然週期性
 * 重合，走 partial 會把該次電壓變化寫進 lastDisplaySnapshot 卻不重繪，永久遺失。
 */
static void test_countdown_plus_battery_millivolts_forces_full_redraw() {
    ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(batteryMillivolts, 3700, 3712);
}

/**
 * countdownSec 與低電量閃爍相位同一 frame 變化 → 必須走完整重繪。
 *
 * 閃爍相位每 500ms 翻轉、倒數每 1000ms 前進，兩者必然重合。
 */
static void test_countdown_plus_low_blink_forces_full_redraw() {
    ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(batteryLowBlinkOn, false, true);
}

/**
 * countdownSec 與 §13.16 低電量提示顯示狀態同一 frame 變化 → 必須走完整重繪。
 *
 * 機制上沒有漏洞——snapshotsEqualExceptCountdown() 是整包 memcmp，flags 欄位必然涵蓋
 * 新 flag——但比照上一條 batteryLowBlinkOn 的先例補上對應案例，避免斷了先例讓下一個
 * 新增的 flag 誤以為不需要補（2026-08-23 fix round 1 D3）。
 */
static void test_countdown_plus_low_battery_notice_forces_full_redraw() {
    ASSERT_COUNTDOWN_PLUS_FIELD_FORCES_FULL_REDRAW(lowBatteryNoticeVisible, false, true);
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
    // Phase G 回溯：系統設定選單 cursor 同類別（本類別第 4 次重演）
    RUN_TEST(test_settings_cursor_change_triggers_redraw_phase_g_regression);

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
    // Phase G：flags 擴為 uint32_t 後的新 bit
    RUN_TEST(test_flag_settings_editor_sets_bit_0x10000);
    RUN_TEST(test_flag_settings_restore_confirm_sets_bit_0x20000);
    RUN_TEST(test_flag_settings_battery_info_sets_bit_0x200000);

    // Group 4: combine + uniqueness
    RUN_TEST(test_all_flags_on_combine_all_bits);
    RUN_TEST(test_all_flags_bit_masks_are_unique);

    // Group 5: Impl-Phase H 電池欄位
    RUN_TEST(test_battery_percent_change_triggers_redraw);
    RUN_TEST(test_battery_charge_state_change_triggers_redraw);
    RUN_TEST(test_battery_millivolts_change_triggers_redraw);  // Task 13 fix round 1 CRITICAL
    RUN_TEST(test_battery_absent_differs_from_zero_percent);
    RUN_TEST(test_default_battery_percent_is_absent_sentinel);
    RUN_TEST(test_flag_battery_low_blink_sets_bit_0x40000);
    RUN_TEST(test_flag_low_battery_notice_sets_bit_0x80000);
    RUN_TEST(test_flag_low_battery_start_confirm_sets_bit_0x100000);

    // Group 6: partial update 判斷（漏欄位會吞掉重繪）
    RUN_TEST(test_only_countdown_differs_allows_partial_update);
    RUN_TEST(test_countdown_plus_battery_percent_forces_full_redraw);
    RUN_TEST(test_countdown_plus_charge_state_forces_full_redraw);
    RUN_TEST(test_countdown_plus_battery_millivolts_forces_full_redraw);  // Task 13 fix round 1
    RUN_TEST(test_countdown_plus_low_blink_forces_full_redraw);
    RUN_TEST(test_countdown_plus_low_battery_notice_forces_full_redraw);

    return UNITY_END();
}
