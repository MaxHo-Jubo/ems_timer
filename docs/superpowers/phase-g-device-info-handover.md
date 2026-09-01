# Impl-Phase G 裝置資訊畫面 — 交接文件

- **最後更新**：2026-09-01（Task 2 完成，等使用者確認後續跑 Task 3-6）
- **狀態**：SDD（subagent-driven-development）流程進行中。**Task 1-2/6 完成**，其餘 4 個
  task 未開工。依使用者要求，每完成一個 task 就停下讓使用者確認，本次做完 Task 2。
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
最終 commit `1f924b4`（3 輪 amend）。經 repo 自己的 Tier 3 codex review（6 面向）+
SDD task-reviewer + SDD scoped re-reviewer 三層驗證，共抓到 1 CRITICAL（整數下溢/溢位）
+ 14 IMPORTANT + re-review 額外抓到 3 個殘留 magic number，三輪 fix 後全部收斂，細節見 §4。

**Task 2 完成**（2026-09-01）：設定選單擴充至 SoT §19.1 完整 8 項並支援捲動，最終 commit
`c980927`（1 輪 amend）。經 repo 自己的 Tier 3 codex review 抓到 5 CRITICAL（4 個同源於
`main.cpp:1177` 舊呼叫點參數錯位——team-lead 覆蓋原「留給 Task 4」的裁決，當場修正）+
12 IMPORTANT，全部處理後 SDD task-reviewer 給 Approved（0 Critical/Important，1 Minor
延後）。細節見 §4。**過程中發現一個流程缺口**：implementer 第一次 commit 後，repo 的
Tier 3 codex review 閘門從未被實際執行過（marker 卡在磁碟上、沒有背景 process、implementer
沒有理會 hook 的 systemMessage 指派）——是 team-lead 後來想結束回合被 Stop hook 攔下才發現，
手動補跑才解決，已存 feedback memory（`feedback_sdd_dispatch_must_mention_commit_gate.md`）
供未來 dispatch brief 參考。

**尚未開工**：Task 3-6。

---

## 2. 如何接手

