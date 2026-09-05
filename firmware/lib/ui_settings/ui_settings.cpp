// EMS DoseSync Pro — Wave 1: 系統設定 UI 實作
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.1.2
//
// 設計：
//   - drawSettingsMenu：繪製 8 項目設定選單，捲動顯示一頁 5 項
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

// X 座標：文字左邊距。對齊 drawMainMenu()（src/ui_screens.cpp）的 MENU_TEXT_PAD，
// 兩個選單視覺對齊一致。
#define SETTINGS_MENU_X          24

// X 座標：游標高亮矩形起點。從畫面最左緣開始（滿版寬度橫條），對齊
// drawMainMenu() 的 fillRect(0, y, SCREEN_W, ...) 選取列樣式；文字仍從
// SETTINGS_MENU_X 這個有邊距的位置起繪，兩者故意不同值。
#define SETTINGS_CURSOR_X         0

// 畫面寬度。對齊 app_globals.h 的 SCREEN_W——本檔刻意不 include 該 ESP32 專屬
// 標頭以維持 native test 可攜性，故重複定義；異動螢幕尺寸需同步兩處。
#define SETTINGS_SCREEN_W       320

// Y 座標：標題位置。對齊 drawMainMenu() 標題列 Y。
#define SETTINGS_TITLE_Y         12

// Y 座標：捲動視窗可見列位置。Impl-Phase G 捲動重構前，ITEM1~ITEM5_Y 是 5 個選單
// 項目各自固定的絕對座標；重構後 drawSettingsMenu() STEP 03 迴圈改以
// SETTINGS_ITEM1_Y + shown * SETTINGS_ROW_SPACING 動態算出，同一個 Y 在不同
// scroll_offset 下可能畫出不同項目，不再對應特定選單項目。ITEM2/ITEM3/ITEM5_Y
// 因此已無人使用，一併移除；只留下仍被引用的兩個：
#define SETTINGS_ITEM1_Y         58   // 捲動視窗第 1 可見列（STEP 03 迴圈起始 Y；
                                       //   也被 show_device_name_sub() 當固定位置沿用，與捲動無關）。
                                       //   對齊 drawMainMenu() 的 MENU_Y_START。
#define SETTINGS_ITEM4_Y        166   // 捲動視窗第 4 可見列（58 + 3*36）；STEP 04
                                       //   確認對話框以此為基準加偏移

// Y 座標：編輯器位置
#define SETTINGS_VALUE_Y         50   // 數值顯示
#define SETTINGS_RANGE_Y         90   // 範圍提示

// 字型大小（pt）
#define SETTINGS_FONT_SIZE       2

// 緩衝區大小
#define SETTINGS_VALUE_BUF_SIZE  8
#define SETTINGS_RANGE_BUF_SIZE 16
#define SETTINGS_PAGE_BUF_SIZE   8   // 頁碼指示緩衝區（如 "8/8\0"，8 項選單最長 3 字元）

// 游標高亮矩形寬度：滿版寬度，對齊 drawMainMenu() 選取列樣式（不再是只涵蓋
// 文字區域的小方塊）
#define SETTINGS_CURSOR_WIDTH    SETTINGS_SCREEN_W

// 游標高亮矩形高度（列高）。對齊 drawMainMenu() 的 MENU_ROW_H。
#define SETTINGS_CURSOR_HEIGHT   36

// 確認對話框 Y 座標偏移（相對於 SETTINGS_ITEM4_Y）。刻意寫成
// SETTINGS_CURSOR_HEIGHT + 4，確保對話框文字起點必定落在 shown=3（視窗第 4
// 可見格，即 SETTINGS_ITEM4_Y 那格）的游標高亮矩形底部之後，兩者不會重疊——
// 2026-09-05 版面把高亮列從 20px 加高到 36px 時，這裡若繼續固定寫死 30，
// shown=3 選取中的白色高亮會一路蓋到 Y=202，蓋過對話框 Y=196 的文字起點，
// 造成白底白字（若再改高亮列高，此定義自動跟著調整，不會重演同一個 bug）
#define SETTINGS_CONFIRM_Y_OFFSET  (SETTINGS_CURSOR_HEIGHT + 4)

