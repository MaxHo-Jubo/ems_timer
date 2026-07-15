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

/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
  *
  * @param disp       顯示抽象層（mock 或真實顯示）
  * @param cursor     游標索引（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量），
  *                   預設 3 向後相容
  * @param case_mode  當前案件模式（用於判斷裝置名稱是否置灰），預設非案件（2 = 無案件）
  */
 void drawSettingsMenu(Display& disp, uint8_t cursor = 3, ems::CaseMode case_mode = (ems::CaseMode)2);

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
  * 判斷案件進行中：OHCA / Training 案件執行中 → true（裝置名稱應置灰）
  *
  * 純函式：只讀 case_mode 參數，不讀任何全域狀態。
  * 讓 native test 可直傳 CaseMode 參數驗證，不用碰 lib 內部全域。
  *
  * @param case_mode 當前案件模式（CASE_MODE_OHCA / CASE_MODE_TRAINING / 其他）
  * @return true 案件進行中，裝置名稱項目應置灰且主鍵不可進入
  */
 bool is_device_name_locked(ems::CaseMode case_mode);

/**
  * 裝置名稱子畫面：顯示「請連接 App 設定裝置名稱」
  *
  * 裝置端不做中文輸入，僅提示使用者透過 App 設定。
  *
  * @param disp    顯示抽象層
  */
 void show_device_name_sub(Display& disp);
