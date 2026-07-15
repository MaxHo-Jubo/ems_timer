// EMS DoseSync Pro — Wave 1: 系統設定 UI
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1
//
// 設計：
//   - Display 抽象層：以函式指標封裝 drawText，native test 可 mock
//   - ESP32 端：drawText 指向 LovyanGFX 實作
//   - native test：drawText 指向 mock，記錄 draw call

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstring>

// ============================================================
//  Display 抽象層（native test 可 mock）
// ============================================================

/**
 * 顯示抽象層：以函式指標封裝 drawText
 *
 * 使用方式：
 *   - ESP32 端：disp.text = &real_drawText;
 *   - native test：disp.text = &mock_drawText;
 */
struct Display {
    void (*text)(const char* str, int16_t x, int16_t y, int16_t fontsize, uint32_t color);
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
//  Native test 支援：mock display
// ============================================================

/**
 * Mock draw：記錄最後一次 drawText 呼叫
 */
static const char* mock_last_text = nullptr;
static int16_t mock_last_x = 0;
static int16_t mock_last_y = 0;
static int16_t mock_last_fontsize = 0;
static uint32_t mock_last_color = 0;

static void mock_drawText(const char* str, int16_t x, int16_t y, int16_t fontsize, uint32_t color) {
    mock_last_text = str;
    mock_last_x = x;
    mock_last_y = y;
    mock_last_fontsize = fontsize;
    mock_last_color = color;
}

/**
 * 建立 mock Display 物件
 */
static Display create_mock_display() {
    Display disp;
    disp.text = mock_drawText;
    return disp;
}

/**
 * 重置 mock 狀態
 */
static void mock_display_reset() {
    mock_last_text = nullptr;
    mock_last_x = 0;
    mock_last_y = 0;
    mock_last_fontsize = 0;
    mock_last_color = 0;
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