// 項目值（如裝置名稱）相對於標籤起點的 X 偏移，讓「標籤　值」對齊成兩欄
#define SETTINGS_VALUE_X_OFFSET  70

// 頁碼指示 X 座標：靠右上角，對齊 drawMainMenu() BT 圖示的右側留白風格
#define SETTINGS_PAGE_INDICATOR_X  (SETTINGS_SCREEN_W - 60)

// ============================================================
//  drawSettingsMenu：設定主選單
// ============================================================

/**
 * 一個選單項目的版面資料（游標索引 / 顯示標籤）。
 *
 * 只描述「畫什麼字」，不帶「可調值 vs 導覽」的語意——那個差異是 input_handler.cpp
 * 按鍵分派邏輯的事（BTN_PRIMARY 依 cursor 範圍決定要不要進編輯模式），與這份版面
 * 資料無關，故意不放進這個 struct。
 *
 * Y 座標不存在這張表裡（Impl-Phase G 捲動重構前是查表填死的絕對座標）——8 項要
 * 捲動顯示，某一項畫在螢幕哪個 Y 完全取決於它目前落在捲動視窗內的第幾格，是
 * drawSettingsMenu() STEP 03 迴圈當下算的，不是這張表的靜態屬性。
 */
typedef struct {
    uint8_t     cursor;
    const char* label;
} settings_menu_item_t;

/**
 * 完整選單清單，順序對齊 SoT V1 §19.1。
 *
 * 裝置名稱（cursor 0）納入同一張表、同一套捲動迴圈——Impl-Phase G 捲動重構前它是
 * 獨立於這張表外、固定畫在 Y=30 的特例。8 項全部要能捲動，若裝置名稱不跟著捲會
 * 變成「7 項可捲動 + 1 項永遠釘在頂端」，跟既有歷史紀錄清單的捲動方式不一致，
 * 游標邏輯也會分裂成兩套。裝置名稱的鎖定/置灰渲染邏輯（見 STEP 03.03）內容不變，
 * 只是不再保證畫在固定 Y——見 docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.3。
 *
 * App連線設定／Type-C連線是 Impl-Phase G 新增的 placeholder，未來規劃選到後顯示
 * 「尚未實作」（drawPlaceholder()）——本檔只負責選單這一列的文字繪製，BTN_PRIMARY
 * 的實際分派接線在 input_handler.cpp，尚未接上（見 Task 3），目前按主鍵無反應。
 *
 * 新增設定或導覽項要加一列到這張表，同時記得同步 ui_settings.h 的
 * SETTINGS_MENU_COUNT（wrap-around 用）——漏改會被下方 static_assert 擋下來。
 */
static const settings_menu_item_t kSettingsMenuItems[] = {
    { SETTINGS_CURSOR_DEVICE_NAME,  "裝置名稱" },      // 特例渲染，見 STEP 03.03
    { SETTINGS_CURSOR_BRIGHTNESS,   "螢幕亮度" },
    { SETTINGS_CURSOR_SYSTEM_VOL,   "系統音量" },
    { SETTINGS_CURSOR_VENT_VOL,     "通氣音量" },
    { SETTINGS_CURSOR_BATTERY_INFO, "電池資訊" },
    { SETTINGS_CURSOR_APP_CONN,     "App連線設定" },
    { SETTINGS_CURSOR_TYPEC_CONN,   "Type-C連線" },
    { SETTINGS_CURSOR_DEVICE_INFO,  "裝置資訊" },
};

// drawSettingsMenu() STEP 03 迴圈所用的邊界常數：kSettingsMenuItems[] 的實際列數，
// 表格加/減列時自動跟著改，不必手動同步一個裸數字。
#define SETTINGS_MENU_ITEM_COUNT \
    (sizeof(kSettingsMenuItems) / sizeof(kSettingsMenuItems[0]))

