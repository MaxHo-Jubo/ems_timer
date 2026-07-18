// EMS DoseSync Pro — Wave 1 + Wave 2 Unit Test: 系統設定 UI
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.6 / §2.2.3 / §2.2.5
// 涵蓋：G1.1 ~ G1.8 / G2.3
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

// ----- G1.1: drawSettingsMenu 顯示 4 項目 -----

/**
 * G1.1: drawSettingsMenu → 標題與 4 個項目標籤都被繪製。
 *
 * 原斷言為「最後一項應為確認對話框文字」——那是因為確認對話框被無條件畫出，
 * 與本測試宣稱的「顯示 4 項目」無關。改為逐項查表斷言。
 */
static void test_g11_draw_settings_menu_items() {
    drawSettingsMenu(g_disp);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統設定"), "G1.1: 應繪製選單標題");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("裝置名稱"), "G1.1: 應繪製項目「裝置名稱」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("螢幕亮度"), "G1.1: 應繪製項目「螢幕亮度」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("系統音量"), "G1.1: 應繪製項目「系統音量」");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("通氣音量"), "G1.1: 應繪製項目「通氣音量」");
}

// ----- G1.2: 游標位置 -----

/** G1.2: 游標高亮 → fillRect 繪製於最後一項（Y=150, cursor=3 預設） */
static void test_g12_cursor_position_default() {
    drawSettingsMenu(g_disp);

    // 游標高亮矩形應存在（fillRect 應被呼叫）
    TEST_ASSERT_EQUAL_INT16_MESSAGE(10, mock_get_last_fill_x(),
        "G1.2: 游標 fillRect X 座標應從左邊距開始");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, mock_get_last_fill_y(),
        "G1.2: 游標 fillRect Y 座標應為 150（最後一項通氣音量位置）");
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

// ----- G1.8: drawSettingEditor 繪製 -----

/** G1.8: drawSettingEditor 繪製標題、數值、範圍 */
static void test_g18_draw_setting_editor() {
    drawSettingEditor(g_disp, "螢幕亮度", 3, 1, 5);

    // 驗證有 draw 文字（最後一項：範圍提示）
    const char* text = mock_get_last_text();
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "G1.8: drawSettingEditor 應呼叫 drawText 繪製文字");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1 ~ 5", text, "G1.8: 最後一項應為範圍提示");
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
    RUN_TEST(test_g18_draw_setting_editor);
    // G2.3: 案件中裝置名稱置灰 + 名稱顯示
    RUN_TEST(test_g23_locked_renders_dim_and_hides_name);
    RUN_TEST(test_g23_unlocked_renders_white_and_shows_name);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
