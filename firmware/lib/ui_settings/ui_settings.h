// EMS DoseSync Pro — Wave 1: 系統設定 UI 函式
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.1 ~ §2.1.2
//
// 設計：
//   - drawSettingsMenu(Display&, cursor, scroll_offset, device_name_locked,
//     restore_confirm)：繪製設定主選單（7 項目，捲動顯示一頁 5 項）。五個
//     參數皆無預設值、呼叫端必須明確傳入，理由見該函式宣告的 @param cursor。
//   - drawToggleEditor(Display&, title, enabled)：繪製開/關切換編輯器
//   - 使用 Display 抽象層（display_abstraction.h），native test 可 mock

#pragma once

#include "display_abstraction.h"
#include "ems_storage_logic.h"
#include "ui_scroll.h"

// 設定選單項目索引。定義於此（而非 .cpp）供 input_handler / main.cpp 共用，
// 避免游標語意退化成散落各處的裸數字（故意不在文字裡列一份數值範圍，這裡只是
// 常數集中定義處，不是新增項目唯一要改的地方——加一項通常還要動：
// kSettingsMenuItems[]〔ui_settings.cpp〕；下方的
// SETTINGS_MENU_COUNT〔本檔，wrap-around 用，漏改會被 static_assert 擋下來〕；
// 可調值項目還要動 BTN_PRIMARY 的判斷範圍〔input_handler.cpp〕）。
// 註（2026-09-06 工程決策 1）：原 SETTINGS_CURSOR_BRIGHTNESS（值 1）已移除，其後
// 各項一律往前遞補一號。根因是背光（BL）焊死在 3.3V 常亮、沒接任何可控 GPIO
// （原規劃的 GPIO 1 已被 TFT DC 佔走），setBrightness() 只更新 s_brightness、
// 沒有任何 PWM 驅動硬體，選單留著只會讓使用者調了卻沒反應。NVS 欄位與
// getBrightness()／setBrightness() 保留（見本檔下方該兩個宣告的說明）。
#define SETTINGS_CURSOR_DEVICE_NAME  0
#define SETTINGS_CURSOR_SYSTEM_VOL   1
#define SETTINGS_CURSOR_VENT_VOL     2
// Phase H：電池資訊（唯讀導覽項，非可調值）。游標停在此項時按主鍵
// （input_handler.cpp）進入 drawBatteryInfo() 子畫面，按返回鍵離開，
// 見 settingsBatteryInfoMode（Task 13 接線，Task 12 只新增選單顯示與 UP/DOWN 捲動）。
#define SETTINGS_CURSOR_BATTERY_INFO 3
// Impl-Phase G：App 連線設定／Type-C 連線（SoT §19.1 選單順序要求的 placeholder，
// 未來規劃選到後顯示「尚未實作」〔drawPlaceholder()〕，接線在 input_handler.cpp
// BTN_PRIMARY 分派，見 Task 3；本 task 只佔選單位置，主鍵目前對這兩項無反應）
#define SETTINGS_CURSOR_APP_CONN     4
#define SETTINGS_CURSOR_TYPEC_CONN   5
// Impl-Phase G：裝置資訊（唯讀導覽項，比照電池資訊的既有 pattern）
#define SETTINGS_CURSOR_DEVICE_INFO  6

// 設定選單項目總數。UP/DOWN 捲動 wrap-around 用（見 wrapSettingsCursor()）。
// 原本只在 input_handler.cpp 定義，搬到這裡與其他游標常數放一起，native test
// 才能拿到跟正式路徑同一份常數值，不必在測試裡另外複製一份數字。
#define SETTINGS_MENU_COUNT 7

// 選單一次可見列數（超過此數量需捲動）。與既有 HISTORY_VISIBLE_ROWS
// （app_globals.h）同值，維持畫面資訊密度一致；7 項選單一頁顯示其中 5 項。
#define SETTINGS_VISIBLE_ROWS 5

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

/**
 * 游標與捲動視窗的成對更新結果。cursor／scroll_offset 兩個欄位必須一起讀寫，
 * 不拆開使用——這正是 advanceSettingsCursorAndScroll() 存在的理由（見下方）。
 */
struct SettingsCursorScroll {
    uint8_t  cursor;
    uint16_t scroll_offset;
};

