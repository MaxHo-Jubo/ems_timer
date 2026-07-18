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

// 裝置名稱緩衝區（從 LittleFS / NVS 讀取，由 App 寫入）
static char s_device_name[DEVICE_NAME_MAX_LEN];

// 註：SETTINGS_COLOR_DIM / SETTINGS_COLOR_WHITE 已移至 ui_settings.h，
//     供 native test 驗證「置灰 vs 正常」的實際繪製顏色。

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

// 緩衝區大小
#define SETTINGS_VALUE_BUF_SIZE  8
#define SETTINGS_RANGE_BUF_SIZE 16

// 游標高亮矩形寬度（涵蓋文字區域）
#define SETTINGS_CURSOR_WIDTH    80

// 游標高亮矩形高度（文字行高）
#define SETTINGS_CURSOR_HEIGHT   20

// 確認對話框 Y 座標偏移（相對於 SETTINGS_ITEM4_Y）
#define SETTINGS_CONFIRM_Y_OFFSET  30

// 項目值（如裝置名稱）相對於標籤起點的 X 偏移，讓「標籤　值」對齊成兩欄
#define SETTINGS_VALUE_X_OFFSET  70

// ============================================================
//  drawSettingsMenu：設定主選單
// ============================================================

/** 一個可調設定項目的版面資料（游標索引 / Y 座標 / 顯示標籤） */
typedef struct {
    uint8_t     cursor;
    int16_t     y;
    const char* label;
} settings_menu_item_t;

/**
 * 3 個可調項目的版面表。
 *
 * 裝置名稱（cursor 0）不在此表：它有置灰與顯示當前值的特殊行為，單獨繪製。
 * 其餘三項版面與行為完全一致，查表繪製即可，新增設定只需加一列。
 */
static const settings_menu_item_t kSettingsAdjustableItems[] = {
    { SETTINGS_CURSOR_BRIGHTNESS, SETTINGS_ITEM2_Y, "螢幕亮度" },
    { SETTINGS_CURSOR_SYSTEM_VOL, SETTINGS_ITEM3_Y, "系統音量" },
    { SETTINGS_CURSOR_VENT_VOL,   SETTINGS_ITEM4_Y, "通氣音量" },
};

#define SETTINGS_ADJUSTABLE_ITEM_COUNT \
    (sizeof(kSettingsAdjustableItems) / sizeof(kSettingsAdjustableItems[0]))

/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
  *
  * 預設參數宣告於 ui_settings.h，此處不重複。
  *
  * @param disp                顯示抽象層（mock 或真實顯示）
  * @param cursor              游標索引（SETTINGS_CURSOR_*）
  * @param device_name_locked  裝置名稱是否鎖定（由呼叫端算好）
  * @param restore_confirm     恢復預設確認對話框是否顯示中
  */
 void drawSettingsMenu(Display& disp, uint8_t cursor, bool device_name_locked, bool restore_confirm) {
     // STEP 01: 讀取裝置名稱（ESP32: LittleFS / native: mock FS）
     //   注意：此處不得重設 s_brightness——開機時 main.cpp setup() 已用 NVS 值灌入，
     //   在每次重繪時覆寫回預設值會靜默丟掉使用者剛調好的亮度。
#ifdef ARDUINO
     settings_get_device_name(s_device_name, sizeof(s_device_name));
#else
     {
         size_t len = 0;
         mock_fs_read(DEVICE_NAME_FILE, s_device_name, sizeof(s_device_name), &len);
     }
#endif

     // STEP 02: 繪製選單標題
     disp.text("系統設定", SETTINGS_MENU_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);

     // STEP 03: 裝置名稱項目 — 游標高亮 + 鎖定時置灰且不顯示名稱
     if (cursor == SETTINGS_CURSOR_DEVICE_NAME) {
         disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
     }
     if (device_name_locked) {
         // STEP 03.01: 有未同步案件 → 置灰且隱藏名稱，避免使用者誤以為可改
         disp.text("裝置名稱", SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_DIM);
     } else {
         // STEP 03.02: 未鎖定 → 正常顯示 + 當前名稱
         disp.text("裝置名稱", SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
         disp.text(s_device_name, SETTINGS_MENU_X + SETTINGS_VALUE_X_OFFSET, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
     }

     // STEP 04: 其餘 3 個可調項目 — 版面與行為一致，查表繪製
     for (size_t i = 0; i < SETTINGS_ADJUSTABLE_ITEM_COUNT; i++) {
         const settings_menu_item_t& item = kSettingsAdjustableItems[i];
         if (cursor == item.cursor) {
             disp.fill_rect(SETTINGS_MENU_X, item.y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
         }
         disp.text(item.label, SETTINGS_MENU_X, item.y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
     }

    // STEP 05: 恢復預設確認對話框 — 僅在長按觸發後顯示
    //   （原實作無條件畫出，使得 settingsRestoreConfirm 在畫面上毫無差異）
    if (restore_confirm) {
        disp.text("是否恢復預設設定？", SETTINGS_MENU_X, SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    }
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

// 註：原 confirmRestoreDefaults() 已移除——它未在 header 宣告、全 repo 無呼叫點，
//   且內部呼叫的是 settings_*_mock 系列，本就不該存在於 production 路徑。
//   正式恢復預設流程走 input_handler.cpp 的 settings_init + settings_reset_defaults。
//   原 test_g15 宣稱測它，實際只是讀到 static 變數初值，刪掉該函式測試照樣會過。

/**
  * 取消恢復預設（亮度/系統音量/通氣音量→不變更）
  * @return true 成功
  */
 bool cancelRestore() {
     // STEP 01: 不執行任何設定值的變更，直接回傳
     return true;
 }

// 註：原 is_device_name_locked(CaseMode) 已移除。
//   g_case_mode 的值域只有 OHCA(0)/TRAINING(1)，永遠不等於當初當 sentinel 用的
//   (CaseMode)2，導致該函式在正式路徑上恆回 true（裝置名稱永遠置灰）。且「案件
//   進行中」情境本身不可達——進設定選單必經 exitOhcaCase()，案件已重置。
//   判準改為 storage_has_unsynced_case()，由呼叫端算好傳入 drawSettingsMenu。
//   詳見 docs/phase-g-system-settings-plan.md §2.2.5 與 §二、零、一 #6。

// ============================================================
//  show_device_name_sub：裝置名稱子畫面
// ============================================================

/**
  * 裝置名稱子畫面：顯示「請連接 App 設定裝置名稱」
  *
  * 裝置端不做中文輸入，僅提示使用者透過 App 設定。
  *
  * @param disp    顯示抽象層
  */
 void show_device_name_sub(Display& disp) {
     // STEP 01: 繪製標題
     disp.text("裝置名稱", SETTINGS_MENU_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
     // STEP 02: 繪製提示文字（裝置端不支援輸入，僅提示連接 App）
     disp.text("請連接 App 設定裝置名稱", SETTINGS_MENU_X, SETTINGS_ITEM1_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
 }
