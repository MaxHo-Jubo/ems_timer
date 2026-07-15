// EMS DoseSync Pro — Wave 1: 系統設定 UI
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1
//
// 設計：
//   - Display 抽象層：以函式指標封裝 drawText / fillRect，native test 可 mock
//   - ESP32 端：drawText 指向 LovyanGFX 實作
//   - native test：drawText / fillRect 指向 mock，記錄 draw call
//
// ⚠️ mock 函式以 extern 宣告（非 static），確保所有 translation unit 共享同一份 mock state

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstring>

// ============================================================
//  Display 抽象層（native test 可 mock）
// ============================================================

/**
 * 顯示抽象層：以函式指標封裝 drawText + fillRect（游標高亮用）
 *
 * 使用方式：
 *   - ESP32 端：disp.text = &real_drawText; disp.fill_rect = nullptr;
 *   - native test：disp.text = mock_drawText; disp.fill_rect = mock_fillRect;
 */
struct Display {
    void (*text)(const char* str, int16_t x, int16_t y, int16_t fontsize, uint32_t color);
    void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
};

// ============================================================
//  ESP32 端實作（ARDUINO 環境）
// ============================================================

#ifdef ARDUINO

#include <LovyanGFX.hpp>

/**
 * 真實 drawText：封裝 LovyanGFX 呼叫
 * @param str      文字
 * @param x        X 座標
 * @param y        Y 座標
 * @param fontsize 字型大小（pt）
 * @param color    顏色（RGB565）
 */
static void real_drawText(const char* str, int16_t x, int16_t y, int16_t fontsize, uint32_t color) {
    // LovyanGFX: setCursor(x, y) + setTextSize(fontsize) + print(str)
    // 這裡需要外部注入 display 物件，不直接依賴全域
}

#endif  // ARDUINO

// ============================================================
//  Native test 支援：mock display（extern，跨 translation unit 共享）
// ============================================================

/**
 * Mock fillRect：記錄最後一次 fillRect 呼叫（用於游標高亮驗證）
 */
extern int16_t mock_last_fill_x;
extern int16_t mock_last_fill_y;
extern int16_t mock_last_fill_w;
extern int16_t mock_last_fill_h;
extern uint32_t mock_last_fill_color;

/**
 * Mock fillRect 實作
 */
static void mock_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color) {
    mock_last_fill_x = x;
    mock_last_fill_y = y;
    mock_last_fill_w = w;
    mock_last_fill_h = h;
    mock_last_fill_color = color;
}

/**
 * Mock draw：記錄最後一次 drawText 呼叫
 */
extern const char* mock_last_text;
extern int16_t mock_last_x;
extern int16_t mock_last_y;
extern int16_t mock_last_fontsize;
extern uint32_t mock_last_color;

/**
 * Mock drawText 實作
 */
static void mock_drawText(const char* str, int16_t x, int16_t y, int16_t fontsize, uint32_t color) {
    mock_last_text = str;
    mock_last_x = x;
    mock_last_y = y;
    mock_last_fontsize = fontsize;
    mock_last_color = color;
}

/**
 * 建立 mock Display 物件（fillRect + text 都指向 mock）
 */
static Display create_mock_display() {
    Display disp;
    disp.text = mock_drawText;
    disp.fill_rect = mock_fillRect;
    return disp;
}

/**
 * 重置 mock 狀態（draw + fillRect）
 */
static void mock_display_reset() {
    mock_last_text = nullptr;
    mock_last_x = 0;
    mock_last_y = 0;
    mock_last_fontsize = 0;
    mock_last_color = 0;
    mock_last_fill_x = 0;
    mock_last_fill_y = 0;
    mock_last_fill_w = 0;
    mock_last_fill_h = 0;
    mock_last_fill_color = 0;
}

/**
 * 取得 mock 最後記錄的文字
 */
static const char* mock_get_last_text() {
    return mock_last_text;
}

/**
 * 取得 mock 最後記錄的 X 座標
 */
static int16_t mock_get_last_x() {
    return mock_last_x;
}

/**
 * 取得 mock 最後記錄的 Y 座標
 */
static int16_t mock_get_last_y() {
    return mock_last_y;
}

/**
 * 取得 mock 最後 fillRect 的 X 座標（游標高亮驗證用）
 */
static int16_t mock_get_last_fill_x() {
    return mock_last_fill_x;
}

/**
 * 取得 mock 最後 fillRect 的 Y 座標（游標高亮驗證用）
 */
static int16_t mock_get_last_fill_y() {
    return mock_last_fill_y;
}

/**
 * 取得 mock 最後 fillRect 的寬度（游標高亮驗證用）
 */
static int16_t mock_get_last_fill_w() {
    return mock_last_fill_w;
}

/**
 * 取得 mock 最後 fillRect 的高度（游標高亮驗證用）
 */
static int16_t mock_get_last_fill_h() {
    return mock_last_fill_h;
}
