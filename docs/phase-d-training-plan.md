# EMS DoseSync Pro — Phase D 訓練模式（Training）實作計畫

> **狀態**：計畫已定稿，**尚未開工**。下次開工直接讀本檔，照 §7 Wave 順序執行即可。
> **範圍決策**：完整 SoT V1 §15 一次到位（15.1 ~ 15.18 全部）。
> **依據規格**：SoT V1 §15（Training 全文）+ §12（歷史分類 / 刪除）+ §22.7（Training 測試）；`docs/pm-dev-spec.md §四 Phase D` + §8。
> **UI 藍本**：`docs/demo/index.html`（已有 Training 原型：倒數選擇 / 重置訓練對話框 / 訓練歷史操作選單 / 刪除確認）。
> **現況基準**：2026-07-04 探索確認 —— **Phase E 持久化 + Phase F 同步早已完成且 mode-aware**（見 §1）。
> **計畫建立**：2026-07-04（brainstorming session）。

---

## 0. 一句話定義（不可違反）

> **Training = OHCA 的訓練版本**（SoT §15.1）。**複用 OHCA 核心狀態機**，差異僅：可選 EPI 倒數、全程「訓練模式」浮水印、標題標 Training、存獨立 Training 區、可裝置端刪除、可重置。
>
> **禁止**另寫獨立簡化流程或省略 OHCA 核心事件邏輯（§15.1 / §15.18 明令）。

SoT §15.18 官方建議的參數化藍圖（本計畫的骨架依據）：

```text
case_mode            = OHCA / Training
epi_interval_seconds = 240 / 60 / 30
history_limit        = 50 / 20
allow_device_delete  = false / true
show_training_label  = false / true
```

---

## 1. 現況基準：什麼已就緒（→ 不用重寫）

探索員（2026-07-04）逐行查證，Phase D 的依賴大多已上線：

| 依賴 | 狀態 | 證據 |
|------|------|------|
| **持久化 storage 全鏈路** | ✅ 已 mode-aware + 已測 | `ems_storage_logic.h`：`EMS_CASE_TYPE_TRAINING=1`、`EMS_STORAGE_TRAINING_CAP=20`、`storage_save_case/list/load/delete/set_synced_at` **全帶 `type` 參數**；`ems_storage_fs.cpp:145` 已建 `/cases/training` 目錄；native test `test_ems_storage_logic/test_main.cpp` 含 `D5/D7/D8` Training FIFO 案例 |
| **App 同步全鏈路** | ✅ 已 mode-aware | `sync_send.cpp:110` 吃 `g_sync_target.type`；`:142` 依 type 帶 `mode="training"`；`case_sync_serializer.h:37` payload 有 `mode` 欄位；dispatcher/serializer 無需改 |
| **OHCA 子狀態機** | ✅ 純函式可複用 | `ems_ohca_state.h:99` `nextOhcaState(...)`；兩段確認 `ems_two_step_confirm.h`；`ohca_logic.cpp:26` `dispatchOhcaEvent` |
| **案件總覽 / Timeline 聚合** | ✅ 純函式無 OHCA 耦合 | `ems_case_summary.h:74` `caseSummary_build(...)`、`:97` `caseSummary_buildTimeline(...)` |
| **歷史列表 UI pattern** | ✅ 已完整（非 placeholder） | `ui_screens.cpp:167` `drawHistoryList()`：捲動 + cursor + 同步標記；資料源真 storage |
| **裝置端刪除 API** | ✅ 已就緒（缺 UI） | `storage_delete(be, type, id)` |

**唯一要動的既有 lib**：`ems_ohca_countdown`（倒數參數化，見 Wave 1）。

> ⚠️ `ohca_logic.cpp:153` 的「in-memory only」註解**已過時**：OHCA 案件結束（首進 LOCKED）確實 flush 進 flash（`ohca_logic.cpp:55-88`，`storage_save_case`）。順手修正此註解。

---

## 2. 要新寫的 6 塊（總覽）

