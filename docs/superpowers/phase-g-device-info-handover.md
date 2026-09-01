# Impl-Phase G 裝置資訊畫面 — 交接文件

- **最後更新**：2026-09-01（Task 1 完成，等使用者確認後續跑 Task 2-6）
- **狀態**：SDD（subagent-driven-development）流程進行中。**Task 1/6 完成**，其餘 5 個
  task 未開工。依使用者要求，每完成一個 task 就停下讓使用者確認，本次先做完 Task 1。
- **branch**：`feat/phase-g-device-info`（git worktree，路徑
  `.worktrees/phase-g-device-info`，從本機 `main`（`a634ba9`）分支——**不是**
  `origin/main`，本機 main 領先 origin/main 99+ commit 未推送）

> 本文件是單一時間線，比照 `docs/superpowers/phase-h-handover.md` 的格式（取代分散在
> commit message 裡的追蹤方式）。

---

## 1. 三十秒看懂現況

**背景**：`docs/pm-dev-spec.md §四 Phase G` 原規劃「韌體版本 read-only」一項從未落地成 UI；
SoT V1 §19.7「裝置資訊」畫面（名稱／型號／序號／韌體／電池／充電狀態六欄）也從未實作。
2026-08-30 使用者裁決把兩者合併，一次併入 Impl-Phase G（詳見 §3-A0）。

**這次要做的事**：
1. 系統設定選單從既有 5 項（裝置名稱／亮度／音量／通氣音量／電池資訊）擴充至 SoT §19.1
   完整 8 項，新增 App連線設定／Type-C連線（placeholder）／裝置資訊
2. 8 項裝不下 240px 螢幕，加捲動機制（比照既有歷史紀錄清單模式）
3. 實作裝置資訊畫面本體（`drawDeviceInfo()`）

**已完成**：
- ✅ Brainstorming（architectural path）：序號來源／選單合併／捲動排版三個關鍵決策已與
  使用者逐項確認，見 `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md` §2
- ✅ Spec 寫入並 commit（`a1c8b1c` 初版 + `30dd2eb` 序號方案修正）
- ✅ Implementation plan 寫入並 commit：`docs/superpowers/plans/2026-09-01-phase-g-device-info.md`
  （6 個 task：捲動 clamp 共用函式 → 選單 8 項化 → 按鍵接線 → DisplaySnapshot 接線 →
  裝置資訊畫面本體 → 文件收尾）

**Task 1 完成**（2026-09-01）：`clampScrollOffset()` 共用純函式 + 10 個 native test，
commit `e5bb45d`。經 repo 自己的 Tier 3 codex review（6 面向）抓到 1 CRITICAL（整數
下溢/溢位）+ 14 IMPORTANT，兩輪 fix 後全部收斂，細節見 §4。

**尚未開工**：Task 2-6。

---

## 2. 如何接手

```bash
cat docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md   # 先讀設計決策
cat docs/superpowers/plans/2026-09-01-phase-g-device-info.md          # 再讀實作計畫

# 開工前先確認測試基準線
cd firmware && pio test -e native                            # 應為 623/624，唯一 ERRORED = test_storage_hw
cd firmware && pio run -e esp32-s3-devkitc-1                  # 應為 SUCCESS
git log --oneline -5                                          # 確認在 main 上，Phase H 已 merge
```

**續跑方式**：用 `superpowers:subagent-driven-development`（已在跑，SDD ledger 見
`.worktrees/phase-g-device-info/.superpowers/sdd/2026-09-01-phase-g-device-info/progress.md`——
worktree 內的 git-ignored 目錄，換機器要重建但 git log 是最終記錄）。6 個 task 有嚴格順序
依賴——Task 3 依賴 Task 1（`clampScrollOffset()`）與 Task 2（游標常數）；Task 4 依賴
Task 3 定義的全域變數；Task 5 的 `drawDeviceInfo()` 是 Task 4 Step 8 呼叫點的前向引用
（計畫內已註明，序列執行時 Task 4 完成當下編譯會失敗，Task 5 完成後才會通過）——
**不建議打散成平行 task**，依 1→2→3→4→5→6 序列執行。

