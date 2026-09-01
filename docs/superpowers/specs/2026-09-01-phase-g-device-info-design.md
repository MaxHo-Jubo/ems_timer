# Impl-Phase G 裝置資訊畫面 — 設計 Spec

- **日期**：2026-09-01
- **範圍**：系統設定選單擴充至 SoT §19.1 完整 8 項（原 5 項 + App連線設定/Type-C連線 placeholder +
  裝置資訊）+ 裝置資訊畫面本體（名稱／型號／序號／韌體／電池／充電狀態）
- **前置**：Impl-Phase H 電量顯示已完成（`g_battery_percent`/`g_battery_charge_state` 全域可用）、
  Phase G Wave 1/2 已完成（`settings_get_device_name()` 可用）
- **狀態**：設計已確認，待產實作計畫

---

## 1. 背景

`docs/pm-dev-spec.md §四 Phase G` 原規劃「韌體版本 read-only」一項，從未落地成 UI；SoT V1
§19.7「裝置資訊」畫面（名稱／型號／序號／韌體／電池／充電狀態六欄）也從未實作，只存在於 SoT
文字描述與 `docs/demo/index.html` 網頁 mockup。2026-08-30 使用者裁決：把兩者合併，一次併入
Impl-Phase G 擴充範圍（詳見 `docs/superpowers/phase-h-handover.md §3-A7`）。

本 spec 是那次裁決後、真正動工前補的設計文件（handover §3-A7 原文：「日後要 dispatch 這塊時，
需要先幫 Phase G 補一份 spec/plan，比照 Phase H 的做法」）。

### 1.1 SoT 相關條文

| 條文 | 要求 |
|---|---|
| §19.1 | 系統設定選單完整 9 項：裝置名稱／螢幕亮度／系統音量／通氣音量／電池資訊／App連線設定／Type-C連線／裝置資訊／韌體版本 |
| §19.7 | 裝置資訊畫面顯示「名稱：安康91／型號：EMS DoseSync Pro／序號：DSP-0001／韌體：v1.0.0／電池：86%／充電狀態：充電中」 |

### 1.2 規格缺口（本 spec 補上的決策）

SoT 沒有定義下列任何一項，以下為本 spec 拍板（皆經與使用者逐項確認，見 §2）：

- 序號的資料來源（SoT 只給了範例字串 `DSP-0001`，沒說怎麼產生）
- 「裝置資訊」與「韌體版本」是否為同一畫面（SoT §19.1 字面上是兩個選單項目）
- 8 項選單在 240px 螢幕上的排版方式（SoT 未考慮實體螢幕限制）
- 韌體版本字串本身的格式（`SYNC_FW_VERSION = "v0.6-phaseF"` 是既有的過渡期占位字串）

---

## 2. 已確認決策（brainstorming 逐項討論記錄）

| # | 問題 | 決策 | 理由 |
|---|---|---|---|
| 1 | 序號資料來源 | ESP32 efuse MAC 衍生，開機時算一次 | 不需硬體改動、不需產線流程、每台裝置天然唯一 |
| 2 | 裝置資訊／韌體版本選單項目 | 合併成一個「裝置資訊」項，韌體版本是畫面內一列 | 沿用 pm-dev-spec.md 已記錄的裁決 |
| 3 | 韌體版本字串本身 | 沿用 `SYNC_FW_VERSION`，內容不變 | 版號管理是獨立規範問題，不在本次畫面設計範圍 |
| 4 | 型號欄位 | 字面常數 `"EMS DoseSync Pro"` | 沿用專案命名慣例，無需動態來源 |
| 5 | 選單順序 | 維持 SoT §19.1 順序，中間插入 App連線設定／Type-C連線 placeholder | 使用者選擇維持規格順序而非圖省事重排 |
| 6 | 8 項裝不下 240px 畫面 | 加捲動機制，比照既有 `historyScrollOffset` 模式，一頁顯示 5 項 | 使用者選擇正規解法而非壓縮既有已驗收版面 |
| 7 | 裝置名稱（游標 0）是否納入捲動窗 | 納入，統一 8 項同一套捲動邏輯 | 與歷史紀錄清單的捲動方式一致，避免游標邏輯分裂成兩套 |