| # | 塊 | 類型 | 對應 SoT | Wave |
|---|-----|------|----------|------|
| 1 | EPI 倒數參數化（30/60/240s） | 🔧 核心 lib | §15.3 / §15.6 / §15.7 | W1 |
| 2 | CaseMode 執行期分支（存檔 type / 守門 / 標題） | 🔧 核心骨架 | §15.18 | W2 |
| 3 | Training 入口：倒數選擇畫面 + 初始化 | 🎨 UI+接線 | §15.3 / §15.4 | W3 |
| 4 | 全程「訓練模式」浮水印 + 標題傳參 | 🎨 UI | §15.2 / §12/13 標題 | W4 |
| 5 | 結束後保存 / 不保存流程 | 🎨 UI+接線 | §15.11 / §15.12 | W5 |
| 6 | Training 歷史分層 + 操作選單 + 刪除 + 重置 | 🎨 UI+接線 | §12.1 / §15.16 / 重置 | W6~W8 |

---

## 3. 架構決策（ADR）

### AD-1 — Training 複用方式：CaseMode enum + 共用 `GLOBAL_OHCA`（採用）

**決策**：新增 `enum CaseMode { CASE_MODE_OHCA, CASE_MODE_TRAINING }` 全域變數 `g_case_mode`。Training 進行中**走既有 `GLOBAL_OHCA` 狀態**，由 `g_case_mode` 分支「存檔 type / 倒數週期 / 浮水印 / 標題 / 保存流程」。Training 專屬的**前段（倒數選擇）與後段（保存/不保存）**用獨立子狀態承載。

**理由**：
- SoT §15.18 官方明令「共用核心狀態機，以參數區分」，此為唯一合規方向。
- OHCA 全部輸入處理集中在 `onShortPress`/`onLongPress` 的 `globalState == GLOBAL_OHCA` 守門（`input_handler.cpp:330/707`）。共用 → **守門條件零改**，Training 中段完全免費複用。
- `g_case_mode` 可推導出 §15.18 全部 5 個參數（type / interval / cap / delete / label）。

**替代（不採用）**：新增獨立 `GLOBAL_TRAINING` 狀態。缺點：所有 OHCA 守門條件要改成 `(GLOBAL_OHCA || GLOBAL_TRAINING)`，散落多處易漏，違反「共用」精神。

### AD-2 — 倒數參數化：純函式加 `epi_cycle_ms` 參數（採用）

**決策**：把 `ems_ohca_countdown` 的 `constexpr EPI_CYCLE_MS` 從編譯期常數，改為**執行期參數**貫穿三個純函式：
```c
ohca_phase_t advanceOhcaPhase(ohca_phase_t current, uint32_t since_last_epi_ms, uint32_t epi_cycle_ms);
ohca_output_t decideOhcaOutput(ohca_phase_t phase, uint32_t prev_since_ms, uint32_t since_last_epi_ms, uint32_t epi_cycle_ms);
uint32_t computeRemainingMs(uint32_t since_last_epi_ms, uint32_t epi_cycle_ms);
```
衍生的 WARNING/ALARMING 邊界改由 `epi_cycle_ms` 執行期推導。OHCA 呼叫端傳 `240000`（保留 `OHCA_EPI_CYCLE_MS_DEFAULT` 具名常數），Training 傳 `g_training_epi_cycle_ms`（30000/60000/240000）。

**§15.7 短倒數不進 WARNING**：`decideOhcaOutput` 依 cycle 分支 —— `epi_cycle_ms <= 60000` 時**跳過 WARNING**（30s/60s 不顯示「請準備給藥」），僅 `240000` 於剩 60s 時進 WARNING。用具名常數 `TRAINING_WARNING_MIN_CYCLE_MS = 240000`。

**理由**：純函式加參數是最小侵入、可 native 測（不同 cycle 邊界）、不破壞 OHCA 行為（default 傳 240000 等價原狀）。

### AD-3 — 浮水印：常駐 draw helper，mode 驅動（採用）

