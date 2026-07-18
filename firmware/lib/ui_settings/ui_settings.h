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
#include "ems_storage_logic.h"

// 設定選單項目索引。定義於此（而非 .cpp）供 input_handler / main.cpp 共用，
// 避免游標語意退化成散落各處的裸數字 0/1/2/3。
#define SETTINGS_CURSOR_DEVICE_NAME  0
#define SETTINGS_CURSOR_BRIGHTNESS   1
#define SETTINGS_CURSOR_SYSTEM_VOL   2
#define SETTINGS_CURSOR_VENT_VOL     3

// 文字顏色（RGB565）。定義於此供 native test 驗證「置灰 vs 正常」的實際繪製顏色，
// 否則測試只能斷言「有畫出文字」，無法分辨置灰與否。
#define SETTINGS_COLOR_WHITE  0xFFFF
#define SETTINGS_COLOR_DIM    0x6B4D  // 暗灰：項目不可操作時的置灰色

/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
  *
  * @param disp   顯示抽象層（mock 或真實顯示）
  * @param cursor 游標索引（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量），
  *               預設 3 向後相容
  * @param device_name_locked 裝置名稱是否鎖定（true = 置灰且不顯示當前名稱）。
  *               由呼叫端以 storage_has_unsynced_case() 算好再傳入，對齊
  *               DisplaySnapshot「衍生值呼叫端先算，lib 不依賴 runtime 狀態」的原則。
  *               判準與理由見 docs/phase-g-system-settings-plan.md §2.2.5。
  * @param restore_confirm 恢復預設確認對話框是否顯示中（true 才畫出提示文字）
  */
 void drawSettingsMenu(Display& disp,
                       uint8_t cursor = SETTINGS_CURSOR_VENT_VOL,
                       bool device_name_locked = false,
                       bool restore_confirm = false);

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

/**
 * 取得目前系統音量值（測試用）
 * @return 系統音量值
 */
uint8_t getSystemVolume();

/**
 * 取得目前通氣音量值（測試用）
 * @return 通氣音量值
 */
uint8_t getVentVolume();

/**
 * 設定亮度值（測試用）
 * @param value 亮度值
 */
void setBrightness(uint8_t value);

/**
 * 設定系統音量值（測試用）
 * @param value 系統音量值
 */
void setSystemVolume(uint8_t value);

/**
 * 設定通氣音量值（測試用）
 * @param value 通氣音量值
 */
void setVentVolume(uint8_t value);

/**
  * 取消恢復預設（亮度/系統音量/通氣音量→不變更）
  * @return true 成功
  */
 bool cancelRestore();

/**
  * 裝置名稱子畫面：顯示「請連接 App 設定裝置名稱」
  *
  * 裝置端不做中文輸入，僅提示使用者透過 App 設定。
  *
  * @param disp    顯示抽象層
  */
 void show_device_name_sub(Display& disp);
