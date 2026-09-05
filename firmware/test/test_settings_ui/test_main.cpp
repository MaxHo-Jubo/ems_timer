// EMS DoseSync Pro — Wave 1 + Wave 2 Unit Test: 系統設定 UI
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.6 / §2.2.3 / §2.2.5
// 涵蓋：G1.1 ~ G1.9 / G2.3
//
// 測試 framework：Unity（與 test_time / test_settings 相同模式）
//   - test 函式：static void test_*()
//   - 註冊：RUN_TEST(test_name) in main()
//   - 不使用 TEST_CASE() / TEST() 等其他 framework 風格 macro
//
// 狀態：GREEN 完成（Wave 1）。TDD 的「禁止修改」凍結期已結束，
// 後續異動走一般 code review 流程即可。

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "display_abstraction.h"
#include "ui_settings.h"
#include "ems_settings.h"
#include "ems_storage_logic.h"

// ============================================================
//  共用 fixture
// ============================================================

static Display g_disp;

void setUp() {
    mock_display_reset();
    g_disp = create_mock_display();
}

void tearDown() {}

// ============================================================
//  Wave 1: 系統設定 UI + 恢復預設
// ============================================================

// ----- G1.1: drawSettingsMenu 顯示 5 項目 -----

/**
 * G1.1: drawSettingsMenu → 標題與 5 個項目標籤都被繪製。
 *
 * 原斷言為「最後一項應為確認對話框文字」——那是因為確認對話框被無條件畫出，
 * 與本測試宣稱的「顯示 5 項目」無關。改為逐項查表斷言。
 *
 * Phase H：新增「電池資訊」為第 5 項（導覽項；子畫面與主鍵行為由 Task 13 接線，
 * 本 task 僅新增選單顯示與捲動，見 SETTINGS_CURSOR_BATTERY_INFO）。
 */
static void test_g11_draw_settings_menu_items() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統設定"), "G1.1: 應繪製選單標題");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("裝置名稱"), "G1.1: 應繪製項目「裝置名稱」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("螢幕亮度"), "G1.1: 應繪製項目「螢幕亮度」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統音量"), "G1.1: 應繪製項目「系統音量」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("通氣音量"), "G1.1: 應繪製項目「通氣音量」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"), "G1.1: 應繪製項目「電池資訊」");
}

// ----- G1.2: 游標位置 -----

/** G1.2: 游標高亮 → fillRect 繪製於明確傳入的 cursor=3（通氣音量）位置（Y=166）。
 *  cursor 已無預設值，本測試明確傳入 SETTINGS_CURSOR_VENT_VOL；驗證的 Y=166
 *  不是捲動視窗〔scroll_offset=0〕內最後一個可見項——8 項化後視窗內最後一個
 *  可見項是電池資訊〔cursor=4〕，選單真正的最後一項則是裝置資訊〔cursor=7〕。
 *  座標對齊 drawMainMenu()：滿版寬度高亮條（X=0, W=SCREEN_W）+ 36px 列高。 */
static void test_g12_cursor_position_at_vent_vol() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 游標高亮矩形應存在（fillRect 應被呼叫）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, mock_get_last_fill_x(),
        "G1.2: 游標 fillRect X 座標應從畫面最左緣開始（滿版寬度，對齊主選單）");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(166, mock_get_last_fill_y(),
        "G1.2: 游標 fillRect Y 座標應為 166（cursor=3 通氣音量位置，非選單最後一項）");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(320, mock_get_last_fill_w(),
        "G1.2: 游標 fillRect 寬度應為 320（滿版寬度，對齊主選單）");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(36, mock_get_last_fill_h(),
        "G1.2: 游標 fillRect 高度應為 36（列高，對齊主選單）");
}

// ----- G1.3: 數值調整在範圍內 -----

/** G1.3: 亮度值在 SETTINGS_BRIGHTNESS_MIN~MAX 範圍內 */
static void test_g13_brightness_in_range() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    uint8_t brightness = getBrightness();
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, brightness, "G1.3: 亮度應 > 0");
    TEST_ASSERT_LESS_THAN_MESSAGE(6, brightness, "G1.3: 亮度應 < 6");
}

// ----- G1.4: 長按彈出確認對話框 -----

/** G1.4: restore_confirm = true → 畫出確認對話框文字 */
static void test_g14_confirm_dialog_shown_when_flag_set() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ true);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "G1.4: restore_confirm=true 時應畫出恢復預設確認對話框");
}