**決策**：新增 `drawTrainingWatermark()`（畫「訓練模式」字樣，半透明/角落，不擋主資訊）。在 `main.cpp` draw dispatch，當 `g_case_mode == CASE_MODE_TRAINING` 時，於每個 Training 相關畫面**畫完主內容後疊上浮水印**。因主韌體用 LovyanGFX `LGFX_Sprite` + `pushSprite` DMA 全頁切換（見專案記憶），浮水印須畫在 **sprite buffer 上**（pushSprite 之前），不可直接畫 panel。

> 樣式/位置屬視覺細節：第 1 輪先實作 70~80%（如頂部置中淡色小字或右上角標籤），實機截圖後精調（對齊 `docs/demo/index.html` 與 OHCA 美學：黑底白字 + monospace）。

### AD-4 — 歷史分層 + 操作選單 + 刪除（採用）

**決策**：
- 歷史紀錄主入口先加**分類層**（§12.1）：`> OHCA 案件 / Training 紀錄`。現有 `drawHistoryList` + 載入邏輯**加 `type` 參數**（目前 `input_handler.cpp:135/285` 寫死 `EMS_CASE_TYPE_OHCA`）。
- Training 列表選取某筆 → **Training 案件操作選單**（§15.16）：`查看總覽 / 同步至 App / 刪除此訓練紀錄 / 返回`（OHCA 列表**無**刪除項）。
- 刪除走既有 `storage_delete(be, EMS_CASE_TYPE_TRAINING, id)` + 二次確認對話框（§15.16）。

---

## 4. 狀態機設計（Training 專屬前後段 + 共用中段）

```text
主選單「訓練模式」(case 2)
  │ 短按
  ▼
GLOBAL_TRAINING_SETUP  ← 由 GLOBAL_TRAINING_PLACEHOLDER 改名承載
  「訓練倒數  > 30秒 / 1分鐘 / 4分鐘」        (§15.3)
  ├─ 上/下：切換    主鍵：確認 → 設 g_training_epi_cycle_ms + g_case_mode=TRAINING
  │                          + 複製 OHCA case-start 初始化 → 切 GLOBAL_OHCA
  └─ 返回：回主選單
  ▼
GLOBAL_OHCA  (g_case_mode == TRAINING)         ← 完全複用 OHCA 中段
  Training 初始狀態「待本機 EPI」(§15.4，不啟動倒數)
   → 首筆 EPI 二段確認 → 啟動 Training 倒數 (§15.5)
   → COUNTDOWN / (WARNING 僅 4min) / ALARMING「請給藥」/ OVERTIME  (§15.6/15.7)
   → 電擊 / Amio / 補登：與 OHCA 同 (§15.8/15.9/15.10)
   → 快速功能選單多一項「重置訓練」→「重置訓練？」對話框 → 清空事件回 §15.4 初始（重置）
   → 長按主鍵 3s → END_CHECK「結束前檢查｜Training」(§15.11)
        > 完成並結束訓練 / 前往補登 / 返回訓練
   → 完成並結束 → 「確認結束訓練？」→ 主鍵確認 → LOCKED
  ▼
TRAINING_SAVE_PROMPT   ← 新子狀態（OHCA 無此段）        (§15.12)
  「訓練紀錄  > 保存 / 不保存」
  ├─ 保存   → storage_save_case(type=TRAINING) →「訓練紀錄已保存」1s → 案件總覽｜Training
  └─ 不保存 →「訓練紀錄未保存」1s → 主選單（不占 Training 筆數）
```

> **與 OHCA 收尾的差異**：OHCA 首進 LOCKED **立即** `storage_save_case`（`ohca_logic.cpp:72`）。Training **不可**在 LOCKED 自動存 —— 必須等 `TRAINING_SAVE_PROMPT` 選「保存」才寫。故 Wave 2 的 mode 分支要讓 `dispatchOhcaEvent` STEP 04 在 `g_case_mode==TRAINING` 時**跳過**自動存檔，改由保存流程觸發。

---

## 5. 資料 / global 變更（`firmware/src/app_globals.h`）

