# Impl-Phase G 裝置資訊畫面 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 系統設定選單擴充至 SoT §19.1 完整 8 項（新增 App連線設定／Type-C連線 placeholder ＋ 裝置資訊），並實作裝置資訊畫面本體（名稱／型號／序號／韌體／電池／充電狀態）。

**Architecture:** 沿用既有 `ui_settings`（選單）+ `ui_screens.cpp`（子畫面）+ `DisplaySnapshot`（重繪去重）三層架構。選單從固定 5 項無捲動改為 8 項可捲動（比照既有 `historyScrollOffset` 模式），新增的捲動 clamp 邏輯抽成共用純函式供選單與歷史紀錄清單共用。裝置資訊畫面比照 `drawBatteryInfo()`（Task 13）既有寫法：純讀取既有全域/常數，不新增資料來源。

**Tech Stack:** C++17（native test）/ C++11（ESP32-S3 target，Arduino framework）、PlatformIO、Unity test framework、LovyanGFX（display abstraction 已隔離，native 走 mock）。

**Spec:** `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md`

## Global Constraints

- **STEP 註解**：函式內部每個階段用「STEP XX」，遇縮排/邏輯分支 +1 階（最多 4 階），插入既有序列中段要整段重排，插入尾端直接接續編號。禁止 STEP 00。
- **DisplaySnapshot 五步驟**（本 repo 連踩 5 次的 bug class，新增任何 user-visible 狀態都要做滿）：① struct 欄位 ② `DisplaySnapshotInputs` 對應 input ③ `captureSnapshot()` 映射 ④ 呼叫端（`main.cpp` `captureDisplaySnapshot()`）填值 ⑤ `test_display_snapshot` 加 regression case。
- **VLW 字型**：新增任何會上 TFT 的中文字串後，必須重跑 `bash scripts/regen_vlw.sh`（在 `firmware/` 目錄下執行）並確認 0 缺字，否則實機顯示 ▯ 但編譯與 native test 都不會報錯。
- **if 大括號**：一律用 `{ }`，禁止單行省略。
- **Magic Number**：具名常數 + 用途註解。
- **不可變性**：新物件、不 mutate 既有物件。
- **測試框架**：Unity（`static void test_*()` + `RUN_TEST()`），不用 `TEST_CASE()`/`TEST()` 等其他風格。
- **native build 排除 `src/`**：`platformio.ini` `[env:native]` 的 `build_src_filter = -<*>` 排除整個 `src/`，`input_handler.cpp`/`main.cpp` 的按鍵分派與接線邏輯無法被 native test 直接呼叫——純邏輯一律先抽到 `lib/` 再由 `src/` 呼叫，native test 測 `lib/` 那份。
- **每個 task 結束跑**：`cd firmware && pio test -e native`（全數通過或維持既有唯一失敗 `test_storage_hw`，該項與本計畫無關）+ `cd firmware && pio run -e esp32-s3-devkitc-1`（SUCCESS）。

---

### Task 1: 捲動 clamp 共用純函式

**Files:**
- Create: `firmware/lib/ui_scroll/ui_scroll.h`
- Test: `firmware/test/test_ui_scroll/test_main.cpp`

**Interfaces:**
- Produces: `inline uint16_t clampScrollOffset(uint16_t cursor, uint16_t offset, uint8_t visible_rows)` — Task 2（選單捲動）與 Task 3（接線，含既有歷史紀錄清單改用此函式）皆呼叫此函式。

- [ ] **Step 1: 寫失敗測試**

Create `firmware/test/test_ui_scroll/test_main.cpp`:

```cpp
// EMS DoseSync Pro — Impl-Phase G: 捲動視窗 clamp 共用邏輯
//
// 動機：settingsScrollOffset（本檔）與既有 historyScrollOffset（input_handler.cpp
// inline 邏輯）是同一個概念判斷的第二個出現點，依 EXTRACT-SHARED-HELPER 規則抽出
// 共用純函式。Task 3 會把兩處呼叫點都改用這個函式。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.2

#include <unity.h>
#include "ui_scroll.h"

void setUp()    {}
void tearDown() {}

// ============================================================
//  游標在可見視窗內 → offset 不變
// ============================================================

static void test_cursor_within_window_offset_unchanged() {
    // 視窗 [2, 7)，游標在中間
    TEST_ASSERT_EQUAL_UINT16(2, clampScrollOffset(4, 2, 5));
}

static void test_cursor_at_window_top_edge_offset_unchanged() {
    // 游標恰為視窗第一項（下界含）
    TEST_ASSERT_EQUAL_UINT16(2, clampScrollOffset(2, 2, 5));
}

static void test_cursor_at_window_bottom_edge_offset_unchanged() {
    // 游標恰為視窗最後一項（offset + visible_rows - 1）
    TEST_ASSERT_EQUAL_UINT16(2, clampScrollOffset(6, 2, 5));
}

// ============================================================
//  游標移出視窗上緣 → 視窗跟著往上捲，游標成為新視窗第一項
// ============================================================

static void test_cursor_above_window_scrolls_up() {
    TEST_ASSERT_EQUAL_UINT16(1, clampScrollOffset(1, 2, 5));
}

static void test_cursor_at_zero_above_nonzero_offset_scrolls_to_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, clampScrollOffset(0, 3, 5));
}

// ============================================================
//  游標移出視窗下緣 → 視窗跟著往下捲，游標成為新視窗最後一項
// ============================================================

static void test_cursor_below_window_scrolls_down() {
    // 游標=7，視窗 [2,7) 容不下 → 新視窗 [3,8)，7 - (5-1) = 3
    TEST_ASSERT_EQUAL_UINT16(3, clampScrollOffset(7, 2, 5));
}

static void test_cursor_at_last_item_scrolls_to_show_it_last() {
    // 8 項清單（cursor 0~7），游標在最後一項、視窗仍在頂端
    TEST_ASSERT_EQUAL_UINT16(3, clampScrollOffset(7, 0, 5));
}

// ============================================================
//  邊界：visible_rows 涵蓋全部項目時 offset 恆為 0
// ============================================================

static void test_visible_rows_covers_entire_list_offset_stays_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, clampScrollOffset(4, 0, 8));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cursor_within_window_offset_unchanged);
    RUN_TEST(test_cursor_at_window_top_edge_offset_unchanged);
    RUN_TEST(test_cursor_at_window_bottom_edge_offset_unchanged);
    RUN_TEST(test_cursor_above_window_scrolls_up);
    RUN_TEST(test_cursor_at_zero_above_nonzero_offset_scrolls_to_zero);
    RUN_TEST(test_cursor_below_window_scrolls_down);
    RUN_TEST(test_cursor_at_last_item_scrolls_to_show_it_last);
    RUN_TEST(test_visible_rows_covers_entire_list_offset_stays_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: 執行測試確認失敗（header 不存在）**

Run: `cd firmware && pio test -e native -f test_ui_scroll`
Expected: FAIL — `ui_scroll.h: No such file or directory`

- [ ] **Step 3: 寫最小實作**

Create `firmware/lib/ui_scroll/ui_scroll.h`:

```cpp
// EMS DoseSync Pro — Impl-Phase G: 捲動視窗 clamp 共用邏輯
//
// 為什麼存在：歷史紀錄清單（historyScrollOffset）與系統設定選單
// （settingsScrollOffset）都需要「游標移出可見視窗時視窗跟著捲」的同一種判斷。
// 依 EXTRACT-SHARED-HELPER 規則（同一概念判斷出現 2+ 呼叫點即該抽），抽成這個
// 純函式，兩處呼叫點統一引用，不各自維護一份 inline if。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.2
#ifndef EMS_UI_SCROLL_H
#define EMS_UI_SCROLL_H

#include <stdint.h>

/**
 * 捲動視窗跟隨游標的 clamp 計算（純函式，無副作用）。
 *
 * 游標超出可見視窗時，回傳讓游標重新落入視窗邊緣的新 offset；游標仍在視窗內
 * 則原樣回傳 offset（不動）。呼叫端負責把回傳值寫回自己的 scroll offset 全域。
 *
 * @param cursor       目前游標值（清單中的索引）
 * @param offset       目前捲動視窗起點（目前顯示清單第幾筆開始）
 * @param visible_rows 可見視窗大小（一次顯示幾列，如 SETTINGS_VISIBLE_ROWS）
 * @return             新的捲動視窗起點
 */
inline uint16_t clampScrollOffset(uint16_t cursor, uint16_t offset, uint8_t visible_rows) {
    // STEP 01: 游標在視窗上緣之上（游標值小於視窗起點）→ 視窗跟著往上捲，
    //   游標成為新視窗第一項
    if (cursor < offset) {
        return cursor;
    }
    // STEP 02: 游標在視窗下緣之下（游標值 >= 視窗起點 + 可見列數）→ 視窗跟著
    //   往下捲，游標成為新視窗最後一項
    if (cursor >= (uint16_t)(offset + visible_rows)) {
        return (uint16_t)(cursor - (visible_rows - 1));
    }
    // STEP 03: 游標仍在視窗內，不動
    return offset;
}

