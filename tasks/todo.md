# EMS Timer 開發進度

> **2026-05-25 重整**：折疊 5/8 TFT 整合過時內容 + 合併重複 review baseline 段。Impl-Phase A 韌體已完成（2026-05-04），網頁端 Phase F 移至 [`tasks/phase-f-todo.md`](phase-f-todo.md) 為權威來源。本檔聚焦：當前焦點 + 已修 bug 索引 + 各層 backlog + 後續階段。

---

## 🎯 當前焦點（2026-05-25）

> **2026-07-04 更新**：電源模組電池供電 TFT 白屏 blocker **已清**（負極/地線接點焊死，使用者實測通過，見下方 §⚡ V5IN 塊）。當前活躍軌道＝純韌體 backlog（DS3231 R1-R6 進行中）；剩餘電源工作＝整合測試（30 分放電等，需上機）。

- **🔥 進入電源模組手工焊接 + 電源管理階段**（採購已到貨）：1000mAh LiPo + TP4056 + JST PH2.0
  - 採購驗收 / 組裝 SOP / 整合測試見下方 §⚡「電源模組採購驗收」段
  - 對應 SoT V1 §20.4 充放電拓樸 + §21.1 主要硬體清單
  - 量產化前最後一階段硬體封版
- **📌 下次 session 優先（純韌體 backlog）**：見 §🔬 B1-B5 follow-up（5 項）+ §🔬 DS3231 R1-R6（6 項）— 兩段共 11 項純韌體重構/補 test，電源模組工作不阻擋
- **DS3231 永續性測試待跑**（5 分鐘）：CR2032 紐扣電池備援能力未實測
- **5/24 實機 UI bug 5 隻全清完**（B1-B5）：見下方「實機 bug 已修」段；全部 PM 實機驗證 + POST-COMMIT-REVIEW 28/30 + 推 origin（commit `bdf6647`）
- **網頁端 Phase F**：BLE 鏈路 + DS3231 + UI bug 全清，**階段 1 主要里程碑達成**（見 `docs/progress.md` 進度 6+7+8）
- **Phase E review-pr Batch 2**：工程層全部完成；剩 Group 2C UI 反饋等 PM 對齊失敗哲學 A/B/C
- **韌體 Phase B~H 規劃**：見 [`docs/pm-dev-spec.md §四`](../docs/pm-dev-spec.md)

---

## ✅ 2026-05-24 實機 bug 已修（B1-B5 全部 PM 實機驗證通過）

> **背景**：DS3231 RTC 整合 6 wave 完成、實機驗收通過（見 `docs/progress.md` 進度 7）。同時段在實機上觀察到以下 4 個 UI / 顯示 bug + 6 秒給氣 LOCKED 殘留（B5），與 RTC 整合無關，獨立修。**5/25 全部修完並通過 PM 實機驗證。詳見 `docs/progress.md` 進度 8**。

### B1. 字串缺字（dialog + 案件總覽 OHCA）

- **症狀**：
  - dialog「是否再次同步**至** App？」缺「至」
  - 案件總覽「同步**狀態：**App未同步」缺「狀 態 ：」3 字
  - 案件總覽「同步**至** App」缺「至」
- **真根因（2026-05-25 第二輪調查）**：
  - firmware 透過 `display.loadFont(ems_zh_24_vlw)` 載入**嵌在 binary 的 PROGMEM C array**（`firmware/src/ems_zh_24_vlw.h`），**不是 LittleFS 的 .vlw 檔**
  - 過去 `regen_vlw.sh` **只生 .vlw binary，沒重生 .h header**，導致 build firmware 仍嵌入舊字集（258 glyphs，缺「狀 態 ： 至」）
  - commit `e79f4f9` (5/24) commit msg 提「字型」但實際只更 `.vlw` 沒更 `.h` → 補字看似完成但實機仍缺
  - `firmware/src/ems_zh_24_vlw.h` mtime 自 commit `078d3b2` (5/15) 之後**從未更新**
- **修法**：
  - 新增 `firmware/tools/vlw2header.py` (.vlw → PROGMEM C array)
  - `regen_vlw.sh` STEP 09 自動呼叫 vlw2header.py，確保 .h 跟 .vlw 同步
  - 重生 `firmware/src/ems_zh_24_vlw.h` 為 282 glyphs / 110.2 KB / `ems_zh_24_vlw_len = 112844`
- **build**：Flash 68.6% / RAM 32.3% SUCCESS
- [x] 已修（commit 7f81f6b + c80ac31）
- [x] **PM 實機驗證通過（2026-05-25）**：重燒 firmware 後 dialog + 案件總覽缺字全部消失

### B2. OHCA 案件總覽畫面排版異常（互相疊字）

- **症狀**：進入「案件總覽」OHCA 畫面後，標題 / 同步狀態 / EPI label 三行幾乎完全重疊
- **真根因（2026-05-25）**：drawOhcaSummary 兩個 bug 疊加：
  1. `SYNC_STATUS_Y = OHCA_BADGE_Y + 14` 對 1.2× 標題（佔 29px 高，y=14~43）只往下 14 → sync row y=28 **完全在標題內**
  2. 整個 layout 塞 9 row（標題 + 同步 + EPI 3行 + 電擊 2-3行 + Amio + sub-menu 2行）在 240px 高度內，原 LINE_H=22 + size 1.0× row 無法 fit，會撞 sub-menu Y_BASE=166