| 變更 | 內容 | 備註 |
|------|------|------|
| 新增 enum | `enum CaseMode : uint8_t { CASE_MODE_OHCA=0, CASE_MODE_TRAINING=1 }` | 對映 `storage_case_type_t` |
| 新增 global | `CaseMode g_case_mode`（`main.cpp` 定義 + `extern`） | 入口設定，exitCase 復位 OHCA |
| 新增 global | `uint32_t g_training_epi_cycle_ms`（30000/60000/240000） | 倒數選擇寫入 |
| 改名 enum | `GLOBAL_TRAINING_PLACEHOLDER` → `GLOBAL_TRAINING_SETUP`（值仍 3） | 連動 `input_handler.cpp:131/310`、`ui_screens.cpp` placeholder 繪製、SyncReturnTo 映射確認無誤 |
| 新增 GlobalState | `GLOBAL_TRAINING_SAVE`（保存/不保存畫面） | 或用 ohcaSubState 承載；擇一，計畫傾向獨立 GlobalState 較清楚 |
| 新增 global | 倒數選擇 cursor `trainingSetupCursor`、保存選單 cursor `trainingSaveCursor`、重置對話框旗標、Training 歷史分類 cursor、Training 操作選單 cursor、刪除確認旗標 | **每一個都必須同步進 `DisplaySnapshot`**（見 §10 教訓） |

> `SyncTarget`（`app_globals.h:294`）已帶 `storage_case_type_t type` —— Training 同步只需設 `g_sync_target.type = EMS_CASE_TYPE_TRAINING`（`enterSyncFlow` fallback `input_handler.cpp:783` 要補 mode 分支）。

---

## 6. 檔案改動清單（逐檔）

| 檔案 | 改動 |
|------|------|
| `firmware/lib/ems_ohca/ems_ohca_countdown.{h,cpp}` | AD-2 倒數參數化：3 純函式加 `epi_cycle_ms`；WARNING 依 cycle 分支 |
| `firmware/test/test_ohca_countdown/test_main.cpp`（或既有對應 test） | 加不同 cycle（30/60/240s）邊界 + 短倒數不進 WARNING 的 RED test |
| `firmware/src/app_globals.h` | §5 全部 global / enum / DisplaySnapshot 欄位 |
| `firmware/lib/ems_display_snapshot/ems_display_snapshot.h` | 新 cursor/flag 全部進 `DisplaySnapshot` + `DisplaySnapshotInputs` + `captureSnapshot` 映射 |
| `firmware/src/input_handler.cpp` | case 2 入口初始化；`GLOBAL_TRAINING_SETUP` 倒數選擇輸入；`TRAINING_SAVE` 保存選單；歷史分類層 + Training 操作選單 + 刪除確認；歷史載入/同步 `type` 分支；重置訓練入口 |
| `firmware/src/ohca_logic.cpp` | STEP 04 存檔依 `g_case_mode` 分支（Training 不自動存）；tick 傳 `epi_cycle_ms`；修過時 in-memory 註解 |
| `firmware/src/ui_ohca.cpp` | `drawOhcaSummary` 標題傳參/mode 分支（`案件總覽｜Training`） |
| `firmware/src/ui_screens.cpp` | `drawTimeline` 標題（`事件時間軸｜Training`）；`drawHistoryList` 加 type；新增倒數選擇 / 保存選單 / 分類層 / 操作選單 / 對話框繪製 |
| `firmware/src/main.cpp` | mutable global 定義；draw dispatch 加 Training 畫面 + `drawTrainingWatermark` 疊圖；保存流程 observer；exitCase 復位 mode |
| `docs/progress.md` / `tasks/todo.md` | 完工後更新進度 + 勾選 |

---

## 7. 實作 Waves（TDD：每 Wave 先 RED test → GREEN impl → 驗收；全程禁改 test 遷就 impl）

> 依專案工作流（`feedback_tdd_alignment_gate_workflow`）：Phase 等級功能，RED test → 三 subagent 並行對齊審查 → GREEN impl。native test 用 `pio test -e native`，韌體編譯 `pio run`。