#endif  // EMS_UI_SCROLL_H
```

- [ ] **Step 4: 註冊 native test 環境**

`platformio.ini` native test 環境依 `test/test_*/` 目錄自動探索（比照既有 `test_ui_scroll` 不需手動加 `[env:native]` 設定，與 `test_display_snapshot` 等既有純邏輯測試相同模式——若執行 Step 5 發現需要手動註冊，依現有 `test_display_snapshot` 或 `test_settings` 的 `platformio.ini` 段落照樣加一份）。

- [ ] **Step 5: 執行測試確認通過**

Run: `cd firmware && pio test -e native -f test_ui_scroll`
Expected: `8 test cases: 8 succeeded`

- [ ] **Step 6: 執行完整 native test suite 確認無迴歸**

Run: `cd firmware && pio test -e native`
Expected: 前一版基準 +8（無其他變化）

- [ ] **Step 7: Commit**

```bash
cd firmware
git add lib/ui_scroll/ui_scroll.h test/test_ui_scroll/test_main.cpp
git commit -m "[PHASE-G] feat: 捲動視窗 clamp 共用純函式

依 EXTRACT-SHARED-HELPER 規則抽出 clampScrollOffset()，供 Task 2 新增的
設定選單捲動與 Task 3 改寫的既有歷史紀錄清單捲動共用，取代各自一份
inline if。8 個 native test 涵蓋視窗內/上緣/下緣/邊界情境。"
```

---

### Task 2: 選單結構重構——8 項清單 + 捲動

**Files:**
- Modify: `firmware/lib/ui_settings/ui_settings.h`
- Modify: `firmware/lib/ui_settings/ui_settings.cpp`
- Modify: `firmware/test/test_settings_ui/test_main.cpp`

**Interfaces:**
- Consumes: 無新依賴（本 task 純選單結構，尚不接線 `settingsScrollOffset` 全域——Task 3 才接）
- Produces:
  - `#define SETTINGS_CURSOR_APP_CONN 5`／`SETTINGS_CURSOR_TYPEC_CONN 6`／`SETTINGS_CURSOR_DEVICE_INFO 7`（`ui_settings.h`）
  - `#define SETTINGS_MENU_COUNT 8`（改值）
  - `#define SETTINGS_VISIBLE_ROWS 5`（`ui_settings.h`，供 Task 3 的 `input_handler.cpp` clamp 呼叫引用）
  - `void drawSettingsMenu(Display& disp, uint8_t cursor, uint8_t scroll_offset = 0, bool device_name_locked = false, bool restore_confirm = false)` — 新增 `scroll_offset` 參數，插在 `cursor` 之後（Task 3/4 呼叫端會明確傳值，預設 0 只為讓本 task 不改動呼叫端也能編譯過渡）

- [ ] **Step 1: 更新 `ui_settings.h` 游標常數與選單項目數**

Modify `firmware/lib/ui_settings/ui_settings.h`，在既有 `SETTINGS_CURSOR_BATTERY_INFO` 定義後（第 28 行後）插入：

```cpp
#define SETTINGS_CURSOR_BATTERY_INFO 4
// Impl-Phase G：App 連線設定／Type-C 連線（SoT §19.1 選單順序要求的 placeholder，
// 選到後顯示「尚未實作」，見 drawPlaceholder()，本 task 不接畫面邏輯只佔選單位置）
#define SETTINGS_CURSOR_APP_CONN     5
#define SETTINGS_CURSOR_TYPEC_CONN   6
// Impl-Phase G：裝置資訊（唯讀導覽項，比照電池資訊的既有 pattern）
#define SETTINGS_CURSOR_DEVICE_INFO  7

// 設定選單項目總數。UP/DOWN 捲動 wrap-around 用（見 wrapSettingsCursor()）。
// 原本只在 input_handler.cpp 定義，搬到這裡與其他游標常數放一起，native test
// 才能拿到跟正式路徑同一份常數值，不必在測試裡另外複製一份數字。
#define SETTINGS_MENU_COUNT 8

// 選單一次可見列數（超過此數量需捲動）。與既有 HISTORY_VISIBLE_ROWS
// （app_globals.h）同值，維持畫面資訊密度一致；8 項選單一頁顯示其中 5 項。
#define SETTINGS_VISIBLE_ROWS 5
```

移除原本的 `#define SETTINGS_MENU_COUNT 5`（被上面新值取代，不要留兩份定義）。

- [ ] **Step 2: 更新 `drawSettingsMenu()` 宣告**

Modify `firmware/lib/ui_settings/ui_settings.h` 的函式宣告區塊：

```cpp
/**
  * 設定主選單畫面
  * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量 / 電池資訊 / App連線設定 /
  *       Type-C連線 / 裝置資訊（Impl-Phase G 擴充至 SoT §19.1 完整 8 項）
  *
  * @param disp   顯示抽象層（mock 或真實顯示）
  * @param cursor 游標索引（SETTINGS_CURSOR_*），預設 3 向後相容
  * @param scroll_offset 捲動視窗起點（顯示清單第幾項開始），由呼叫端以
  *               clampScrollOffset() 算好再傳入（見 ui_scroll.h）。預設 0（不捲動）
  * @param device_name_locked 裝置名稱是否鎖定（true = 置灰且不顯示當前名稱）。
  *               由呼叫端以 storage_has_unsynced_case() 算好再傳入，對齊
  *               DisplaySnapshot「衍生值呼叫端先算，lib 不依賴 runtime 狀態」的原則。
  *               判準與理由見 docs/phase-g-system-settings-plan.md §2.2.5。
  * @param restore_confirm 恢復預設確認對話框是否顯示中（true 才畫出提示文字）
  */
 void drawSettingsMenu(Display& disp,
                       uint8_t cursor = SETTINGS_CURSOR_VENT_VOL,
                       uint8_t scroll_offset = 0,
                       bool device_name_locked = false,
                       bool restore_confirm = false);
```

- [ ] **Step 3: 重構 `kSettingsAdjustableItems[]` 為統一 8 項表格（含裝置名稱）**

Modify `firmware/lib/ui_settings/ui_settings.cpp`，替換第 73-127 行整段（`settings_menu_item_t` 型別定義到 `static_assert`）：

```cpp
/**
 * 一個選單項目的版面資料（游標索引 / 顯示標籤）。
 *
 * 只描述「畫什麼字」，不帶「可調值 vs 導覽」的語意——那個差異是 input_handler.cpp
 * 按鍵分派邏輯的事（BTN_PRIMARY 依 cursor 範圍決定要不要進編輯模式），與這份版面
 * 資料無關，故意不放進這個 struct。
 *
 * Y 座標不存在這張表裡（Impl-Phase G 捲動重構前是查表填死的絕對座標）——8 項要
 * 捲動顯示，某一項畫在螢幕哪個 Y 完全取決於它目前落在捲動視窗內的第幾格，是
 * drawSettingsMenu() STEP 04 迴圈當下算的，不是這張表的靜態屬性。
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
 * 游標邏輯也會分裂成兩套。裝置名稱的鎖定/置灰渲染邏輯（見 STEP 04.02）內容不變，
 * 只是不再保證畫在固定 Y——見 docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.3。
 *
 * App連線設定／Type-C連線是 Impl-Phase G 新增的 placeholder（選到後顯示「尚未
 * 實作」，見 drawPlaceholder()，本檔不處理其畫面邏輯，接線在 input_handler.cpp）。
 *
 * 新增設定或導覽項要加一列到這張表，同時記得同步 ui_settings.h 的
 * SETTINGS_MENU_COUNT（wrap-around 用）——漏改會被下方 static_assert 擋下來。
 */
static const settings_menu_item_t kSettingsMenuItems[] = {
    { SETTINGS_CURSOR_DEVICE_NAME,  "裝置名稱" },      // 特例渲染，見 STEP 04.02
    { SETTINGS_CURSOR_BRIGHTNESS,   "螢幕亮度" },
    { SETTINGS_CURSOR_SYSTEM_VOL,   "系統音量" },
    { SETTINGS_CURSOR_VENT_VOL,     "通氣音量" },
    { SETTINGS_CURSOR_BATTERY_INFO, "電池資訊" },
    { SETTINGS_CURSOR_APP_CONN,     "App連線設定" },
    { SETTINGS_CURSOR_TYPEC_CONN,   "Type-C連線" },
    { SETTINGS_CURSOR_DEVICE_INFO,  "裝置資訊" },
};

// drawSettingsMenu() STEP 04 迴圈所用的邊界常數：kSettingsMenuItems[] 的實際列數，
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

// 選單項目之間的垂直間距（px）。與既有 5 項版面沿用同一個 40px 間距，8 項時
// 一頁只顯示 SETTINGS_VISIBLE_ROWS（5）項，超出視窗的項目捲動後才看得到。
#define SETTINGS_ROW_SPACING 40
```