- **修法（PM 對齊「縮字 + 縮 LINE_H 不砍 row」策略）**：
  - `SYNC_STATUS_Y`: `OHCA_BADGE_Y + 14` → `+32`（避開標題）
  - EPI 起點 y: 50 → 78（避開 sync row）
  - body `setTextSize(1)` → `setTextSize(0.85f, 0.85f)`（vlw 24×0.85 ≈ 20px）
  - LINE_H: 22 → 18
  - SUMMARY_SUBMENU_ROW_H: 24 → 22
  - SUMMARY_SUBMENU_BOTTOM_MARGIN: 8 → 4
  - sub-menu 也 setTextSize(0.85f) 配合 ROW_H 22
- **build**：native 419/420 PASSED / ESP32 Flash 68.6% SUCCESS
- [x] 已修（commit ae953ca + 後續 sub-menu 下移）
- [x] **PM 第一輪實機驗證（2026-05-25）**：標題/同步/EPI/電擊 不疊 ✓，但 Amio 仍撞 sub-menu「事件時間軸」
- [x] **B2.2 後續修**：SUMMARY 的 `SUBMENU_Y_BASE` 算式拿掉 `OHCA_COUNTER_BOTTOM`（SUMMARY 不顯示底部 counter，原扣 18px 是浪費）→ sub-menu 從 y=174 下移到 y=192
- [x] **PM 第二輪實機驗證通過（2026-05-25）**：Amio 跟 sub-menu 不再重疊

### B3. OHCA 案件總覽返主選單後游標位移未對齊文字

- **症狀**：從案件總覽 / OHCA 畫面按返回回到主選單後，游標 highlight 框與選項文字位置沒對齊
- **真根因（2026-05-25）**：`drawMainMenu` 文字渲染用 `setCursor + print`（Print stream 路徑）；其他畫面（drawCenteredText / drawOhcaSummary 等）用 `drawString + setTextDatum`。`print()` 在 vlw 字型下走 baseline-relative 偏移，跟 fillRect highlight 用的 raw Y 座標含義不同 → 文字與 highlight 框 Y 軸錯位
- **修法**：drawMainMenu 改用 `setTextDatum(textdatum_t::top_left) + drawString(text, x, y)`，與其他畫面 path 一致（top_left datum 明確指定 (x, y) 為文字 top-left 角，跟 fillRect 同座標含義）
- **build**：native 32/32 PASSED / ESP32 Flash 68.6% SUCCESS
- [x] 已修（commit 7a80392）
- [x] **PM 實機驗證通過（2026-05-25）**：返主選單後 highlight 框與文字對齊

### B5. 6 秒給氣在 OHCA 案件結束後（LOCKED/SUMMARY）反覆顯示

- **症狀**：OHCA 結束前開過 6 秒給氣 overlay，案件結束進 LOCKED / 案件總覽後畫面反覆閃
- **真根因（2026-05-25）**：main.cpp:794 渲染條件已排除 LOCKED/SUMMARY 不畫 overlay，但 `ventStartMs` 沒清 → `captureDisplaySnapshot` line 639 仍每秒算出新 `ventBeat` → snapshot dedup 每秒 miss → LOCKED/SUMMARY 畫面每秒全螢幕重畫造成視覺閃爍（不是 overlay 反覆出現，是底層 redraw 反覆觸發）
- **SoT 對齊**：§14.12「OHCA 案件結束後，OHCA 內 6秒通氣提示自動停止」— LOCKED 為案件結束時點，「停止」spirit 涵蓋 timer + render 都停（韌體原本只停 render 沒停 timer，半套）
- **修法**：
  - `ohca_logic.cpp:dispatchOhcaEvent` STEP 05 新增「首次進 LOCKED」block reset `ohcaVentOverlayEnabled=false / ohcaVentPaused=false / ventStartMs=0`
  - `exitOhcaCase` 補 `ventStartMs=0`（exit 原漏項，防 timer 殘留下次進 OHCA 沿用舊 beat）
- **build**：native 419/420 PASSED（1 ERROR 為 baseline test_storage_hw）/ ESP32 Flash 68.6% SUCCESS
- [x] 已修（commit 0b2b595）
- [x] **PM 實機驗證通過（2026-05-25）**：LOCKED 畫面與案件總覽不再每秒閃爍
- **後續觀察**：若需「進 LOCKED 前」更早結束 6 秒給氣（例：END_CHECK 顯示時）日後再修

### B4. 結束前檢查頁面游標上下移動異常