- [x] **W1 — 倒數參數化（純 lib，先做，最獨立）** ✅ 韌體完成（2026-07-13）
  - RED：`test_ohca_countdown` 加 cycle=30000/60000/240000 的 `advanceOhcaPhase`/`decideOhcaOutput`/`computeRemainingMs` 邊界 + §15.7「≤60s 不進 WARNING」case
  - GREEN：3 函式加 `epi_cycle_ms` 參數；OHCA 呼叫端傳 `OHCA_EPI_CYCLE_MS_DEFAULT`
  - 驗收：native 全綠；OHCA 行為與改前等價（240000 路徑不變）
  - 實機待測：§9.5 W1 三檔倒數 + WARNING 分支

- [x] **W2 — CaseMode 骨架 + 存檔 mode 分支** ✅ 韌體完成（2026-07-13）
  - `enum CaseMode` + `g_case_mode` + `g_training_epi_cycle_ms`；`ohca_logic.cpp` STEP 04 依 mode 分支（Training 不自動存）；tick 傳對應 cycle
  - 驗收：OHCA 路徑（mode=OHCA）完全不變、仍自動存；韌體編譯過
  - 實機待測：§9.5 W2 OHCA 自動存 / Training 不自動存

- [x] **W3 — Training 入口：倒數選擇畫面 + 初始化** ✅ 韌體完成（2026-07-13）
  - `GLOBAL_TRAINING_PLACEHOLDER` → `GLOBAL_TRAINING_SETUP`；case 2 進 setup；上/下切換 + 主鍵確認 → 設 cycle + mode=TRAINING + 複製 OHCA case-start 初始化 → 切 `GLOBAL_OHCA`；返回回主選單
  - 驗收：實機進訓練模式可選 30/60/240s 進入「待本機 EPI」初始畫面；首筆 EPI 啟動所選倒數
  - 實機待測：§9.5 W3 入口 + 倒數選擇 + EPI 二段確認

- [x] **W4 — 浮水印 + 標題傳參** ✅ 韌體完成（2026-07-13）
  - `drawTrainingWatermark()` 疊在 sprite；`drawOhcaSummary`/`drawTimeline` 標題依 mode 標 Training
  - 驗收：Training 全程有浮水印；OHCA 無浮水印；總覽/Timeline 標題正確
  - 實機待測：§9.5 W4 浮水印 + 標題

- [x] **W5 — 結束保存 / 不保存** ✅ 韌體完成（2026-07-13）
  - END_CHECK「結束前檢查｜Training」→「確認結束訓練？」→ LOCKED → `TRAINING_SAVE`「保存/不保存」；保存 → `storage_save_case(TRAINING)` +「已保存」1s → 案件總覽｜Training；不保存 →「未保存」1s → 主選單
  - 驗收：保存的 Training 出現在 storage/`/cases/training`；不保存不占筆數
  - 實機待測：§9.5 W5 END_CHECK + 保存/不保存流程

- [x] **W6 — 歷史分層 + Training 列表 + 從歷史進總覽 + 同步** ✅ 韌體完成（2026-07-13）
  - 歷史主入口加分類層（OHCA 案件 / Training 紀錄）；`drawHistoryList`/載入加 `type`；Training 列表可進總覽；同步設 `g_sync_target.type=TRAINING`
  - 驗收（**pm-dev-spec Phase D 主驗收**）：Training 與 OHCA 列表**完全分離**；Training 同步 App 端標 `mode=training`
  - 實機待測：§9.5 W6 歷史分類 + Training 列表 + 同步標記

- [x] **W7 — Training 裝置端刪除** ✅ 韌體完成（2026-07-13）
  - Training 操作選單「刪除此訓練紀錄」→「刪除此訓練紀錄？」二次確認 → `storage_delete(TRAINING)` →「已刪除」1s → 回列表；OHCA 列表**無**刪除項
  - 驗收：刪除 Training 成功且不影響 OHCA；OHCA 案件無法從裝置端刪除
  - 實機待測：§9.5 W7 刪除 + OHCA 無刪除項