- [ ] **Step 4: 重寫 `drawSettingsMenu()` 本體：統一迴圈 + 捲動 + 動態 Y**

Modify `firmware/lib/ui_settings/ui_settings.cpp`，替換原本的 `drawSettingsMenu()` 函式本體（原 STEP 01-05，第 128-201 行）：

```cpp
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
 void drawSettingsMenu(Display& disp, uint8_t cursor, uint8_t scroll_offset,
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
         //   對話框文字（Y=180，SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET，見
         //   STEP 04）——兩者只差 10px 會疊在一起看不清楚。判準改成「是不是視窗
         //   內最後一格」而非「是不是電池資訊項」：Impl-Phase G 前選單固定 5 項
         //   不捲動，電池資訊恰好永遠是最後一格，兩個判準等價；捲動後游標可能
         //   停在任何項目、對話框仍可能被觸發（settingsRestoreConfirm 不限定
         //   cursor 位置才能長按開啟，見 input_handler.cpp:1404-1406），視窗最後
         //   一格可能是任何項目，必須依畫面位置判斷，不能再依項目身分判斷。
         bool is_last_visible_slot = (shown == SETTINGS_VISIBLE_ROWS - 1);
         if (restore_confirm && is_last_visible_slot) {
             continue;
         }

         bool selected = (cursor == item.cursor);

         // STEP 03.02: 裝置名稱（游標 0）特例渲染——鎖定時置灰且不顯示名稱，
         //   否則正常顯示 + 當前名稱。渲染邏輯內容與捲動重構前完全相同，只是
         //   不再保證畫在固定 Y=30，改用本迴圈算出的 y。
         if (item.cursor == SETTINGS_CURSOR_DEVICE_NAME) {
             if (selected) {
                 disp.fill_rect(SETTINGS_MENU_X, y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
             }
             if (device_name_locked) {
                 disp.text("裝置名稱", SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_DIM);
             } else {
                 disp.text("裝置名稱", SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
                 disp.text(s_device_name, SETTINGS_MENU_X + SETTINGS_VALUE_X_OFFSET, y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
             }
             continue;
         }

         // STEP 03.03: 其餘項目（可調值 3 項 + 導覽項 4 項）— 版面與行為一致。
         //   文字一律用 SETTINGS_COLOR_WHITE（不分選取與否）——fix round 1 曾在
         //   Task 12 改成選取時用黑字想解決「白底白字」，但高亮框（80×20）比
         //   實際渲染文字小很多，那個修法讓文字其餘 83% 面積變成黑字疊黑底，
         //   比原本更看不清楚，已撤銷，詳見 handover §3-A8。
         if (selected) {
             disp.fill_rect(SETTINGS_MENU_X, y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
         }
         disp.text(item.label, SETTINGS_MENU_X, y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
     }

    // STEP 04: 恢復預設確認對話框 — 僅在長按觸發後顯示
    if (restore_confirm) {
        disp.text("是否恢復預設設定？", SETTINGS_MENU_X, SETTINGS_ITEM4_Y + SETTINGS_CONFIRM_Y_OFFSET, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
    }
}
```

- [ ] **Step 5: 更新既有測試呼叫點與過時斷言**

Modify `firmware/test/test_settings_ui/test_main.cpp`：

1. 所有既有 `drawSettingsMenu(g_disp, cursor)` / `drawSettingsMenu(g_disp, cursor, locked, confirm)` 呼叫點——新增的 `scroll_offset` 參數在 `cursor` 之後、`device_name_locked` 之前，既有呼叫點若只傳 `cursor` 保持不變（`scroll_offset` 吃預設值 0）；若同時傳 `device_name_locked`/`restore_confirm` 的呼叫點，插入 `/* scroll_offset= */ 0`：

```cpp
// 例：test_g17_battery_info_hidden_when_confirm_dialog_shown 原本
drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO, /* device_name_locked= */ false,
                 /* restore_confirm= */ true);
// 改為
drawSettingsMenu(g_disp, SETTINGS_CURSOR_BATTERY_INFO, /* scroll_offset= */ 0,
                 /* device_name_locked= */ false, /* restore_confirm= */ true);
```

2. 替換 G1.9 三個 wrap-around 測試（第 250-285 行）——原本以 `SETTINGS_CURSOR_BATTERY_INFO`（cursor=4）為選單邊界，現在邊界移到 `SETTINGS_CURSOR_DEVICE_INFO`（cursor=7）：

```cpp
// ----- G1.9: 選單游標 UP/DOWN wrap-around 邊界（Impl-Phase G：8 項化後邊界移至 cursor=7）-----

/**
 * G1.9: wrapSettingsCursor() 邊界行為——8 項化後新邊界：DOWN 7→0、UP 0→7。
 * cursor=3→4（通氣音量→電池資訊）不再是邊界（8 項化前是，因為電池資訊曾是
 * 最後一項），改成一般序列測試，涵蓋新增的 4 個項目也接進同一條 wrap 鏈。
 *
 * 同既有註記：input_handler.cpp 屬 src/，native build 排除，此組測試只涵蓋
 * wrapSettingsCursor() 本身的邊界數學，不涵蓋按鍵分派接線（見 handover §8 殘餘風險 ⑥）。
 */
static void test_g19_sequential_no_wrap_through_new_items() {
    // 通氣音量(3) → 電池資訊(4) → App連線設定(5) → Type-C連線(6) → 裝置資訊(7)
    //   全部依序前進，中途不 wrap
    uint8_t c = SETTINGS_CURSOR_VENT_VOL;
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_BATTERY_INFO, c, "G1.9: 3→4");
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_APP_CONN, c, "G1.9: 4→5");
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_TYPEC_CONN, c, "G1.9: 5→6");
    c = wrapSettingsCursor(c, SETTINGS_CURSOR_DELTA_DOWN);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO, c, "G1.9: 6→7");
}

/** G1.9: DOWN cursor=7（裝置資訊，新的最後一項）→ 0（裝置名稱，wrap 回第一項） */
static void test_g19_wrap_down_from_device_info_to_device_name() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_NAME,
        wrapSettingsCursor(SETTINGS_CURSOR_DEVICE_INFO, SETTINGS_CURSOR_DELTA_DOWN),
        "G1.9: DOWN cursor=7 應 wrap 回 0（裝置名稱）");
}

/** G1.9: UP cursor=0（裝置名稱）→ 7（裝置資訊，wrap 到新的最後一項） */
static void test_g19_wrap_up_from_device_name_to_device_info() {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(SETTINGS_CURSOR_DEVICE_INFO,
        wrapSettingsCursor(SETTINGS_CURSOR_DEVICE_NAME, SETTINGS_CURSOR_DELTA_UP),
        "G1.9: UP cursor=0 應 wrap 到 7（裝置資訊）——項目數變成 8 之後的新邊界");
}
```

3. 移除舊的 `test_g19_wrap_down_from_vent_vol_to_battery_info`、`test_g19_wrap_down_from_battery_info_to_device_name`、`test_g19_wrap_up_from_device_name_to_battery_info` 三個測試（被上面三個取代）。

4. 更新 `main()` 內 `RUN_TEST()` 註冊列表，移除舊 3 個、加新 3 個。

5. 新增選單捲動與 8 項渲染的測試（緊接在既有 G1.9 之後）：

```cpp
// ----- G-Phase-G-Scroll: 8 項選單捲動渲染 -----

/** 捲動視窗預設 0 時，只畫前 5 項（裝置名稱~電池資訊），不畫捲出視窗外的 3 項 */
static void test_scroll_offset_zero_shows_first_five_items() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_NAME, /* scroll_offset= */ 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("電池資訊"),
        "視窗內第 5 項（電池資訊）應被畫出");
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("App連線設定"),
        "視窗外項目（App連線設定，第 6 項）不應被畫出");
    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置資訊"),
        "視窗外項目（裝置資訊，第 8 項）不應被畫出");
}

/** 捲動視窗 offset=3 時，畫第 4~8 項（通氣音量~裝置資訊），裝置名稱捲出視窗不畫 */
static void test_scroll_offset_three_shows_last_five_items() {
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_DEVICE_INFO, /* scroll_offset= */ 3);

    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置名稱"),
        "捲出視窗的裝置名稱不應被畫出");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("裝置資訊"),
        "視窗內最後一項（裝置資訊）應被畫出");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("Type-C連線"),
        "視窗內項目（Type-C連線）應被畫出");
}

/** 恢復預設確認對話框顯示中時，視窗最後一格讓給對話框——不論該格當下是哪個項目 */
static void test_restore_confirm_hides_last_visible_slot_regardless_of_item() {
    // scroll_offset=3 時視窗最後一格（第 5 格，shown=4）是裝置資訊（cursor 7）
    drawSettingsMenu(g_disp, SETTINGS_CURSOR_TYPEC_CONN, /* scroll_offset= */ 3,
                     /* device_name_locked= */ false, /* restore_confirm= */ true);

    TEST_ASSERT_NULL_MESSAGE(mock_text_log_find("裝置資訊"),
        "視窗最後一格顯示中的項目在對話框顯示時應跳過繪製，不論它是哪一項");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("Type-C連線"),
        "視窗內非最後一格的項目在對話框顯示時仍應正常繪製");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_text_log_find("是否恢復預設設定？"),
        "對話框本身應正常顯示");
}
```