- **症狀**：進「結束前檢查」頁面，預設游標停在「返回案件」項目。第一次按 UP 不會往上移，要按兩次才動；後續游標順序也異常
- **真根因（2026-05-25）**：`DisplaySnapshot` struct **漏 `endCheckCursor` 欄位**（同 Phase E `historyCursor` / Phase F `summarySubmenuCursor` 漏欄位 bug 的第三次重演）。按 UP/DOWN 改 `endCheckCursor` 但 snapshot memcmp 看不到變化 → `updateDisplay` 早 return 跳過 redraw → 看起來「第一次按沒動」；第二次按碰巧搭上其他 state 變化才觸發重繪 → 看起來「按兩次才動」
- **修法**：
  - `ems_display_snapshot.h` `DisplaySnapshot` + `DisplaySnapshotInputs` 加 `endCheckCursor` 欄位 + `captureSnapshot` 映射
  - `main.cpp:captureDisplaySnapshot` 帶入 `in.endCheckCursor`
  - `main.cpp:sameStateAsLast` 比對加 `endCheckCursor`（避免被誤判 partial path）
  - `test_display_snapshot.cpp` 加 `test_end_check_cursor_change_triggers_redraw_b4_regression` 鎖死（仿 Phase E/F 同類 regression test pattern）
- **build**：native 32/32 PASSED / ESP32 Flash 68.6% SUCCESS
- [x] 已修（commit 8388ce7）
- [x] **PM 實機驗證通過（2026-05-25）**：cursor 第一次 UP 即動 + 順序 wrap 正確

> 💡 修 bug 前先實機重現確認症狀；建議單 commit 一個 bug，便於 PR review。

---

## 🔬 2026-05-25 B1-B5 POST-COMMIT-REVIEW follow-up backlog（📌 下次 session 優先 — 純韌體）

範圍 `7f81f6b..183468b` review agents 找到的 MEDIUM/SUGGESTION 留待後續：

- [ ] **drawOhcaSummary 6 段 setTextDatum + drawString 重複可抽 `drawKVRow` helper**（quality agent）— 減 ~60 行重複 + 消除 datum-toggle 隱式依賴
- [ ] **drawOhcaSummary magic numbers 衍生**：`+32 / 78 / 18 / 0.85f` 從字型 size 常數衍生，避免 PM 改字級時 layout drift（quality agent）
- [ ] **B3 datum fix 可能未傳播至 `drawBackfillCategory` / `drawBackfillType` / `drawBackfillCount` / `drawQuickMenu`**（reuse agent）— 仍用 setCursor+print pattern，可能有同類 baseline 偏移問題；等 PM 實機確認後再修
- [ ] **B5 `dispatchOhcaEvent` 進 LOCKED reset vent state 缺 native test**（pr-test-analyzer rating 7/10）— 需 stub 10+ globals + storage backend；overhead 大留 backlog。建議新建 `test_ohca_dispatch.cpp` 覆蓋 `prev != LOCKED && state == LOCKED → vent state cleared` invariant
- [ ] **`regen_vlw.sh` STEP 09 失敗訊息可補 `trap ERR`**（silent-failure-hunter）— 避免 .vlw 已更但 .h 未同步時使用者未察覺
- [x] **`ventPaused` vs `ohcaVentPaused` 命名/scope 確認**（2026-05-25 grep 確認）— 兩個獨立變數共存於不同 cpp（`ventPaused` = GLOBAL_VENT 獨立模式 scope，`ohcaVentPaused` = OHCA overlay scope），命名合理保留

---

## 🔬 2026-05-24 DS3231 整合 follow-up refactor（📌 下次 session 優先 — 純韌體）

> **背景**：commit `8948b13` DS3231 整合 + amend 後跑 4 個 review agent（pr-reviewer / silent-failure-hunter / pr-test-analyzer / type-design-analyzer）。Critical/Important 已在 commit 修；以下為 deferred 給後續獨立 commit。

### R1. 抽 boot-seed / write-back helper 進 lib（reuse review #2）

- `main.cpp:412-430` boot seed `if (begin) → seed_if_valid` 邏輯與 `test_rtc_integration` 的 `simulate_boot_detect_and_seed` 是同一個函式換皮
- `main.cpp:262-272` BLE write-back 同類重複
- 對齊 `feedback_extract_testable_pure_logic` 規則：抽 `ems::rtc_try_seed(backend, state, now, floor, ceiling)` + `ems::rtc_write_back_if_applied(backend, state, now)` 到新 lib `ems_rtc_glue/`（或 `ems_time_sync` 內），main.cpp 與 test 都呼叫
- 風險：跨 lib 依賴（ems_rtc + ems_time_sync），`lib_ldf_mode = deep+` 已開應可
- [ ] 待做

### R2. `RtcReading` struct 取代 sentinel 0（type-design review）

- `now_epoch_ms()` 用 0 同時表示「not present」與「合法 1970-01-01 epoch」conflate state
- 改成 `struct RtcReading { bool valid; uint64_t epoch_ms; }`，type-level 明確「未對時」概念
- 影響：RtcBackend 介面 + 3 個 backend 實作 + 6 個 caller 點
- [ ] 待做

### R3. `SetResult` enum 取代 set_epoch_ms bool（type-design review）

- 現 `bool set_epoch_ms` 無法區分 `NullBackend no-op` vs `DS3231 I2C 寫失敗`
- 改成 `enum class SetResult { Ok, NotPresent, IoError }`，main.cpp write-back log 才能精準分類
- 配套：DS3231Backend.cpp 加 read-back verify（每筆 BLE write-back 多一次 I2C 往返；可選）
- [ ] 待做