/** G1.4b: restore_confirm = false → 不得畫出確認對話框（原實作無條件畫出，此為 regression） */
static void test_g14_confirm_dialog_hidden_when_flag_clear() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "G1.4b: restore_confirm=false 時不應出現確認對話框，否則該旗標在畫面上毫無意義");
}

// ----- G1.5: 重繪不得覆寫使用者已調整的設定值 -----

/**
 * G1.5: drawSettingsMenu 重繪不得把設定值清回預設。
 *
 * regression：原實作在函式開頭無條件 `s_brightness = SETTINGS_BRIGHTNESS_DEFAULT`，
 * 使用者調完亮度回到選單、畫面一重繪就被靜默清掉（且只對亮度做，音量沒有）。
 */
static void test_g15_redraw_preserves_adjusted_values() {
    const uint8_t adjusted_brightness = SETTINGS_BRIGHTNESS_MAX;
    setBrightness(adjusted_brightness);
    setSystemVolume(SETTINGS_VOLUME_MAX);
    setVentVolume(SETTINGS_VENT_VOLUME_MIN);

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(adjusted_brightness, getBrightness(),
        "G1.5: 重繪不得把亮度清回預設值");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_VOLUME_MAX, getSystemVolume(),
        "G1.5: 重繪不得更動系統音量");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_VENT_VOLUME_MIN, getVentVolume(),
        "G1.5: 重繪不得更動通氣音量");
}

// ----- G1.6: 取消 → 設定值不變 -----

/** G1.6: 取消 → 設定值不變 */
static void test_g16_cancel_preserves_values() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 先設為非預設值，再取消 → 應恢復非預設值
    setBrightness(5);
    setSystemVolume(1);
    setVentVolume(0);

    cancelRestore();

    // 驗證設定值不變（保持設定的非預設值）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, getBrightness(),
        "G1.6: 取消後 brightness 應保持 5");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, getSystemVolume(),
        "G1.6: 取消後 system_volume 應保持 1");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, getVentVolume(),
        "G1.6: 取消後 vent_volume 應保持 0");
}

// ----- G1.7: 游標高亮依參數變化 -----