- [x] **W8 — 重置訓練** ✅ 韌體完成（2026-07-13）
  - 訓練進行中快速功能選單加「重置訓練」→「重置訓練？」對話框 → 確認清空當前 training 事件回 §15.4 初始狀態（倒數停、EPI/電擊歸零）
  - 驗收：重置後回「待本機 EPI」；OHCA 無此功能
  - 實機待測：§9.5 W8 重置訓練 + OHCA 無此功能

- [ ] **W9 — 整合煙霧測試 + 實機驗收** ✅ 韌體完成（2026-07-13） — ⚠️ 僅程式碼面，實機待跑
  - 跑 §8 SoT §15 逐項對照 + §9 驗收清單；native 全綠；韌體編譯（記錄 Flash/RAM %）；實機走完整訓練流程
  - 實機待測：§9.5 W9 完整流程整合測試

---

## 8. SoT §15 逐項規格對照（追蹤用）

| SoT | 需求 | Wave |
|-----|------|------|
| §15.1/15.2 | Training = OHCA 訓練版；差異表 | W2 骨架 |
| §15.3 | 入口 + 倒數 30/60/240s 選擇 | W1+W3 |
| §15.4 | 初始「待本機 EPI」不啟動倒數 | W3 |
| §15.5 | EPI 二段確認 → 啟動所選倒數 | W3（複用 OHCA） |
| §15.6 | 倒數畫面（到期「請給藥」+ 累計） | W1 |
| §15.7 | 30s/60s 不顯示 WARNING；4min 剩 1min 顯示 | W1 |
| §15.8/15.9/15.10 | 電擊 / Amio / 補登同 OHCA | 免（複用） |
| §15.11 | 結束前檢查｜Training | W5 |
| §15.12 | 保存 / 不保存 | W5 |
| §15.13 | 案件總覽｜Training | W4+W5 |
| §15.14 | 事件時間軸｜Training | W4 |
| §15.15 | 歷史保存 20 筆 FIFO | 免（storage 已測 D5/D8） |
| §15.16 | 裝置端刪除 + 操作選單 + 二次確認 | W7 |
| §15.17 | App 同步標 Training | W6 |
| §15.18 | 共用狀態機參數化 | AD-1/AD-2 |
| §22.7 重置 | 重置訓練 | W8 |

---

## 9. 驗收清單（完工 DoD）

> **韌體實作完成日**：2026-07-13（commit `d5f64ab` W2-W8 + `aa39def` W9）
> **實機測試**：❌ 尚未執行 — 待實機回傳時逐項對照 §9.5 清單

- [x] native test 全綠（456 test cases / 455 passed / 1 expected errored `test_storage_hw`）
- [x] 韌體編譯過（RAM 32.3% / Flash 68.8%）
- [x] OHCA 既有行為零回歸（mode=OHCA 路徑不變，`ems_ohca_state.cpp:96` 傳 default）
- [ ] **pm-dev-spec Phase D 驗收**：Training 與 OHCA 列表**完全分離**（歷史分兩類、存兩區、同步標記不同）
- [ ] 倒數 30/60/240s 三檔皆正確；4min 剩 1min 才「請準備給藥」，短檔不進 WARNING
- [ ] 全程「訓練模式」浮水印；OHCA 無浮水印
- [ ] 保存 → 進 Training 總覽 + 列入 20 筆；不保存 → 回主選單不占筆數
- [ ] Training 可裝置端刪除（二次確認）；OHCA 不可刪
- [ ] 重置訓練回初始狀態
- [ ] Training 同步 App 端標 `mode=training`，不混入 OHCA 列表

### 9.5 實機測試對照清單（韌體已實作，待實機驗收）

> 以下逐項對應 §7 Wave，實機測試時依序執行，勾選確認。
> 測試前請確認：裝置已充電、電池供電狀態正常、TFT 顯示正常。

#### W1 — 倒數參數化