### R4. ohca_logic case_start/end_epoch 路徑 native test（pr-test-analyzer #2）

- save 路徑改成 live epoch 後，partial-sync 場景（case 開始時未對時、結束時已對時）無 test
- 加 `test_ohca_logic` 純測試（或 `test_ohca_state` 內補）覆蓋三種 sync 狀態 × 兩個欄位
- [ ] 待做

### R5. seed_from_rtc with rtc_epoch < now_millis underflow guard test

- F4 已加 guard，但無對應 native test 鎖死。`test_time_sync` 加 1 case：seed underflow 後 state 仍 unsynced
- [ ] 待做

### R6. BLE write-back with two distinct now_millis test（pr-test-analyzer #4）

- `simulate_ble_apply_with_write_back` 用單一 now_millis；production `time_sync_handle` 與 `current_epoch_ms` 各讀一次 millis
- 加 test 鎖死兩讀漂移容忍
- [ ] 待做

---

## ⏳ 2026-05-24 DS3231 永續性測試待跑

> **背景**：DS3231 整合 Wave 1-6 已完成並實機驗收 boot 偵測 + seed + write-back 路徑。但 **CR2032 紐扣電池備援能力尚未實測**。

### 測試流程

1. 接 DS3231 + 連 App + 完成 BLE time_sync（log 應顯示 `[RTC] write-back from BLE time_sync: <epoch>`）
2. 拔 USB 電源 → 等 30 秒（裝置完全斷電，但 DS3231 走 CR2032 維持走時）
3. 重接電源 → 看 boot log
4. **預期**：`[RTC] DS3231 detected at 0x68` + `[RTC] seeded software clock from RTC: <epoch>`（epoch 比上次對時值大 ~30000）
5. **若失敗**：log 顯示 `[RTC] DS3231 present but time not set, 等 BLE time_sync` → 表示 CR2032 沒電或座未通

### 驗收標準

- [ ] boot log 顯示 seeded（不需 BLE）
- [ ] seeded epoch 比上次對時值合理（30 秒誤差 ± 1 秒）

> 💡 失敗時的硬體排查：(1) CR2032 電壓 > 2.7V，(2) 紐扣電池座焊點通，(3) DS3231 VBAT 腳與 CR2032 正極連通

---

## 🎨 TFT 整合 backlog（2026-05-08 立案 → 2026-05-25 主要 Step 完成）

> **歷史背景**（2026-05-08 ~ 2026-05-25）：SH1106 OLED 升級到 ST7789 2.8" TFT 320×240，lib 由 Adafruit_GFX → LovyanGFX + DMA。Step 1/2a/2b/2c/2d + 中文化（efontTW_24 → 自訂 vlw）+ Phase B 補登/總覽/QuickMenu + Demo 對齊 Batch 1/2/3 + 字體放大 + 5/24-25 實機 bug B1-B5 收尾。Step 1-3 詳細歷史見 git log。
>
> **硬體**：ESP32-S3 N16R8、ST7789 蝦皮紅板、SPI GPIO 2/3、DC GPIO 1（避開 GPIO 48 板上 WS2812）、`setRotation(3)`、panel cfg `invert=false`。詳見 memory `feedback_lovyangfx_dma_for_tft.md`、`project_tft_ui_design_target.md`。

### 剩餘 backlog（5 項）

- [x] **SoT V1 §21.2 外殼開孔尺寸**（2026-05-25 grep 確認 line 2948「TFT 60×45mm」+ 2949 標 OLED 已棄用）
- [x] **CLAUDE.md 更新 OLED → TFT 描述**（2026-05-25 grep 確認，OLED 字眼僅剩 Phase 1 歷史紀錄合理保留）
- [x] **移除 `OLED_WIDTH/HEIGHT` 別名 + `SH110X_WHITE/BLACK`**（2026-05-25 grep 確認 firmware/src+lib 全 0 命中；commit `578bc1e` 清完）
- [ ] **抽出 `firmware/lib/ems_display/ems_display.{h,cpp}` 模組**（多畫面穩定後，nice-to-have）
- [x] **drawMainMenu 標題「EMS DOSESYNC PRO」本地化**：B 不動（demo 也用英文）；同 drawOhcaCountdownCommon OHCA badge 不動 — 結案

### 22 commits POST-COMMIT-REVIEW 待補跑

> 5/9 ~ 5/14 期間 22 commits（中文化 / Demo 對齊 / TFT lib / Phase B / VLW 字型整合等）尚未跑 5 步驟 review。baseline 之後 Phase E Batch 2 工程 4 commits 為 review 行動本身可略過。本批跟 B1-B5 (`7f81f6b..af5f54a` 已 review) 範圍獨立。

**baseline**：`3d44950`（demo.html 字級對齊韌體 PM 反饋放大調整，2026-05-09 16:45 GMT+8）

涉及檔案：`firmware/src/ui_*.cpp` / `main.cpp` / vlw 字型 / `docs/demo/index.html` / Phase B 補登流程等。完整 commit list 見 git `git log 3d44950..7f81f6b~1 --oneline`。

執行：`step 1 eslint skip（無 .eslintrc）｜step 2 /simplify｜step 3 pr-reviewer (lite)｜step 4 review-pr (5 agent)｜step 5 osascript 通知`