// 編譯期鎖住「表格列數 = 選單總項目數」這個不變量。SETTINGS_MENU_COUNT 是
// wrapSettingsCursor() 讀的手動維護常數，跟這張表各自獨立寫死，兩邊沒有自動
// 同步；未來只改表格忘了同步 SETTINGS_MENU_COUNT 會讓 wrap-around 邊界悄悄錯位
// 且沒有任何錯誤訊號。同型態既有寫法見 input_handler.cpp:1501 的
// `static_assert(SUMMARY_SUBMENU_COUNT == 2, ...)`。
static_assert(SETTINGS_MENU_ITEM_COUNT == SETTINGS_MENU_COUNT,
    "kSettingsMenuItems 列數變動時要同步更新 SETTINGS_MENU_COUNT（wrap-around 用）");

// 選單項目之間的垂直間距（px）。對齊 drawMainMenu() 的 MENU_ROW_H，8 項時
// 一頁只顯示 SETTINGS_VISIBLE_ROWS（5）項，超出視窗的項目捲動後才看得到。
#define SETTINGS_ROW_SPACING 36

/**
  * 設定主選單畫面（Impl-Phase G：SoT §19.1 完整 8 項，捲動顯示一頁 5 項）
  *
  * 預設參數宣告於 ui_settings.h，此處不重複。
  *
  * @param disp                顯示抽象層（mock 或真實顯示）
  * @param cursor              游標索引（SETTINGS_CURSOR_*）
  * @param scroll_offset       捲動視窗起點（呼叫端以 clampScrollOffset() 算好）
  * @param device_name_locked  裝置名稱是否鎖定（由呼叫端算好）
  * @param restore_confirm     恢復預設確認對話框是否顯示中
  */
 void drawSettingsMenu(Display& disp, uint8_t cursor, uint16_t scroll_offset,
                       bool device_name_locked, bool restore_confirm) {
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

     // STEP 03: 捲動視窗內的項目——依序畫，Y 座標由視窗內第幾格算出，不查表存死值
     uint8_t shown = 0;
     for (size_t i = scroll_offset;
          i < SETTINGS_MENU_ITEM_COUNT && shown < SETTINGS_VISIBLE_ROWS;
          i++, shown++) {
         const settings_menu_item_t& item = kSettingsMenuItems[i];
         int16_t y = SETTINGS_ITEM1_Y + (int16_t)shown * SETTINGS_ROW_SPACING;

         // STEP 03.01: 恢復預設確認對話框顯示中時，視窗內最後一個可見格要讓給
         //   對話框文字（SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET，見 STEP 04）
         //   ——兩者疊在一起會看不清楚。判準改成「是不是視窗內最後一格」而非
         //   「是不是電池資訊項」：Impl-Phase G 前選單固定 5 項不捲動，電池資訊
         //   恰好永遠是最後一格，兩個判準等價；捲動後游標可能停在任何項目、
         //   對話框仍可能被觸發（settingsRestoreConfirm 不限定 cursor 位置才能
         //   長按開啟，見 input_handler.cpp:1404-1406），視窗最後一格可能是任何
         //   項目，必須依畫面位置判斷，不能再依項目身分判斷。
         //   SETTINGS_CONFIRM_Y_OFFSET 定義為 SETTINGS_CURSOR_HEIGHT + 4（見該
         //   常數 doc comment），保證對話框文字必定在倒數第 2 格（shown=3）的
         //   游標高亮矩形底部之後才開始，兩者不重疊——這裡不需要另外判斷倒數
         //   第 2 格，只讓最後一格淨空即可。
         bool is_last_visible_slot = (shown == SETTINGS_VISIBLE_ROWS - 1);
         if (restore_confirm && is_last_visible_slot) {
             continue;
         }

         // STEP 03.02: selected — 這一列是否為目前游標所在項目，下面兩個分支
         //   （裝置名稱特例 / 其餘項目）各自依此決定要不要疊加高亮 fill_rect。
         bool selected = (cursor == item.cursor);

         // STEP 03.03: 裝置名稱（游標 0）特例渲染——鎖定時置灰且不顯示名稱，
         //   否則正常顯示 + 當前名稱。鎖定/置灰這段邏輯內容與捲動重構前完全
         //   相同，只是不再保證畫在固定 Y=30，改用本迴圈算出的 y；非鎖定時另
         //   新增依選取狀態切換反白文字色（2026-09-05），見 STEP 03.03.02。
         if (item.cursor == SETTINGS_CURSOR_DEVICE_NAME) {
             // STEP 03.03.01: 游標高亮——選取中才疊加 fill_rect
             if (selected) {
                 disp.fill_rect(SETTINGS_CURSOR_X, y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
             }
             // STEP 03.03.02: 依鎖定狀態決定顯示內容——鎖定則置灰隱藏名稱（不分
             //   選取與否，鎖定樣式優先於選取樣式），否則依選取狀態反白文字色
             //   並顯示當前名稱（對齊 drawMainMenu() 選取列黑字風格，見
             //   SETTINGS_COLOR_SELECTED_TEXT doc comment）
             if (device_name_locked) {
                 disp.text("裝置名稱", SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_DIM);
             } else {
                 const uint32_t text_color = selected ? SETTINGS_COLOR_SELECTED_TEXT : SETTINGS_COLOR_WHITE;
                 disp.text("裝置名稱", SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE, text_color);
                 disp.text(s_device_name, SETTINGS_MENU_X + SETTINGS_VALUE_X_OFFSET, y, SETTINGS_FONT_SIZE, text_color);
             }
             continue;
         }

         // STEP 03.04: 其餘項目（可調值 3 項 + 導覽項 4 項）— 版面與行為一致。
         //   選取列文字反白（黑字疊白底），對齊 drawMainMenu()。fix round 1 曾在
         //   Task 12 這樣做但那時高亮框只有 80×20、蓋不住整個文字，反白後大部分
         //   文字變黑字疊黑底更難看，已撤銷；2026-09-05 把高亮框改滿版寬度後
         //   這個顧慮不再成立，重新加回反白，詳見 SETTINGS_COLOR_SELECTED_TEXT
         //   doc comment（ui_settings.h）。
         // STEP 03.04.01: 游標高亮——選取中才疊加 fill_rect
         if (selected) {
             disp.fill_rect(SETTINGS_CURSOR_X, y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
         }
         disp.text(item.label, SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE,
                   selected ? SETTINGS_COLOR_SELECTED_TEXT : SETTINGS_COLOR_WHITE);
     }

    // STEP 04: 恢復預設確認對話框 — 僅在長按觸發後顯示
    if (restore_confirm) {
        disp.text("是否恢復預設設定？", SETTINGS_MENU_X, SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    }

    // STEP 05: 捲動頁碼指示（游標位置/總項目數）——8 項僅顯示 5 項時，讓使用者
    //   知道選單還能往下捲，否則捲出視窗外的項目容易被誤以為選單只有 5 項。
    //   項目數 <= 可見列數時（無需捲動）不顯示，避免多餘資訊。
    if (SETTINGS_MENU_ITEM_COUNT > SETTINGS_VISIBLE_ROWS) {
        // STEP 05.01：static 緩衝——disp.text() 的 mock 實作只存字串指標不複製
        //   內容（native test），函式返回後棧上緩衝區會被覆寫；同檔
        //   drawSettingEditor() 的 range 緩衝區已用同一手法處理過這個問題，
        //   這裡沿用。
        static char page_buf[SETTINGS_PAGE_BUF_SIZE];
        // STEP 05.02：格式化「游標位置/總項目數」（1-indexed，符合使用者直覺）
        snprintf(page_buf, sizeof(page_buf), "%u/%u",
                 (unsigned)(cursor + 1), (unsigned)SETTINGS_MENU_ITEM_COUNT);
        // STEP 05.03：畫在標題列右側，置灰色不搶主要內容視覺焦點
        disp.text(page_buf, SETTINGS_PAGE_INDICATOR_X, SETTINGS_TITLE_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_DIM);
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