/** G1.7: cursor=0 → 高亮裝置名稱（Y=58） */
static void test_g17_cursor_0_highlights_device_name() {
    drawSettingsMenu(g_disp, 0, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 最後一次 fillRect 應為裝置名稱列（Y=58）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(58, mock_get_last_fill_y(),
        "G1.7: cursor=0 時高亮應為裝置名稱 Y=58");
}

/** G1.7: cursor=1 → 高亮螢幕亮度（Y=94） */
static void test_g17_cursor_1_highlights_brightness() {
    drawSettingsMenu(g_disp, 1, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 最後一次 fillRect 應為螢幕亮度列（Y=94）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(94, mock_get_last_fill_y(),
        "G1.7: cursor=1 時高亮應為螢幕亮度 Y=94");
}

/** G1.7: cursor=2 → 高亮系統音量（Y=130） */
static void test_g17_cursor_2_highlights_system_volume() {
    drawSettingsMenu(g_disp, 2, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 最後一次 fillRect 應為系統音量列（Y=130）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(130, mock_get_last_fill_y(),
        "G1.7: cursor=2 時高亮應為系統音量 Y=130");
}

/**
 * G1.7: cursor=4（電池資訊）→ 高亮電池資訊（Y=202）。
 *
 * Fix round 1 新增：原本刪掉電池資訊那段 STEP 04.02 的 fill_rect 分支也不會讓
 * 任何既有測試變紅（G1.1 只驗文字有沒有畫出來，不驗高亮），這裡補上鑑別力。
 *
 * Fix round 3：拿掉這裡原本驗證「選取中文字色為對比色」與「未選取文字色維持
 * 白色」的兩個斷言——fix round 1 曾把選取時的文字色改成黑色想解決白底白字，
 * fix round 3 發現那個修法讓文字其餘 83% 面積變成黑字疊黑螢幕背景，比原本更
 * 看不清楚，已撤銷改回全部項目一律白字（不分選取與否），色彩區分當時因此不
 * 存在，這兩個斷言隨之失去意義而移除。fill_rect Y 座標這個斷言仍是有效的
 * regression guard（驗證「游標命中哪一列」邏輯本身沒壞），保留。
 *
 * 2026-09-05 更新：高亮框改滿版寬度後，選取列文字反白已重新加回（見
 * SETTINGS_COLOR_SELECTED_TEXT），色彩區分現在確實存在——對應斷言見
 * test_g23d_regular_item_text_color_follows_selection()，不在本測試重複驗證。
 */
static void test_g17_cursor_4_highlights_battery_info() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // 確認電池資訊列本身有被正確高亮（Y=202）；不證明其他列沒有被誤觸發高亮——電池資訊
    // 是預設捲動視窗（scroll_offset=0）內最後一個可見項（8 項化後選單真正的最後一項是
    // 裝置資訊 cursor=7，捲出視窗外），較早的列即使也誤呼叫 fill_rect，最後一次記錄的
    // Y 仍會是 202，這個斷言照樣會過。要涵蓋互斥性得記錄全部 fill_rect 呼叫並斷言呼叫次數為 1。
    TEST_ASSERT_EQUAL_INT16_MESSAGE(202, mock_get_last_fill_y(),
        "G1.7: cursor=4 時高亮應為電池資訊 Y=202");

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"),
        "G1.7: 應繪製「電池資訊」標籤文字");
}

/**
 * Fix round 1 item 5 regression：cursor=4 且確認對話框顯示中時，電池資訊列必須
 * 跳過繪製。它的 Y=202 與對話框文字 Y=196（SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET）
 * 只差 6px，兩段文字若同時畫出會疊在一起看不清楚；長按 BTN_PRIMARY 觸發此對話框
 * 不受目前游標位置限制，cursor=4 時一樣可能觸發，須驗證。
 *
 * Fix round 3 補一個範圍精確性斷言：原本 STEP 04.01 的 guard 條件是
 * `item.cursor == SETTINGS_CURSOR_BATTERY_INFO && restore_confirm`，如果有人
 * 簡化成只剩 `restore_confirm`（拿掉 cursor 判斷、對話框顯示時整批項目都跳過），
 * 上面兩個既有斷言依然會過（它們只查電池資訊跟對話框本身）。加上驗證另一個
 * 表格項目（通氣音量）在對話框顯示中仍正常繪製，證明 guard 只影響電池資訊這一列。
 *
 * Impl-Phase G 捲動重構後，guard 條件已改為 STEP 03.01 的
 * `restore_confirm && is_last_visible_slot`（判準從「是不是電池資訊項」改為
 * 「是不是視窗最後一格」，兩者在 scroll_offset=0 時等價，見 ui_settings.cpp
 * STEP 03.01 doc comment）；本測試固定用 scroll_offset=0 呼叫，驗證的仍是這個
 * guard 的行為，斷言內容不需變動。
 */
static void test_g17_battery_info_hidden_when_confirm_dialog_shown() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ true);

    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("電池資訊"),
        "cursor=4 且確認對話框顯示中時，電池資訊列應跳過繪製避免與對話框重疊");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "確認對話框本身仍應正常顯示，不受電池資訊列跳過繪製影響");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("通氣音量"),
        "確認對話框顯示中時，其餘表格項目（通氣音量）仍應正常繪製，guard 不得誤傷其他列");
}

// ----- G1.8: drawSettingEditor 繪製 -----

/** G1.8: drawSettingEditor 繪製標題、數值、範圍 */
static void test_g18_draw_setting_editor() {
    drawSettingEditor(g_disp, "螢幕亮度", 3, 1, 5);

    // 驗證有 draw 文字（最後一項：範圍提示）
    const char* text = mock_get_last_text();
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "G1.8: drawSettingEditor 應呼叫 drawText 繪製文字");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1 ~ 5", text, "G1.8: 最後一項應為範圍提示");
}

// ----- G1.9: 選單游標 UP/DOWN wrap-around 邊界（Impl-Phase G：8 項化後邊界移至 cursor=7）-----

/**
 * G1.9: wrapSettingsCursor() 邊界行為——8 項化後新邊界：DOWN 7→0、UP 0→7。
 * cursor=3→4（通氣音量→電池資訊）不再是邊界（8 項化前是，因為電池資訊曾是
 * 最後一項），改成一般序列測試，涵蓋新增的 4 個項目也接進同一條 wrap 鏈。
 *
 * 同既有註記：input_handler.cpp 屬 src/，native build 排除，此組測試只涵蓋
 * wrapSettingsCursor() 本身的邊界數學，不涵蓋按鍵分派接線（見 handover §8 殘餘風險 ⑥）。
 */