### 風險與決策點

- 🔴 **GPIO 21 衝突**：TFT CS 跟震動馬達同腳。整合時 `ENABLE_VIBRATION = 0` 必須維持；Prod-Phase 要震動回饋的話必須先解 GPIO 衝突（見 `gpio-allocation.md` §3 註記）
- 🟡 **Refresh strategy**：partial update 已實作於 OHCA countdown row（commit `4d831ab`）；其他畫面走全頁 DMA pushSprite ~4ms

---

## 🔧 Phase E review-pr Batch 2 待處理（2026-05-13 立案）

> **背景**：Phase E 持久化（commit `5a2027e` + `a0c5b9c`）跑 `/pr-review-toolkit:review-pr`
> 5-agent 審查，找到 13 CRITICAL + 23 IMPORTANT + 15 SUGGESTION 共 51 項。低風險批次已處理：
> - `7ebec92` /simplify 8 項清理
> - `3ea5884` Batch 1：註解 + magic number 13 項
> - `8844e9f` Batch 3：補測 13 case + 強化 4 既有
>
> **Batch 2 是行為改動 + 醫療安全相關**，需要設計決策 + 實機驗證，故獨立追蹤。

### Group 2A — 純 log 加固 ✅ **已完成（2026-05-14 commit `40bad66`）**

- [x] **C-3** `enforce_fifo_cap` 加 `[STORAGE] FIFO evict type=N id=X` audit log（fs 層 delete_file always return true，logic 層補語意層 trace）
- [x] **I-3** `storage_init` persist_index 失敗加 warn（含「下次 save 會修復」備註）
- [x] **I-4** rebuild 路徑 `fill_meta_from_bin` 損毀檔加 warn（OHCA / Training 各一條）
- [x] **I-5** `fs_read_file` short read 加 warn 印 `path/expected/actual`
- [x] **I-6** `fs_rename_file` remove-to 失敗加 warn
- [x] 新增 `EMS_STORAGE_LOG` macro（`#ifdef ARDUINO` 包 Serial.printf；native test 0 開銷）

### Group 2B — C-1 FIFO bug 修 ✅ **已完成（2026-05-14 commit `97e38fc`）**

- [x] **C-1** `storage_save_case` STEP 01.5 加 pre-evict + `enforce_fifo_cap` while 條件 `>` → `>=`
  - 修法：把 enforce_fifo_cap call 從 STEP 07 搬到 STEP 01.5（pre-write），拿掉 STEP 01 `case_count >= kTotalCapacity` guard（cap 算術保證 case_count + 1 <= kTotalCapacity）
- [x] **TDD**：D7 翻轉 `TEST_ASSERT_FALSE` → `TEST_ASSERT_TRUE`（OHCA seq=1 被擠 + Training 不動 + 新 seq=71 存在）；新增 D8 對稱情境（Training 被擠 + OHCA 不動）
- [ ] **實機驗證**：50 OHCA + 20 Training + 1 OHCA → 應成功 + 最舊 OHCA seq 1 被擠掉、Training 不動（native test 已 cover，實機留 Phase F 整合驗收）

### Group 2C — 醫療失敗哲學重設計（部分完成）

> agent 建議的失敗哲學頂層原則：「**資料遺失優於不一致**」→ 寫盤失敗就拒絕進 LOCKED
> 完成態，強制使用者重試或承認失敗。當前實作三選一全沒選清楚。

#### ✅ 工程層 invariant 修復（不等 PM）

- [x] **I-7** `storage_delete` 順序反轉 + persist 失敗 rollback（2026-05-14 commit `9426004`）
  - snapshot case_meta → shift left RAM → persist_index 失敗 shift right + 寫回 snapshot + return false → 成功才砍檔
  - 新增 G6 test 注入 `rename_file = fail_rename` 驗 rollback
- [x] **C-2** `storage_save_case` persist 失敗 rollback（2026-05-14 commit `4b91e3c`）
  - case_count-- + delete events.bin + return false + log；`next_seq` 不退（保 D6 單調 invariant + Phase F BLE 增量同步安全），失敗的 seq 成 hole
  - 新增 G7 test 驗 rollback + hole 行為（restore 後下一筆拿 seq+1 不重用）
- [x] **C-4 最小修** `main.cpp:1393` `g_locked_saved = ok`（2026-05-14 commit `4b91e3c`）
  - 只在成功設 true → 下個 tick 自動 retry；失敗印「will auto-retry」trace
  - 完整 UI 反饋（紅色警告 + 蜂鳴 + 重試按鈕）留下方 PM 對齊
- [x] **EMS_STORAGE_INDEX_BUFFER_SIZE 12288 → 16384** follow-up：C-2 改成 check persist return 後暴露既有 buffer overflow bug（header 註解明示 TODO），bump 解 70 cases 場景

#### ⏳ 需 PM 對齊（暫緩到 Phase E 整合驗收會議）