```bash
cat docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md   # 先讀設計決策
cat docs/superpowers/plans/2026-09-01-phase-g-device-info.md          # 再讀實作計畫

# 開工前先確認測試基準線（Task 2 完成後現況）
cd firmware && pio test -e native                            # 應為 636/637，唯一 ERRORED = test_storage_hw
cd firmware && pio run -e esp32-s3-devkitc-1                  # 應為 SUCCESS，Flash 72.7%
git log --oneline -5                                          # 確認在 feat/phase-g-device-info worktree 上
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
  已授權 implementer 偏離 plan brief 字面程式碼補齊 Global Constraints。

**三輪 fix 摘要**：
- **Round 1**（對 codex 的 1 CRITICAL + 14 IMPORTANT）：CRITICAL 結構性修正（確認正確）；
  doc comment 改未來式；STEP 01.01/02.01/03.01 補齊巢狀編號；測試檔全數補 JSDoc+STEP+
  部分具名常數；新增 2 個邊界測試（`visible_rows=0`、`UINT16_MAX` 附近溢位）。
- **Round 2**（controller 自行核對 diff 抓到的殘留）：`(visible_rows - 1)` 的 `1` 抽成
  `LAST_VISIBLE_INDEX_OFFSET`；doc comment 修正為準確描述目前的 guard 行為（不再宣稱
  「計算錯誤」）；兩處裸數字 `3` 抽成 `EXPECTED_OFFSET_AFTER_SCROLL_DOWN`。
- **Round 3**（SDD scoped re-reviewer `phase-g-task1-rereview` 額外抓到、round 2 未覆蓋
  的殘留）：`test_main.cpp:140` 的 `8`、`:155` 的 `0`、`:175` 的 `65531` 三處裸數字，
  分別抽成 `FIXTURE_VISIBLE_ROWS_FULL_LIST`／`INVALID_VISIBLE_ROWS_ZERO`／
  `EXPECTED_OFFSET_NEAR_MAX`。

**SDD 流程自身的三層驗證意見**（延遲約 15-20 分鐘才以 idle_notification 批次送達，
過程中一度誤判為「未回報」，見下方異常說明）：
- `phase-g-task1-review`（對 round 0／原始 commit `016c9e0` 的 task-review）：Approved，
  0 Critical/Important，獨立手算重新驗證全部 8 個原始測試案例數學正確。**唯一分歧**：
  它把 `visible_rows==0` 未防護標記為 Minor「不需要修」，跟 codex 的 CRITICAL 判定不同調——
  已按 codex（repo 強制閘門、機械判定）為準處理，未採用 SDD reviewer 這條「不用修」的
  建議。
- `phase-g-task1-rereview`（對 fix round 1／commit `6e0c144` 的 scoped re-review）：確認
  CRITICAL 結構性修正無誤；額外抓到 round 2 dispatch 時我自己漏掉的 3 個殘留裸數字
  （即上方 round 3 處理的三項），並確認 round 2 已把「明確指示錯誤」但實際靜默 fallback
  的矛盾註解修正掉。

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

**已知異常（已解除，記錄供參考）**：SDD 流程自己的 task-reviewer 與 fix round 1 的
scoped re-reviewer 兩個 subagent 完成工作後（`ListAgents` 顯示 idle）長時間沒有
task-notification 送達，補發訊息詢問也沒有立即回應，一度判斷為「未回報」並改用其他
驗證管道收工 Task 1。約 15-20 分鐘後兩者的報告才以 `idle_notification` 批次送達（連同
補發訊息的回覆一起），內容完整且發現了 round 2 遺漏的 3 個殘留（已在 round 3 處理）。
**結論：不是遺失，是嚴重延遲**——已用 `SendFeedback` 記錄這個延遲異常。教訓：往後遇到
subagent 顯示 idle 但報告遲遲不到，除了補發訊息詢問，也該考慮再多等一段時間再改用
備援驗證手段，避免像這次一樣兩條路徑分別跑（雖然最後互補、沒有造成錯誤結論，但多花了
一輪不必要的 controller 自行核對）。

### Task 2 完成（2026-09-01）——設定選單擴充至 SoT §19.1 完整 8 項並支援捲動

**現況**：`firmware/lib/ui_settings/ui_settings.h`／`.cpp`（8 項 `kSettingsMenuItems[]`、
捲動視窗迴圈、`scroll_offset` 參數）+ `firmware/test/test_settings_ui/test_main.cpp`
（21 個 test case），commit `c980927`（1 輪 amend，最終版）。native test 637 cases /
636 通過（唯一 ERRORED 仍是既有的 `test_storage_hw`，與本次無關），韌體編譯 SUCCESS，
Flash 72.7%（較 Task 1 前 71.4% 微增）。

**Repo 自己的 Tier 3 codex review 抓到的問題**（跟 SDD 流程平行的獨立閘門）：

- **5 CRITICAL，其中 4 個同源**：`firmware/src/main.cpp:1177` 仍用舊 4-arg 呼叫
  `drawSettingsMenu(settingsDisp, settingsCursor, g_device_name_locked,
  settingsRestoreConfirm)`。新簽名插入 `scroll_offset` 於第 3 位，這個舊呼叫編譯通過
  （bool→uint8_t 隱式轉換 + 尾端預設值）但靜默錯位：`g_device_name_locked`→
  `scroll_offset`、`settingsRestoreConfirm`→`device_name_locked`、`restore_confirm`
  永遠吃預設 `false`。實機上恢復預設對話框永遠不顯示、名稱鎖定狀態錯、選單不會捲動。
  第 5 個 CRITICAL（silent-failure）是獨立問題：`drawSettingsMenu()` 的捲動迴圈信任
  呼叫端已執行過 `clampScrollOffset()`，越界 `scroll_offset` 會靜默畫出空白選單。
- **12 IMPORTANT**：多為 STEP 編號未跟著 Step 4 的迴圈重寫更新（STEP 04→03 殘留）、
  多處註解過時（Y 座標常數說明、`drawPlaceholder()` 已接線的錯誤暗示、JSDoc 宣稱呼叫端
  已算好 scroll_offset）、`SETTINGS_ITEM2/3/5_Y` 死碼未清、捲動測試斷言力度不足（只查
  文字有無畫出，未驗證 Y 座標）。

**Ruling（推翻我自己先前的裁決）**：main.cpp 那 4 個 CRITICAL 的根因（呼叫點錯位）**team-lead
裁定必須當場修，不留給 Task 4**——這推翻了本文件先前版本、以及 Task 1 pre-flight scan 對
「2→4」依賴關係下的「Clean」判斷。理由：(a) 本專案 GLOBAL-MUTATION 規則要求修改共用函式簽名
時同一改動內搜尋並更新全部呼叫點，不能拖到兩個 task 之後；(b) 一個編譯過但實機行為全錯的
commit 留在分支歷史裡是真實風險，這個 worktree 隨時可能被燒錄到硬體測試；(c) 修法不需要
提前接 Task 3 的範圍（`settingsScrollOffset` 全域與按鍵驅動的更新邏輯仍歸 Task 3）——只需
把兩個既有值重排到新位置，`scroll_offset` 傳字面值 `0`（跟目前尚不能捲動的行為完全等價，
零迴歸，零 scope creep）。第 5 個 CRITICAL（呼叫端信任 clamp 過的 offset）**裁定接受、不修**，
跟 Task 1 `clampScrollOffset()` 的 `visible_rows==0` guard 是同一種已核准的 trade-off——
內部 helper、目前所有呼叫端不是編譯期常數就是走 `clampScrollOffset()`，迴圈邊界
（`i < SETTINGS_MENU_ITEM_COUNT`）本身已避免真正的越界記憶體存取。12 個 IMPORTANT 全部
授權修正（非 plan-mandated，是 implementer 自我審查真的漏掉的缺口）。

**Fix round 1 摘要**：main.cpp 依裁定的字面順序修正；`input_handler.cpp`／`app_globals.h`／
`ems_display_snapshot.h` 三處游標範圍註解更新為誠實描述現況（0~7，但 5~7 尚未接線）；
`ui_settings.cpp` STEP 04→03 全部改回正確編號並補齊巢狀 STEP（03.02/03.03/03.03.01/
03.03.02/03.04/03.04.01）；`SETTINGS_ITEM2/3/5_Y` 死碼移除（grep 全庫確認無殘留引用）；
`drawPlaceholder()`／JSDoc 誇大現況的措辭改為誠實的「未來規劃」；6 個測試函式補 STEP 註解；
`test_scroll_offset_three_shows_last_five_items` 強化為逐項斷言 5 個可見列的 Y 座標
（30/70/110/150/190）與高亮 fill_rect 的 Y。修正後 focused 21/21、full suite 636/637、
ESP32 build SUCCESS（Flash 大小不變，純參數順序 + 註解修正）。

**SDD task-reviewer（`phase-g-task2-review`）**：Approved，0 Critical/Important，1 Minor
（`STEP 03.03`／`03.04` 的 `if (selected) fill_rect(...)` 重複樣式——brief 原文就是這樣寫、
不算本 task 缺陷，已知的未來清理項見 §5「⑨」）。獨立 grep 驗證 `drawSettingsMenu()` 全庫
只剩這一個非測試呼叫點、零殘留舊符號引用；獨立核對新增中文字（連/線/設/定/裝/置/資/訊）
確實已存在既有畫面字串中，佐證「VLW byte-identical、0 缺字」的說法不只是信implementer報告。

**過程中發現的流程缺口（已記錄 feedback memory）**：implementer 第一次 commit（`a750a5c`）
觸發 repo 的 Tier 3 codex-review 閘門（PostToolUse hook 寫 marker + 印 systemMessage 軟指派
`Skill(commit-review)`），但 implementer 沒有執行這個 skill——它接著在自我審查時想疊第二個
commit，撞上 `commit-gate-guard` 的 PreToolUse deny，正確地改用 `git commit --amend` 繞過，
但從未真正跑過那輪 review。這個 marker 就這樣卡在磁碟上，直到 team-lead 自己想結束回合時
被 `stop-review-guard`（Stop hook）攔下才發現（`~/.claude/state/pending-review/` 有一個
未清的 marker，`ps`／codex-review state 目錄都查無任何背景 process 在跑）。team-lead 手動
用 `commit-review` skill 補跑 `compute-tier.ts` + `codex-review.ts --tier=3`，找出上述 5
CRITICAL + 12 IMPORTANT，裁決、派 fix round、驗證、`clear-pending-review.ts` 解鎖，才讓
自己的回合能正常結束。**教訓**：dispatch 會在啟用此 gate 的 repo 內 commit 的 subagent 時，
brief 必須明講這個機制存在並要求「收到 systemMessage 指派就要執行」，否則 marker 會安靜
卡住直到某個 session 的 Stop 事件命中它——已存為
`feedback_sdd_dispatch_must_mention_commit_gate.md`，Task 3 起的 dispatch prompt 應該
把這段提醒加進去。

### ⏳ 待辦（2026-09-01 使用者確認記錄、留給下次接手）：amend round 從未跑過完整 codex Tier 3

使用者事後追問「Task 1 跟 Task 2 是否都完整跑過 commit-review」時查出的落差，記錄於此、
本次不補跑，留給下次接手或下次 whole-branch review 時決定是否要補。

**事實**（查 `~/.claude/state/pending-review/unlock-audit.log` 逐筆核對，非憑印象）：兩個
task 全程只有各自的**第一次** commit 真正跑過完整 6 面向 codex Tier 3 review 並解鎖——
`016c9e0`（Task 1）與 `a750a5c`（Task 2），各自 `expected=6 done=6 result=OK`。之後**所有**
修 bug 的 amend（Task 1 的 `6e0c144`／`e5bb45d`／`1f924b4` 三輪；Task 2 implementer 自我審查
的 `815c8d8`、以及 team-lead 裁決 fix round 1 的 `c980927`）都沒有再跑過這套機制的第二輪。

**根因**（讀 `~/.claude/scripts/post-commit-review.ts` 原始碼確認）：
```ts
const skipMarker = /\[skip-review\]/i.test(command) || /--amend/.test(command);
```
commit 指令只要含 `--amend`，hook 就不寫新的 pending-review marker——仍會印 systemMessage
建議跑 `/commit-review`，但沒有 PreToolUse/Stop 兩層機械閘門背書，純軟性建議，跟
`feedback_sdd_dispatch_must_mention_commit_gate.md` 記錄的那個「implementer 沒理會
systemMessage」是同一種訊息、同一種沒人接住的落差——差別是這次連 team-lead 自己（我）
在裁決並派 fix round 時也沒注意到 `git commit --amend` 這個動作本身會讓新一輪的修正完全
繞過機械閘門。

**這些 amend 實際上靠什麼把關**（不是機械閘門，是兩條獨立的替代驗證路徑）：
1. Controller（我）自行逐行讀 diff——兩個 task 的每一輪 amend 我都有做，細節見上方各輪
   fix round 摘要。
2. SDD 流程自己的 task-reviewer／re-reviewer subagent（`phase-g-task1-review`／
   `phase-g-task1-rereview`／`phase-g-task2-review`）——對 amend 後的最終 diff 給出
   Approved，是跟 codex Tier 3 平行、獨立存在的第二套機制，不是同一套機制的重跑。

**使用者裁示**：不現在補跑，先記錄到本文件，下次接手時再決定要不要對 Task 1／Task 2 的
最終 commit（`1f924b4`/`77c5bba` 與 `c980927`/`f9982b6`）補一次完整 6 面向 codex Tier 3
（`bun ~/.claude/scripts/codex-review.ts --tier=3 --target=<commit>`）。**風險評估**：兩層
替代驗證都已跑過且都是 Approved，殘餘風險偏低，但 codex Tier 3 過去確實抓到過 SDD
reviewer 沒抓到的 CRITICAL（Task 1 的整數下溢／溢位、Task 2 的 main.cpp 呼叫點錯位皆是
codex 先抓到，SDD reviewer 對前者只標 Minor）——若要徹底排除疑慮，仍以補跑一次最省事。

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