同步在 `main()` 加入這 3 個新測試的 `RUN_TEST()`。

- [ ] **Step 6: 執行測試確認全數通過**

Run: `cd firmware && pio test -e native -f test_settings_ui`
Expected: 全數 PASSED（原有 case 數 - 3 舊 wrap 測試 + 3 新 wrap 測試 + 3 新捲動測試）

- [ ] **Step 7: 執行完整 native test suite 確認無迴歸**

Run: `cd firmware && pio test -e native`
Expected: 全數通過或維持既有唯一失敗 `test_storage_hw`

- [ ] **Step 8: 重生 VLW 字型（新增選單標籤字串）**

`App連線設定`／`Type-C連線`／`裝置資訊` 是新字串，執行：

```bash
cd firmware
bash scripts/regen_vlw.sh
```

檢查腳本輸出的缺字報告為 0（腳本本身會在缺字時印警告，若有缺字先確認 `SRC_FILES` allowlist 涵蓋 `lib/ui_settings/ui_settings.cpp`——已在既有清單中，不需修改腳本）。

- [ ] **Step 9: 韌體編譯確認**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 10: Commit**

```bash
cd firmware
git add lib/ui_settings/ui_settings.h lib/ui_settings/ui_settings.cpp \
        test/test_settings_ui/test_main.cpp src/ems_zh_24_vlw.h \
        data/fonts/ems_zh_24.vlw
git commit -m "[PHASE-G] feat: 設定選單擴充至 SoT §19.1 完整 8 項並支援捲動

新增 App連線設定／Type-C連線（placeholder）／裝置資訊 三個游標值。
kSettingsAdjustableItems 重構為 kSettingsMenuItems，裝置名稱納入同一張表
與同一套捲動邏輯，Y 座標改由捲動視窗內位置動態算出（不再查表存死值）。
恢復預設確認對話框的跳過繪製判準從「是不是電池資訊項」改為「是不是視窗
最後一格」，因應游標可能停在捲動後的任何項目。

字型重生：新增選單標籤三字串，0 缺字。native test 全綠、韌體編譯 SUCCESS。"
```

---

### Task 3: input_handler.cpp 接線——新游標分派

> 📌 **2026-09-02 範圍調整**：原 Step 1/2/4 的「捲動」半部分（`settingsScrollOffset`
> 全域、UP/DOWN 分派改用 clamp）已在 2026-09-02 對 Task 2 的補跑 CRITICAL 修復
> （commit `1b93baa`）中提前完成——不是照本節原始 Step 1/2/4 的字面程式碼做的，
> 細節見下方 Step 1/2 的「現況」說明。**Task 3 剩下的範圍只有「新游標分派」**：
> `BTN_PRIMARY` 對 cursor 5~7（App連線設定／Type-C連線／裝置資訊）目前仍無反應，
> 需要接上對應的畫面切換。

**Files:**
- Modify: `firmware/src/input_handler.cpp`
- Modify: `firmware/src/app_globals.h`（新增 `GlobalState` 列舉值 + `settingsDeviceInfoMode` extern）
- Modify: `firmware/src/main.cpp`（新增 `settingsDeviceInfoMode` 全域定義）

**Interfaces:**
- Consumes: `SETTINGS_CURSOR_APP_CONN`/`SETTINGS_CURSOR_TYPEC_CONN`/`SETTINGS_CURSOR_DEVICE_INFO`（Task 2，已存在）、`advanceSettingsCursorAndScroll()`（2026-09-02 補跑修復新增，已存在，UP/DOWN 分派不要改回舊的 `wrapSettingsCursor()`+`clampScrollOffset()` 兩行寫法）
- Produces: `settingsDeviceInfoMode` 的讀寫時機（供 Task 4 的 `DisplaySnapshotInputs` 填值使用）

- [ ] **Step 1: 現況確認（不需要新增程式碼，此步驟純檢查）**

以下三項已在 2026-09-02 完成，開工前先 `grep` 確認存在，不要重做：
- `firmware/src/main.cpp`：`uint16_t settingsScrollOffset = 0;`（注意型別是 `uint16_t`
  不是本節舊版寫的 `uint8_t`——2026-09-02 review 修正過一次窄化問題）
- `firmware/src/app_globals.h` / `firmware/src/input_handler.cpp`：對應的
  `extern uint16_t settingsScrollOffset;`
- `firmware/src/input_handler.cpp` 的 `BTN_UP`/`BTN_DOWN` 分支已呼叫
  `advanceSettingsCursorAndScroll()`（定義在 `firmware/lib/ui_settings/ui_settings.h`），
  不是本節原始版本寫的 `wrapSettingsCursor()` + `clampScrollOffset()` 兩行分開呼叫

- [ ] **Step 2: `app_globals.h` / `main.cpp` 新增 `settingsDeviceInfoMode`**

Modify `firmware/src/main.cpp`，在既有 `settingsBatteryInfoMode` 定義（約第 249 行）後插入：

```cpp
bool    settingsBatteryInfoMode = false;  // Phase H：電池資訊子畫面顯示中（Task 13）
bool    settingsDeviceInfoMode = false;   // Impl-Phase G：裝置資訊子畫面顯示中
```

Modify `firmware/src/app_globals.h`，在既有 `settingsBatteryInfoMode` extern（約第 538 行）後插入：

```cpp
extern bool    settingsBatteryInfoMode; // Phase H：true = 電池資訊子畫面顯示中（Task 13）
extern bool    settingsDeviceInfoMode;  // Impl-Phase G：true = 裝置資訊子畫面顯示中
```

Modify `firmware/src/input_handler.cpp`，在既有 `settingsBatteryInfoMode` extern（約第 23 行）後插入：

```cpp
extern bool    settingsBatteryInfoMode; // Phase H：true = 電池資訊子畫面顯示中（Task 13）
extern bool    settingsDeviceInfoMode;  // Impl-Phase G：true = 裝置資訊子畫面顯示中
```

- [ ] **Step 3: 新增 `GlobalState` 列舉值**

Modify `firmware/src/app_globals.h` 第 253-261 行——`GlobalState` 用 `uint8_t` 顯式賦值，新增值接在既有最大值 `GLOBAL_SYNC = 6` 之後，不要插入既有值中間（避免不必要的既有數值變動）：

```cpp
enum GlobalState : uint8_t {
    GLOBAL_MAIN_MENU            = 0,
    GLOBAL_OHCA                 = 1,
    GLOBAL_VENT                 = 2,
    GLOBAL_TRAINING_SETUP       = 3,
    GLOBAL_HISTORY_PLACEHOLDER  = 4,
    GLOBAL_SETTINGS_PLACEHOLDER = 5,
    GLOBAL_SYNC                 = 6,
    // Impl-Phase G：App連線設定／Type-C連線 placeholder（drawPlaceholder() 顯示中，
    // 任意鍵返回 GLOBAL_SETTINGS_PLACEHOLDER，見 input_handler.cpp）
    GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER = 7,
    GLOBAL_SETTINGS_TYPEC_PLACEHOLDER    = 8,
};
```

- [ ] **Step 4: 新增裝置資訊子畫面出口 + BTN_PRIMARY 新游標分派**

> 📌 **範圍已縮小**：`GLOBAL_SETTINGS_PLACEHOLDER` 整段（含 STEP 03 電池資訊子畫面、
> STEP 04 的 UP/DOWN 分派）已經存在且不需要改動——UP/DOWN 目前呼叫的是
> `advanceSettingsCursorAndScroll()`（2026-09-02 補跑修復新增），**不要改回**下方
> 舊版範例程式碼裡的 `wrapSettingsCursor()` + `clampScrollOffset()` 兩行寫法，那是
> 已經被取代的舊版本，改回去會讓 2026-09-02 修的 CRITICAL（游標移出可見視窗時畫面
> 不重繪）重新出現。本步驟只新增兩塊：STEP 03 之後插入新的 STEP 03.5（裝置資訊
> 子畫面出口），以及在既有 `BTN_PRIMARY` 的 if-else 鏈末端插入三個新游標分支。