- [ ] **PM 對齊**：失敗該硬擋（拒絕 LOCKED）還是 fallback（先保住 in-RAM）？三選一：
  - **A. 硬擋**：寫失敗就拒絕 LOCKED，使用者必須重試或承認失敗（一致性最強，但 partition 真壞 → 卡死無法結案）
  - **B. fallback**：寫失敗印 warning + 繼續 LOCKED，下次或重啟試 retry（救護不中斷，但突然斷電 → 紀錄全失）
  - **C. 妥協**：LOCKED 仍進入但 SUMMARY 紅色警告 + 蜂鳴；提供「重試」按鈕；3 次失敗才放棄（平衡，但實作複雜度最高）
- [ ] **C-4 完整修** UI 反饋（依 PM 對齊結果實作）：
  - SUMMARY 畫面加紅色「保存失敗」橫條
  - 蜂鳴器 pattern（連續 vs 間歇）
  - 重試按鈕 / 三次失敗才放棄
- [ ] **實機驗證**：手動觸發 LittleFS 寫入失敗（拉滿 partition）→ UI 看到紅色警告

### Group 2D — 型別大重構（暫緩到 Phase F BLE 整合穩定後）

- [ ] **C-12** `case_meta_t` 9 metric mirror `ohca_case_summary_t` → 抽 `case_metric_counts_t` sub-struct
- [ ] **I-21** `storage_case_type_t` 升 `enum class : uint8_t` 擋 `(cast)42` 亂塞
- [ ] **I-22** `case_meta_t::id` 升 `case_id_t` newtype + factory `make_case_id(uint32_t seq)`
- [ ] **I-23** `IStorageBackend` 改 C++ virtual base 或至少在 `storage_init` 開頭 assert 全 fn ptr 非 NULL

### 對應 commits 與 review 報告

| 階段 | commit | 內容 |
|------|--------|------|
| baseline | `5a2027e` + `a0c5b9c` | Phase E 原始實作（feat + fix）|
| /simplify | `7ebec92` | 8 項清理（純函式內 lambda 抽出、static 名稱、註解修正）|
| Batch 1 | `3ea5884` | 註解 + magic number 13 項清理 |
| Batch 3 | `8844e9f` | 補測 13 case（A6-A8 / C5-C6 / D7 / E6-E7 / G1-G5）+ 強化 4 既有 |
| **Batch 2A** | `40bad66` | 5 處 silent failure 加 log + `EMS_STORAGE_LOG` macro |
| **Batch 2B** | `97e38fc` | C-1 FIFO pre-evict + D7 翻轉 + D8 新增 |
| **Batch 2C/I-7** | `9426004` | storage_delete 順序反轉 + rollback + G6 |
| **Batch 2C/C-2+C-4** | `4b91e3c` | save persist rollback + LOCKED auto-retry + index buffer 12→16KB + G7 |

**累計成果**：native test 263 cases / 262 PASSED（test_storage_hw native ERROR 為 baseline 既有）；ESP32 build SUCCESS，Flash 27.1% / RAM 19.5%（前 16.4%，+8KB BSS 給 index buffer）

### 進場順序建議

1. ✅ **Group 2A 已完成**
2. ✅ **Group 2B 已完成**（native test cover 完整，實機 50+20+1 場景留 Phase F 整合驗收）
3. ✅ **Group 2C 工程層**（I-7 + C-2 + C-4 最小修）**已完成**
4. ⏳ **Group 2C UI 層**：等 PM 對齊失敗哲學 A/B/C → 決定 C-4 完整修法 → SUMMARY 紅色警告 + 蜂鳴 pattern + 重試流程
5. **Group 2D 不建議近期做**：型別重構動序列化結構，等 Phase F BLE 整合穩定後再評估

---

## 📋 近期工作

### 已完成（5/12 ~ 5/25）

- ✅ **Phase F 網頁端落地**：Cloudflare Pages + D1 部署完成（commit `19bc495`）；F-5/F-6/F-9 對齊 SoT §17 完整 UI 全綠（見 `tasks/phase-f-todo.md`）
- ✅ **進度報告 5 ~ 8 PM HTML**（`docs/progress.html`）— 5 Phase F 網頁、6 BLE 鏈路、7 DS3231 RTC、8 UI bug B1-B5
- ✅ **手機 demo 320×240 文字溢出修正**（Android line-height 1.5→1.2）
- ✅ **硬體採購清單 V1 功能標記**（`docs/hardware-procurement-v2.{md,html}`）
- ✅ **HTML 文件未定義 CSS 變數修正**
- ✅ **2026-05-25 POST-COMMIT-REVIEW 範圍 `7f81f6b..af5f54a`** 五步驟（B1-B5，commit `bdf6647`，評分 28/30）
- ✅ **push origin (GitLab)** — 5/25 推 21 commits（GitHub backup 暫緩，認證未驗證）

### 待跑

- [x] **22 commits POST-COMMIT-REVIEW**（baseline `3d44950`）— 結案（已過時，commit 過久不補跑）
- [x] **硬體採購清單下單**（蝦皮 / 露天）— 2026-05-25 已下單到貨
- [ ] **更新硬體採購清單文件**：依實際到貨內容更新 `docs/hardware-procurement-v2.{md,html}`（價格 / 規格 / 廠商 / 拍照記錄）

---

## 後續階段