- [ ] 30s 倒數：EPI → 二段確認 → COUNTDOWN → 30s 到 → ALARMING「請給藥」
- [ ] 60s 倒數：EPI → 二段確認 → COUNTDOWN → 60s 到 → ALARMING「請給藥」
- [ ] 240s 倒數：EPI → 二段確認 → COUNTDOWN → 剩 60s 顯示「請準備給藥」→ 240s 到 → ALARMING
- [ ] 30s/60s **不進 WARNING**（僅 COUNTDOWN → ALARMING，無「請準備給藥」）
- [ ] 240s 剩 60s 進入 WARNING 階段（黃燈 + 「請準備給藥」）

#### W2 — CaseMode 骨架 + 存檔 mode 分支

- [ ] OHCA 案件：首進 LOCKED 自動存（不受 Training 改動影響）
- [ ] Training 案件：首進 LOCKED **不自動存**，轉 `SUBSTATE_TRAINING_SAVE` 讓使用者選擇

#### W3 — Training 入口：倒數選擇畫面 + 初始化

- [ ] 主功能表 → 訓練模式（case 2）→ 進倒數選擇畫面
- [ ] 上/下鍵切換 30s / 60s / 240s，主鍵確認
- [ ] 確認後進「待本機 EPI」初始畫面（不啟動倒數）
- [ ] 首筆 EPI → 二段確認 → 啟動所選倒數
- [ ] 返回鍵 → 回主選單

#### W4 — 全程「訓練模式」浮水印 + 標題傳參

- [ ] Training 全程畫面右上角顯示「訓練模式」浮水印
- [ ] OHCA 案件無浮水印
- [ ] 案件總覽標題：「案件總覽｜Training」
- [ ] 事件時間軸標題：「事件時間軸｜Training」

#### W5 — 結束後保存 / 不保存

- [ ] 長按主鍵 3s → END_CHECK「結束前檢查｜Training」
- [ ] 完成並結束 → 「確認結束訓練？」→ 主鍵確認 → LOCKED
- [ ] LOCKED 後進 `TRAINING_SAVE`「保存/不保存」選單
- [ ] 選保存 → `storage_save_case(TRAINING)` +「訓練紀錄已保存」1s → 案件總覽｜Training
- [ ] 選不保存 →「訓練紀錄未保存」1s → 回主選單（不占 Training 筆數）

#### W6 — 歷史分層 + Training 列表 + 同步

- [ ] 歷史主入口加分類層：`> OHCA 案件` / `> Training 紀錄`
- [ ] 切換到 Training 列表 → 可看到已保存的 Training 案件
- [ ] 選取 Training 案件 → 進 Training 操作選單
- [ ] Training 同步 → App 端標 `mode=training`，不混入 OHCA 列表
- [ ] OHCA 列表與 Training 列表**完全分離**（筆數、存檔區獨立）

#### W7 — Training 裝置端刪除

- [ ] Training 操作選單 →「刪除此訓練紀錄」→「刪除此訓練紀錄？」二次確認
- [ ] 確認刪除 → `storage_delete(TRAINING)` +「已刪除」1s → 回列表
- [ ] 刪除後 Training 列表該筆消失
- [ ] OHCA 列表**無**刪除選項
- [ ] 刪除 Training 不影響 OHCA 案件

#### W8 — 重置訓練

- [ ] Training 進行中 → 快速功能選單 →「重置訓練」
- [ ]「重置訓練？」對話框 → 確認
- [ ] 清空當前 Training 事件 → 回「待本機 EPI」初始狀態
- [ ] 倒數停止、EPI/電擊歸零
- [ ] OHCA 案件**無**重置功能

#### W9 — 整合煙霧測試（完整流程）

