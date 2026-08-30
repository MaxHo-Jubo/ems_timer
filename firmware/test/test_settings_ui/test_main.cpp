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
    drawSettingsMenu(g_disp);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統設定"), "G1.1: 應繪製選單標題");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("裝置名稱"), "G1.1: 應繪製項目「裝置名稱」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("螢幕亮度"), "G1.1: 應繪製項目「螢幕亮度」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統音量"), "G1.1: 應繪製項目「系統音量」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("通氣音量"), "G1.1: 應繪製項目「通氣音量」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"), "G1.1: 應繪製項目「電池資訊」");
}

// ----- G1.2: 游標位置 -----

/** G1.2: 游標高亮 → fillRect 繪製於預設游標位置（Y=150, cursor=3 為向後相容預設值，
 *  非選單最後一項——新增電池資訊〔cursor=4〕後最後一項是它，不是通氣音量） */
static void test_g12_cursor_position_default() {
    drawSettingsMenu(g_disp);

    // 游標高亮矩形應存在（fillRect 應被呼叫）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(10, mock_get_last_fill_x(),
        "G1.2: 游標 fillRect X 座標應從左邊距開始");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, mock_get_last_fill_y(),
        "G1.2: 游標 fillRect Y 座標應為 150（cursor=3 通氣音量位置，非選單最後一項）");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(80, mock_get_last_fill_w(),
        "G1.2: 游標 fillRect 寬度應為 80（文字區域）");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(20, mock_get_last_fill_h(),
        "G1.2: 游標 fillRect 高度應為 20（文字行高）");
}

// ----- G1.3: 數值調整在範圍內 -----

/** G1.3: 亮度值在 SETTINGS_BRIGHTNESS_MIN~MAX 範圍內 */
static void test_g13_brightness_in_range() {
    drawSettingsMenu(g_disp);

    uint8_t brightness = getBrightness();
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, brightness, "G1.3: 亮度應 > 0");
    TEST_ASSERT_LESS_THAN_MESSAGE(6, brightness, "G1.3: 亮度應 < 6");
}

// ----- G1.4: 長按彈出確認對話框 -----

/** G1.4: restore_confirm = true → 畫出確認對話框文字 */
static void test_g14_confirm_dialog_shown_when_flag_set() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* device_name_locked= */ false,
                     /* restore_confirm= */ true);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "G1.4: restore_confirm=true 時應畫出恢復預設確認對話框");
}

/** G1.4b: restore_confirm = false → 不得畫出確認對話框（原實作無條件畫出，此為 regression） */
static void test_g14_confirm_dialog_hidden_when_flag_clear() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_VENT_VOL, /* device_name_locked= */ false,
                     /* restore_confirm= */ false);

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

    drawSettingsMenu(g_disp);

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
    drawSettingsMenu(g_disp);

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

/** G1.7: cursor=0 → 高亮裝置名稱（Y=30） */
static void test_g17_cursor_0_highlights_device_name() {
    drawSettingsMenu(g_disp, 0);

    // 最後一次 fillRect 應為裝置名稱列（Y=30）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(30, mock_get_last_fill_y(),
        "G1.7: cursor=0 時高亮應為裝置名稱 Y=30");
}

/** G1.7: cursor=1 → 高亮螢幕亮度（Y=70） */
static void test_g17_cursor_1_highlights_brightness() {
    drawSettingsMenu(g_disp, 1);

    // 最後一次 fillRect 應為螢幕亮度列（Y=70）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(70, mock_get_last_fill_y(),
        "G1.7: cursor=1 時高亮應為螢幕亮度 Y=70");
}

/** G1.7: cursor=2 → 高亮系統音量（Y=110） */
static void test_g17_cursor_2_highlights_system_volume() {
    drawSettingsMenu(g_disp, 2);

    // 最後一次 fillRect 應為系統音量列（Y=110）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(110, mock_get_last_fill_y(),
        "G1.7: cursor=2 時高亮應為系統音量 Y=110");
}