### Phase 1.5 — INMP441 重試（等硬體）
- [ ] 新 INMP441 模組到貨
- [ ] `ENABLE_MIC_MONITOR` 設 1 重編
- [ ] 驗證靈敏度（呼吸、說話、背景音）
- [ ] 錄音檔 WAV + SD 卡模組（GPIO 10~13，目前僅保留腳位）

### Phase 3 — 手機 App（DS3231 RTC 已完成）
- [x] **DS3231 RTC 模組升級**（commit `c83f1c2` 5/24，runtime 偵測 + 雙模式 backend，TDD 6 wave）
- [x] **離線時間戳保存**（DS3231 + CR2032 紐扣電池備援，永續性測試待跑見 §⏳）
- [ ] 手機 App（React Native / Flutter，待定）
  - [ ] BLE 掃描/連線
  - [ ] 接收 Notify 即時更新時間軸
  - [ ] SQLite 本地歷史儲存
  - [ ] App 分發策略（TestFlight / 自架 APK 等，細節留存於舊版 todo）
  - [ ] **按鈕設定編輯**：App 可編輯 BTN1~4 各群組的計時配置，透過 BLE `config` 命令推送至 ESP32（群組數量可擴充，不限於兩組）
    - 可設定項目：
      - `label`：藥物名稱（自訂顯示文字）
      - `mode`：計時模式（`up` 正數 / `down` 倒數）
      - `duration`：倒數總時長（秒，`up` 模式忽略）
      - 蜂鳴器觸發條件（可複選）：
        - `beep_on_expire`：計時結束時嗶聲
        - `beep_interval`：固定區間提醒（每 N 秒嗶一聲）
        - `beep_at`：指定時間點提醒（倒數剩 N 秒時嗶）
    - ESP32 端用 `Preferences`（NVS）持久化，重開機不遺失
    - 不需要 OTA，純資料層操作

### Phase 4 — 整合測試、優化與量產化

#### 整合測試
- [ ] 電源方案選型（實測耗電後決定 18650 / LiPo / 乾電池）
- [ ] 長時間穩定性測試（連續 > 2 小時出勤情境）
- [ ] 最終驗收

#### 電源模組採購驗收（2026-05-06 立案）

> 詳見 [`docs/power-module-purchase.md`](../docs/power-module-purchase.md)（含 HTML 版 `.html`）

- [ ] **採購下單**
  - [ ] 1000mAh 523450 LiPo PH2.0（左紅右黑、含保護板）× 1
  - [ ] TP4056 充電升壓模組 Type-C（蓮騰 MTARDTP4056S）× 2
  - [ ] JST PH2.0 母座 直插 2P × 5
- [ ] **收貨驗收**（依 §4 SOP）
  - [ ] 電池：極性、保護板、充飽電壓、容量、外觀
  - [ ] 模組：IC 標示、LED 反應、升壓電壓校正、假負載穩定度
  - [ ] PH2.0 母座：對插順暢、焊腳間距 2.0mm
- [ ] **組裝**（依 §5 步驟）
  - [ ] PH2.0 母座焊到蓮騰板 B+/B-
  - [ ] 升壓電位器校正到 5.00V ± 0.05V，鎖定
  - [ ] 接 ESP32-S3 GOOUUU VIN/GND（GOOUUU 板絲印為 **V5IN**，與 VIN 同節點 5V 軌，TFT 共用此腳）