static void test_g19_sequential_no_wrap_through_new_items() {
    // STEP 01: 初始游標設為通氣音量(3)，序列往下逐一驗證新增的 4 個項目不誤 wrap
    uint8_t c = SETTINGS_CURSOR_VENT_VOL;

    // STEP 02: 通氣音量(3) → 電池資訊(4)
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_BATTERY_INFO, c, "G1.9: 3→4");

    // STEP 03: 電池資訊(4) → App連線設定(5)
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_APP_CONN, c, "G1.9: 4→5");

    // STEP 04: App連線設定(5) → Type-C連線(6)
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_TYPEC_CONN, c, "G1.9: 5→6");

    // STEP 05: Type-C連線(6) → 裝置資訊(7)
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO, c, "G1.9: 6→7");
}

/** G1.9: DOWN cursor=7（裝置資訊，新的最後一項）→ 0（裝置名稱，wrap 回第一項） */
static void test_g19_wrap_down_from_device_info_to_device_name() {
    // STEP 01: DOWN 從最後一項應 wrap 回第一項
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_NAME,
        wrapSettingsCursor(SETTINGS_CURSOR_DEVICE_INFO, SETTINGS_CURSOR_DELTA_DOWN),
        "G1.9: DOWN cursor=7 應 wrap 回 0（裝置名稱）");
}

/** G1.9: UP cursor=0（裝置名稱）→ 7（裝置資訊，wrap 到新的最後一項） */
static void test_g19_wrap_up_from_device_name_to_device_info() {
    // STEP 01: UP 從第一項應 wrap 到新的最後一項（8 項化後是 cursor=7，不是舊的 4）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO,
        wrapSettingsCursor(SETTINGS_CURSOR_DEVICE_NAME, SETTINGS_CURSOR_DELTA_UP),
        "G1.9: UP cursor=0 應 wrap 到 7（裝置資訊）——項目數變成 8 之後的新邊界");
}

// ----- G-Pairing: advanceSettingsCursorAndScroll() 游標＋捲動視窗成對更新 -----
//
// 2026-09-02 codex Tier 3 補跑對 89917d5 抓到的 IMPORTANT：input_handler.cpp
// 的 BTN_UP/BTN_DOWN 分支（wrapSettingsCursor() + clampScrollOffset() 串接）
// 屬 src/，native build 排除，這段「成對更新」邏輯先前完全沒有測試涵蓋，唯一
// 防線是註解警告。抽成 advanceSettingsCursorAndScroll() 後這裡直接測。

/** 游標在視窗內移動（未觸發捲動）：cursor 1→2，視窗仍是 [0,5)，offset 不變 */
static void test_pairing_move_within_window_offset_unchanged() {
    // STEP 01: 從 cursor=1、offset=0 往下一步，視窗容納 cursor=2，offset 應不變
    SettingsCursorScroll result = advanceSettingsCursorAndScroll(SETTINGS_CURSOR_BRIGHTNESS, 0, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_SYSTEM_VOL, result.cursor, "G-Pairing: cursor 1→2");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, result.scroll_offset, "G-Pairing: 視窗內移動 offset 應不變");
}

/** 游標移出視窗下緣觸發捲動：cursor 4→5，視窗從 [0,5) 跟著捲到 [1,6) */
static void test_pairing_move_below_window_scrolls_down() {
    // STEP 01: 從 cursor=4（電池資訊，視窗 [0,5) 最後一項）往下一步
    SettingsCursorScroll result = advanceSettingsCursorAndScroll(SETTINGS_CURSOR_BATTERY_INFO, 0, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_APP_CONN, result.cursor, "G-Pairing: cursor 4→5");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1, result.scroll_offset, "G-Pairing: 移出視窗下緣應觸發捲動，新視窗 [1,6)");
}

/**
 * 連續下捲直到清單末項：cursor 5→6→7，視窗跟著捲到 [3,8)。
 * 對應既有 test_scroll_offset_three_shows_last_five_items 的視窗狀態，證明
 * advanceSettingsCursorAndScroll() 連續呼叫算出的 offset 跟渲染測試假設的一致。
 */
static void test_pairing_sequential_scroll_down_to_last_item() {
    // STEP 01: cursor=5,offset=1 → DOWN → cursor=6
    SettingsCursorScroll step1 = advanceSettingsCursorAndScroll(SETTINGS_CURSOR_APP_CONN, 1, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_TYPEC_CONN, step1.cursor, "G-Pairing: cursor 5→6");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, step1.scroll_offset, "G-Pairing: 視窗跟著捲到 [2,7)");

    // STEP 02: cursor=6,offset=2 → DOWN → cursor=7，視窗應為 [3,8)
    SettingsCursorScroll step2 = advanceSettingsCursorAndScroll(step1.cursor, step1.scroll_offset, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO, step2.cursor, "G-Pairing: cursor 6→7");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, step2.scroll_offset, "G-Pairing: 視窗跟著捲到 [3,8)，同渲染測試的視窗狀態");
}