- [ ] 主選單 → 訓練模式 → 選 240s → 進「待本機 EPI」
- [ ] EPI → 二段確認 → COUNTDOWN → 等 240s → ALARMING
- [ ] 電擊 / Amio / 補登：與 OHCA 同（複用，可快速過）
- [ ] 長按主鍵 3s → END_CHECK「結束前檢查｜Training」
- [ ] 完成並結束 → 確認 → LOCKED →「保存/不保存」→ 保存
- [ ] 保存後進案件總覽｜Training → 事件時間軸｜Training
- [ ] 返回 → 歷史紀錄 → 切換到 Training 紀錄
- [ ] 選取 → 操作選單 → 刪除 → 二次確認 → 刪除成功
- [ ] 重新進入訓練模式 → 選 30s → 倒數 → 重置訓練 → 確認 → 回初始
- [ ] 不保存 → 回主選單 → 確認 Training 列表不佔筆數

#### 測試記錄

| 項目 | 結果 | 備註 |
|------|------|------|
| W1 倒數參數化 | ⬜ 待測 | |
| W2 CaseMode 骨架 | ⬜ 待測 | |
| W3 Training 入口 | ⬜ 待測 | |
| W4 浮水印 + 標題 | ⬜ 待測 | |
| W5 保存/不保存 | ⬜ 待測 | |
| W6 歷史分層 + 同步 | ⬜ 待測 | |
| W7 裝置端刪除 | ⬜ 待測 | |
| W8 重置訓練 | ⬜ 待測 | |
| W9 整合煙霧測試 | ⬜ 待測 | |

---

## 10. 風險與已知坑（開工前必讀，皆為本專案踩過的雷）

1. **DisplaySnapshot 漏欄位 = redraw 被跳過**（`feedback_display_snapshot_field_sync`，**已連踩 3 次**：historyCursor / summarySubmenuCursor / endCheckCursor）。本 Phase 新增 6+ 個 cursor/flag，**每一個**都要跑 5 步驟：加進 `DisplaySnapshot` + `DisplaySnapshotInputs` + `captureSnapshot` 映射 + `sameStateAsLast` 比對 + 加 regression test。**這是本 Phase 最高風險項。**
2. **按鍵 debounce press/release 共用門檻**（`feedback_button_debounce_press_release`）：新畫面（倒數選擇/保存/操作選單/對話框）的上下鍵在慢渲染期 bounce 會穿透雙觸發，沿用既有 debounce 框架。
3. **浮水印畫在 sprite buffer 上**（`feedback_lovyangfx_dma_for_tft`）：主韌體 `LGFX_Sprite` + `pushSprite` DMA，浮水印必須在 pushSprite **之前**畫進 sprite，不可直接畫 panel。
4. **倒數參數化別漏衍生邊界**：WARNING/ALARMING/OVERTIME 邊界原是 `EPI_CYCLE_MS` 編譯期推導，改參數後**每個衍生量都要用 `epi_cycle_ms` 執行期算**，且 §15.7 短倒數分支別漏。
5. **Training 不可在 LOCKED 自動存**：OHCA 首進 LOCKED 立即存（`ohca_logic.cpp:72`），Training 必須延後到「保存」選項才存 —— mode 分支要蓋住這點（§4 註記）。
6. **拆檔/搬函式註解易錯位**（`feedback_file_split_comment_drift`）：若新增畫面讓 `ui_screens.cpp` 過大需拆檔，前置註解整段跟著搬。
7. **API 換簽章的 magic literal**（`feedback_api_migration_magic_literal`）：`advanceOhcaPhase(..., 240000)` 這種 raw 數字加 `/* epi_cycle_ms= */` inline comment 或用 `OHCA_EPI_CYCLE_MS_DEFAULT` 具名常數。

---

## 11. 實機測試下一步

> **現況**：2026-07-13 韌體實作全部完成（commit `d5f64ab` W2-W8 + `aa39def` W9），native test 455/456 綠，韌體編譯通過。**唯一未完成：實機測試。**

1. 帶裝置回實機環境，照 §9.5 實機測試對照清單逐項執行。
2. 每項實機驗收通過後，回到 §9 驗收清單勾選 `[x]`。
3. 實機測試發現問題 → 記錄備註 + 回程式碼修正 + 重新燒錄驗證。
4. 全部實機驗收通過 → Phase D 完工。
