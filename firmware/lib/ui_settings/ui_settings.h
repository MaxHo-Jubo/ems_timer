// EMS DoseSync Pro — Wave 1: 系統設定 UI 函式
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.1 ~ §2.1.2
//
// 設計：
//   - drawSettingsMenu(Display&)：繪製設定主選單（5 項目）
//   - drawSettingEditor(Display&, ...)：繪製設定編輯器
//   - 使用 Display 抽象層（display_abstraction.h），native test 可 mock

#pragma once

#include "display_abstraction.h"
#include "ems_storage_logic.h"

// 設定選單項目索引。定義於此（而非 .cpp）供 input_handler / main.cpp 共用，
// 避免游標語意退化成散落各處的裸數字（故意不在文字裡列一份數值範圍，這裡只是
// 常數集中定義處，不是新增項目唯一要改的地方——加一項通常還要動：
// kSettingsAdjustableItems[]／Y 座標常數〔ui_settings.cpp〕；下方的
// SETTINGS_MENU_COUNT〔本檔，wrap-around 用，漏改會被 static_assert 擋下來〕；
// 可調值項目還要動 BTN_PRIMARY 的判斷範圍〔input_handler.cpp〕）。
#define SETTINGS_CURSOR_DEVICE_NAME  0
#define SETTINGS_CURSOR_BRIGHTNESS   1
#define SETTINGS_CURSOR_SYSTEM_VOL   2
#define SETTINGS_CURSOR_VENT_VOL     3
// Phase H：電池資訊（導覽項）。本 task（Task 12）只新增選單顯示與 UP/DOWN 捲動；
// 子畫面與按主鍵進入的行為尚未接線，目前按主鍵是 no-op，由 Task 13 接線。
#define SETTINGS_CURSOR_BATTERY_INFO 4

// 設定選單項目總數。UP/DOWN 捲動 wrap-around 用（見 wrapSettingsCursor()）。
// 原本只在 input_handler.cpp 定義，搬到這裡與其他游標常數放一起，native test
// 才能拿到跟正式路徑同一份常數值，不必在測試裡另外複製一份數字。
#define SETTINGS_MENU_COUNT 5

// UP/DOWN 傳給 wrapSettingsCursor() 的方向增量。具名常數取代呼叫端裸寫 -1/+1，
// 語意（往上一項 vs 往下一項）不必靠背下數字符號才看得懂。
#define SETTINGS_CURSOR_DELTA_UP    (-1)  // 往上一項（wrap 到最後一項）
#define SETTINGS_CURSOR_DELTA_DOWN  (+1)  // 往下一項（wrap 回第一項）

/**
 * 設定選單游標 UP/DOWN 捲動的 wrap-around 計算（純函式）。
 *
 * input_handler.cpp 的 src/ 不編進 native build（platformio.ini `build_src_filter =
 * -<*>`），UP/DOWN 按鍵分派本身無法直接被 native test 呼叫到；這個函式把原本內嵌在
 * 分派邏輯裡的 `(cursor + count ± 1) % count` 算式原地抽出，行為數學上完全等價
 * （下方 STEP 01 對 delta=-1／+1 展開後與原公式逐項相同，只是加了一個對 count 取模
 * 不影響結果的 `+count`，讓正負 delta 共用同一行不必分支），生產路徑呼叫的就是這個
 * 函式本身，native test 測到的是真正的邊界行為，不是另外複製一份公式。
 *
 * count 不開放呼叫端傳入，直接讀本檔的 SETTINGS_MENU_COUNT——選單項目數是這個模組
 * 的內部不變量，交給每個呼叫端各自傳同一個值等於要求大家都不傳錯／不傳舊值，傳 0
 * 還會整除歸零；不變量收在函式內部才有單一出處，呼叫端不可能傳出不一致的項目數。
 *
 * @param cursor 目前游標值，範圍 [0, SETTINGS_MENU_COUNT - 1]
 * @param delta  SETTINGS_CURSOR_DELTA_DOWN（下一項）或 SETTINGS_CURSOR_DELTA_UP（上一項）
 * @return       wrap 後的新游標值，範圍 [0, SETTINGS_MENU_COUNT - 1]
 */
inline uint8_t wrapSettingsCursor(uint8_t cursor, int8_t delta) {
    // STEP 01: (cursor + count + delta) % count —— 加一個對 count 取模不影響結果的
    //   `+count`，讓 delta=-1（cursor=0 時避免算出負值）跟 delta=+1 共用同一條算式。
    return (uint8_t)((cursor + SETTINGS_MENU_COUNT + delta) % SETTINGS_MENU_COUNT);
}

// 文字顏色（RGB565）。定義於此供 native test 驗證「置灰 vs 正常」的實際繪製顏色，
// 否則測試只能斷言「有畫出文字」，無法分辨置灰與否。
#define SETTINGS_COLOR_WHITE  0xFFFF
#define SETTINGS_COLOR_DIM    0x6B4D  // 暗灰：項目不可操作時的置灰色

// 註：fix round 1 曾在此加過 SETTINGS_COLOR_HIGHLIGHT_TEXT（選取時改黑字），
// 想解決游標高亮框（80×20）蓋不住整個渲染文字（約 192×48）造成的局部白底白字。
// 但那個修法讓文字其餘 ~83% 面積變成黑字疊黑螢幕背景，比原本更看不清楚，
// fix round 3 已撤銷並移除這個常數。正確修法是仿 ui_screens.cpp drawMainMenu()
// 改成整列高亮，超出本 task 範圍，見 ui_settings.cpp 的 kSettingsAdjustableItems
// doc comment 與 handover §3-A8。

/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量 / 電池資訊
  *
  * @param disp   顯示抽象層（mock 或真實顯示）
  * @param cursor 游標索引（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量 / 4=電池資訊），
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
