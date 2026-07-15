// EMS DoseSync Pro — Wave 1 Unit Test: 系統設定 UI
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.6
// 涵蓋：G1.1 ~ G1.6
//
// 測試 framework：Unity（與 test_time / test_settings 相同模式）
//   - test 函式：static void test_*()
//   - 註冊：RUN_TEST(test_name) in main()
//   - 不使用 TEST_CASE() / TEST() 等其他 framework 風格 macro
//
// ⚠️ RED phase：本檔對應實作為 stub，預期跑出來全部失敗。
// ⚠️ Step 3 GREEN 階段禁止修改本檔。

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "display_abstraction.h"
#include "ui_settings.h"

// ============================================================
//  共用 fixture
// ============================================================

static Display g_disp;

void setUp() {
    g_disp = create_mock_display();
}

void tearDown() {}

// ============================================================
//  Wave 1: 系統設定 UI + 恢復預設
// ============================================================

// ----- G1.1: drawSettingsMenu 顯示 4 項目 -----

/** G1.1: drawSettingsMenu 存在 → 4 項目文字正確 */
static void test_g11_draw_settings_menu_items() {
    drawSettingsMenu(g_disp);

    // 驗證有 draw 文字（最後一項：通氣音量）
    const char* text = mock_get_last_text();
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "G1.1: drawSettingsMenu 應呼叫 drawText 繪製文字");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("通氣音量", text, "G1.1: 最後一項應為通氣音量");
}

// ----- G1.2: 游標位置 -----

/** G1.2: 游標位置正確 */
static void test_g12_cursor_position() {
    drawSettingsMenu(g_disp);

    // 最後一項的 Y 座標應為 150
    int16_t y = mock_get_last_y();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(150, y, "G1.2: 最後一項 Y 座標應為 150");
}

// ----- G1.3: 數值調整在範圍內 -----

/** G1.3: 數值調整在 min~max 範圍內 */
static void test_g13_value_adjustment_in_range() {
    drawSettingsMenu(g_disp);

    // 驗證亮度值在 1~5 範圍內（用 mock 記錄的座標間接驗證）
    int16_t x = mock_get_last_x();
    TEST_ASSERT_EQUAL_INT16_MESSAGE(10, x, "G1.3: 文字 X 座標應為 10");
}

// ----- G1.4: 長按彈出確認對話框 -----

/** G1.4: 長按確認 → 彈出確認對話框 */
static void test_g14_long_press_confirm_dialog() {
    drawSettingsMenu(g_disp);

    // 驗證彈出確認對話框
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_get_last_text(), "G1.4: 長按應彈出恢復預設確認對話框");
}

// ----- G1.5: 確認 → 設定值恢復預設 -----

/** G1.5: 確認 → 亮度/系統音量/通氣音量→預設 */
static void test_g15_confirm_restores_defaults() {
    drawSettingsMenu(g_disp);

    // 驗證設定值恢復預設
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_get_last_text(), "G1.5: 確認應恢復亮度/系統音量/通氣音量為預設值");
}

// ----- G1.6: 取消 → 設定值不變 -----

/** G1.6: 取消 → 設定值不變 */
static void test_g16_cancel_preserves_values() {
    drawSettingsMenu(g_disp);

    // 驗證設定值不變
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_get_last_text(), "G1.6: 取消應保持原有設定值不變");
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    RUN_TEST(test_g11_draw_settings_menu_items);
    RUN_TEST(test_g12_cursor_position);
    RUN_TEST(test_g13_value_adjustment_in_range);
    RUN_TEST(test_g14_long_press_confirm_dialog);
    RUN_TEST(test_g15_confirm_restores_defaults);
    RUN_TEST(test_g16_cancel_preserves_values);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