Modify `firmware/src/input_handler.cpp`，在既有 STEP 03（電池資訊子畫面，`if
(settingsBatteryInfoMode) { ... }`）後插入新的 STEP 03.5：

```cpp
        // STEP 03.5: 裝置資訊子畫面中（唯讀導覽頁，同電池資訊 pattern）
        if (settingsDeviceInfoMode) {
            if (btnIdx == BTN_BACK) {
                settingsDeviceInfoMode = false;
                Serial.println("[SETTINGS] device info — back to menu");
            }
            return;
        }
```

既有 `BTN_PRIMARY` 分支的完整 if-else 鏈現況如下——只需在 `SETTINGS_CURSOR_BATTERY_INFO`
分支之後、`break;` 之前插入三個新游標分派（`SETTINGS_CURSOR_APP_CONN` 起的三個
`else if` 區塊），前面的 `DEVICE_NAME`／可調值三項／`BATTERY_INFO` 分支已存在、不用動：

```cpp
                if (settingsCursor == SETTINGS_CURSOR_DEVICE_NAME) {
                    // §2.2.5：有未同步案件 → 裝置名稱鎖定，主鍵不可進入
                    if (g_device_name_locked) {
                        Serial.println("[SETTINGS] device name locked — 有未同步案件");
                    } else {
                        Serial.println("[SETTINGS] device name — show sub（子畫面尚未接線，見 §2.2.3）");
                    }
                } else if (settingsCursor >= SETTINGS_CURSOR_BRIGHTNESS &&
                           settingsCursor <= SETTINGS_CURSOR_VENT_VOL) {
                    // 三個可調項目行為一致：進入編輯模式
                    settingsEditorMode = true;
                } else if (settingsCursor == SETTINGS_CURSOR_BATTERY_INFO) {
                    // Task 13：進入電池資訊子畫面
                    settingsBatteryInfoMode = true;
                    Serial.println("[SETTINGS] battery info — show sub");
                } else if (settingsCursor == SETTINGS_CURSOR_APP_CONN) {
                    // Impl-Phase G：App連線設定尚未實作，顯示 placeholder
                    globalState = GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER;
                    Serial.println("[SETTINGS] app conn — placeholder");
                } else if (settingsCursor == SETTINGS_CURSOR_TYPEC_CONN) {
                    // Impl-Phase G：Type-C連線尚未實作，顯示 placeholder
                    globalState = GLOBAL_SETTINGS_TYPEC_PLACEHOLDER;
                    Serial.println("[SETTINGS] type-c conn — placeholder");
                } else if (settingsCursor == SETTINGS_CURSOR_DEVICE_INFO) {
                    // Impl-Phase G：進入裝置資訊子畫面
                    settingsDeviceInfoMode = true;
                    Serial.println("[SETTINGS] device info — show sub");
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Impl-Phase G：App連線設定／Type-C連線 placeholder =====
    //   比照 Training/History 主選單當年用過的 drawPlaceholder() pattern：
    //   任意鍵返回設定選單，不需要子畫面狀態機（沒有真正的畫面內容要記）。
    if (globalState == GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER ||
        globalState == GLOBAL_SETTINGS_TYPEC_PLACEHOLDER) {
        globalState = GLOBAL_SETTINGS_PLACEHOLDER;
        return;
    }
```

> 📌 這裡用到的兩個 `GlobalState` 列舉值 `GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER` /
> `GLOBAL_SETTINGS_TYPEC_PLACEHOLDER` 已在上方 Step 3 定義。placeholder 用獨立
> `globalState` 而非 `GLOBAL_SETTINGS_PLACEHOLDER` 內的旗標，理由是它們**不需要記任何
> 子畫面狀態**（跟電池資訊/裝置資訊那種「唯讀但仍算設定選單底下的子畫面」不同——
> placeholder 只是「顯示一個提示、任意鍵就離開」，用全域狀態切換比多兩個
> bool flag 更直接，也不會誤觸 `DisplaySnapshot` 五步驟的欄位同步負擔，因為
> `globalState` 本身已經在 snapshot 裡）。

- [ ] **Step 5: 既有 `historyScrollOffset` clamp 改用共用函式（EXTRACT-SHARED-HELPER 第二處呼叫點）**

Modify `firmware/src/input_handler.cpp` 第 619-636 行：

```cpp
        // STEP 02: 列表模式
        switch (btnIdx) {
            case BTN_UP:
                if (historyCursor > 0) {
                    historyCursor--;
                }
                historyScrollOffset = clampScrollOffset(historyCursor, historyScrollOffset, HISTORY_VISIBLE_ROWS);
                break;
            case BTN_DOWN:
                if (historyCursor + 1 < historyCount) {
                    historyCursor++;
                }
                historyScrollOffset = clampScrollOffset(historyCursor, historyScrollOffset, HISTORY_VISIBLE_ROWS);
                break;
```

（`clampScrollOffset()` 接受 `uint16_t cursor`，`historyCursor`/`historyScrollOffset` 已是 `uint16_t`，型別相容不需轉型；`HISTORY_VISIBLE_ROWS` 是 `uint8_t`，函式簽章第三參數本就是 `uint8_t`，直接傳入。）

- [x] **Step 6: 跳過——原步驟是計畫缺陷，Ruling 見下方**

> **Ruling（2026-09-02，team-lead 裁決，跳過本步驟不執行）**：本步驟原本要求進入
> 設定選單時 `settingsScrollOffset = 0`，是照抄 `historyScrollOffset` 進入歷史紀錄
> 清單時的重置慣例——但 grep 全庫確認 `historyCursor`/`historyScrollOffset` 在全部
> 3 個重置點永遠成對重置（`input_handler.cpp:473-474/591-592/998-999`），而
> `settingsCursor` 從未有任何重置邏輯（sticky，開機初始化一次後就不再重設，見
> `main.cpp:242` 註解與既有行為）。若只重置 `settingsScrollOffset` 不重置
> `settingsCursor`，游標停在上次離開時的位置（例如 cursor=7）但視窗被重置回
> `[0,5)`，會立刻重新製造 2026-09-02 剛修好的 CRITICAL（游標在可見視窗外、高亮
> 消失）——這是 `dont-blindly-mirror` 原則的典型案例：鏡像 `historyScrollOffset`
> 的重置慣例前，沒有先確認 `settingsCursor` 端是否也有對應的重置（它沒有）。
> **決定**：不執行本步驟，`settingsScrollOffset` 維持 2026-09-02 backfill fix 已
> 實作的 sticky 行為（跟 `settingsCursor` 一樣不重置，兩者才能保持成對一致）。
> **代價**：如果這個裁決錯了（例如使用者其實期待每次進設定選單都從第一項開始），
> 之後要改就是把 `settingsCursor` 也一併加重置邏輯，而不是只加回這一行——兩者
> 必須一起改，不能只改一邊。

- [ ] **Step 7: 執行完整 native test suite 確認無迴歸**

Run: `cd firmware && pio test -e native`
Expected: 全數通過或維持既有唯一失敗 `test_storage_hw`（本 task 只動 `src/`，native build 排除 `src/`，理論上測試數字不變——這步是確認「沒有連帶弄壞」而非本 task 直接產出的驗證）

- [ ] **Step 8: 韌體編譯確認**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS（這是本 task 邏輯正確性的主要驗證管道，因為 `src/` 不進 native build）

- [ ] **Step 9: Commit**

```bash
cd firmware
git add src/input_handler.cpp src/main.cpp src/app_globals.h
git commit -m "[PHASE-G] feat: 設定選單新游標分派 + 歷史清單捲動改用共用函式

BTN_PRIMARY 新增 App連線設定／Type-C連線／裝置資訊 三個游標的分派：
前兩者進 placeholder 全域狀態（任意鍵返回，無需子畫面 flag），裝置資訊
進 settingsDeviceInfoMode（比照 Task 13 電池資訊 pattern）。既有歷史
紀錄清單的 inline clamp 改用共用 clampScrollOffset()（EXTRACT-SHARED-
HELPER 第二處呼叫點，第一處是 2026-09-02 已接線的設定選單）。捲動視窗
本身的接線（settingsScrollOffset）已在 2026-09-02 補跑修復先完成，本次
不重複。

韌體編譯 SUCCESS（本 task 主要驗證管道，input_handler.cpp 不在 native
build 範圍內）。"
```

---

### Task 4: DisplaySnapshot 接線 + `updateDisplay()` 分派