/**
 * 設定選單 UP/DOWN 按鍵分派的游標＋捲動視窗成對更新（純函式）。
 *
 * 把 wrapSettingsCursor() 的 wrap-around 位移與 clampScrollOffset()（ui_scroll.h）
 * 的捲動視窗跟隨合併成一次呼叫，回傳新的一對值。存在理由：c980927 CRITICAL 的
 * 根因是這兩者原本各自 inline 在 input_handler.cpp 的 BTN_UP/BTN_DOWN 分支，
 * 只改了項目數（SETTINGS_MENU_COUNT）卻沒人記得同步接上捲動視窗；抽成單一
 * 純函式後，呼叫端只能拿到「已經配對好」的結果，沒有機會只更新其中一個。
 * input_handler.cpp 屬 src/，native build 排除（platformio.ini
 * `build_src_filter = -<*>`），本函式抽到這裡讓 native test 能直接涵蓋這段
 * 配對邏輯，不必只靠 input_handler.cpp 分支旁的註解警告（2026-09-02 codex Tier
 * 3 補跑 review 對 89917d5 抓到的 IMPORTANT：這段配對邏輯先前完全沒有測試涵蓋）。
 *
 * @param cursor        目前游標值（SETTINGS_CURSOR_*）
 * @param scroll_offset 目前捲動視窗起點
 * @param delta         SETTINGS_CURSOR_DELTA_UP 或 SETTINGS_CURSOR_DELTA_DOWN
 * @return              wrap 後的新游標與跟隨新游標算出的新捲動視窗起點
 */
inline SettingsCursorScroll advanceSettingsCursorAndScroll(uint8_t cursor, uint16_t scroll_offset, int8_t delta) {
    // STEP 01: 先算 wrap 後的新游標
    SettingsCursorScroll result;
    result.cursor = wrapSettingsCursor(cursor, delta);
    // STEP 02: 用新游標與既有 scroll_offset 算出新的捲動視窗起點
    result.scroll_offset = clampScrollOffset(result.cursor, scroll_offset, SETTINGS_VISIBLE_ROWS);
    return result;
}

// 開/關兩態設定的顯示字串。設定編輯器（drawToggleEditor）與通氣畫面
// （ui_screens.cpp drawVentPre / drawVentStandalone）共用同一份——同一個概念在
// 兩處各寫一份字面值必然分歧，而且這些中文字都要進 .vlw 字型子集
// （scripts/regen_vlw.sh），集中一處才數得清用到哪些字。
#define SETTINGS_TOGGLE_ON_TEXT   "開"
#define SETTINGS_TOGGLE_OFF_TEXT  "關"

/**
 * 取得開/關狀態的顯示字串。
 *
 * @param enabled true = 開，false = 關
 * @return 對應的顯示字串（字面值，呼叫端不需複製或釋放）
 */
inline const char* settingsToggleLabel(bool enabled) {
    // STEP 01: 依狀態回傳對應的顯示字串（兩者都是字面值，生命週期同程式）
    return enabled ? SETTINGS_TOGGLE_ON_TEXT : SETTINGS_TOGGLE_OFF_TEXT;
}

// 文字顏色（RGB565）。定義於此供 native test 驗證「置灰 vs 正常」的實際繪製顏色，
// 否則測試只能斷言「有畫出文字」，無法分辨置灰與否。
#define SETTINGS_COLOR_WHITE  0xFFFF
#define SETTINGS_COLOR_DIM    0x6B4D  // 暗灰：項目不可操作時的置灰色
#define SETTINGS_COLOR_SELECTED_TEXT 0x0000  // 選取列反白文字色，對齊 app_globals.h 的 COLOR_BG

// 註：fix round 1 曾加過 SETTINGS_COLOR_HIGHLIGHT_TEXT（選取時改黑字），想解決
// 游標高亮框（80×20）蓋不住整個渲染文字（約 192×48）造成的局部白底白字。但那個
// 修法讓文字其餘 ~83% 面積變成黑字疊黑螢幕背景，比原本更看不清楚，fix round 3
// 撤銷並移除該常數，備註「正確修法是仿 drawMainMenu() 改成整列高亮，超出本 task
// 範圍」。2026-09-05 系統設定 TFT 版式對齊修正已把游標高亮改成滿版寬度（對齊
// drawMainMenu() 的 fillRect(0, y, SCREEN_W, ...)），fix round 1 的顧慮不再成立，
// 選取列文字改回黑字（SETTINGS_COLOR_SELECTED_TEXT）不會再有局部黑字疊黑底問題。