---

## 3. 選單結構變更

### 3.1 最終 8 項清單與游標值

```
0  裝置名稱      （既有，特例渲染：鎖定/置灰邏輯不變）
1  螢幕亮度      （既有，可調值）
2  系統音量      （既有，可調值）
3  通氣音量      （既有，可調值）
4  電池資訊      （既有，導覽項，Task 12/13 完成）
5  App連線設定   （新增，placeholder）
6  Type-C連線    （新增，placeholder）
7  裝置資訊      （新增，導覽項，本 spec 主體）
```

`SETTINGS_MENU_COUNT`：5 → 8。

### 3.2 捲動機制

比照既有 `firmware/src/ui_screens.cpp` `drawHistoryList()` / `historyScrollOffset` 模式：

- 新增 `SETTINGS_VISIBLE_ROWS = 5`（與既有 `HISTORY_VISIBLE_ROWS` 同值，維持畫面資訊密度一致；8 項選單一頁顯示其中 5 項）
- 新增 `settingsScrollOffset` 全域，UP/DOWN 按鍵時套用與歷史紀錄清單相同的 clamp 邏輯：
  - `cursor < offset` → `offset = cursor`（游標上移超出可見窗頂部，跟著往上捲）
  - `cursor >= offset + SETTINGS_VISIBLE_ROWS` → `offset = cursor - (SETTINGS_VISIBLE_ROWS - 1)`（游標下移超出可見窗底部，跟著往下捲）
- 進入設定選單時 `settingsScrollOffset` 重置為 0（比照 `historyScrollOffset` 進入歷史紀錄時的重置慣例）

**共用化**：這是本 repo 第二個需要同一種「游標／捲動窗」clamp 邏輯的畫面（第一個是歷史紀錄清單）。
依 `EXTRACT-SHARED-HELPER` 規則（2+ 呼叫點出現同一概念判斷即該抽），本次把 clamp 邏輯抽成
共用純函式：

```cpp
// 建議位置：firmware/lib/ui_settings/ 或既有 ems_ohca 之外的通用 UI lib
uint16_t clampScrollOffset(uint16_t cursor, uint16_t currentOffset, uint8_t visibleRows);
```

`historyScrollOffset` 既有的 inline clamp（`input_handler.cpp` 現有兩處）與新的
`settingsScrollOffset` clamp 都改用這個函式，兩處各自加 native test 或至少 regression 測試不因抽換而變。

### 3.3 裝置名稱納入捲動窗的結構性影響

`kSettingsAdjustableItems[]`（`firmware/lib/ui_settings/ui_settings.cpp`）目前：

- `y` 欄位是**查表填死的絕對螢幕座標**（`SETTINGS_ITEM2_Y = 70` 這類常數）
- 裝置名稱（游標 0）**不在這張表裡**，獨立於 `drawSettingsMenu()` 的 STEP 03 特例分支，固定畫在
  `SETTINGS_ITEM1_Y = 30`

本次改動後裝置名稱要跟其餘 7 項一起參與捲動，`y` 欄位語意必須從「絕對座標」改成
「捲動窗內的相對列序」，由呼叫端在繪製當下算：

```
row_in_window = table_index - scrollOffset   // 只有落在 [0, SETTINGS_VISIBLE_ROWS) 才畫
actual_y = SETTINGS_ITEM1_Y + row_in_window * SETTINGS_ROW_SPACING
```

裝置名稱的鎖定/置灰渲染邏輯（STEP 03.01/03.02）**內容不變**，只是不再保證畫在固定 Y——它現在
是表格裡的第 0 列，捲出可見窗時跟其他項目一樣不畫。

`kSettingsAdjustableItems[]` 新增 3 列：