> ⚠️ 本次工作在 `feat/phase-g-device-info` git worktree（`git worktree add
> .worktrees/phase-g-device-info -b feat/phase-g-device-info`，明確從本機 HEAD 分支，
> 沒用 harness 內建的 `EnterWorktree` 工具——那個工具預設 `baseRef=fresh` 會從
> `origin/main` 分支，而 origin/main 落後本機 main 99+ commit，會漏掉 Phase G/H 全部
> 既有工作）。續跑時 `cd .worktrees/phase-g-device-info` 或用 `EnterWorktree` 的
> `path` 參數切進去，不要在 `main` 上直接改。

> ⚠️ `firmware/src/` 整個被 `[env:native]` 的 `build_src_filter = -<*>` 排除，`input_handler.cpp`／
> `main.cpp`／`ui_screens.cpp` 的邏輯無法被 native test 直接呼叫。這也是為什麼 Task 5
> （`drawDeviceInfo()`）沒有 native test——跟既有 `drawBatteryInfo()`（Task 13）同一個
> 既有限制，不是本次新增的缺口。這些檔案的正確性驗證管道是**韌體編譯 + 上機驗收**，
> 見計畫末尾的「上機驗收清單」。

---

## 3. 接手待辦

### 3-A0. 背景裁決（2026-08-30，Phase H handover 移出）

「裝置資訊」畫面併入 Impl-Phase G 的完整裁決過程記錄在
`docs/superpowers/phase-h-handover.md §3-A7`，本文件不重複，只連結參照。

### 3-A1. Brainstorming 逐項決策記錄（2026-09-01）

完整記錄在 spec §2，這裡摘要三個最關鍵、且中途發現需要修正的決策：

1. **序號來源**：原訂 ESP32 efuse MAC 衍生，**寫計畫前查現有程式碼發現此方案有問題**——
   `app_globals.h` 已有 `SYNC_DEVICE_ID = "DSP-0001"` 常數且已用於案件同步 metadata
   （`sync_send.cpp` 的 `device_id` 欄位）。efuse 衍生會產生第二套不同來源的序號，
   跟 App 端看到的案件 metadata 對不起來。**已修正為直接讀取 `SYNC_DEVICE_ID`**，
   spec 對應修正見 `30dd2eb` commit。這是 brainstorming 完成、寫計畫前的例行程式碼
   核對抓到的問題，不是事後 review 才發現——供未來類似情境參考：**brainstorming
   階段拍板的資料來源決策，動工前務必再核對一次現有 codebase 有沒有同名/同義的
   既有機制，不能只憑 spec 文字描述就開工**。
2. **選單排序**：8 項維持 SoT §19.1 原始順序（不是圖方便直接接在既有 5 項後面），
   代價是要為未實作的 App連線設定／Type-C連線 補 placeholder 項目。
3. **8 項裝不下 240px 螢幕**：加捲動機制，不壓縮既有版面、不砍項目。這個發現是
   brainstorming 過程中才算出來的（既有選單 40px 等間距 × 8 項 = 超出螢幕），
   不是預先規劃好的——過程記錄見 spec §1.2「規格缺口」。

### 3-B. 上機驗收清單（尚未執行，等 Task 5 完成後）

完整清單在計畫檔末尾，此處僅列摘要：選單捲動流暢度、placeholder 顯示與返回、裝置資訊
六欄正確性（含序號與 App 端案件同步紀錄的 device_id 交叉核對）、恢復預設對話框捲動後
觸發位置正確性、裝置名稱捲出視窗行為。

---

## 4. 已完成的 task

### Task 1 完成（2026-09-01）——捲動 clamp 共用純函式

**現況**：`firmware/lib/ui_scroll/ui_scroll.h`（`clampScrollOffset()`）+
`firmware/test/test_ui_scroll/test_main.cpp`（10 個 native test），commit `e5bb45d`
（2 輪 amend，最終版）。native test 634 cases / 633 通過（唯一 ERRORED 仍是既有的
`test_storage_hw`，與本次無關），韌體編譯 SUCCESS。

**Repo 自己的 Tier 3 codex review 抓到的問題**（跟 SDD 流程平行的獨立閘門，非本計畫
文件定義的 task-review）：

- **1 CRITICAL**：`clampScrollOffset()` 對 `visible_rows == 0` 會整數下溢（`uint8_t`
  的 `visible_rows - 1`），靜默回傳看似合法但錯誤的 offset；`offset + visible_rows`
  轉型 `uint16_t` 在 cursor/offset 接近 `UINT16_MAX` 時會回繞。fix round 1 修正：加
  `visible_rows == 0` guard（回傳 offset 不變）+ 改用 `cursor - offset >= visible_rows`
  避免加法溢位（不用模板/`static_assert`，維持函式內部直接處理，見下方 Ruling）。
