// EMS DoseSync Pro — Wave 1: 系統設定 UI 實作
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.2
//
// 設計：
//   - drawSettingsMenu：繪製 4 項目設定選單
//   - drawSettingEditor：繪製設定值編輯器
//   - 使用 Display 抽象層，ESP32 端換成真實 display 物件

#include "ui_settings.h"
#include "ems_settings.h"
#include <stdio.h>

// ============================================================
//  顯示常數（座標 + 字型 + 顏色）
// ============================================================

// X 座標：左邊距
#define SETTINGS_MENU_X          10

// Y 座標：標題位置
#define SETTINGS_TITLE_Y         10

// Y 座標：設定項目位置（4 項目等間距）
#define SETTINGS_ITEM1_Y         30   // 裝置名稱
#define SETTINGS_ITEM2_Y         70   // 螢幕亮度
#define SETTINGS_ITEM3_Y        110   // 系統音量
#define SETTINGS_ITEM4_Y        150   // 通氣音量

// Y 座標：編輯器位置
#define SETTINGS_VALUE_Y         50   // 數值顯示
#define SETTINGS_RANGE_Y         90   // 範圍提示

// 字型大小（pt）
#define SETTINGS_FONT_SIZE       2

// 顏色：白色（RGB565）
#define SETTINGS_COLOR_WHITE     0xFFFF

// 緩衝區大小
#define SETTINGS_VALUE_BUF_SIZE  8
#define SETTINGS_RANGE_BUF_SIZE 16

// ============================================================
//  drawSettingsMenu：設定主選單
// ============================================================

/**
 * 設定主選單畫面
 * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
 *
 * @param disp 顯示抽象層（mock 或真實顯示）
 */
void drawSettingsMenu(Display& disp) {
    // STEP 01: 繪製選單標題
    disp.text("系統設定", SETTINGS_MENU_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    // STEP 02: 繪製 4 個設定項目（Y: 30, 70, 110, 150）
    disp.text("裝置名稱", SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    disp.text("螢幕亮度", SETTINGS_MENU_X, SETTINGS_ITEM2_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    disp.text("系統音量", SETTINGS_MENU_X, SETTINGS_ITEM3_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    disp.text("通氣音量", SETTINGS_MENU_X, SETTINGS_ITEM4_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
}

// ============================================================
//  drawSettingEditor：設定值編輯器
// ============================================================

/**
 * 設定子畫面（亮度/音量調整）
 *
 * @param disp    顯示抽象層
 * @param title   設定名稱（「螢幕亮度」等）
 * @param value   當前值
 * @param min     最小值
 * @param max     最大值
 */
void drawSettingEditor(Display& disp, const char* title, uint8_t value, uint8_t min, uint8_t max) {
    // STEP 01: 繪製標題
    disp.text(title, SETTINGS_MENU_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    // STEP 02: 繪製當前值
    char buf[SETTINGS_VALUE_BUF_SIZE];
    snprintf(buf, sizeof(buf), "%d", value);
    disp.text(buf, SETTINGS_MENU_X, SETTINGS_VALUE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    // STEP 03: 繪製範圍提示
    char range[SETTINGS_RANGE_BUF_SIZE];
    snprintf(range, sizeof(range), "%d ~ %d", min, max);
    disp.text(range, SETTINGS_MENU_X, SETTINGS_RANGE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
}