/** wrap DOWN：cursor 7→0（wrap 回第一項），視窗應跟著捲回頂端 [0,5) */
static void test_pairing_wrap_down_resets_scroll_to_top() {
    // STEP 01: 從 cursor=7、offset=3（視窗 [3,8)）往下一步，wrap 回 cursor=0
    SettingsCursorScroll result = advanceSettingsCursorAndScroll(SETTINGS_CURSOR_DEVICE_INFO, 3, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_NAME, result.cursor, "G-Pairing: DOWN wrap 7→0");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, result.scroll_offset, "G-Pairing: wrap 回頂端後視窗應跟著捲回 [0,5)");
}

/** wrap UP：cursor 0→7（wrap 到最後一項），視窗應跟著捲到底端 [3,8) */
static void test_pairing_wrap_up_scrolls_to_bottom() {
    // STEP 01: 從 cursor=0、offset=0（視窗 [0,5)）往上一步，wrap 到 cursor=7
    SettingsCursorScroll result = advanceSettingsCursorAndScroll(SETTINGS_CURSOR_DEVICE_NAME, 0, SETTINGS_CURSOR_DELTA_UP);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO, result.cursor, "G-Pairing: UP wrap 0→7");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, result.scroll_offset, "G-Pairing: wrap 到底端後視窗應跟著捲到 [3,8)");
}

// ----- G-Phase-G-Scroll: 8 項選單捲動渲染 -----

/** 捲動視窗預設 0 時，只畫前 5 項（裝置名稱~電池資訊），不畫捲出視窗外的 3 項 */
static void test_scroll_offset_zero_shows_first_five_items() {
    // STEP 01: scroll_offset=0，游標停在裝置名稱，繪製選單
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // STEP 02: 視窗內第 5 項（電池資訊）應被畫出
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"),
        "視窗內第 5 項（電池資訊）應被畫出");

    // STEP 03: 視窗外的項目（第 6、8 項）不應被畫出
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("App連線設定"),
        "視窗外項目（App連線設定，第 6 項）不應被畫出");
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置資訊"),
        "視窗外項目（裝置資訊，第 8 項）不應被畫出");
}

/**
 * 捲動視窗 offset=3 時，畫第 4~8 項（通氣音量~裝置資訊），裝置名稱捲出視窗不畫。
 *
 * 除了「有沒有畫出來」，同時驗證視窗內 5 項各自的 Y 座標——通氣音量原本（offset=0
 * 時）查表固定畫在 Y=166，捲動後它落在視窗第 1 格改畫在 Y=58，證明 Y 真的是依
 * 「目前在視窗內第幾格」動態算出，不是查表存死值；並驗證游標高亮 fill_rect 的
 * Y 與文字 Y 用同一份捲動位置計算，不會分裂成兩套算式（fix round 1 這裡曾只查
 * 文字有無畫出，Y 座標算錯或高亮位置對不上文字都測不出來）。
 */