/**
 * G1.7: cursor=4（電池資訊）→ 高亮電池資訊（Y=190）。
 *
 * Fix round 1 新增：原本刪掉電池資訊那段 STEP 04.02 的 fill_rect 分支也不會讓
 * 任何既有測試變紅（G1.1 只驗文字有沒有畫出來，不驗高亮），這裡補上鑑別力。
 *
 * Fix round 3：拿掉這裡原本驗證「選取中文字色為對比色」與「未選取文字色維持
 * 白色」的兩個斷言——fix round 1 曾把選取時的文字色改成黑色想解決白底白字，
 * fix round 3 發現那個修法讓文字其餘 83% 面積變成黑字疊黑螢幕背景，比原本更
 * 看不清楚，已撤銷改回全部項目一律白字（不分選取與否），色彩區分因此不存在，
 * 這兩個斷言隨之失去意義。fill_rect Y 座標這個斷言仍是有效的 regression guard
 * （驗證「游標命中哪一列」邏輯本身沒壞），保留。
 */
static void test_g17_cursor_4_highlights_battery_info() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO);

    // 確認電池資訊列本身有被正確高亮（Y=190）；不證明其他列沒有被誤觸發高亮——電池資訊
    // 是查表迴圈的最後一項，較早的列即使也誤呼叫 fill_rect，最後一次記錄的 Y 仍會是 190，
    // 這個斷言照樣會過。要涵蓋互斥性得記錄全部 fill_rect 呼叫並斷言呼叫次數為 1。
    TEST_ASSERT_EQUAL_INT16_MESSAGE(190, mock_get_last_fill_y(),
        "G1.7: cursor=4 時高亮應為電池資訊 Y=190");

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"),
        "G1.7: 應繪製「電池資訊」標籤文字");
}

/**
 * Fix round 1 item 5 regression：cursor=4 且確認對話框顯示中時，電池資訊列必須
 * 跳過繪製。它的 Y=190 與對話框文字 Y=180（SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET）
 * 只差 10px，兩段文字若同時畫出會疊在一起看不清楚；長按 BTN_PRIMARY 觸發此對話框
 * 不受目前游標位置限制，cursor=4 時一樣可能觸發，須驗證。
 *
 * Fix round 3 補一個範圍精確性斷言：STEP 04.01 的 guard 條件是
 * `item.cursor == SETTINGS_CURSOR_BATTERY_INFO && restore_confirm`，如果有人
 * 簡化成只剩 `restore_confirm`（拿掉 cursor 判斷、對話框顯示時整批 4 個項目
 * 都跳過），上面兩個既有斷言依然會過（它們只查電池資訊跟對話框本身）。加上
 * 驗證另一個表格項目（通氣音量）在對話框顯示中仍正常繪製，證明 guard 只影響
 * 電池資訊這一列。
 */
static void test_g17_battery_info_hidden_when_confirm_dialog_shown() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO, /* device_name_locked= */ false,
                     /* restore_confirm= */ true);

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

// ----- G1.9: 選單游標 UP/DOWN wrap-around 邊界（Fix round 1 新增） -----

/**
 * G1.9: wrapSettingsCursor() 邊界行為——涵蓋新增電池資訊項（cursor=4）後新出現／
 * 改變的三個邊界轉換：DOWN 3→4、DOWN 4→0、UP 0→4。cursor 1/2 的 UP/DOWN 轉換
 * 不受新增第 5 項影響（沿途經過、沒有 wrap），本組測試不重複涵蓋。
 *
 * input_handler.cpp 屬於 src/，native build 用 `build_src_filter = -<*>` 整個排除
 * （platformio.ini `[env:native]`），UP/DOWN 按鍵分派本身無法被 native test 直接呼叫；
 * wrapSettingsCursor() 是把該處內嵌算式原地抽出的純函式（定義於 ui_settings.h），
 * input_handler.cpp 的 BTN_UP/BTN_DOWN 分支呼叫的就是這個函式本身，不是另外複製一份
 * 公式。但本組測試只涵蓋 wrapSettingsCursor() 本身的邊界數學，不涵蓋 input_handler.cpp
 * 的按鍵分派接線——delta 傳反、少呼叫這個 helper、接錯按鍵分支這幾類接線錯誤，本組測試
 * 都測不出來；該檔不進 native build，接線層 regression 靠 review/grep，見 handover §8
 * 殘餘風險 ⑥。
 */
