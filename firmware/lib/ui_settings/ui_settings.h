// EMS DoseSync Pro — Wave 1: 系統設定 UI 函式
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.1 ~ §2.1.2
//
// 設計：
//   - drawSettingsMenu(Display&)：繪製設定主選單（4 項目）
//   - drawSettingEditor(Display&, ...)：繪製設定編輯器
//   - 使用 Display 抽象層（display_abstraction.h），native test 可 mock

#pragma once

#include "display_abstraction.h"

/**
 * 設定主選單畫面
 * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
 *
 * @param disp 顯示抽象層（mock 或真實顯示）
 */
void drawSettingsMenu(Display& disp);

/**
 * 設定子畫面（亮度/音量調整）
 *
 * @param disp    顯示抽象層
 * @param title   設定名稱（「螢幕亮度」等）
 * @param value   當前值
 * @param min     最小值
 * @param max     最大值
 */
void drawSettingEditor(Display& disp, const char* title, uint8_t value, uint8_t min, uint8_t max);

/**
 * 取得目前亮度值（測試用）
 * @return 亮度值
 */
uint8_t getBrightness();