- **14 IMPORTANT**：多數是我寫 Task 1 plan brief 時內嵌的範例程式碼本身沒做到位
  （doc comment 用完成式語氣宣稱「兩處呼叫點統一引用」但這個 commit 還沒有任何呼叫點、
  8 個測試函式缺 STEP/JSDoc、測試斷言用裸數字）——**Ruling: plan-mandated 缺陷**，
  已授權 implementer 偏離 plan brief 字面程式碼補齊 Global Constraints，兩輪 fix 後
  全部收斂。

**Rulings（供之後驗證/翻案參考）**：

1. `visible_rows == 0` 時該不該用更重的機制（模板 + `static_assert`）杜絕，還是函式內
   guard 靜默回傳 offset？**Ruling：函式內 guard，不用模板**——理由：這是一個內部
   helper，目前唯二呼叫端（Task 3 待接線的 `settingsScrollOffset`／`historyScrollOffset`）
   都傳編譯期常數（`SETTINGS_VISIBLE_ROWS=5`／`HISTORY_VISIBLE_ROWS=5`），非公開 API，
   模板化是過度工程化。**代價**：若未來有人以非常數 `visible_rows=0` 呼叫，會靜默拿到
   不動的 offset 而非明確錯誤訊號——風險評估為低（純 UI 捲動內部邏輯，非安全關鍵路徑，
   跟 EPI 劑量這類判斷不同量級）。
2. Plan brief 內嵌的範例程式碼（doc comment 用詞、測試檔缺 STEP/JSDoc/具名常數）算不算
   implementer 的缺陷？**Ruling：不算，是 plan 文字本身的缺陷**——Global Constraints
   是 binding authority，已授權 implementer 偏離 brief 字面程式碼補齊。**教訓**：日後
   writing-plans 階段把完整程式碼寫進 task brief 時，那段範例程式碼也要先過一次
   Global Constraints checklist，不能假設「brief 裡的程式碼一定合規」。

**已知異常**：SDD 流程自己的 task-reviewer（`phase-g-task1-review`）與 fix round 1 的
scoped re-reviewer（`phase-g-task1-rereview`）兩個 subagent 完成後皆未回傳報告（`ListAgents`
顯示 idle，但 task-notification 從未送達，事後補發訊息詢問兩次也未收到回覆）——已用
`SendFeedback` 記錄為工具異常。Task 1 的完成判定改依「repo 自己的 Tier 3 codex review
（6/6 面向）+ controller 自行逐行核對兩輪 fix diff」，覆蓋率不算低，但缺少 SDD 計畫原定的
獨立 task-reviewer 意見——若之後這兩個 agent 的回報意外出現，應回頭核對是否有新發現。

---

## 5. 殘餘風險（延續自 Phase H whole-branch review，本次擴大範圍時需留意）

Phase H whole-branch review（`docs/superpowers/phase-h-handover.md §3-A10`）留下的
架構債 ⑨⑩⑪ 都跟本次要動的 `kSettingsAdjustableItems`／`settings_menu_item_t` 直接相關：

- **⑨ 選單反白框覆蓋不足**（17% 面積問題）：8 項擴充後這個既有視覺缺陷會出現在
  全部 8 列，本次計畫不修（spec §7 已明列不在範圍），維持既有 park
- **⑩⑪ 型別設計債**（`settings_menu_item_t` 缺 action-kind 欄位、子畫面狀態仍是
  平行 bool 不是 discriminated enum）：本次選擇繼續維持既有查表模式擴充，不藉此機會
  重構（brainstorming 時明確問過使用者，選了「不重構」，見 spec §2 決策 #7 替代方案 B
  未採用的理由）。**這次擴充後平行 bool 又多了一個（`settingsDeviceInfoMode`），
  下次若還要在這裡加第三個真正的 modal（不是 placeholder），這個判斷可能要重新評估**。

---

## 6. 對應文件

| 文件 | 用途 |
|---|---|
| `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md` | 設計 spec（決策記錄 + 架構） |
| `docs/superpowers/plans/2026-09-01-phase-g-device-info.md` | 實作計畫（6 task，TDD 步驟） |
| `docs/pm-dev-spec.md §四 Phase G` | 高層 Phase 描述（完成後待更新，見計畫 Task 6） |
| `docs/superpowers/phase-h-handover.md §3-A7` | 本次工作範圍的裁決背景（上一階段文件） |