> 📌 **2026-09-02 範圍調整**：`settingsScrollOffset` 的 DisplaySnapshot 五步驟（struct
> 欄位／Inputs／captureSnapshot 映射／main.cpp 填值）已在 2026-09-02 對 Task 2 的補跑
> CRITICAL 修復（commit `1b93baa`）提前做完——型別是 `uint16_t` 不是本節原文的
> `uint8_t`（同一輪修過一次窄化問題）。**Task 4 剩下的範圍只有 `settingsDeviceInfo`**
> 相關的三件事：新 flag、struct/Inputs/映射/填值、以及 `updateDisplay()` 的實際渲染
> 分派（這正是 Task 3 完成後、repo Tier 2 review 抓到的 2 個 CRITICAL 的缺口——
> App連線設定／Type-C連線／裝置資訊三個游標選了之後畫面是空白的，因為渲染分派
> 一直是本 task 的範圍，Task 3 的裁決已明確記錄這個缺口留給本 task 補）。

**Files:**
- Modify: `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/test/test_display_snapshot/test_display_snapshot.cpp`

**Interfaces:**
- Consumes: `settingsScrollOffset`（已存在，2026-09-02 提前完成）／`settingsDeviceInfoMode`（Task 3 已定義）
- Produces: `SNAP_FLAG_SETTINGS_DEVICE_INFO = 0x00400000`——Task 5 的 on-target 驗收與往後任何要讀這個 flag 的程式碼引用

- [ ] **Step 1: 現況確認（不需要新增程式碼，此步驟純檢查）**

以下已在 2026-09-02 完成，開工前先 `grep` 確認存在，不要重做：
- `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`：`DisplaySnapshot` struct 已有
  `uint16_t settingsScrollOffset;` 欄位（緊接在 `settingsCursor` 之後）
- 同檔 `DisplaySnapshotInputs` struct 已有 `uint16_t settingsScrollOffset = 0;`
- 同檔 `captureSnapshot()` 已有 `s.settingsScrollOffset = in.settingsScrollOffset;` 映射
- `firmware/src/main.cpp` 的 `captureDisplaySnapshot()` 已有
  `in.settingsScrollOffset = settingsScrollOffset;` 填值
- `firmware/src/main.cpp` 的 `drawSettingsMenu()` 呼叫點已傳入 `settingsScrollOffset`
  （`/* scroll_offset= */ settingsScrollOffset`），不是本節原文寫的字面值或需要新增參數
- `firmware/test/test_display_snapshot/test_display_snapshot.cpp` 已有
  `test_settings_scroll_offset_change_triggers_redraw_phase_g_regression`——**不要**
  再依下方 Step 5 加一個同語意的新測試（會是重複覆蓋同一個欄位），Step 5 只需新增
  `settingsDeviceInfo` 相關的兩個測試

- [ ] **Step 2: 新增 `SNAP_FLAG_SETTINGS_DEVICE_INFO`**

Modify `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`，在 `SNAP_FLAG_SETTINGS_BATTERY_INFO`（第 88 行）後插入：

```cpp
    SNAP_FLAG_SETTINGS_BATTERY_INFO = 0x00200000,  // Phase H：電池資訊子畫面顯示中（Task 13）
    SNAP_FLAG_SETTINGS_DEVICE_INFO = 0x00400000,   // Impl-Phase G：裝置資訊子畫面顯示中
```

- [ ] **Step 3: `DisplaySnapshotInputs` 新增 `settingsDeviceInfo` 欄位**

> `settingsScrollOffset` 已存在於 `DisplaySnapshotInputs`（見 Step 1 現況確認），本步驟
> 只新增 `settingsDeviceInfo`，不要重複加 `settingsScrollOffset`。

Modify 同檔 `DisplaySnapshotInputs` struct，在 `bool settingsBatteryInfo = false;` 後插入：

```cpp
    bool     settingsBatteryInfo    = false;  // Phase H：電池資訊子畫面顯示中（Task 13）
    bool     settingsDeviceInfo     = false;  // Impl-Phase G：裝置資訊子畫面顯示中
```

- [ ] **Step 4: `captureSnapshot()` 映射（僅 `settingsDeviceInfo` 這個 flag）**

> `s.settingsScrollOffset = in.settingsScrollOffset;` 已存在於 STEP 01 區塊（見 Step 1
> 現況確認），本步驟只在 STEP 02 區塊（bool → bit-packed flags）新增一行。

Modify 同檔 `captureSnapshot()` 函式，STEP 02 區塊，在 `if (in.settingsBatteryInfo) s.flags |= SNAP_FLAG_SETTINGS_BATTERY_INFO;` 後插入：

```cpp
    if (in.settingsBatteryInfo)    s.flags |= SNAP_FLAG_SETTINGS_BATTERY_INFO;
    if (in.settingsDeviceInfo)     s.flags |= SNAP_FLAG_SETTINGS_DEVICE_INFO;
```

- [ ] **Step 5: 寫失敗測試（僅 `settingsDeviceInfo` flag，`settingsScrollOffset` 已有測試）**

> `settingsScrollOffset` 的 regression test 已存在
> （`test_settings_scroll_offset_change_triggers_redraw_phase_g_regression`），本步驟
> **不要**再加一個同語意的新測試。只新增下面兩個 `settingsDeviceInfo` 相關的測試。

Modify `firmware/test/test_display_snapshot/test_display_snapshot.cpp`，在既有 Phase H 電池欄位測試群組附近新增：

```cpp
// ============================================================
//  Impl-Phase G: settingsDeviceInfo flag 覆蓋
// ============================================================

static void test_settings_device_info_flag_change_triggers_redraw() {
    DisplaySnapshotInputs in_a;
    in_a.settingsDeviceInfo = false;
    DisplaySnapshot a = captureSnapshot(in_a);

    DisplaySnapshotInputs in_b;
    in_b.settingsDeviceInfo = true;
    DisplaySnapshot b = captureSnapshot(in_b);

    TEST_ASSERT_FALSE_MESSAGE(snapshotsEqual(a, b),
        "settingsDeviceInfo 進出必須觸發重繪，否則裝置資訊子畫面進出不重繪");
}

static void test_settings_device_info_flag_maps_to_unique_bit() {
    DisplaySnapshotInputs in;
    in.settingsDeviceInfo = true;
    DisplaySnapshot s = captureSnapshot(in);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SNAP_FLAG_SETTINGS_DEVICE_INFO, s.flags,
        "settingsDeviceInfo=true 時 flags 應恰為 SNAP_FLAG_SETTINGS_DEVICE_INFO 這一個 bit");
}
```

同步在 `main()` 加入這 2 個新測試的 `RUN_TEST()`。

- [ ] **Step 6: 執行測試確認全數通過**

Run: `cd firmware && pio test -e native -f test_display_snapshot`
Expected: 全數 PASSED（原有 case 數 +2，不是 +3——`settingsScrollOffset` 那個已經在基準線裡）

- [ ] **Step 7: `main.cpp` `captureDisplaySnapshot()` 填值（僅 `settingsDeviceInfo` 這一行）**

> `in.settingsScrollOffset = settingsScrollOffset;` 已存在（見 Step 1 現況確認），
> 本步驟只新增 `settingsDeviceInfo` 這一行，不要重複加 scroll offset。

Modify `firmware/src/main.cpp`，在既有 `in.settingsBatteryInfo = settingsBatteryInfoMode;` 那行後插入：

```cpp
    in.settingsBatteryInfo    = settingsBatteryInfoMode; // Phase H：Task 13，漏此項會使電池資訊子畫面進出不重繪
    in.settingsDeviceInfo     = settingsDeviceInfoMode;  // Impl-Phase G：漏此項會使裝置資訊子畫面進出不重繪
```

- [ ] **Step 8: `updateDisplay()` 新增 `settingsDeviceInfoMode` 分派 + 兩個 placeholder 分派**

> `drawSettingsMenu()` 呼叫點已經傳入 `settingsScrollOffset`（見 Step 1 現況確認），
> 不要動那一行。本步驟只新增 `settingsDeviceInfoMode` 這個 `else if` 分支（插在
> `settingsBatteryInfoMode` 分支之後、`settingsEditorMode` 分支之前）以及
> `GLOBAL_SETTINGS_PLACEHOLDER` 整個 if-else 鏈結束後的兩個新頂層分支。

Modify `firmware/src/main.cpp`，現況的 `updateDisplay()` 相關區塊如下（僅供對照，
`drawSettingsMenu()` 那行維持原樣）：

```cpp
            drawSettingsMenu(settingsDisp, settingsCursor, /* scroll_offset= */ settingsScrollOffset,
```

改為（`scroll_offset` 參數插在 `cursor` 之後，對齊 Task 2 的新函式簽章）：

```cpp
            drawSettingsMenu(settingsDisp, settingsCursor, settingsScrollOffset, g_device_name_locked, settingsRestoreConfirm);
```