```cpp
{ SETTINGS_CURSOR_APP_CONN,    "App連線設定" },  // placeholder
{ SETTINGS_CURSOR_TYPEC_CONN,  "Type-C連線" },   // placeholder
{ SETTINGS_CURSOR_DEVICE_INFO, "裝置資訊" },      // 本 spec 主體
```

（表格移除固定 `y` 欄位，改由 STEP 04 迴圈依 `table_index - scrollOffset` 算圖層座標；裝置名稱
併入同一張表或保留獨立分支但納入同一套座標公式，實作階段依程式碼現況決定，不影響本設計的
外部行為）

### 3.4 Placeholder 兩項的行為

`App連線設定` / `Type-C連線` 選到後按主鍵，呼叫既有 `drawPlaceholder(title, phase)`
（`firmware/src/ui_screens.cpp:370`，Training/History 主選單當年用過的同一個函式）：畫「尚未實作」
提示，任意鍵返回設定選單。**不需要新的 mode flag**——沒有真正的子畫面狀態要記，這兩項在
`DisplaySnapshot` 裡不佔任何新欄位。

---

## 4. 裝置資訊畫面

### 4.1 六個欄位與資料來源

| 欄位 | 來源 | 型態 | 是否進 DisplaySnapshot |
|---|---|---|---|
| 名稱 | `settings_get_device_name()` | 每次繪製重新讀 | 否（比照 `drawSettingsMenu()` 既有做法，非 snapshot 驅動） |
| 型號 | 字面常數 `"EMS DoseSync Pro"` | 靜態 | 否 |
| 序號 | ESP32 efuse MAC 衍生，`setup()` 時算一次快取 | 開機後不變 | 否 |
| 韌體 | 既有 `SYNC_FW_VERSION` 常數 | 靜態，內容不變 | 否 |
| 電池 % | `g_battery_percent`（既有全域） | 動態 | 是（已在，Task 7 起） |
| 充電狀態 | `g_battery_charge_state`（既有全域） | 動態 | 是（已在，Task 7 起） |

**名稱不進 snapshot 的已知行為**：`drawSettingsMenu()` 本身也是每次呼叫重讀名稱、不經 snapshot
diff 觸發重繪——若使用者停在裝置資訊畫面時 App 在背景改名，畫面要等下一次「有其他理由觸發重繪」
（如電量變化）才會連帶顯示新名稱。這是既有設定選單本來就有的行為，本次不新增風險，也不在本次
範圍內修。

### 4.2 序號格式

`ESP.getEfuseMac()` 取後 2 bytes 轉 4 位大寫 hex，組成 `"DSP-XXXX"`（與 SoT mockup `DSP-0001`
同樣長度）。純轉換邏輯抽成：

```cpp
// firmware/lib/ems_settings/ 或新檔，純函式不碰硬體
void format_serial_from_mac(uint64_t mac, char* out, size_t out_size);
```

native test 可直接餵固定 `uint64_t` 值驗證格式，不需要真的燒 ESP32 efuse（比照
`fuel_gauge_logic.h` 把純邏輯與硬體 I/O 分離的既有慣例）。

### 4.3 畫面本體

新增 `firmware/src/ui_screens.cpp` 的 `drawDeviceInfo()`，比照 `drawBatteryInfo()`（Task 13）的
既有寫法：STEP 編號、統一出口、不編造缺值。與電池資訊畫面不同的是本畫面沒有「不在線」狀態——
六個欄位在啟動後必定都有值（序號/型號/韌體是靜態常數，名稱有預設「未命名」，電池沿用既有
`is_battery_absent()` 判斷只影響電池/充電狀態兩列的顯示文字，不影響整個畫面的可用性）。

### 4.4 DisplaySnapshot 變更