static void test_g19_wrap_down_from_vent_vol_to_battery_info() {
    // DOWN：cursor=3（通氣音量，5 項化前的最後一項）→ 4（電池資訊，新的最後一項）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_BATTERY_INFO,
        wrapSettingsCursor(SETTINGS_CURSOR_VENT_VOL, SETTINGS_CURSOR_DELTA_DOWN),
        "G1.9: DOWN cursor=3 應進到 4（電池資訊）");
}

/** G1.9: DOWN cursor=4（電池資訊，新的最後一項）→ 0（裝置名稱，wrap 回第一項） */
static void test_g19_wrap_down_from_battery_info_to_device_name() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_NAME,
        wrapSettingsCursor(SETTINGS_CURSOR_BATTERY_INFO, SETTINGS_CURSOR_DELTA_DOWN),
        "G1.9: DOWN cursor=4 應 wrap 回 0（裝置名稱）");
}

/** G1.9: UP cursor=0（裝置名稱）→ 4（電池資訊，wrap 到新的最後一項，不是舊的 3） */
static void test_g19_wrap_up_from_device_name_to_battery_info() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_BATTERY_INFO,
        wrapSettingsCursor(SETTINGS_CURSOR_DEVICE_NAME, SETTINGS_CURSOR_DELTA_UP),
        "G1.9: UP cursor=0 應 wrap 到 4（電池資訊）——項目數變成 5 之後的新邊界");
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

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* device_name_locked= */ true);

    const MockTextCall* label = mock_text_log_find("裝置名稱");
    TEST_ASSERT_NOT_NULL_MESSAGE(label, "G2.3: 裝置名稱標籤應被繪製");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SETTINGS_COLOR_DIM, label->color,
        "G2.3: 鎖定時裝置名稱應以 DIM 色置灰");
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("測試站"),
        "G2.3: 鎖定時不應顯示當前裝置名稱");
}

/**
 * G2.3b: device_name_locked = false → 正常白色 + 顯示當前名稱。
 *
 * 這個方向原本完全沒測，導致 is_device_name_locked 恆回 true（永遠置灰）
 * 的 bug 在 483 個綠燈下存活。
 */
static void test_g23_unlocked_renders_white_and_shows_name() {
    mock_fs_write(DEVICE_NAME_FILE, "測試站", strlen("測試站"));

    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* device_name_locked= */ false);

    const MockTextCall* label = mock_text_log_find("裝置名稱");
    TEST_ASSERT_NOT_NULL_MESSAGE(label, "G2.3b: 裝置名稱標籤應被繪製");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SETTINGS_COLOR_WHITE, label->color,
        "G2.3b: 未鎖定時裝置名稱應為正常白色");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("測試站"),
        "G2.3b: 未鎖定時應顯示當前裝置名稱");
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    RUN_TEST(test_g11_draw_settings_menu_items);
    RUN_TEST(test_g12_cursor_position_default);
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
    RUN_TEST(test_g19_wrap_down_from_vent_vol_to_battery_info);
    RUN_TEST(test_g19_wrap_down_from_battery_info_to_device_name);
    RUN_TEST(test_g19_wrap_up_from_device_name_to_battery_info);
    // G2.3: 案件中裝置名稱置灰 + 名稱顯示
    RUN_TEST(test_g23_locked_renders_dim_and_hides_name);
    RUN_TEST(test_g23_unlocked_renders_white_and_shows_name);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