同段插入 `settingsDeviceInfoMode` 分支與兩個 placeholder 頂層分支後，完整區塊應長這樣
（`drawSettingsMenu()` 那行不變，只是讓你確認插入位置前後對不對）：

```cpp
    } else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
        Display settingsDisp = getSettingsDisplay();
        if (settingsBatteryInfoMode) {
            // 電池資訊子畫面，無需 Display 參數（畫面直接寫 display sprite，沿用
            // 其他全螢幕 draw 函式的慣例）。不在此呼叫 presentFrame() 或提前
            // return——讓流程照常往下走到本函式 STEP 04 統一出口，理由見下方 STEP 04 註解。
            drawBatteryInfo();
        } else if (settingsDeviceInfoMode) {
            // Impl-Phase G：裝置資訊子畫面，同電池資訊 pattern。
            drawDeviceInfo();
        } else if (settingsEditorMode) {
            // 編輯模式：依游標索引繪製對應設定項目的數值調整畫面
            if (settingsCursor == SETTINGS_CURSOR_BRIGHTNESS) {
                drawSettingEditor(settingsDisp, "螢幕亮度", getBrightness(), SETTINGS_BRIGHTNESS_MIN, SETTINGS_BRIGHTNESS_MAX);
            } else if (settingsCursor == SETTINGS_CURSOR_SYSTEM_VOL) {
                drawSettingEditor(settingsDisp, "系統音量", getSystemVolume(), SETTINGS_VOLUME_MIN, SETTINGS_VOLUME_MAX);
            } else if (settingsCursor == SETTINGS_CURSOR_VENT_VOL) {
                drawSettingEditor(settingsDisp, "通氣音量", getVentVolume(), SETTINGS_VENT_VOLUME_MIN, SETTINGS_VENT_VOLUME_MAX);
            }
        } else {
            drawSettingsMenu(settingsDisp, settingsCursor, settingsScrollOffset, g_device_name_locked, settingsRestoreConfirm);
        }
    } else if (globalState == GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER) {
        drawPlaceholder("App連線設定", "Phase G");
    } else if (globalState == GLOBAL_SETTINGS_TYPEC_PLACEHOLDER) {
        drawPlaceholder("Type-C連線", "Phase G");
    }
```

（`drawDeviceInfo()` 由 Task 5 提供，本 task 先寫呼叫點，Task 5 完成前這行會編譯失敗——若照本計畫順序逐一 task 完成則不會有問題；若採平行 subagent 開發，Task 4 需等 Task 5 的函式宣告先進 `app_globals.h` 才能獨立編譯過，建議依本計畫順序序列執行。）

- [ ] **Step 9: 執行完整 native test suite 確認無迴歸**

Run: `cd firmware && pio test -e native`
Expected: 全數通過或維持既有唯一失敗 `test_storage_hw`（`test_display_snapshot` 應為前一版 +2，不是 +3——見 Step 1 現況確認）

- [ ] **Step 10: 韌體編譯確認**

> ⚠️ 此步驟在 Task 5 完成前會因 `drawDeviceInfo()` 未定義而編譯失敗（見 Step 8 備註）——若依本計畫序列執行到此處，直接跳過本步驟的獨立驗證，改在 Task 5 完成後一併驗證兩個 task 的合併結果。若要本 task 獨立驗證，可暫時把 Step 8 的 `drawDeviceInfo();` 呼叫註解掉編譯確認語法正確，再取消註解交給 Task 5 補上定義（不建議，徒增一次修改）。

- [ ] **Step 11: Commit**

```bash
cd firmware
git add lib/ems_display_snapshot/ems_display_snapshot.h src/main.cpp \
        test/test_display_snapshot/test_display_snapshot.cpp
git commit -m "[PHASE-G] feat: DisplaySnapshot 接線——裝置資訊子畫面 flag + 畫面渲染分派

新增 SNAP_FLAG_SETTINGS_DEVICE_INFO 與 settingsDeviceInfo 欄位，依
既有五步驟 checklist 走完（struct/Inputs/captureSnapshot/呼叫端填值/
regression test；settingsScrollOffset 那組已在 2026-09-02 補跑修復
提前做完，本次不重複）。updateDisplay() 新增 settingsDeviceInfoMode
分派與兩個 placeholder 全域狀態分派（drawPlaceholder 複用既有函式）
——這是 Task 3 完成後 repo Tier 2 review 抓到的 2 個 CRITICAL 的缺口
（新游標選了之後畫面空白），本 commit 補上畫面渲染那一半。

native test +2（test_display_snapshot），本 commit 依賴 Task 5 的
drawDeviceInfo() 宣告才能完整編譯，序列執行下一 task 補上。"
```

---

### Task 5: 裝置資訊畫面本體

> 📌 **2026-09-02 修正**：Step 3 原始程式碼片段的 STEP 03（讀裝置名稱）誤把
> `ui_settings.cpp`（`lib/`，native + Arduino 雙軌編譯）的 `#ifdef ARDUINO` /
> `mock_fs_read` 分支模式鏡像到本檔——但 `ui_screens.cpp` 屬 `src/`，native build
> 整檔排除，`#else` 分支在這裡永遠不可達，是死碼（`dont-blindly-mirror` 原則：
> grep 全庫確認 `ui_screens.cpp` 從無任何 `#ifdef ARDUINO` 用法）。已修正為直接呼叫，
> 下方程式碼片段已更新，照抄即可。

**Files:**
- Modify: `firmware/src/ui_screens.cpp`
- Modify: `firmware/src/app_globals.h`

**Interfaces:**
- Consumes: `settings_get_device_name()`（既有）、`SYNC_DEVICE_ID`/`SYNC_FW_VERSION`（既有 `app_globals.h` 常數）、`g_battery_percent`/`g_battery_charge_state`（既有全域）、`ems::is_battery_absent()`（既有）
- Produces: `void drawDeviceInfo()`（供 Task 4 Step 8 呼叫，本 task 完成後 Task 4 才能完整編譯）

> ⚠️ **無 native test**：`ui_screens.cpp` 屬於 `src/`，`platformio.ini` `[env:native]` 的
> `build_src_filter = -<*>` 排除整個 `src/`（native 環境 `Arduino.h` 不可用）。`drawBatteryInfo()`
> （Task 13）與其他所有 `ui_screens.cpp` 內的畫面函式都沒有 native test——`grep -rln
> "drawBatteryInfo(" firmware/test/` 零筆結果可驗證這點。本 task 沿用同一個既有慣例：
> `drawDeviceInfo()` 不寫 native test，正確性靠 STEP-01 韌體編譯通過（ESP32 target 才會
> 真正編到 `src/`）+ 上機視覺驗收（見計畫末尾「上機驗收清單」）。

- [ ] **Step 1: 新增 `drawDeviceInfo()` 函式宣告**

Modify `firmware/src/app_globals.h`，在既有 `void drawPlaceholder(const char* title, const char* phase);`（第 703 行）附近或電池資訊函式宣告旁插入：

```cpp
void drawDeviceInfo();  // Impl-Phase G：裝置資訊子畫面（名稱/型號/序號/韌體/電池/充電狀態）
```

- [ ] **Step 2: 核對 `drawBatteryInfo()` 既有的充電狀態三態轉譯文字，逐字沿用**

Run: `grep -n "充電中\|放電中\|靜置\|判斷中" firmware/src/ui_screens.cpp`

確認 `Idle` 狀態既有文字為「靜置」（`ui_screens.cpp:263`），下一步的 `drawDeviceInfo()` 實作必須逐字沿用這三個狀態文字，不得自創不同措辭——同一個 `ems::ChargeState` 列舉值在兩個畫面（電池資訊／裝置資訊）必須顯示相同文字，否則使用者會以為兩個畫面代表不同狀態。

- [ ] **Step 3: 寫實作**

Add to `firmware/src/ui_screens.cpp`，緊接在既有 `drawBatteryInfo()` 函式（第 217-267 行左右）之後：