static void test_scroll_offset_three_shows_last_five_items() {
    // STEP 01: scroll_offset=3，游標停在裝置資訊（視窗內第 5 格，會被高亮）
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_INFO, /* scroll_offset= */ 3,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    // STEP 02: 捲出視窗的裝置名稱不應被畫出
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置名稱"),
        "捲出視窗的裝置名稱不應被畫出");

    // STEP 03: 視窗內 5 項應依序畫在 Y=58/94/130/166/202
    const MockTextCall* vent = mock_text_log_find("通氣音量");
    TEST_ASSERT_NOT_NULL_MESSAGE(vent, "視窗內第 1 項（通氣音量）應被畫出");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(58, vent->y,
        "通氣音量在 offset=3 時應落於視窗第 1 格 Y=58（非其 offset=0 時的固定位置 Y=166）");

    const MockTextCall* battery = mock_text_log_find("電池資訊");
    TEST_ASSERT_NOT_NULL_MESSAGE(battery, "視窗內第 2 項（電池資訊）應被畫出");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(94, battery->y, "電池資訊在 offset=3 時應落於視窗第 2 格 Y=94");

    const MockTextCall* app_conn = mock_text_log_find("App連線設定");
    TEST_ASSERT_NOT_NULL_MESSAGE(app_conn, "視窗內第 3 項（App連線設定）應被畫出");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(130, app_conn->y, "App連線設定在 offset=3 時應落於視窗第 3 格 Y=130");

    const MockTextCall* typec = mock_text_log_find("Type-C連線");
    TEST_ASSERT_NOT_NULL_MESSAGE(typec, "視窗內第 4 項（Type-C連線）應被畫出");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(166, typec->y, "Type-C連線在 offset=3 時應落於視窗第 4 格 Y=166");

    const MockTextCall* device_info = mock_text_log_find("裝置資訊");
    TEST_ASSERT_NOT_NULL_MESSAGE(device_info, "視窗內最後一項（裝置資訊）應被畫出");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(202, device_info->y, "裝置資訊在 offset=3 時應落於視窗第 5 格 Y=202");

    // STEP 04: cursor=7（裝置資訊）為視窗最後一格，游標高亮 fill_rect 應落在同一個 Y=202
    TEST_ASSERT_EQUAL_INT16_MESSAGE(202, mock_get_last_fill_y(),
        "cursor=7（裝置資訊）的游標高亮在 offset=3 時應落於視窗第 5 格 Y=202，與文字 Y 一致");
}

/** 恢復預設確認對話框顯示中時，視窗最後一格讓給對話框——不論該格當下是哪個項目 */
static void test_restore_confirm_hides_last_visible_slot_regardless_of_item() {
    // STEP 01: scroll_offset=3 時視窗最後一格（第 5 格，shown=4）是裝置資訊（cursor 7）
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_TYPEC_CONN, /* scroll_offset= */ 3,
                     /* device_name_locked= */ false, /* restore_confirm= */ true);

    // STEP 02: 視窗最後一格（裝置資訊）顯示中應跳過繪製，不論它是哪一項
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置資訊"),
        "視窗最後一格顯示中的項目在對話框顯示時應跳過繪製，不論它是哪一項");

    // STEP 03: 視窗內非最後一格的項目（Type-C連線）仍應正常繪製
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("Type-C連線"),
        "視窗內非最後一格的項目在對話框顯示時仍應正常繪製");

    // STEP 04: 對話框本身應正常顯示
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "對話框本身應正常顯示");
}

/**
 * Regression（2026-09-05 CONFIRM_Y_OFFSET 修正）：游標在通氣音量（scroll_offset=0
 * 時為 shown=3，視窗倒數第 2 格，不在「跳過最後一格」範圍內因此仍會正常繪製
 * 高亮）且確認對話框顯示中時，該列的游標高亮矩形範圍不得與對話框文字重疊，
 * 否則白色高亮疊白色對話框文字會造成白底白字。
 *
 * 高亮列高從 20px 改 36px 前，SETTINGS_CONFIRM_Y_OFFSET 固定寫死 30 在此情境
 * 下會產生重疊（Y=202 的高亮底部 vs Y=196 的對話框文字起點）；此測試驗證改為
 * `SETTINGS_CURSOR_HEIGHT + 4` 自適應公式後兩者不再重疊。
 */
static void test_restore_confirm_no_overlap_with_second_to_last_row_highlight() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ true);

    // STEP 01: 通氣音量（shown=3）被選取時仍應正常繪製高亮
    TEST_ASSERT_EQUAL_INT16_MESSAGE(166, mock_get_last_fill_y(),
        "通氣音量在 shown=3 選取中時高亮仍應正常繪製於 Y=166");

    // STEP 02: 對話框文字必須落在該列高亮矩形（Y=166~202）底部之後
    const MockTextCall* confirm_text = mock_text_log_find("是否恢復預設設定？");
    TEST_ASSERT_NOT_NULL_MESSAGE(confirm_text, "對話框本身應正常顯示");
    TEST_ASSERT_TRUE_MESSAGE(confirm_text->y >= 166 + 36,
        "對話框文字 Y 座標必須在通氣音量高亮矩形（Y=166~202）底部之後，"
        "否則白色高亮會壓住白色對話框文字造成白底白字");
}

// ----- G2.3: 案件中裝置名稱置灰 + 名稱顯示 -----

/**
 * G2.3: device_name_locked = true → 裝置名稱以 DIM 色繪製，且不顯示當前名稱。
 *
 * 判準本身（有無未同步案件）由 storage_has_unsynced_case() 負責，
 * 其邏輯測試在 test_ems_storage_logic Group I；此處只驗 UI 對該旗標的反應。
 */