/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 系統音量 / 通氣音量 / 電池資訊 / App連線設定 /
  *       Type-C連線 / 裝置資訊（SoT §19.1 八項扣除 2026-09-06 移除的螢幕亮度＝7 項）
  *
  * @param disp   顯示抽象層（mock 或真實顯示）
  * @param cursor 游標索引（SETTINGS_CURSOR_*）——不提供預設值，呼叫端必須明確
  *               傳入。移除預設值前，main.cpp 曾有一個舊的 4 引數呼叫點
  *               （disp/cursor/device_name_locked/restore_confirm）；插入
  *               scroll_offset 成為新簽名第 3 位後，這個舊呼叫仍能編譯過
  *               （bool→uint16_t 隱式轉換 + 尾端預設值），但實際靜默錯位：
  *               device_name_locked 的值誤綁到 scroll_offset、restore_confirm
  *               的值誤綁到 device_name_locked，新的 restore_confirm 則永遠
  *               吃不到呼叫端的值、只會是預設 false（Task 2 codex Tier 3
  *               review 抓到的 5 CRITICAL 之一）；拿掉預設值讓這類錯位在
  *               編譯期就會因缺少必要引數而報錯，不會再靜默通過（Phase G
  *               全分支整合 review 額外抓到的 IMPORTANT，同一個地雷機制，
  *               趁還沒再插入新參數前先拆除）
  * @param scroll_offset 捲動視窗起點（顯示清單第幾項開始）。由呼叫端以
  *               clampScrollOffset() 算好再傳入（見 ui_scroll.h）；正式呼叫點
  *               （main.cpp）已接線，跟隨 settingsCursor 由 UP/DOWN 分派同步更新
  *               （input_handler.cpp）。不提供預設值，理由同 cursor。
  * @param device_name_locked 裝置名稱是否鎖定（true = 置灰且不顯示當前名稱）。
  *               由呼叫端以 storage_has_unsynced_case() 算好再傳入，對齊
  *               DisplaySnapshot「衍生值呼叫端先算，lib 不依賴 runtime 狀態」的原則。
  *               判準與理由見 docs/phase-g-system-settings-plan.md §2.2.5。
  *               不提供預設值，理由同 cursor。
  * @param restore_confirm 恢復預設確認對話框是否顯示中（true 才畫出提示文字）。
  *               不提供預設值，理由同 cursor。
  */
 void drawSettingsMenu(Display& disp,
                       uint8_t cursor,
                       uint16_t scroll_offset,
                       bool device_name_locked,
                       bool restore_confirm);

/**
 * 設定子畫面：開/關切換編輯器。
 *
 * 取代原本的數值編輯器 drawSettingEditor(disp, title, value, min, max)——2026-09-06
 * 之後選單上僅存的兩個可調設定（系統音量／通氣音量）都是開/關兩態，蜂鳴器是主動式、
 * 只有 digitalWrite 開關可控，沒有任何數值級距可調，數值版編輯器因此再無呼叫點。
 *
 * @param disp    顯示抽象層
 * @param title   設定名稱（「系統音量」／「通氣音量」）
 * @param enabled 目前狀態（true = 開，false = 關）
 */
void drawToggleEditor(Display& disp, const char* title, bool enabled);

/**
 * 取得目前亮度值。
 *
 * 2026-09-06 起「螢幕亮度」已從設定選單移除（背光焊死 3.3V，無可控 GPIO，
 * 見本檔上方游標常數區的說明），此 getter 在正式路徑上已無讀取者，只剩
 * NVS 值的保存與 native test 使用；保留是為了未來接上可控背光時不必重建
 * 整條 NVS→UI 的存取路徑。
 *
 * @return 亮度值
 */
uint8_t getBrightness();

/**
 * 取得目前系統音量值。
 *
 * 0 = 關 / 1 = 開（2026-09-06 起為兩態，見 ems_settings.h）。UI 確認音以此
 * 為 gate（input_handler.cpp uiConfirmBeep()）；危急警報不看這個值。
 *
 * @return 系統音量值（0 或 1）
 */
uint8_t getSystemVolume();

// 註：原 getVentVolume() / setVentVolume() / s_vent_volume 已於 2026-09-06 移除。
//   通氣音量的唯一 runtime 真相是 app_globals.h 的 ventVolume 全域——通氣畫面的
//   UP/DOWN 與 decideVentOutput() 用的都是它。本 lib 這份副本只有設定選單這條路
//   在讀寫（編輯器畫面顯示的就是它），節奏邏輯從來沒讀過，兩者也從未同步：
//   在設定選單調通氣音量，畫面上的數字會跟著動，實際節奏卻完全不受影響
//   （2026-09-06 發現）。設定選單改用 input_handler.cpp 內直接讀寫 ventVolume
//   的 slot getter/setter。

/**
 * 設定亮度值。
 *
 * 選單移除後仍有兩個呼叫點：開機時把 NVS 值灌入（main.cpp setup()）與恢復
 * 預設（input_handler.cpp）。兩者都只是讓 s_brightness 與 NVS 保持一致，
 * 不會改變實際背光——理由同 getBrightness() 的說明。
 *
 * @param value 亮度值
 */
void setBrightness(uint8_t value);

/**
 * 設定系統音量值。
 * @param value 系統音量值（0 = 關 / 1 = 開）
 */
void setSystemVolume(uint8_t value);

/**
  * 取消恢復預設（設定值→不變更）
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