```cpp
/**
 * 裝置資訊子畫面（Impl-Phase G，SoT §19.7）：名稱／型號／序號／韌體／電池／
 * 充電狀態六列。比照 drawBatteryInfo()（Task 13）的既有寫法：STEP 編號、
 * 統一出口、不編造缺值。
 *
 * 與電池資訊畫面不同：本畫面沒有「不在線」整頁狀態——六個欄位啟動後必定都有
 * 值（名稱有預設「未命名」、型號/序號/韌體是靜態常數，電池/充電狀態沿用既有
 * is_battery_absent() 判斷只影響那兩列的顯示文字，不影響整個畫面可用性）。
 */
void drawDeviceInfo() {
    // 版面常數：左邊界、首列 Y、行距——沿用 drawBatteryInfo() 的既有版面慣例
    constexpr int16_t DEVICE_INFO_TEXT_X       = 24;
    constexpr int16_t DEVICE_INFO_LINE1_Y      = 50;   // 名稱列
    constexpr int16_t DEVICE_INFO_LINE_SPACING = 28;   // 每列間距（px，六列需比電池資訊三列更密）

    // STEP 01: 文字樣式設定
    useZhFont();
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::top_left);

    // STEP 02: 標題
    drawCenteredText("裝置資訊", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // STEP 03: 名稱（每次繪製重新讀，比照 drawSettingsMenu() 既有做法，非 snapshot 驅動）。
    //   不像 ui_settings.cpp 的 drawSettingsMenu() 需要 #ifdef ARDUINO / mock_fs_read
    //   雙軌分支——本檔 ui_screens.cpp 屬 src/，native build 排除整個檔案
    //   （platformio.ini build_src_filter = -<*>），這個函式只會在 ESP32/Arduino
    //   環境被編譯到，#else 分支永遠不可達，寫了也是死碼，直接呼叫即可。
    char device_name[DEVICE_NAME_MAX_LEN];
    settings_get_device_name(device_name, sizeof(device_name));
    char buf[48];  // 最長行含中文標籤 + 裝置名稱，留餘裕
    snprintf(buf, sizeof(buf), "名稱：%s", device_name);
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y);

    // STEP 04: 型號（固定字面常數）
    snprintf(buf, sizeof(buf), "型號：%s", "EMS DoseSync Pro");
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING);

    // STEP 05: 序號（既有 SYNC_DEVICE_ID 常數，與案件同步 metadata 的 device_id 同源，
    //   見 spec §4.1.1——不可另外衍生一組不同來源的值）
    snprintf(buf, sizeof(buf), "序號：%s", SYNC_DEVICE_ID);
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 2);

    // STEP 06: 韌體（既有 SYNC_FW_VERSION 常數）
    snprintf(buf, sizeof(buf), "韌體：%s", SYNC_FW_VERSION);
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 3);

    // STEP 07: 電池／充電狀態——不在線時顯示「—」而非編造數值，經 is_battery_absent()
    //   統一出口判斷，不自行比對 255 哨兵（同 drawBatteryInfo() 既有規範）
    if (ems::is_battery_absent(g_battery_percent)) {
        display.drawString("電池：—", DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 4);
        display.drawString("充電狀態：—", DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 5);
        return;
    }

    snprintf(buf, sizeof(buf), "電池：%u%%", g_battery_percent);
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 4);

    // STEP 08: 充電狀態文字——逐字沿用 drawBatteryInfo() 既有三態轉譯（Step 2 已核對
    //   Idle="靜置"）。Unknown 顯示「判斷中」而非猜一個狀態，理由同 Task 13：趨勢窗
    //   未滿時本來就不知道
    const char* state_text = "判斷中";
    if (g_battery_charge_state == ems::ChargeState::Charging) {
        state_text = "充電中";
    } else if (g_battery_charge_state == ems::ChargeState::Discharging) {
        state_text = "放電中";
    } else if (g_battery_charge_state == ems::ChargeState::Idle) {
        state_text = "靜置";
    }
    snprintf(buf, sizeof(buf), "充電狀態：%s", state_text);
    display.drawString(buf, DEVICE_INFO_TEXT_X, DEVICE_INFO_LINE1_Y + DEVICE_INFO_LINE_SPACING * 5);
}
```

- [ ] **Step 4: 執行完整 native test suite 確認無迴歸**

Run: `cd firmware && pio test -e native`
Expected: 全數通過或維持既有唯一失敗 `test_storage_hw`（本 task 不新增任何 native test case，
數字應與 Task 4 完成時完全相同——這步純粹確認 `ui_settings.h`/`ems_display_snapshot.h` 沒有
連帶被本 task 的 `app_globals.h` 改動破壞）

- [ ] **Step 5: 重生 VLW 字型（新增六列標籤字串）**

`名稱`／`型號`／`序號`／`韌體` 是新字串（`電池`／`充電狀態`已存在），執行：

```bash
cd firmware
bash scripts/regen_vlw.sh
```

確認腳本輸出 0 缺字。`ui_screens.cpp` 已在既有 `SRC_FILES` glob（`src/ui_*.cpp`）涵蓋範圍內，不需修改腳本。

- [ ] **Step 6: 韌體編譯確認（含 Task 4 的 `drawDeviceInfo()` 呼叫點一併驗證）**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS——這一步是本 task 邏輯正確性的主要驗證管道（`src/` 不進 native build），
同時驗證 Task 4 Step 8 遺留的呼叫點現在能正確連結

- [ ] **Step 7: Commit**

```bash
cd firmware
git add src/ui_screens.cpp src/app_globals.h \
        src/ems_zh_24_vlw.h data/fonts/ems_zh_24.vlw
git commit -m "[PHASE-G] feat: 裝置資訊畫面本體（SoT §19.7）

drawDeviceInfo() 六列：名稱（每次重讀，比照 drawSettingsMenu 既有模式）／
型號（固定字面常數）／序號（既有 SYNC_DEVICE_ID，與案件同步 metadata 同源，
見 spec §4.1.1）／韌體（既有 SYNC_FW_VERSION）／電池／充電狀態（逐字沿用
drawBatteryInfo 既有三態轉譯與 is_battery_absent 統一出口）。

無 native test（ui_screens.cpp 屬 src/，native build 排除，與 drawBatteryInfo
等既有畫面函式同一慣例）。字型重生：新增名稱/型號/序號/韌體四字串，0 缺字。
韌體編譯 SUCCESS（含 Task 4 遺留呼叫點一併驗證通過）。"
```

---

### Task 6: Wave 收尾——文件更新

**Files:**
- Modify: `docs/pm-dev-spec.md`
- Modify: `docs/superpowers/phase-g-device-info-handover.md`（brainstorming 完成後已建立，見任務外的初始版本）

**Interfaces:** 無程式碼變更，純文件收尾。

- [ ] **Step 1: 更新 `pm-dev-spec.md §四 Phase G`**

Modify `docs/pm-dev-spec.md`，把「裝置資訊畫面（...）：... 未開工」的段落改為已完成，標註本計畫最終 commit hash 與完成日期，比照 Phase H 各 task 完成時的文件更新慣例。

- [ ] **Step 2: 更新 handover 文件**

Modify `docs/superpowers/phase-g-device-info-handover.md`（初始版本已在計畫產出當下建立），記錄 5 個 task 的完成狀態、最終 commit hash、native test 數字、韌體 Flash/RAM 占用、以及本計畫執行過程中發現的設計修正（§4.1.1 序號方案修正）。格式比照 `docs/superpowers/phase-h-handover.md` 的單一時間線 + §3-A 系列小節模式。

- [ ] **Step 3: 全套驗證**

Run: `cd firmware && pio test -e native`
Expected: 全數通過或維持既有唯一失敗 `test_storage_hw`

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS，記錄 Flash/RAM 占用百分比供 handover 文件引用

- [ ] **Step 4: Commit**

```bash
git add docs/pm-dev-spec.md docs/superpowers/phase-g-device-info-handover.md
git commit -m "[PHASE-G] docs: 裝置資訊畫面 Wave 收工——pm-dev-spec 與 handover 文件更新

5 個 task 全部完成：捲動 clamp 共用邏輯、選單 8 項化、按鍵接線、
DisplaySnapshot 接線、裝置資訊畫面本體。pm-dev-spec.md §四 Phase G
標記完成，handover 文件記錄最終狀態與所有上機驗收待辦。"
```

---

## 上機驗收清單（W3 完成後，需要實體硬體）

以下項目 native test 與韌體編譯無法涵蓋，計畫完成後累積待驗（比照 Phase H handover §3-B 模式）：

| 驗收項 | 步驟 | 預期結果 |
|---|---|---|
| 選單捲動流暢度 | 進系統設定，連續按 DOWN 8 次繞完整圈 | 畫面平滑捲動，無殘影/閃爍，游標高亮位置正確跟隨 |
| App連線設定／Type-C連線 placeholder | 游標停在第 6/7 項按主鍵 | 顯示「尚未實作」，任意鍵返回設定選單 |
| 裝置資訊六欄正確性 | 進裝置資訊畫面 | 名稱與裝置本身設定一致、型號固定顯示 EMS DoseSync Pro、序號與 App 端案件同步紀錄的 device_id 一致、韌體版本字串正確、電池%與充電狀態跟電池資訊畫面顯示一致 |
| 恢復預設對話框捲動後觸發 | 捲到選單中段（如第 6 項）長按主鍵 | 對話框正確顯示，視窗最後一格項目正確跳過繪製不與對話框重疊 |
| 裝置名稱捲出視窗行為 | 捲到選單底部（裝置資訊項）再捲回頂部 | 裝置名稱正確恢復顯示，鎖定/置灰狀態正確（若當下有未同步案件） |