static void test_g23_locked_renders_dim_and_hides_name() {
    mock_fs_write(DEVICE_NAME_FILE, "測試站", strlen("測試站"));

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* scroll_offset= */ 0,
                     /* device_name_locked= */ true, /* restore_confirm= */ false);

    const MockTextCall* label = mock_text_log_find("裝置名稱");
    TEST_ASSERT_NOT_NULL_MESSAGE(label, "G2.3: 裝置名稱標籤應被繪製");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SETTINGS_COLOR_DIM, label->color,
        "G2.3: 鎖定時裝置名稱應以 DIM 色置灰");
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("測試站"),
        "G2.3: 鎖定時不應顯示當前裝置名稱");
}

/**
 * G2.3b: device_name_locked = false、游標在裝置名稱（選取中）→ 反白文字色
 * （SETTINGS_COLOR_SELECTED_TEXT，對齊 drawMainMenu() 選取列黑字風格）+ 顯示當前名稱。
 *
 * 這個方向原本完全沒測，導致 is_device_name_locked 恆回 true（永遠置灰）
 * 的 bug 在 483 個綠燈下存活。
 *
 * 2026-09-05：游標高亮改滿版寬度後，選取列文字改回反白（見 ui_settings.h
 * SETTINGS_COLOR_SELECTED_TEXT doc comment），原本斷言的 SETTINGS_COLOR_WHITE
 * 已隨之改變；未選取時的白色改由 test_g23c 驗證。
 */
static void test_g23_unlocked_renders_white_and_shows_name() {
    mock_fs_write(DEVICE_NAME_FILE, "測試站", strlen("測試站"));

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    const MockTextCall* label = mock_text_log_find("裝置名稱");
    TEST_ASSERT_NOT_NULL_MESSAGE(label, "G2.3b: 裝置名稱標籤應被繪製");
    // 斷言硬編碼字面值 0x0000（RGB565 黑），不直接比對 SETTINGS_COLOR_SELECTED_TEXT
    // 常數本身——否則常數若被誤改成白色，兩邊同步出錯，斷言仍會通過，測不出
    // 白底白字這個回歸
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x0000, label->color,
        "G2.3b: 未鎖定且選取中時裝置名稱應為反白文字色（RGB565 黑）");
    TEST_ASSERT_TRUE_MESSAGE(label->color != SETTINGS_COLOR_WHITE,
        "G2.3b: 反白文字色不應等於一般白字色，否則選取與非選取視覺無區別");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("測試站"),
        "G2.3b: 未鎖定時應顯示當前裝置名稱");
}

/**
 * G2.3c: device_name_locked = false、游標不在裝置名稱（未選取）→ 正常白色。
 *
 * 補 G2.3b 沒涵蓋的另一半：確認反白只發生在選取列，非選取列仍是一般白字，
 * 不是整個畫面在未鎖定時都變成反白文字色。
 */
static void test_g23c_unlocked_unselected_renders_white() {
    mock_fs_write(DEVICE_NAME_FILE, "測試站", strlen("測試站"));

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BRIGHTNESS, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    const MockTextCall* label = mock_text_log_find("裝置名稱");
    TEST_ASSERT_NOT_NULL_MESSAGE(label, "G2.3c: 裝置名稱標籤應被繪製（視窗內第 1 項，未選取）");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SETTINGS_COLOR_WHITE, label->color,
        "G2.3c: 未鎖定且未選取時裝置名稱應維持正常白色");
}

/**
 * G2.3d: 一般項目（非裝置名稱特例）選取中 → 反白文字色；未選取 → 正常白色。
 * 對齊 drawMainMenu() 選取列黑字風格，驗證 STEP 03.04 分支（非 STEP 03.03 特例）。
 */