新增 `SNAP_FLAG_SETTINGS_DEVICE_INFO = 0x00400000`（下一個可用 bit，接續 Task 13 的
`0x00200000`）+ `settingsScrollOffset` 欄位（比照既有 `historyScrollOffset` 五步驟：struct
欄位／`DisplaySnapshotInputs`／`captureDisplaySnapshot()` 填值／`captureSnapshot()` 映射／
`snapshotsEqualExceptCountdown()` 自動涵蓋——Task 7 修完的比較路徑會自動接住新欄位，不需要
再手動同步第二套比較清單）。

新增全域 `bool settingsDeviceInfoMode`，比照 Task 13 `settingsBatteryInfoMode` 的既有 pattern：
`input_handler.cpp` 主鍵進入時開啟、返回鍵離開時關閉，`main.cpp` `updateDisplay()` 依此 flag
分派進 `drawDeviceInfo()`。

---

## 5. 測試策略

**native**

- `clampScrollOffset()`：上捲/下捲/窗內不動三種邊界，游標在 0 與 `SETTINGS_MENU_COUNT - 1`
  的邊界案例
- `format_serial_from_mac()`：固定輸入值 → 固定輸出字串，含 MAC 低 2 bytes 為 `0x0000` 與
  `0xFFFF` 的邊界案例
- `drawDeviceInfo()` 在 native mock display 上的六列文字斷言（比照 `test_settings_ui`
  既有 pattern）
- 選單捲動後裝置名稱（游標 0）捲出可見窗時不繪製、捲回時正確恢復鎖定/置灰狀態
- `SETTINGS_MENU_COUNT` 8 項的 wrap-around（`wrapSettingsCursor()`）回歸測試

**on-target**

- 實機驗證 8 項選單捲動流暢、裝置資訊畫面六欄數值與電池資訊畫面/App 端顯示的名稱一致
- 序號欄位換機測試：不同 ESP32 板子（不同 MAC）顯示不同序號

---

## 6. Wave 拆分（初步，實作計畫階段再細化）

| Wave | 內容 | 可獨立驗收的點 |
|---|---|---|
| **W1** | 選單捲動重構（`clampScrollOffset()` 抽出 + 8 項表格 + 裝置名稱納入捲動窗） | 選單能捲動到 8 項，既有 5 項行為不變（回歸測試通過） |
| **W2** | Placeholder 兩項（App連線設定／Type-C連線） | 選到後顯示「尚未實作」，返回鍵可離開 |
| **W3** | 裝置資訊畫面本體（含序號衍生邏輯） | 設定選單進得去，六欄數值正確 |

---

## 7. 不在本 spec 範圍

- **App 連線設定／Type-C 連線的真正實作**：本次只做 SoT 選單順序要求的 placeholder，兩者的
  真實畫面與功能是各自獨立的未來工作（Type-C 連線對應 `pm-dev-spec.md §四 Phase G` 的
  「Type-C 管理工具 MVP（列案件／匯出／清除）」，尚未排入本次範圍）
- **`settings_menu_item_t` 型別設計債重構**（whole-branch review 殘餘風險 ⑩⑪）：本次選擇
  維持既有查表模式擴充，不藉此機會做 `enum class SettingsViewMode` / action-kind 欄位的
  結構性重構，理由與取捨見 brainstorming 討論記錄（本 spec §2 決策 #7 的替代方案 B 未採用）
- **選單反白框覆蓋不足**（whole-branch review 殘餘風險 ⑨，17% 面積問題）：8 項擴充後這個
  既有視覺缺陷會出現在全部 8 列，不在本次修復範圍，維持既有 park 狀態
- **韌體版本字串格式重新設計**：沿用 `SYNC_FW_VERSION` 現有內容，版號管理規範是獨立問題

## 8. 已知限制

- 裝置名稱在裝置資訊畫面內不經 `DisplaySnapshot` 驅動重繪（見 §4.1），與既有 `drawSettingsMenu()`
  行為一致，非本次新增缺口
- 序號僅在單一 ESP32 晶片上衍生，沒有跨裝置註冊/管理機制——量產階段若需要正式序號制度
  （如追溯生產批次），現行方案需要重新設計，本 spec 僅涵蓋 V1 開發階段需求
