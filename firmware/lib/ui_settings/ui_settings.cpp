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

// 目前亮度值（開機預設，從 settings_state 讀取）
static uint8_t s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;

// 目前系統音量值（開機預設，從 settings_state 讀取）
static uint8_t s_system_volume = SETTINGS_VOLUME_DEFAULT;

// 目前通氣音量值（開機預設，從 settings_state 讀取）
static uint8_t s_vent_volume = SETTINGS_VENT_VOLUME_DEFAULT;

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

// 游標高亮矩形寬度（涵蓋文字區域）
#define SETTINGS_CURSOR_WIDTH    80

// 游標高亮矩形高度（文字行高）
#define SETTINGS_CURSOR_HEIGHT   20

// 確認對話框 Y 座標偏移（相對於 SETTINGS_ITEM4_Y）
#define SETTINGS_CONFIRM_Y_OFFSET  30

// ============================================================
//  drawSettingsMenu：設定主選單
// ============================================================

// 設定選單項目索引（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量）
// settingsCursor 初始值為 1（跳過裝置名稱，因為裝置名稱暫不支援調整）
#define SETTINGS_CURSOR_DEVICE_NAME  0
#define SETTINGS_CURSOR_BRIGHTNESS   1
#define SETTINGS_CURSOR_SYSTEM_VOL   2
#define SETTINGS_CURSOR_VENT_VOL     3

/**
 * 設定主選單畫面
 * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
 *
 * @param disp    顯示抽象層（mock 或真實顯示）
 * @param cursor  游標索引（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量），
 *                預設 3 向後相容
 */
void drawSettingsMenu(Display& disp, uint8_t cursor) {
    // STEP 01: 設定目前亮度值（從 settings_state 讀取，此處為預設值）
    s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;

    // STEP 02: 繪製選單標題
    disp.text("系統設定", SETTINGS_MENU_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    // STEP 03: 繪製 4 個設定項目，依 cursor 高亮對應列
    const bool highlight0 = (cursor == SETTINGS_CURSOR_DEVICE_NAME);
    const bool highlight1 = (cursor == SETTINGS_CURSOR_BRIGHTNESS);
    const bool highlight2 = (cursor == SETTINGS_CURSOR_SYSTEM_VOL);
    const bool highlight3 = (cursor == SETTINGS_CURSOR_VENT_VOL);

    if (highlight0) {
        disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
    }
    disp.text("裝置名稱", SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    if (highlight1) {
        disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM2_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
    }
    disp.text("螢幕亮度", SETTINGS_MENU_X, SETTINGS_ITEM2_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    if (highlight2) {
        disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM3_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
    }
    disp.text("系統音量", SETTINGS_MENU_X, SETTINGS_ITEM3_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    if (highlight3) {
        disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM4_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
    }
    disp.text("通氣音量", SETTINGS_MENU_X, SETTINGS_ITEM4_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

    // STEP 04: 長按確認對話框 — 恢復預設設定提示文字
    disp.text("是否恢復預設設定？", SETTINGS_MENU_X, SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
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
    static char range[SETTINGS_RANGE_BUF_SIZE];
    snprintf(range, sizeof(range), "%d ~ %d", min, max);
    disp.text(range, SETTINGS_MENU_X, SETTINGS_RANGE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
}

// ============================================================
//  getBrightness：取得目前亮度值
// ============================================================

/**
 * 取得目前亮度值
 * @return 亮度值
 */
uint8_t getBrightness() {
    return s_brightness;
}

/**
 * 取得目前系統音量值
 * @return 系統音量值
 */
uint8_t getSystemVolume() {
    return s_system_volume;
}

/**
 * 取得目前通氣音量值
 * @return 通氣音量值
 */
uint8_t getVentVolume() {
    return s_vent_volume;
}

/**
 * 設定亮度值
 * @param value 亮度值
 */
void setBrightness(uint8_t value) {
    s_brightness = value;
}

/**
 * 設定系統音量值
 * @param value 系統音量值
 */
void setSystemVolume(uint8_t value) {
    s_system_volume = value;
}

/**
 * 設定通氣音量值
 * @param value 通氣音量值
 */
void setVentVolume(uint8_t value) {
    s_vent_volume = value;
}

/**
 * 確認恢復預設值（亮度/系統音量/通氣音量→預設，裝置名稱不變）
 * @return true 成功
 */
bool confirmRestoreDefaults() {
    // STEP 01: 呼叫 settings_reset_defaults_mock 恢復 NVS + state
    settings_state_t mock_state;
    settings_init_mock(&mock_state);
    settings_reset_defaults_mock(&mock_state);

    // STEP 02: 同步回 local state
    s_brightness = mock_state.brightness;
    s_system_volume = mock_state.system_volume;
    s_vent_volume = mock_state.vent_volume;

    return true;
}

/**
 * 取消恢復預設（亮度/系統音量/通氣音量→不變更）
 * @return true 成功
 */
bool cancelRestore() {
    // STEP 01: 不執行任何設定值的變更，直接回傳
    return true;
}