static void test_g23d_regular_item_text_color_follows_selection() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BRIGHTNESS, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);
    const MockTextCall* selected_label = mock_text_log_find("螢幕亮度");
    TEST_ASSERT_NOT_NULL_MESSAGE(selected_label, "G2.3d: 螢幕亮度標籤應被繪製");
    // 硬編碼字面值，理由同 test_g23_unlocked_renders_white_and_shows_name 的說明
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x0000, selected_label->color,
        "G2.3d: 游標在螢幕亮度時應為反白文字色（RGB565 黑）");
    TEST_ASSERT_TRUE_MESSAGE(selected_label->color != SETTINGS_COLOR_WHITE,
        "G2.3d: 反白文字色不應等於一般白字色，否則選取與非選取視覺無區別");

    // mock text log 累積、不會在同一個測試函式內的兩次 draw 呼叫之間自動清空
    // （只有 setUp() 在每個 TEST_FUNCTION 開始前清一次）——不重置的話
    // mock_text_log_find() 會找到上一次呼叫留下的舊紀錄，斷言會誤判成功
    mock_display_reset();

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_SYSTEM_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);
    const MockTextCall* unselected_label = mock_text_log_find("螢幕亮度");
    TEST_ASSERT_NOT_NULL_MESSAGE(unselected_label, "G2.3d: 螢幕亮度標籤應被繪製（視窗內第 2 項，未選取）");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SETTINGS_COLOR_WHITE, unselected_label->color,
        "G2.3d: 游標不在螢幕亮度時應維持正常白色");
}

// ----- G-Page-Indicator: 捲動選單頁碼指示 -----

/**
 * 8 項選單超出可見列數（5）時，應顯示「游標位置/總項目數」頁碼指示，
 * 提示使用者選單還能往下捲，不是只有畫面上看到的 5 項。
 */
static void test_page_indicator_shown_when_scrollable() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_SYSTEM_VOL, /* scroll_offset= */ 0,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("3/8"),
        "游標在系統音量（cursor=2，第 3 項）時應顯示頁碼指示 3/8");
}

/** 游標移動時頁碼指示的分子應跟著變動，不是釘死在某個固定值 */
static void test_page_indicator_follows_cursor() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_INFO, /* scroll_offset= */ 3,
                     /* device_name_locked= */ false, /* restore_confirm= */ false);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("8/8"),
        "游標在裝置資訊（cursor=7，第 8 項）時應顯示頁碼指示 8/8");
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    RUN_TEST(test_g11_draw_settings_menu_items);
    RUN_TEST(test_g12_cursor_position_at_vent_vol);
    RUN_TEST(test_g13_brightness_in_range);
    RUN_TEST(test_g14_confirm_dialog_shown_when_flag_set);
    RUN_TEST(test_g14_confirm_dialog_hidden_when_flag_clear);
    RUN_TEST(test_g15_redraw_preserves_adjusted_values);
    RUN_TEST(test_g16_cancel_preserves_values);
    RUN_TEST(test_g17_cursor_0_highlights_device_name);
    RUN_TEST(test_g17_cursor_1_highlights_brightness);
    RUN_TEST(test_g17_cursor_2_highlights_system_volume);
    RUN_TEST(test_g17_cursor_4_highlights_battery_info);
    RUN_TEST(test_g17_battery_info_hidden_when_confirm_dialog_shown);
    RUN_TEST(test_g18_draw_setting_editor);
    // G1.9: 選單游標 UP/DOWN wrap-around 邊界
    RUN_TEST(test_g19_sequential_no_wrap_through_new_items);
    RUN_TEST(test_g19_wrap_down_from_device_info_to_device_name);
    RUN_TEST(test_g19_wrap_up_from_device_name_to_device_info);

    // G-Pairing: advanceSettingsCursorAndScroll() 游標＋捲動視窗成對更新
    RUN_TEST(test_pairing_move_within_window_offset_unchanged);
    RUN_TEST(test_pairing_move_below_window_scrolls_down);
    RUN_TEST(test_pairing_sequential_scroll_down_to_last_item);
    RUN_TEST(test_pairing_wrap_down_resets_scroll_to_top);
    RUN_TEST(test_pairing_wrap_up_scrolls_to_bottom);
    // G-Phase-G-Scroll: 8 項選單捲動渲染
    RUN_TEST(test_scroll_offset_zero_shows_first_five_items);
    RUN_TEST(test_scroll_offset_three_shows_last_five_items);
    RUN_TEST(test_restore_confirm_hides_last_visible_slot_regardless_of_item);
    RUN_TEST(test_restore_confirm_no_overlap_with_second_to_last_row_highlight);
    // G2.3: 案件中裝置名稱置灰 + 名稱顯示
    RUN_TEST(test_g23_locked_renders_dim_and_hides_name);
    RUN_TEST(test_g23_unlocked_renders_white_and_shows_name);
    RUN_TEST(test_g23c_unlocked_unselected_renders_white);
    RUN_TEST(test_g23d_regular_item_text_color_follows_selection);
    // G-Page-Indicator: 捲動選單頁碼指示
    RUN_TEST(test_page_indicator_shown_when_scrollable);
    RUN_TEST(test_page_indicator_follows_cursor);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