> 🚨🚨🚨 **接 V5IN 前必做兩件事，跳過任一件都可能燒晶片** 🚨🚨🚨
>
> **① ✅ 萬用表確認 V5IN 是 5V 軌，不是 3V3 — 已通過（2026-05-31 實測）。**
> 板子自身 USB-C 供電下實測：**V5IN 5.06V / 3V3 3.26V / 拔 USB 後 V5IN 0.47V→歸零**。三點對齊確認 V5IN 為 USB 5V 直通軌、與 3V3（獨立 LDO）不同節點，腳位身份無誤，OUT+ 可接 V5IN。
>
> **② ✅ 根因已定位（2026-06-14）+ 已修復（2026-07-04）：負極/GND 回流接觸不良掉 0.5V（ground bounce），非升壓板出力、非正極。已焊接修復並實測通過。**
>
> **根因定位（2026-06-14，改鱷魚夾固定後帶載量測）**：
> - 電池 B+/B- = 4.0V（健康）、**B+ 對 B- = 5.0V（升壓板輸出正常，電位器別動）**
> - OUT+ 對 GND 4.57V ≈ V5IN 對 GND 4.55V → 正極接線壓降僅 0.02V，**正極側良好**
> - **V5IN 對 B- = 5.0V 但 V5IN 對 GND = 4.5V**；B+ 對 GND 也 = 4.5V → **ESP32 GND 比 B- 高 0.5V = 負極回流線接觸電阻吃掉 0.5V**
> - 結果：ESP32 實看 V5IN-GND 僅 4.5V，卡 3V3 LDO brown-out 邊緣 → 湧浪一凹跌破 → 閃/開不了機；「碰一下就開機」= 地線接觸間歇
> - 前幾輪誤判正極/升壓板，是因為一律「對 GND 量」，而 GND 基準本身被污染翹高 0.5V（經典只盯正極忽略地線回流的坑）
> - 詳見 memory `project_power_battery_tft_whitescreen_debug.md` §「根因定位（2026-06-14）」
>
> **修法（✅ 2026-07-04 已完成）**：修**負極/地線**（OUT-/B- → ESP32 GND）那條的爛接觸 → 焊死/換 2P 螺絲端子；正極/電位器未動。使用者實測通過。
> **驗收（已通過）**：帶載背光全亮時 GND 對 B- < 0.05V + V5IN 對 GND ≈ 4.95V + 連開 5~10 次不閃。
>
> **進度（2026-05-31，已被 6-14 定位取代，保留歷史）**：
> - ✅ 升壓模組單獨上電測試（不接電池/不接 ESP32）：OUT+ 對 OUT- 量到 **6.0V** → 確認**拓樸 A（升壓吃 USB 路徑，不接電池也輸出）**，可單獨校壓，不必等電池
> - ✅ 電位器工具已到手（小一字起子）
> - 🚧 **帶載 brown-out（下午實測）**：接上 ESP32 + TFT 後，開機背光全亮瞬間**一直重啟 + 背光閃爍** = 典型 brown-out（帶載掉壓，非燒晶片）
> - 🔵 **根因（高信心，待驗證）**：**測試時沒接電池**，升壓板獨扛 TFT 背光湧浪電流就垮。拓樸 A「不接電池也輸出」≠「不接電池也帶得動」；設計本就靠電池當低內阻緩衝吸湧浪。
>
> **下次續做步驟（接電池驗證）**：
> 1. 斷 ESP32，升壓模組空載量 OUT+/OUT- → 確認電位器還在 **5.0~5.1V**（若剛剛轉過頭低於 5V，校回）
> 2. 🚨 **接充飽鋰電池到 B+/B−，極性務必對（左紅+ / 右黑−，接反冒煙不可逆，用萬用表確認電池線正負再接）**
> 3. 電池若 < 3.3V 先讓 TP4056 充（USB 插著，紅燈充電 / 藍綠燈滿）再測
> 4. 整機開機 → 背光全亮時量 V5IN：應**穩住不掉到 4.7V 以下**，重啟消失
> 5. 熱熔膠/指甲油點住電位器防震動跑掉（§5.2 步驟 4）
> 6. **接電池仍重啟** → OUT+/OUT- 跨一顆 **470~1000µF 電解電容**吸湧浪；還垮表示升壓板帶載不足（標稱 ≥1A），換片
- [ ] **整合測試**（依 §5.4）
  - [ ] 滿電拔 USB-C 連續運作 ≥ 30 分鐘
  - [ ] 放電到 3.4V 仍正常（升壓有效）
  - [ ] 過放保護觸發測試
  - [ ] 邊充邊用不重開機
- [ ] **驗證通過後同步 SoT**（依 §8）
  - [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §20.4.3 / §21.1
  - [ ] `docs/pcb-outsourcing-guide.md` PCB 預留要求
  - [ ] `docs/gpio-allocation.md` VIN 接腳對應
  - [ ] `tasks/production-roadmap.md` BOM 段落

#### 量產化路線
採漸進式硬體升級，韌體不需改動：

| 階段 | 硬體方案 | 適用時機 |
|------|---------|---------|
| 現在 | 麵包板 + 杜邦線 | 韌體驗證（當前） |
| 原型機 | 洞洞板手焊 / 杜邦線固定 | 使用者試用、功能測試 |
| 小批量 | 客製 PCB + 手焊（KiCad → JLCPCB） | 5~10 台，確認設計定版 |
| 正式版 | PCB + SMT 代焊（JLCPCB 焊被動元件，自焊模組） | 設計定版後降低組裝成本 |

- [ ] **PCB 設計**（KiCad）：ESP32-S3-MINI 模組 + 8 按鈕座 + OLED 連接器 + 蜂鳴器 + 電源管理
  - 按鈕改 JST 連接器（插拔式，方便換位置或更換開關）
  - 換單行程 tactile 按鍵（同時啟用 BTN8 長按 deep sleep）
- [ ] **外殼設計**（3D 列印）：開孔對齊 OLED 視窗、8 顆按鈕、USB-C 口、蜂鳴器孔
- [ ] 洞洞板原型機組裝與使用者測試
- [ ] PCB 打樣（JLCPCB，5 片起）
- [ ] **OTA 韌體更新**：WiFi STA 模式（ESP32 從 server 拉 `.bin`，手機 App 觸發）
  - `.bin` 初期放 GitHub Releases
  - 使用場景：非出勤時段，於辦公室/家中連 WiFi 更新

## 設計決策參考

- Phase 2 設計決策：`CLAUDE.md` § Phase 2 設計決策（2026-04-18）
- 按鈕互動：單行程按鍵計畫、開關店員/模組切換功能延後
- BLE 協議：NUS + JSON（先求通，省電化延後）
- 時間同步：Phase 2 軟體對時 → Phase 3 DS3231 RTC
- 電源方案 E（乾電池）已記錄為候選

## 硬體備註

- 主控：ESP32-S3-DevKitC-1（從早期 ESP32 WROOM-32 仿製板升級）
- 按鈕：目前為開關式（toggle），未來改單行程（tactile momentary）
- 共地：所有按鈕 GND 接同一軌
