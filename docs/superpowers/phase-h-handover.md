# Impl-Phase H 電量顯示 — 交接文件

- **最後更新**：2026-09-01（**whole-branch review 完成並收尾**，見 §3-A10；下一步是 `finishing-a-development-branch`，尚未執行）
- **狀態**：**Phase H 全部 13 個 task 完成**（Task 14 已移出併入 Impl-Phase G，見 §3-A7）。W1 完成（Task 1–6）。W2 完成（Task 7–9）。W3 全部完成（Task 10 見 §3-A4、Task 11 見 §3-A6）。W4 完成——Task 12（系統設定選單新增「電池資訊」第 5 項，4 輪 fix，最終 `e6bf46d`，見 §3-A8）與 Task 13（電池資訊畫面本體，1 輪 fix，最終 `5cffdae`，見 §3-A9）皆已收工。**Whole-branch review 已完成**（`7fdf1ee..5cffdae` 全範圍，見 §3-A10）：無新 Critical/Important，2 個 Minor 已修（commit `e45d855`）。native test 623/624（唯一 ERRORED 是既有的 `test_storage_hw`，與 Phase H 無關），韌體編譯 SUCCESS，Flash 72.4%。**尚未執行**：`superpowers:finishing-a-development-branch`——branch 仍未推送。所有上機驗收累積待硬體（§3-B，11 條，最大殘餘風險）。
- **branch**：`feat/phase-g-system-settings`（未推送）

> 本文件是單一時間線，取代先前三層疊加的版本。裡面所有數字與 commit 都在 2026-08-23 收工時實測過。
>
> ⚠️ **commit hash 變過兩次。** 2026-08-23 補跑 Task 7 review 時 rebase，`df33d97` → `94bc3fb`。
> 2026-08-24 Task 10 經 6 輪 fix，每輪都把修正折回同一個 feat commit，中間產生的
> `e81f9b7` / `fffc27f` / `8cfb2f1` / `98b6983` / `5953893` / `2216bd4` **全部已脫離分支**
> （`git gc` 後會消失）。Task 10 現行是 `dc4aaf1`（feat）+ `0905947`（docs）。

---

## 1. 三十秒看懂現況

MAX17043 燃料計硬體驗收通過，主韌體每 10 秒輪詢一次寫進四個全域。W2 顯示層（電量圖示、低電量閃爍）與 W3（Task 10 §13.16 低電量一次性提示、Task 11 §20.3 低電量開案確認框）都已完成。

**但整個 Phase H 一次都沒在實機上跑過**——本階段全程無硬體，§3-B 累積的整套上機驗收全部未執行（僅 Task 6 的「失敗不清警示」一項已在 2026-08-22 首次上機時通過）。所有其餘「已完成」的結論都建立在 native test 加靜態推理上。這是本階段第一號殘餘風險，見 §8。

| 項目 | 狀態 |
|---|---|
| 硬體 | ✅ 驗收通過（I2C `0x36`、3.844V vs 電表 3.88V） |
| spec | ✅ `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md` |
| 實作計畫 | ✅ `docs/superpowers/plans/2026-08-22-phase-h-battery-display.md`（13 task，Task 14 已移出見 §3-A7） |
| W1 讀取層 | ✅ Task 1–6 review clean；**上機驗收剩兩項待硬體** |
| W2 顯示層 | ✅ 完成：Task 7（`94bc3fb`）、Task 8（`3333235`）、Task 9（`5634b52`）review clean |
| W3 低電量行為 | ✅ 完成：Task 10（`dc4aaf1`，6 輪 fix，見 §3-A4）、Task 11（`58efa0e` + 3 處 STEP 註解補丁，5 輪 fix，見 §3-A6） |
| W4 電池資訊畫面 | ✅ 完成：Task 12（4 輪 fix，最終 `e6bf46d`，見 §3-A8）、Task 13（1 輪 fix，最終 `5cffdae`，見 §3-A9）（原 Task 14 已移出併入 Impl-Phase G，見 §3-A7） |

**實測數字**（2026-08-24 Task 10 收工時）：全套 **599 cases / 598 通過**（Task 10 淨增 20 條）。唯一未過的 `test_storage_hw` 是**既有**編譯錯誤（已用 worktree checkout 到本工作起點驗證過，與 Phase H 無關）。ESP32-S3 韌體編譯 SUCCESS，**Flash 71.4%**（字型兩次重生共 +21.8KB）。

**實測數字**（2026-08-30 Task 11 收工時）：全套 **616 cases / 615 通過**（Task 11 淨增 17 條，唯一未過者同上、與 Phase H 無關）。ESP32-S3 韌體編譯 SUCCESS，**Flash 71.5%**。

---

## 2. 如何接手

```bash
# 1. 讀 spec 與計畫（計畫是 spec 的論證，兩者一起讀）
docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md
docs/superpowers/plans/2026-08-22-phase-h-battery-display.md

# 2. 確認目前狀態
cd firmware && pio test -e native -f test_fuel_gauge_logic   # 應為 69/69
cd firmware && pio test -e native                            # 應為 621/620，唯一 ERRORED = test_storage_hw
cd firmware && pio run -e esp32-s3-devkitc-1                 # 應為 SUCCESS
git log --oneline 7fdf1ee..HEAD                              # Phase H 的全部 commit
```

**續跑方式**：用 `superpowers:subagent-driven-development`。它會偵測 `.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md` 這個 ledger 並從第一個沒有 `complete` 記號的 task 接續。**Task 1–12 全部 complete，下一個是 Task 13**（電池資訊畫面本體）——ledger 內已有 Task 12 完整四輪 findings/ruling 可供參考，不需要重新查證。

> ⚠️ `.superpowers/` 是 git-ignored 的本機工作區，換機器就沒有了。本文件是它的持久化摘要；ledger 內的逐輪細節（每個 review 面向的原始 findings、48 條 ruling 的完整上下文）只在本機。

---

## 3. 接手待辦（剩一筆，需要硬體）

### 3-A. ✅ Task 7 的 review 債已清（2026-08-23）

原本的 `--force` 放行債務已還完。跑了兩條獨立通道：

- **codex 六面向**（`rules` / `code-review` / `silent-failure` / `comments` / `tests` / `types`，effort=high）：6/6 通過無降級
- **SDD task review**（spec 合規 + 品質）：✅ Spec compliant / Approved

**兩邊結論分歧很大**——SDD reviewer 判零 Critical，codex 報 1 Critical + 13 Important。
差別在於 SDD reviewer 被 prompt 限制在 diff 範圍內，codex 去查了 `main.cpp` 的呼叫端。
逐條驗證後 **codex 對三條、誤報七條**，且它自己把最重的那條標成 Important 而非 Critical。

**修掉的（已 amend 進 `94bc3fb`）**：

1. **Critical — `main.cpp` 的第二套比較漏欄位**。`updateDisplay()` 除了 STEP 01 的整包
   `memcmp`，還有一份手寫的 `sameStateAsLast` 只比 10 個欄位，新增的電池欄位沒進去。
   倒數畫面下電量與 `countdownSec` 同 frame 變化 → 走 partial 路徑 → `lastDisplaySnapshot = now`
   把新電量吞掉且不重繪。10 秒輪詢與每秒 tick 必然週期性重合。
   **這是同型 bug 的第 6 次**（前 5 次見 §7 第 3 條），也是 Task 7 commit message 自稱
   「四處一起改」卻漏掉的第五處。
   解法沒有照 codex 建議「把兩個欄位加進清單」——那第 7 次還會漏。改成
   `snapshotsEqualExceptCountdown()`：複製一份把 `countdownSec` 對齊後整包 `memcmp`，
   10 行欄位清單變 1 行，往後新增任何欄位自動納入。
   **副作用（刻意接受）**：比對範圍變嚴，`syncState` / `historyCursor` 等原本沒比的欄位
   現在也算數 → 只會多重繪，不會漏重繪。
2. **Important — `test_all_flags_on_combine_all_bits` 沒納入新 flag**。名為「所有 flag 同時開」
   卻只開 18 個，新 bit 逃過組合驗證；Group 4 註解還寫「16 個 bit」（舊誤，一併修成 19）。
3. **Important — 電池測試只驗「兩份 snapshot 不相等」**，沒斷言值落在具名欄位上。

**駁回的（附理由，不要再被同一批 finding 打斷）**：

| finding | 為什麼駁回 |
|---|---|
| silent-failure 標的 Critical：三個新 input 有預設值會掩蓋 caller 漏傳 | `DisplaySnapshotInputs` 全部 18 個既有欄位都有預設值，是既有設計不是本次退化。Task 8 必須確實填值 → 見下方 📌 |
| `if` 缺大括號違反 IF-BRACES | 該檔 19/19 全是單行 `if`，改一行製造 1-vs-19 不一致。要改是全檔範圍決策 |
| `captureSnapshot` 欄位賦值違反不可變性 | 全檔 POD 逐欄位賦值慣例，且 esp32 環境是 `gnu++11` |
| types：改用強型別包裝 percent / ChargeState | `DisplaySnapshot` 是 `memcmp` 去重用的扁平 POD，包裝型別直接破壞該語意（已有 ruling） |
| Magic Number 255 應改用 `ems::BATTERY_PERCENT_ABSENT` | `ems_display_snapshot` 不依賴 `ems_fuel_gauge` 是刻意分層 |
| 測試缺 STEP 註解 / 函式註解 | 全檔 45 個測試函式、0 個 STEP 註解，既有慣例（字面檢查只對全新檔案） |

**這次也補上一個上個 session 漏掉的驗證方向**：當時的鑑別力檢查只驗了「拿掉拷貝 → 變紅」，
沒驗「拷貝到錯欄位」。在隔離 worktree 實測：把 `batteryPercent` 與 `batteryChargeState`
**互換寫入**，Task 7 原版測試 **45/45 全綠**，完全無感。這是 §6 `verify-the-observer` 的第 6 次命中。
補上具名欄位值斷言後，同一個 mutation 讓 4 條測試轉紅。

> 📌 **Task 8 會用到 `batteryChargeState`。** snapshot 層只搬運原始 `uint8_t`，**沒有範圍檢查**，
> 上游要確保只餵合法的 `ems::ChargeState` 轉型值。另外 Task 8 必須真的把
> `g_battery_percent` / `g_battery_charge_state` / 閃爍相位填進 `DisplaySnapshotInputs`——
> 不填不會有任何編譯或測試錯誤，畫面會安靜地永遠顯示「燃料計不在線」。

### 3-A2. Task 8 完成，給 Task 9 的三條交接（2026-08-23）

`3333235` — `presentFrame()` 統一重繪出口。15 處 `display.pushSprite(0,0)` 全數收斂
（grep 驗過只剩 `presentFrame()` 內部那一處），`captureDisplaySnapshot()` 已填入三個電池欄位。
fix round 1 修掉兩條 Important。

1. ⚠️ **partial update 路徑不 `clearDisplay()` 就推送。** `updateDisplay()` 的
   `inCountdownGroup && sameStateAsLast` 分支呼叫 `drawOhcaCountdownTimeOnly()` 後直接經
   `presentFrame()` 推送，中間沒有清屏。目前 `drawBatteryIcon()` 是 no-op 所以沒影響，但
   **Task 9 畫出圖示後，圖示必須自己清除它那一小塊區域的背景**，不能依賴外層 `clearDisplay()`，
   否則倒數畫面上的電量圖示會疊字。Task 9 的 review 要特別看這條路徑。
2. **`drawBatteryIcon()` 目前是誠實標示的 no-op placeholder**（`ui_screens.cpp:804-810`
   的 JSDoc 分「現況」與「預定行為」兩段寫）。Task 9 填完內容後記得把那兩段合併回單一敘述。
3. **低電量閃爍相位已經是純函式** `ems::compute_low_battery_blink_on(percent, low, now_ms)`
   （`fuel_gauge_logic.h/.cpp`），含「燃料計不在線一律回 false」的守衛與 6 條 native test。
   Task 9 只要讀 `SNAP_FLAG_BATTERY_LOW_BLINK` 這個 bit 決定畫不畫，**不要自己再算一次相位**。

> 💡 那條守衛擋的是實際問題：失聯時 `g_battery_percent` 變哨兵但 `g_battery_low` 依設計保持
> 鎖存，相位若繼續翻轉會讓 snapshot 每 500ms 變化 → 整片重繪，而圖示在不在線時本來就不畫
> → 畫面毫無變化的永久性空轉重繪。失聯不會自己好，所以那個狀態是永久的。

**Task 8 的 1 個 parked**：`presentFrame()` 的呼叫順序與次數沒有測試（無法驗證「overlay 必定在
pushSprite 前恰好執行一次」、各早退分支是否漏呼叫）。要測得把呈現流程抽成可注入的協調層，
屬計畫層架構決策。日後新增畫面若漏呼叫 `presentFrame()`，native 測試不會發現，只能靠 grep 與 review。

### 3-A3. Task 9 完成（2026-08-23）

**Task 9（`5634b52`）**：四格電量圖示、低電量閃爍、幾何閃電符號。經 2 輪 fix。
新增到 lib 的純函式：`should_draw_battery_icon(percent, low, lowBlinkOn)`、
`battery_segments_for_percent(percent)`，都有 native test。

> ⚠️ Task 9 過程中 controller 抓到一個 Critical：`drawBatteryIcon()` 原本寫成
> `if (!lowBlinkOn) return;`，而 `compute_low_battery_blink_on()` 在非低電量時恆回 false
> ——**電量正常時圖示完全不畫**。根因是 controller 自己 dispatch 的措辭誤導。
> 這個缺陷編譯會過、native 抓不到（`ui_screens.cpp` 不進 native build）、也沒有硬體可上機發現，
> 三層觀測全部涵蓋不到。修法是抽出 `should_draw_battery_icon()` 三分支純函式並加測試。

**給 Task 10–14 的一條約定（仍然有效）**：呼叫 `ems::battery_segments_for_percent()` 前必須先用
`ems::is_battery_absent()` 擋掉哨兵——該函式對 255 會回滿格。Task 9 review 時 codex 要求在函式內
加 guard，implementer 論證「呼叫端本來就得先做不在線的早退判斷」，controller 採納（不可達的
guard 是死碼＋誤導註解）。**代價已知**：正確性押在呼叫端記憶力上。若 Task 12–14 出現第二個
呼叫點，應改採 `enum class BatteryIconState { Absent, Normal, LowOn, LowOff }`。

---

### 3-A4. Task 10 完成（2026-08-24）——§13.16 執行中低電量一次性提示

**commits**：`dc4aaf1`（feat）+ `0905947`（docs）。**經 6 輪 fix、4 個 CRITICAL**。
全套 599 cases / 598 通過（Task 10 淨增 20 條），韌體編譯 SUCCESS，Flash 71.4%。

#### 最終長什麼樣

| 層 | 內容 |
|---|---|
| lib 純邏輯 | `LowBatteryNoticeState{active, start_ms}`、`is_low_battery_notice_visible(state, now)`、`is_low_battery_notice_context(in_ohca, in_vent, vent_pre)`、`low_battery_notice_tick(state&, latch&, ...)` |
| 狀態機 | tick 一次做四件事：①非適用情境→復歸 ②逾期→復歸 ③適用且 latch 有事件→啟動 ④維持。**回傳 void、原地改寫**，caller 無法漏接 |
| main.cpp | `tryStartLowBatteryNotice(now)` 每輪 `loop()` 呼叫（不掛在 10 秒輪詢內），`g_low_battery_notice` 單一全域 |
| snapshot | `SNAP_FLAG_LOW_BATTERY_NOTICE = 0x00080000`，五步驟 checklist 全跑 |
| UI | `drawLowBatteryNotice(bool visible)`，`textWidth()`/`fontHeight()` 動態量測的不透明 panel + 警示框，鏡像 `drawConfirmDialog()` |

#### 四個 CRITICAL（都只有讀程式碼或讀二進位資產才發現得了）

1. **守衛在呼叫端**（`consume_first_entry()` 是公開 API）→ 收斂成 `low_battery_notice_tick()`，生產路徑只剩一個入口
2. **提示文字缺 4 個字**（「低議行源」不在 `ems_zh_24_vlw` 字集）→ 重生字型
3. **重生把「設」union 掉**（`regen_vlw.sh` 掃描清單不含 `.h`，而 `MAIN_MENU_LABELS` 在 `app_globals.h`）→ 主選單「系統設定」會顯示「系統▯定」。**修上一條的動作本身引入的**
4. **tick 回傳值可被漏接**（消費了 latch 但狀態寫回在呼叫端）→ 改吃 reference、回傳 void

第 2、3 條合計讓字型從 282 → 325 glyph，順帶補上 Phase G 設定 UI 長期缺的 38 個字
（**設定畫面一直在顯示缺字方塊，這輪才修掉**）。

#### 三處錯誤源自 controller 寫的計畫或 fix 指示

- 「`millis() > until_ms` 溢位後永久判成已過期」——不成立，取決於 `until` 是否也繞回；連帶讓溢位測試失去鑑別力
- 「latch 每次開機只 pending 一次」——錯，回升 25% 再降 20% 會重新 pending
- 要求「測試邊界由 `LOW_BATTERY_NOTICE_MS` 推導」——副作用是常數改成 5000 測試仍全過，§13.16 的 3 秒規格反而失去保護（後補一條硬編 3000 的獨立斷言）

#### 驗證方法上值得複製的三件事

- **鑑別力用變異測試證明，不用推理**：刪掉 tick 的 STEP 01 → 3 條紅、刪掉 STEP 02 → 2 條紅，三次獨立驗證數字一致
- **字型驗證對已 commit 的 git blob 做，不是工作目錄**（這條線上發生過「讀工作目錄以為改好了，其實還沒 commit」的誤判）
- **雙標準編譯驗證**：`gnu++11`（ESP32）與 `gnu++17`（native）行為不同，NSDMI 那個坑就是這樣抓到的

---

### 3-A5. Task 11 接手包（dispatch 前必讀）

**Task 11 是 §20.3 低電量開案確認框**：低電量狀態下按 OHCA 入口 → 確認框「低電量／建議接上行動電源／是否開始？」→ 選「是」進案；選「否」**回主選單，不建立案件**。

#### 🔴 計畫有一條會讓它畫不出畫面的缺陷

計畫要新增 `SUBSTATE_LOW_BATTERY_CONFIRM` 列舉、用 sub-state 驅動確認框。**但 `main.cpp` 的
`GLOBAL_MAIN_MENU` 分支只呼叫 `drawMainMenu()`，完全不看 `ohcaSubState`**——所有 `SUBSTATE_*`
分派都在 `globalState == GLOBAL_OHCA` 分支內。而 §20.3 的確認框必須在**開案前**顯示
（選「否」要回主選單且不建案）。照計畫寫，確認框畫不出來。

**建議改法**：用獨立 bool 旗標，比照既有的 `resyncConfirmShown` / `trainingDeleteConfirm` /
`settingsRestoreConfirm`。這條路徑**必須跑 DisplaySnapshot 五步驟 checklist**——
下一個可用 flag bit 是 **`0x00100000`**（Task 10 用掉 `0x00080000`）。

#### 已查證的介面（不必重查）

- `SUBSTATE_*` 列舉最大值是 **13**（`SUBSTATE_RESET_CONFIRM`），若仍要加列舉則為 14
- `drawConfirmDialog(const char* title, const char* body)` 在 `ui_screens.cpp:776`，宣告在 `app_globals.h:683`
- 主選單建案的 `case 0:` 在 `input_handler.cpp:212`；Training 開案在 `:485` 附近
- 計畫 Step 2 要把建案流程抽成 `startOhcaCase()` helper——**這個方向是對的**，確認後進案與
  主選單直接進案是同一流程的兩個呼叫點，依 EXTRACT-SHARED-HELPER 該抽

#### Task 10 與 Task 11 的互動（✅ round 1 已解決）

擔心的是「確認框選『是』→ 進案 → 下一次 tick 消費 latch → §13.16 的 3 秒提示又跳出來，
使用者連續看到兩次低電量告知」。round 1 把 `latch` 消費綁進
`try_request_low_battery_start_confirm()`（確認框開啟當下即消費），**不是靠 UX 裁決去抑制
其中一邊，而是靠情境守衛天然互斥**：`low_battery_notice_tick()` 的適用情境判斷
（`is_low_battery_notice_context()`）在確認框顯示期間，三個目標（OHCA／VENT/`ventPreShown`／
Training 尚未切到 `GLOBAL_OHCA`）全部落在「不適用」，不會走到它自己的
`consume_first_entry()`。兩個消費點因此不會搶同一次事件，兩段提示不會重疊或搶跑。
完整推導見 `fuel_gauge_logic.h` `low_battery_notice_tick()` 的 JSDoc（2026-08-30 Task 11
fix round 1 之後補上）。

#### 新增中文字串的話

**一定要重跑 `bash scripts/regen_vlw.sh` 並驗字集。** 確認框的文案若含目前字集沒有的字，
實機會顯示 ▯ 而編譯與 native test 都不會報錯——這個坑在 Task 10 咬了兩次。驗法見 §8 第 ② 條。

### 3-A6. Task 11 完成（2026-08-30）——§20.3 低電量開案確認框

**commits**：`58efa0e`（feat，round 1–5）+ 3 處純 STEP 註解補丁（controller 直接補，未另立 commit，隨本輪 docs commit 一併收）。**經 5 輪 implementer fix + 1 次最終 controller confirmatory review**。
native test 615/616（唯一 ERRORED 是既有的 `test_storage_hw`，與 Phase H 無關），韌體編譯 SUCCESS，Flash 71.5%。

**範圍比計畫原定的大**：§3-A5 只寫了 OHCA 一個入口，dispatch 後才發現 spec §5（line 24／198）
明文 §20.3 要蓋三個入口——OHCA／VENT（6 秒通氣節奏獨立模式）／Training，round 1 已補齊。

**五輪 fix 都是同一個根因的漸進收斂**（guard-placement：低電量守衛該放在共用核心，不是
呼叫端記憶力；按鍵穿透 modal 則是同一種「跨迴圈狀態要在轉換當下就處理乾淨」的問題）：

| 輪次 | commit | CRITICAL | 修法 |
|---|---|---|---|
| round 1 | `e915798` | VENT/Training 入口缺確認框；`consumePendingLowBatteryEntry()` 裸 passthrough | 三入口共用一個 target-aware 攔截區；latch 消費綁進 `requestLowBatteryStartConfirm()` |
| round 2 | `2e40400` | 該函式參數仍能合法收到 `None`；`handleButtons()` 同輪多按鍵可能穿透 modal | 拆 `LowBatteryStartTarget`（不含 None）跟 `LowBatteryConfirmTarget`（含 None）兩個型別；`handleButtons()` snapshot 判斷+`break` |
| round 3 | `2e40400`（再次 amend）| `handleButtons()` 的修法是「延後」不是「吞掉」，下一 tick 一樣穿透；低電量判斷仍在呼叫端 | 同輪其餘按鍵直接同步物理狀態吞掉，不延後；`latch.is_low()` 檢查收進 `try_request_low_battery_start_confirm()`，回傳 bool 取代呼叫端自查 |
| round 4（換新 implementer + opus） | `6fba0dc` | 吞鍵邏輯只處理 modal「關閉」方向，沒處理「開啟」方向；仍按住的按鍵被誤設成新按壓而非清空 | 判斷式改成比對前後「任何」轉換（`!=` 取代單向 `&&!`）；仍按住的鍵一律清 `btnPressStartMs=0` 不寫 `now`；抽出 `isBlockingModalActive()` 共用；`startTrainingCase()` 週期改明確參數 |
| round 5（SDD 上限，implementer 自己提出三個疑慮並各自裁決） | `58efa0e` | 無新 CRITICAL——implementer 主動指出 `lastPressMs[j]` 未同步（防抖繞過窗口）與 `j<i` swallow 索引缺口 | 前者一行修正併入 swallow 迴圈；後者 park 並寫入殘餘風險 ⑦ |

**每輪都有 codex Tier 3 六面向 + SDD task/re-review 雙軌驗證**，多次出現雙邊各自獨立抓到
不同缺口再互相印證的情況（round 1：codex 抓 VENT、SDD reviewer 抓 Training；round 3→4：
SDD re-review 獨立發現跟 codex round 4 同源的按鍵計時器問題，分析角度不同但結論收斂）。

#### 最終 confirmatory review（2026-08-30 22:xx，對 `58efa0e`）

2026-08-30 18:31 的重跑因 codex CLI 額度用完失敗（非 review 找到問題），依使用者裁示等到
21:56 CST 後重試，同一 target 6/6 面向全數通過（`rules`/`code-review`/`silent-failure`/
`comments`/`tests`/`types`，effort=high）。完整報告：
`~/.claude/state/codex-review/-Users-maxhero-Documents-MaxHero-Projects-ems_timer/58efa0e`。

出現 1 個新 CRITICAL + 20 個 IMPORTANT，逐條裁決如下（不開 round 6，比照 handover 既定指示）：

- **CRITICAL（`input_handler.cpp:206`，modal 按鍵穿透）**——逐行核對後確認就是 §8 殘餘風險 ⑦
  本人（`handleButtons()` swallow loop `j = i+1 .. BTN_COUNT-1` 不含 `j < i`）。這是本輪 codex
  獨立再次抓到同一個已知、已 park 的缺口，非新問題。維持 park，§8 已補註「二次獨立確認」。
- **3 個「函式缺 STEP 註解」**（`to_confirm_target()`／`isBlockingModalActive()`／
  `requestLowBatteryStartConfirm()`）——機械式合規、零邏輯風險，controller 直接補上單行
  STEP 01 註解（不算開新 fix round，純註解不影響行為），已跑 native test + ESP32 編譯驗證無異常。
- **if-brace／不可變性（`ems_display_snapshot.h:202`）**——新增行沿用該檔 19/19 既有單行 `if`
  + POD 逐欄位賦值慣例，Task 7 §3-A 已駁回同型 finding，維持駁回。
- **reference-mutation（`try_request_low_battery_start_confirm()` 用 non-const ref 回寫）**——
  round 3 的刻意設計（回傳 bool + 原地寫回，讓呼叫端無法漏接，見 §3-A4 Task 10 同型裁決的
  姊妹決策），非退化，維持現狀。
- **`LowBatteryConfirmDecision` 型別可表示非法狀態、`to_confirm_target()` 窮舉後仍需
  `return None`**——round 4/5 已裁決 park（12 條窮舉測試鎖住行為、唯一呼叫點結構上不可達、
  C++11 tagged union 成本不成比例），本輪 `types`／`silent-failure` 兩面向重複同一 finding，
  維持既有裁決。
- **`input_handler.cpp`／`main.cpp` 整合層測試缺口**（`tests` 面向兩條）——即 §8 殘餘風險 ⑥，
  維持既有 park 理由（native 環境不編譯 `src/`，需抽 coordinator 層）。
- **docs 一致性（handover 狀態過期、簡體字「该」）**——codex 審查的是 commit `58efa0e` 當下
  的文件內容（仍寫 round 3）；本 session 稍早已把本節更新到 round 5 狀態、「该」也已隨改寫
  訂正為「該」，本次 confirmatory review 一併把本節重寫為此定稿版本，無需額外動作。
- **`isLowBatteryStartConfirmActive()` 判斷式重複於 5 處呼叫點**（`input_handler.cpp:143/
  391/1376`、`main.cpp:945/1057`，`code-review` 面向新發現，非前五輪已知項目）——符合
  `EXTRACT-SHARED-HELPER` 該抽共用 helper 的判準，但這 5 個呼叫點橫跨 `input_handler.cpp`／
  `main.cpp` 兩個檔案，且正是這一整個 wave 反覆出包的同一塊 modal 狀態表面。在剛結束
  round 5 之後，為了收斂一個純語法重複而立刻再動這塊程式碼，risk/reward 不成比例——
  已 park 並寫入 §8 殘餘風險 ⑧，留給 §8 ⑥ 的 coordinator 層重構一併評估。

**已 park 的項目彙總**（詳細理由見 ledger，不要重新開 fix round 討論）：
- if-brace／不可變性兩類 codex `rules` finding：沿用 Task 7 對 `ems_display_snapshot.h` 同一模式的既有裁決。
- `onShortPress()`／`onLongPress()` 的 STEP 編號：前者是跨整個函式的既有「每分支各自 STEP 01 起算」慣例（已 grep 驗證 10 處），後者 codex 誤報。
- `LowBatteryConfirmDecision` 的 `next_target`+`proceed` 可表示非法組合、`to_confirm_target()` 窮舉 switch 後的必要 `return None`：12 條窮舉測試已鎖住實際行為，唯一呼叫點結構上保證不可達，C++11 環境做 tagged union 的樣板成本與風險不成比例。
- **`input_handler.cpp`/`main.cpp` 整合層接線缺回歸測試**——native 環境不編譯 `src/`，要測需要抽 coordinator 層，屬更大架構決策。已寫入 §8 殘餘風險 ⑥。
- **`handleButtons()` 的 swallow 是索引順序、不是時間順序**——`j<i` 且同輪被 debounce 跳過的按鍵仍有窄縫未覆蓋。已寫入 §8 殘餘風險 ⑦，2026-08-30 最終 review 二次獨立確認，建議與 ⑥ 的 coordinator 層重構一併評估。
- **Training 案件週期值跨 modal 邊界仍靠 ambient global**——目前唯一入口，實際風險為零；完整修法等第二個使用情境出現再評估。
- **`isLowBatteryStartConfirmActive()` 判斷式重複 5 處呼叫點**——新寫入 §8 殘餘風險 ⑧，理由同上。

### 3-A7. Task 12–14 前置調查與範圍裁決（2026-08-30，等 Task 11 收工後再 dispatch，先讀這節）

趁 Task 11 卡在 codex 額度等待期間做的前置調查，**沒有實際開發**，純確認計畫是否還跟現況程式碼一致（比照 Task 11 dispatch 前抓到 VENT/Training 缺口的做法）。

**Task 12（系統設定選單新增「電池資訊」第 5 項）：驗證通過，計畫可直接照做。**
現況程式碼與計畫假設完全一致，沒有漂移：
- `SETTINGS_CURSOR_DEVICE_NAME 0` ~ `SETTINGS_CURSOR_VENT_VOL 3`（`firmware/lib/ui_settings/ui_settings.h:17-20`）
- `SETTINGS_ITEM4_Y 150`（`firmware/lib/ui_settings/ui_settings.cpp:43`）
- `SETTINGS_MENU_COUNT` 仍在 `firmware/src/input_handler.cpp:13`，值為 4
- `input_handler.cpp:758-771` 的 `BTN_PRIMARY` 分派對游標值 4（未來的 `SETTINGS_CURSOR_BATTERY_INFO`）目前沒有任何分支處理（falls through 到 `default: break;`），這正是計畫把「加選單項」（Task 12）與「加畫面/按鍵行為」（Task 13）拆開兩個 task 的原因——Task 12 做完後第 5 項會顯示但按主鍵沒反應，屬預期中的過渡狀態，Task 13 才會接上。

**Task 13（電池資訊畫面）：沒發現明顯缺口。** 依賴 Task 12 的游標常數與既有的 `settingsEditorMode` 旗標 pattern、DisplaySnapshot 五步驟（Task 7–11 已操練多次，pattern 成熟）。

**Task 14（原題：裝置資訊畫面接上真實電池資料）——⚠️ 已從本計畫移出，改併入 Impl-Phase G。**

計畫 Step 1 寫「搜尋裝置資訊畫面中『電池』與『充電狀態』兩列目前的資料來源」，前提是**這個畫面已經存在**（寫死字串或留白）。已查證**完全不成立**：
- `grep -rn "裝置資訊" firmware/src/ firmware/lib/` 零筆結果。
- `main.cpp` 的畫面分派只有 `GLOBAL_SETTINGS_PLACEHOLDER` → `drawSettingsMenu()`，沒有任何「裝置資訊」子畫面的分派分支。
- 「裝置資訊」目前只存在於兩個地方：`docs/demo/index.html`（網頁 mockup）與 SoT V1 spec `docs/EMS_DoseSync_Pro_Prototype_V1.md §19.7`（文字描述，6 欄：名稱／型號／序號／韌體／電池／充電狀態）。

**根本原因**：對照 §19.1 完整設定選單清單（9 項：裝置名稱／螢幕亮度／系統音量／通氣音量／**電池資訊**／App 連線設定／Type-C 連線／**裝置資訊**／韌體版本），「電池資訊」（Task 12 加的第 5 項）跟「裝置資訊」（清單第 8 項）是**兩個不同畫面**。Phase H 這份計畫只規劃了前者的完整實作（Task 12/13），後者連骨架都不在 Phase H 範圍內——Task 14 卻假設它已經存在，只差接資料。

進一步查證發現：**Impl-Phase G 原計畫**（`docs/pm-dev-spec.md §四`）本來就含「韌體版本 read-only」一項，同樣**從未落地成 UI**（只有內部常數 `SYNC_FW_VERSION` 供 BLE 同步協定用）。也就是說「裝置資訊」的韌體欄位其實是 Phase G 自己欠的債，不是憑空冒出的新範圍。

**裁決**（使用者 2026-08-30）：**擴大 Impl-Phase G 範圍**，把完整的「裝置資訊」畫面（名稱／型號／序號／韌體／電池／充電狀態）連同本 Task 14 原本要做的電池／充電狀態接線，一次併入 Phase G 做掉——不是新開一個 Phase，也不是留在 Phase H。已更新：
- `docs/pm-dev-spec.md §四 Phase G` — 範圍描述改成完整裝置資訊畫面
- `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §6/§9` — 移出電池顯示 spec 範圍
- `docs/superpowers/plans/2026-08-22-phase-h-battery-display.md` — Task 14 段落改成移出說明，本計畫**到 Task 13 為止結束**（不再有 14）

Phase G 目前沒有獨立的 spec/plan 檔，只有 `pm-dev-spec.md` 的高層描述 + git commit 記錄。日後要 dispatch 這塊時，需要先幫 Phase G 補一份 spec/plan（比照 Phase H 的做法），本 Task 14 的原始 Step 1-5（已保留在計畫檔的 git 歷史裡）可以當作「電池／充電狀態接線」那部分的起點參考，但名稱／型號／序號／韌體四個新欄位需要另外設計（序號、型號等的資料來源目前也未定案）。

### 3-A8. Task 12 完成收工（2026-08-31）——系統設定選單新增「電池資訊」第 5 項

**現況**：Task 12 程式碼與測試已完成 **4 輪 fix**，SDD scoped re-review 與本 repo 的
codex Tier 3 六面向 gate 皆已通過，**正式收工**。commit 演進：`edfaead`（初版）→
`6177bb9`（fix round 1，amend）→ `b3ca654`（fix round 2，amend）→ `a2d01f2`（fix
round 3，amend）→ **`e6bf46d`（fix round 4，amend，目前 HEAD）**。30/30 focused
test、620/621 全套（唯一 ERRORED 仍是既有的 `test_storage_hw`，與 Phase H 無關），
ESP32-S3 編譯 SUCCESS，Flash 71.7%（全程未變）。

**收尾驗證流程**（回應上一版 handover 留的三步驟交接）：

1. ✅ 對 `a2d01f2` 跑 fix round 3 的 SDD scoped re-review（`b3ca654..a2d01f2`）——
   6 項 findings 全 ADDRESSED，無新破壞。
2. ✅ 對 `a2d01f2` 重跑一次 codex Tier 3（額度已於 2026-08-31 03:00 重置，`engine=codex`
   成功跑滿 6/6 面向）——這是本 task 第一次對「真正最終狀態」跑通完整 codex Tier 3
   （round 1/2 的 codex Tier 3 跑在中間 amend hash `6177bb9`/`b3ca654` 上，round 3
   因額度用盡改走 agent 引擎替代）。結果 1 CRITICAL + 13 IMPORTANT + 2 MINOR，逐條
   比對前三輪 findings 後：CRITICAL 與 8 條 IMPORTANT 是已裁決 park 的舊案第 4 次
   重現（詳見下方 fix round 4 摘要），1 條 IMPORTANT 是新發現但裁決 park（real but
   not load-bearing），真正該修的 5 條進了 fix round 4。
3. ✅ fix round 4（依 SDD skill「round ≥4 換新 implementer + 更強模型」規則，改派
   opus）修完 5 條小缺口，scoped re-review（`a2d01f2..e6bf46d`）5 項全 ADDRESSED、
   無新破壞、無誤觸 parked 項目。
4. ✅ 確認 repo 目前無 active pending-review marker（機械閘門本來就沒鎖，
   `clear-pending-review.ts` 無需執行）。

**接續 dispatch Task 13**（電池資訊畫面本體）——`superpowers:subagent-driven-development`
會從 SDD ledger 自動接續（Task 12 已寫 `complete` 記號）。

**四輪 fix 摘要**（完整 ruling 見本機 SDD ledger
`.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md`，本節只留跨機器
需要的結論）：

- **Fix round 1**（回應 SDD task reviewer + 第一次 codex Tier 3）：修正 8 項，包含
  一個**真正的 bug**——電池資訊列（Y=190）與恢復預設確認對話框文字（Y=180）只差
  10px 會視覺重疊，這是 Task 12 新引入的問題（改用 STEP 04.01 內的 `continue` guard
  在確認框顯示時跳過該列繪製，不搬動座標）；以及一次**後來被 round 3 推翻的誤判**
  ——當時把電池資訊併入既有 `kSettingsAdjustableItems[]` 查表迴圈時，順手把選取態
  文字色從白改黑，意圖修「選取時白底白字看不見」，controller 核准時沒核對實際渲染
  幾何（見下方 fix round 3）。
- **Fix round 2**（回應第二次 codex Tier 3，對象是 round 1 自己新增的程式碼）：修正
  1 個 CRITICAL（`wrapSettingsCursor()` 的 `count` 參數由呼叫端傳入且無防護，改成
  內部直接讀 `SETTINGS_MENU_COUNT`，消除呼叫端傳錯值的風險面）+ 11 項 STEP/註解/
  Magic Number 規則補完。
- **Fix round 3**（改跑 agent 引擎驗證，`code-reviewer` 面向抓到）：**推翻 round 1
  的顏色修法**。經幾何計算證實：反白框只有 `SETTINGS_CURSOR_WIDTH`×`HEIGHT`＝
  80×20px，而 `ems_zh_24` 是 24px vlw 字型、`SETTINGS_FONT_SIZE=2` 意味實際渲染的
  4 字標籤約 192×48px——反白框只蓋到約 17% 面積。螢幕底色是黑（`main.cpp:483,490`
  `fillScreen(0x0000)`）。round 1 之前：選取時白字，83% 面積白字黑底可見、17% 白字
  白底看不見。round 1 之後：選取時全部改黑字，83% 面積變黑字黑底看不見，比原本
  更糟——**是對已驗收 Phase G 範圍（螢幕亮度／系統音量／通氣音量）的視覺倒退**，
  不是修好。implementer 在 fix round 3 自行核對過 `fillScreen`／`setTextSize` 兩個
  前提屬實才動手，沒有照單全收 findings 檔。修法：撤銷回無條件白字（回到 round 1
  之前的 83% 可見狀態，不完美但不是倒退），移除 `SETTINGS_COLOR_HIGHLIGHT_TEXT`
  常數，拿掉兩個因此失去意義的顏色斷言，doc comment 改寫成誠實描述現況；另加
  `static_assert(SETTINGS_TABLE_ITEM_COUNT + 1 == SETTINGS_MENU_COUNT, ...)`
  （抄自 `input_handler.cpp:1501` 既有同型寫法，round 4 把裸數字 `1` 再抽成具名常數
  `SETTINGS_NON_TABLE_ITEM_COUNT`，見下方）鎖住兩個手動維護常數的不變量、
  訂正一處寫錯的 STEP 引用、補 checklist 缺項、補一個 guard 範圍精確性測試。

- **Fix round 4**（2026-08-31，回應 codex Tier 3 對真正最終狀態 `a2d01f2` 的首次
  真實重跑）：這是本 task 第一次對 amend 後的最終 commit 內容跑通完整 codex Tier 3
  （round 1/2 的 codex Tier 3 都跑在中間 amend hash 上，round 3 因額度用盡改走 agent
  引擎）。結果 1 CRITICAL + 13 IMPORTANT + 2 MINOR，逐條比對前三輪 findings 檔後：
  CRITICAL（cursor 4「電池資訊」可選取但 `BTN_PRIMARY` 無分支、按下無反應）是第 4 次
  重申既有裁決——round 1 就已明文 park 為刻意的 Task 12/13 範圍切分；8 條 IMPORTANT
  是已 park 過的舊案重現（測試缺 STEP/@return——grep 驗證全檔 18 個測試函式 0 個有
  STEP，非本 task 特有的不一致；Y=190 裸數字——round 2 已 park 同款；
  `run_all_tests()` 缺 STEP 編號——grep 驗證整個函式 0 STEP，同一慣例缺口；
  `input_handler.cpp` 整合測試缺口——即本節下方殘餘風險第 3 條；VLW glyph metadata
  測試缺口——round 2 已 park 同款；`settings_menu_item_t` 缺 action-kind 欄位——
  round 1/2/3 已 park 同款）；1 條 IMPORTANT 是新發現但裁決 park（`wrapSettingsCursor()`
  的 `delta` 參數是裸 `int8_t`，型別沒鎖住只能 ±1 的不變量，但兩個生產呼叫點目前都
  只傳具名常數、無外部輸入能觸發，改成 `enum class` 會動到公開函式簽章與全部呼叫點，
  跟已 park 的 action-kind 欄位同量級，併入下方殘餘風險第 4 條，不修）。真正新增且
  值得修的 5 條進了 fix round 4：`ui_settings.cpp:109` 一個檔案層級 `#define` 的
  註解誤用 STEP 標籤格式（STEP 只能標在函式內）改寫成一般散文註解；上述
  `static_assert` 的裸數字 `1` 抽成具名常數 `SETTINGS_NON_TABLE_ITEM_COUNT`；
  `ui_settings.cpp:94` 的 doc comment 原本統稱「四項高亮問題皆非本 task 新增」，但
  電池資訊列確實是本次新增，改寫成區分既有三項與新增列；`test_main.cpp` 兩處測試
  comment 誇大宣稱（Y=190 斷言證明「無其他列被誤觸發高亮」其實不成立、
  `wrapSettingsCursor()` 單元測試宣稱「等同測到生產路徑」但 `input_handler.cpp`
  不進 native build）改寫為誠實描述涵蓋範圍。純 comment/常數層級改動，無邏輯變更，
  static_assert 語意變更做過 sabotage 驗證（暫改常數值→編譯期報錯→改回→diff 位元組
  相同）。30/30 focused、620/621 全套、ESP32 SUCCESS Flash 71.7% 皆與 round 3 後
  一致。scoped re-review（`a2d01f2..e6bf46d`）確認 5 項全 ADDRESSED、無新破壞、
  且未誤觸任何 PARKED/MINOR 項目。commit amend `a2d01f2` → `e6bf46d`。

**本輪新增的殘餘風險**（併入 §8 清單，此處先記錄）：

- **選單反白框（80×20px）遠小於實際渲染文字（約 192×48px，只蓋 ~17%），選取時
  一律有小塊區域文字與背景同色看不清楚**——這是**全部 5 個選單項目共通**的既有
  問題（round 3 撤銷顏色修法後，電池資訊/亮度/系統音量/通氣音量/裝置名稱都是同一
  款「白字白底」現象，不再是「裝置名稱獨有」），根因是 `ui_settings.cpp` 用小方框
  局部反白 + `setCursor`+`print`（baseline-relative，見 `ui_screens.cpp:36-38` 自己
  的既有註解），而非本專案已驗證過的正確做法：`ui_screens.cpp` 的 `drawMainMenu()`
  （約 29-49 行）用**整列** `fillRect(0, y, SCREEN_W, ROW_H, COLOR_TEXT_PRIMARY)` +
  `setTextDatum(top_left)` + `drawString`。正確修法是把 `ui_settings.cpp` 的整個
  反白機制改成比照 `drawMainMenu()`，但牽動 `_settings_text_fn`／
  `_settings_fill_rect_fn`（`main.cpp`）共用層與全部 5 個項目的繪製，超出「加一個
  選單項目」範圍，park 為獨立未來 task。**上機驗收清單應新增「選取狀態下選單文字
  是否可讀」這個明確檢查項**（不只是「文字有沒有出現」）——round 1 的顏色 bug 正是
  native mock 測不出視覺可讀性（只能斷言顏色數值，測不出相對背景是否看得見）的
  直接案例，見 §6 `verify-the-observer`。
- **`settings_menu_item_t` 沒有欄位區分「可調值」vs「導覽」語意**——這個行為只能靠
  `input_handler.cpp` 另外用 cursor 數值範圍判斷，選單資料與按鍵分派因此是兩份真相
  來源。codex 兩輪都提出改用 `enum class` + discriminated kind 的建議，因牽動全
  firmware 所有 `SETTINGS_CURSOR_*` 使用點與 snapshot 序列化邊界，判定為 Phase G
  全域型別設計決策，非「加一個選單項」範圍，維持 park。
- **測試對「電池資訊」字串的字型字集完整性只驗證到 mock display 收到字串，沒有解析
  實際 VLW glyph metadata**——這是這整個測試檔案（含所有既有測試）共通的驗證方法，
  不是 Task 12 特有的缺口；本專案目前用的替代驗證法是手動解析已 commit 的 git blob
  （見下方字型驗證段落），建自動化 VLW 解析測試是獨立的測試基礎建設投資，超出
  「加一個選單項」的任務規模，park 為未來候選項目。
- **`wrapSettingsCursor()` 的 `delta` 參數是裸 `int8_t`，型別沒鎖住「只能 ±1」這個
  不變量**（round 4 codex `types` 面向新發現）——誤傳超出 ±1 的合法 `int8_t`（如
  -128）會讓運算產生負餘數，下一行 `uint8_t` 強制轉型會把它變成 253~255，直接違反
  函式宣告的回傳範圍。裁決 real but not load-bearing：兩個生產呼叫點
  （`input_handler.cpp`）目前都只傳具名常數 `SETTINGS_CURSOR_DELTA_UP`/
  `SETTINGS_CURSOR_DELTA_DOWN`，無外部輸入能觸發；改成 `enum class Direction`
  會動到共用函式簽章與全部 5 個呼叫點（2 生產 + 3 測試），跟上面已 park 的
  action-kind 欄位屬同一類「跟本 task 範圍不成比例」的架構層決策，併入同一個未來
  候選項目一併評估。

**字型驗證**：本次新標籤「電池資訊」新增 3 個字（池／資／訊，非原報告初判的 2 個——
「資」原本只出現在 `main.cpp:512` 的 `//` 註解裡，regen 腳本剝除註解後掃描字面值，
「資」其實從未真正進過字型來源；implementer 在 fix round 1 自行查出並訂正），字型
`.vlw`／header 於 Task 12 初次提交時已重生並驗證零缺字，round 1–4 未再變動字型檔。

### 3-A9. Task 13 完成收工（2026-08-31）——電池資訊畫面本體（Phase H 最後一個 task）

**commits**：`e6bf46d..5cffdae`（1 輪 fix）。**經 1 輪 fix、1 個 CRITICAL**。
86/86 focused test、623/624 全套 native（唯一 ERRORED 仍是既有的 `test_storage_hw`），
ESP32-S3 編譯 SUCCESS，Flash 72.4%。

**Dispatch 前 pre-flight 查核抓到計畫本身兩個缺陷**（比照 Task 11/12 既有慣例）：

1. 計畫寫的 flag bit `SNAP_FLAG_SETTINGS_BATTERY_INFO = 0x00080000` 撞到 Task 10 已用掉的
   `SNAP_FLAG_LOW_BATTERY_NOTICE`，改用 `0x00200000`（下一個可用值）。
2. 計畫 Step 3 給的 `presentFrame(); return;` 早退寫法會建立第二個 `presentFrame()` 呼叫點、
   跳過 STEP 04 的電量圖示繪製與 W9 儲存失敗覆蓋層——正是 Task 8 才清乾淨的同型問題。改成
   併入既有 `if (settingsEditorMode) {...} else {...}` 鏈成為第三個分支，不早退。

兩項修正已在 dispatch 前寫進 implementer context（不改 brief 本身），implementer 正確套用，
且額外找到並修掉一個計畫沒提到的真 bug：長按「恢復預設」確認框的觸發 guard 原本沒排除
`settingsBatteryInfoMode`，會在電池資訊畫面時意外跳出確認框。

**Fix round 1 的 CRITICAL**：`g_battery_millivolts`（電壓）沒有同步進 `DisplaySnapshot`，
電壓單獨變化不會觸發重繪——**這是同型 bug 第 5 次命中**（`feedback_display_snapshot_field_sync`）。
SDD task reviewer 沒抓到這條（scope 是核對 brief，而電壓顯示不在 brief 明文要求範圍），
codex Tier 3 靠 `code-review`/`silent-failure`/`tests` 三個角度各自獨立命中，與 Task 7
SDD-vs-codex 分歧模式相同，採信 codex。修法：`batteryMillivolts` 加入
`DisplaySnapshot`/`DisplaySnapshotInputs`，經 `captureSnapshot()`/`captureDisplaySnapshot()`
映射，補兩條回歸測試並 sabotage 驗證過。

**同輪修掉的其他 6 項**：`is_battery_absent()` reuse（`/simplify` 與 codex `code-review`
兩管道獨立確認同一問題）、3 處過期/誇大 comment、3 處新分支的 STEP 編號補完、
`MILLIVOLTS_PER_VOLT` 具名常數、測試變數用途註解、3 個 Y 座標常數改衍生
（`/simplify` 發現）。

**Park 的 6 項**（皆引用既有先例，未重新討論）：
- `ems_display_snapshot.h:202` 的 if-brace／不可變性——Task 7 §3-A + Task 11 confirmatory
  review 對同檔同款式已裁決兩次。
- `main.cpp` 的 `in.xxx = ...` 不可變性——Task 7 §3-A「POD 逐欄位賦值慣例」同款先例。
- `SettingsViewMode` enum 化建議——codex `types`、`/simplify` 的 simplification 與 altitude
  三個獨立管道收斂到同一結論（`settingsEditorMode`/`settingsRestoreConfirm`/
  `settingsBatteryInfoMode` 該併成一個 enum），但比照 Task 9/12 對同類建議的既有裁決，
  超出「加一個畫面」範圍，park 為新殘餘風險（見 §8 ⑪）。
- `input_handler.cpp`／`drawBatteryInfo()` 缺測試——既有殘餘風險 ⑥ 的重現（`src/` 不進
  native build），非新缺口。
- `ChargeState` 預設值遮蔽疑慮——查證 `g_battery_charge_state` 全 repo 僅 2 處寫入且皆
  型別安全，無 cast-from-raw 路徑能產生非法值，屬 Task 9「不可達的 guard 是死碼＋誤導
  註解」同款情境，不加防禦碼。

**Scoped re-review 留下 2 個 Minor**（依 SDD 規則不進 fix loop，deferred 給未來
whole-branch review 觸接）：`main.cpp:1164` 新的 `// STEP 01（Task 13）` 跟函式本身真正的
STEP 01 撞號、且同層 sibling 分支都沒有 STEP 標籤（應改標 STEP 03.xx 延伸既有序列）；
report 自報「57 cases」但實際檔案 56 個測試函式，off-by-one 純屬文字誤植非程式缺陷。

**收工程序**：本 repo 的 pending-review marker 追蹤 amend 前的 `a8aaf24`（codex Tier 3
已 6/6 面向跑滿，`stopBlockCounts` 達 `MAX_STOP_BLOCKS=3`），CRITICAL 已於 fix round 1
修完並通過 scoped re-review，符合解鎖前置條件。Blast radius：本 session 未配置
codebase-memory-mcp，標記「impact 未取得」跳過。已執行
`clear-pending-review.ts --aspects-done=6` 解鎖。

**Phase H 全部 13 個 task 至此完成。** 下一步是 SDD 流程的最終 whole-branch review
（`superpowers:requesting-code-review`，最強模型）與 `superpowers:finishing-a-development-branch`
——**皆尚未執行**，branch 仍未推送。

### 3-A10. Whole-branch review 完成（2026-09-01）——Phase H 收工前最後一道關卡

對 `7fdf1ee..5cffdae`（43 commits，4 個 wave、13 個 task 全範圍）跑了 SDD 流程最後
一道 `superpowers:requesting-code-review`，獨立重跑 native test + 韌體編譯並逐一核對
DisplaySnapshot 欄位同步、§13.16/§20.3 互斥性、`apply_fuel_reading()` 失敗路徑等跨 task
一致性，**無新 Critical / Important**（§8 殘餘風險 ⑥⑦⑧⑨⑩⑪ 逐條核對後維持既有 park
裁決，不重複列）。找到 2 個 Minor，兩者皆已當場修掉，不需要開新 fix round：

1. `main.cpp:1164` 的 `// STEP 01（Task 13）` 標籤跟同層 `globalState` 分派鏈所有
   sibling 分支（`GLOBAL_OHCA`/`GLOBAL_VENT`/`GLOBAL_TRAINING_SETUP` 等）都沒有 STEP
   標籤，不只是撞號、是唯一一處局部加了標籤——改回散文註解，不補編號。
2. Task 10 §8 殘餘風險 ⑤ 排定的收尾清掃（`grep -rn "fix round\|原本誤寫" firmware/`）
   whole-branch review 才真正跑了一次：52 處命中中抽樣挑出 4 處純「描述前一版寫錯什麼」
   的考古式描述（`fuel_gauge_logic.h:230,263,471`、`main.cpp:653`），違反本 repo
   COMMENT-ACCURACY 判準（只留「為什麼是這個設計」，不留「前一版寫錯什麼」），已清除，
   保留原本的設計理由文字。

修復 commit：`e45d855`（純註解變更）。修復後重跑驗證：native test **623/624**、
韌體編譯 **SUCCESS，Flash 72.4%**——與 whole-branch review 實測數字一致，無邏輯影響。

**Reviewer verdict：Ready to merge / finish — With fixes（兩個 Minor，已處理，不阻塞）。**
真正的風險不在程式碼品質，而在「整個 Phase H 從未上機驗證」（§3-B，11 條，reviewer
重申這是殘餘風險第一位，沒有新資訊要補充）。

**下一步**：`superpowers:finishing-a-development-branch`——決定 branch 如何整合
（merge/PR/push）。

### 3-B. Task 6 的上機驗收剩兩項（需要硬體）

程式碼寫完了，但計畫 Task 6 的 **Step 6.3 / 6.4 尚未執行**。步驟在計畫檔裡，重點如下。

### Step 6.3 判讀表有方向相反的兩條，缺一不可

這是 review 時才補進去的——**原本的 checklist 根本沒排這兩條**，就算上機步驟全做完，這條不變式依然驗不到。

| 檢查 | 通過條件 | 沒過代表什麼 |
|---|---|---|
| 電壓合理 | `mV` 落在 3000~4200，且與電表差距 < 50mV | 換算或 I2C 組裝有問題 |
| 百分比合理 | `%` 落在 0~100 且與電壓大致對得上 | 同上 |
| 趨勢欄位 | 開機前 30 秒為 `Unknown`（窗未滿），之後才轉 Idle/Charging | 趨勢窗邏輯或取樣間隔有問題 |
| ~~失敗不清警示~~ | ✅ **2026-08-22 已通過**（見下方實測 log） | — |
| **失敗不造警示** | 處於 `low=0` 時拔 SDA → 後續每筆 log 的 `low=` **必須維持 0** | 失敗分支把 `reading.percent`（無效時為 0）餵進了 latch |

> ⚠️ **這階段沒有畫面可看。** 電量圖示是 Task 9、閃爍是 Task 11，Task 6 唯一的觀測窗口是
> serial log 的 `[FUEL] ... low=N` 欄位。判讀看 `low=` 數字，不要期待 TFT 上有東西。

純邏輯層已由 `test_apply_fuel_reading_failure_preserves_latched_low_battery` 與 `test_apply_fuel_reading_failure_does_not_fabricate_low_battery` 兩條鎖住；上機這兩條是確認整合層走同一條路。拔 SDA 是最容易製造「暫時性讀取失敗」的手法，插回去應看到讀數恢復。

> 🔍 **參數順序寫反會長什麼樣**：若 `read()` 誤寫成 `make_reading(raw_soc, raw_vcell)`，以實機值代入（VCELL raw `0xC030`、SOC raw `0x366A`）→ `is_plausible_soc_raw(0xC030)` 高位元組 192 > 110 → 每次讀取都 invalid。症狀是「probe 成功、log 印出 detected，但畫面持續不顯示電量」，**不是顯示錯誤數值**。看到這個先查參數順序，不要先懷疑硬體。

### ✅ 已通過：失敗不清警示（2026-08-22 首次上機）

`low=1` 狀態下拔 SDA 再插回，實測 log：

```
2% 3605mV state=3 low=1   ← Idle(3)、低電量鎖存中
Wire error                ← 拔 SDA
read failed ...           ← 失敗 log 印出
Wire error                ← 第二次失敗，未再印 log（was_invalid 節流生效）
2% 3605mV state=0 low=1   ← 插回，state 回 Unknown(0)（trend.reset() 生效）
2% 3605mV state=0 low=1   ← 窗未滿
2% 3603mV state=3 low=1   ← 第 3 筆窗滿回 Idle，精確對上 TREND_WINDOW_SAMPLES=3
```

四個機制同時確認：`low=` 全程維持 1（不清鎖存）、失敗 log 節流、`trend.reset()` 生效、
失敗期間不輸出假讀數。

### Task 10 上機驗收（需要硬體，fix round 1 後，尚未執行）

§13.16 低電量一次性提示的程式碼已寫完、native test 與韌體編譯皆通過，但全程無實體裝置，
以下項目待有硬體時執行。觸發判斷已改由 `tryStartLowBatteryNotice()` 在 `loop()` 每輪檢查
（2026-08-23 fix round 1 A2），不再掛在 `pollBattery()` 的 10 秒節流內：

| 檢查 | 通過條件 | 沒過代表什麼 |
|---|---|---|
| 提示出現 | 進 OHCA 案件後首次跨進低電量，畫面中央出現一次帶不透明背景 panel 的「低電量」／「建議接上行動電源」兩行文字 | flag 未傳到 `presentFrame()`，或 `tryStartLowBatteryNotice()` 判斷條件錯誤 |
| **提示兩行文字無缺字** | 「低電量」與「建議接上行動電源」11 個字完整顯示，不出現 ▯ 或空白缺字方塊 | `ems_zh_24_vlw` 字集缺字（Task 10 fix round 3 之前缺「低議行源」四字，G1 CRITICAL 已重生字型修復；若又出現需重跑 `bash scripts/regen_vlw.sh` 並比照 fix round 3 report 驗證 glyph 表） |
| **panel 不與其他文字重疊** | panel 背景確實蓋住 OHCA 倒數大字（y≈100）／通氣大數字（y=136），兩層文字不互相覆寫、可讀 | `drawLowBatteryNotice()` 的 `display.textWidth()`/`fontHeight()` 動態量測算錯尺寸，或 panel 畫在文字之後而非之前（fix round 1 A1） |
| **開案當下立即提示** | 已在主選單等非適用情境跨進低電量（latch 已 pending）後才進 OHCA/VENT，提示應在進入後的下一個 loop 週期內立即出現，不需等待 10 秒電量輪詢 | 觸發邏輯仍掛在 `pollBattery()` 節流內，或 `is_low_battery_notice_context()` 短路求值失效（fix round 1 A2） |
| 提示消失 | 顯示約 3 秒後自動消失，且過程**不發聲**（依 SoT §13.16 明文） | `LOW_BATTERY_NOTICE_MS` 計時或 `is_low_battery_notice_visible()` 判斷有誤；若有聲響，代表誤接了蜂鳴器邏輯 |
| 消失後續閃爍 | 提示消失後電量圖示依 Task 9/11 節奏持續閃爍，不受提示影響 | `presentFrame()` STEP 01/02 順序或 snapshot flag 互相干擾 |
| per-boot 一次 | 提示顯示過一次後，離開案件再重新進 OHCA，同一次開機不再重複顯示 | `entry_pending_` 或 `g_low_battery_notice.active` 被意外重置（欄位已於 fix round 3 G4 收斂為單一 `ems::LowBatteryNoticeState` struct） |
| Training 同樣觸發 | Training 開案後同樣會在首次跨進低電量時顯示提示 | `is_low_battery_notice_context()` 判斷漏掉 Training（`globalState` 應同為 `GLOBAL_OHCA`） |
| VENT_PRE 不觸發 | 通氣尚未真正開始（VENT_PRE 準備畫面）時跨進低電量不顯示提示，真正開始通氣後才顯示 | `is_low_battery_notice_context()` 未排除 `ventPreShown`（fix round 1 A3） |
| **主選單五個標籤零缺字** | 「OHCA 案件」「6 秒通氣節奏」「訓練模式」「歷史紀錄」「系統設定」五個標籤逐字顯示完整，不出現 ▯ 缺字方塊 | `regen_vlw.sh` 的 `SRC_FILES` allowlist 又漏掉某個含 UI 字串的檔案——本輪曾漏掉 `.h`，`MAIN_MENU_LABELS`（定義在 `app_globals.h`）的「設」字被字型重生 union 重算掃出字集，實機「系統設定」顯示成「系統▯定」 |
| **設定選單各頁與恢復預設確認框文字零缺字** | 系統設定選單各子頁面（含「螢幕亮度」「系統音量」「通氣音量」「裝置名稱」「請連接 App 設定裝置名稱」）、恢復預設確認框「是否恢復預設設定？」、預設裝置名「未命名」逐字顯示完整，不出現缺字方塊 | `lib/ui_settings/ui_settings.cpp` 與 `lib/ems_settings/ems_settings.h` 這兩個檔案的中文不受 `regen_vlw.sh` 的 `src/` glob 涵蓋，過去長期缺「恢、預、命」三字；fix round 3 G1 那輪的字型重生修復的是另一批字（提示文字的「低議行源」），這兩個 lib/ 檔案當時未被納入掃描，缺字延續到本輪才補上；若又出現代表 `SRC_FILES` 手動列舉的 lib/ 條目被移除 |

觸發手法與上方 Task 6 相同：暫時把 `LowBatteryLatch::LOW_BATTERY_ENTER_PERCENT`
（`fuel_gauge_logic.h`，目前 20）改成高於當前實測電量的值（例如 90）後燒錄驗證，
驗完**務必改回 20 並重新燒錄**，不可留在量產設定上。

### ⏳ 待做：失敗不造警示（與靜置擷取一起做）

需要 `low=0` 的前置狀態，但目前電池只有 2%，永遠 `low=1`。做法：把
`firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h:196` 的 `LOW_BATTERY_ENTER_PERCENT` 從 `20`
**暫時**改成 `1`（讓 2% 不算低電量），燒錄後應看到 `low=0`，再拔一次 SDA，確認後續 `low=`
維持 0。**測完改回 20，不要 commit。**

### Step 6.4 順便收兩筆資料

機器既然接上了就一起收，省一次拆裝。這兩筆分別是兩個 park 決定的前提：

1. **VERSION 暫存器實際值** —— 決定要不要加 MAX17043 型號校驗。目前 probe 只驗 ACK 不驗值，因為合理值域需要實機觀測才能定，**猜錯會把好晶片判成不在線**。記進 `docs/power-module-purchase.md §10.8`。
2. **靜置 10 分鐘的 VCELL 抖動幅度** —— 校正趨勢死區 `±3mV` 與 `PLAUSIBLE_SOC_WHOLE_MAX = 110`，兩者都是推導初值不是量測值。

### Task 11 上機驗收（需要硬體，五輪 fix 後，尚未執行）

§20.3 低電量開案確認框的程式碼已寫完、native test 與韌體編譯皆通過，但全程無實體裝置，
以下項目待有硬體時執行。觸發手法比照 Task 6/10：暫時把 `LOW_BATTERY_ENTER_PERCENT`
（`fuel_gauge_logic.h`，目前 20）改成高於當前實測電量的值後燒錄驗證，**驗完務必改回 20**。

| 檢查 | 通過條件 | 沒過代表什麼 |
|---|---|---|
| 三入口皆攔截 | 低電量狀態下分別從主選單按 OHCA、按「6 秒通氣節奏」（VENT_PRE 按主鍵才觸發，不是一進 VENT_PRE 就攔）、Training 設定完週期後按確認，三者都先跳出「低電量／建議接上行動電源／是否開始？」確認框，不直接進案 | `is_blocking modal` 判斷或某入口的 `requestLowBatteryStartConfirm()` 呼叫漏接 |
| 選「否」不進案 | 按 BTN_BACK 取消 → 回到觸發前的畫面（主選單／VENT_PRE／Training 設定），**不建立任何案件**，`eventCount`/`caseStartMs` 等案件狀態不變 | `LowBatteryConfirmDecision` 的 `proceed` 分支判斷錯誤 |
| 選「是」正常進案 | 按 BTN_PRIMARY 確認 → 依原入口啟動對應案件（OHCA/VENT/Training），行為與電量正常時直接進案一致 | `startOhcaCase()`/`startVentActive()`/`startTrainingCase()` 呼叫點錯接 |
| 確認框顯示期間按鍵穿透 | 確認框開啟／關閉的**同一輪** `loop()` 內，若同時有其他按鍵處於按下或防抖窗內，該按鍵**不得**被底層畫面（進案後的 OHCA 畫面、或關閉後的主選單）接收到；使用者需要重新按一次才會生效 | `handleButtons()` 的吞鍵邏輯未涵蓋到；已知窄縫見 §8 殘餘風險 ⑦（`j<i` 索引），機率低但若復現先查是否命中該縫 |
| 提示文字零缺字 | 「低電量」「建議接上行動電源」「是否開始？」逐字顯示完整，不出現 ▯ | 字型字集缺字，重跑 `regen_vlw.sh` 並驗字集（見 §8 第 ② 條） |
| 與 §13.16 提示不重疊 | 選「是」進案後，不會立即再跳出一次 Task 10 的低電量一次性提示（兩者互斥，見 §3-A5「Task 10 與 Task 11 的互動」的守衛推導） | `is_low_battery_notice_context()` 的守衛判斷失效，兩個 `consume_first_entry()` 呼叫點搶到同一次事件 |

---

## 4. 已完成的 task

| Task | commit | 產出 | fix 輪數 | review |
|---|---|---|---|---|
| 1 | `a3f2715` | `vcell_raw_to_mv()` / `soc_raw_to_percent()` | 2 | clean |
| 2 | `00e3be3` | `ChargeState` / `ChargeTrendTracker` | 2（含一次設計變更） | clean |
| 3 | `f465b66` | `LowBatteryLatch` | 1 | clean |
| 4 | `f6a9e07` | `FuelReading` / `FuelGaugeBackend` / `NullFuelGauge` | 1 | clean |
| 5 | `75e3fb0` | `Max17043Backend`、兩個合理性 predicate、`make_reading()` | 3 | clean |
| 6 | `917d072` | main.cpp 掛載、`pollBattery()`、`to_display_percent()`、`apply_fuel_reading()` | 1 | clean |
| 7 | `94bc3fb` | `DisplaySnapshot` 電池欄位、`SNAP_FLAG_BATTERY_LOW_BLINK`、`snapshotsEqualExceptCountdown()` | 1（2026-08-23 補跑） | clean |
| 8 | `3333235` | `presentFrame()` 統一重繪出口（15 處收斂）、snapshot 電池填值、`compute_low_battery_blink_on()` | 1 | clean |
| 9 | `5634b52` | 四格電量圖示繪製、幾何閃電、`should_draw_battery_icon()`、`battery_segments_for_percent()` | 2 | clean |
| 10 | `dc4aaf1` | §13.16 提示：`LowBatteryNoticeState`、`low_battery_notice_tick()`、`is_low_battery_notice_visible/context()`、`SNAP_FLAG_LOW_BATTERY_NOTICE`、`drawLowBatteryNotice()`、字型 282→325 glyph | **6**（4 CRITICAL） | clean |
| 11 | `58efa0e` | §20.3 確認框：三入口（OHCA/VENT/Training）低電量攔截、`LowBatteryStartTarget`/`LowBatteryConfirmTarget`、`try_request_low_battery_start_confirm()`、`isBlockingModalActive()`、`handleButtons()` 同輪多鍵吞鍵 | **5**（3 CRITICAL）+ 最終 confirmatory review 補 3 處 STEP 註解 | clean |
| 12 | `e6bf46d` | 設定選單第 5 項「電池資訊」：`SETTINGS_CURSOR_BATTERY_INFO`、`kSettingsAdjustableItems[]` 查表化、`wrapSettingsCursor()`、`SETTINGS_MENU_COUNT` static_assert、字型 325→328 glyph | **4**（2 CRITICAL，round 1 顏色修法 round 3 撤銷） | clean |
| 13 | `5cffdae` | 電池資訊畫面本體：`drawBatteryInfo()`、`settingsBatteryInfoMode`、`SNAP_FLAG_SETTINGS_BATTERY_INFO`、`batteryMillivolts` 加入 DisplaySnapshot | **1**（1 CRITICAL：電壓未同步進 snapshot，同型 bug 第 5 次） | clean |

> Task 1–6 的 hash 不受 2026-08-23 那次 rebase 影響（rebase 基底是 `c180cb3`，都在它之前）。
>
> Task 10 跑了 6 輪 fix，每輪把修正折回同一個 feat commit，所以中間版本的 hash 全部脫離分支。
> 它的 4 個 CRITICAL 見 §3-A4——共同點是**都只有讀程式碼或讀二進位資產才發現得了**，
> 編譯、native test、上機（無硬體）三層觀測全部涵蓋不到。

每個 task 都跑 SDD task review + 專案 Tier 3 六面向（共 7 個 reviewer），全部 findings 已處理或 parked。
Task 1–6 的 pending-review 閘門正常解鎖（`--aspects-done=6`）；Task 7 當下用 `--force` 放行、
2026-08-23 補跑六面向還清（見 §3-A）。

**Task 5 曾一度欠著 review 債**（兩個 implementer 相繼撞上 session limit），額度恢復後已補跑完整 7 個 seat 並跑完 fix round 3 與 scoped re-review。

---

## 5. 這個 wave 抓到的實質問題

按嚴重度與可傳承性排序。**其中三條打的是 controller 自己寫的東西**。

### ① 我們一路防錯了方向（Task 6，Critical）

整個 wave 都在防「讀取失敗把低電量警示清掉」。**沒有人想過失敗憑空製造警示。**

若失敗分支比照成功分支寫成 `latch.update(reading.percent)`，傳進去的不是哨兵 255，而是無效 `FuelReading` 的建構子預設值 **0**——而 `0 <= SOC_PERCENT_MAX` **會穿透 `LowBatteryLatch` 的 guard**。電量正常時，一次暫時性 I2C 失敗就被判成跌破 20% 門檻，救護人員看到不存在的低電量警告。

原本那句註解還引用 `test_offline_sentinel_does_not_clear_low_battery` 當佐證，**歸屬是反的**——該測試直接呼叫 `latch.update(255)`，證明的是 latch 內部 guard 擋得住 >100，不是「呼叫端不呼叫才安全」。

**解法是 repo 自己早就有的**：`firmware/lib/ems_rtc_glue/` 檔頭明講「把內嵌在 main.cpp 的膠合邏輯抽成純函式」並配 `test_rtc_integration` 測三分支。燃料計沒照做，決策邏輯全寫死在 `pollBattery()` 裡，而 `[env:native]` 明確 `build_src_filter = -<*>`——**0 覆蓋率**。已抽成 `ems::apply_fuel_reading()`，五條測試鎖住兩個方向。

### ② 檔名撞名打壞既有測試（Task 4，Critical）

新建的 `null_backend.h` 與既有 `ems_rtc/null_backend.h` basename 相同，native env 的 `-I$PROJECT_LIB_DIR` 加 LDF 解析順序讓 `test_rtc` / `test_rtc_integration` 撈錯檔。已改名 `null_fuel_gauge.h`。

**七個 reviewer 沒有一個抓到**——它們全都只跑聚焦套件（是綠的），沒人跑完整套件、也沒人跟 BASE 對照。

### ③ 兩個編譯環境的 C++ 標準不同（Task 4 埋、Task 5 才爆）

`native` 是 `-std=gnu++17`，`esp32-s3-devkitc-1` 是 `-std=gnu++11`（Arduino core 2.0.17 預設）。Task 4 review 要求給 `FuelReading` 加 default member initializer——C++11 下帶 NSDMI 的 struct 不是 aggregate，`{false, 0, 0}` 編不過。**那段期間主韌體環境其實一直是壞的**，被下面 ④ 的空驗證整個蓋住。已改用帶預設值的 `constexpr explicit` constructor。

### ④ 「pio run SUCCESS」對新 lib 是空驗證（Task 5）

`firmware/src/` 在 Task 6 之前沒有任何檔案引用 `ems_fuel_gauge`，LDF 因此不把它拉進 build——那個 SUCCESS 完全沒編到該 lib。

更深一層：即使用「注入暫時 include 強制編譯」也只證明**編得過**。有 reviewer 用 `xtensa-esp32s3-elf-nm firmware.elf | grep Max17043` 證明**連結器從未把 `.o` 拉進最終韌體**。Task 6 已還清這筆債（`nm` 對真實韌體撈得到所有符號），計畫 Step 6.2 也把連結驗證列為硬性項目。

### ⑤ 合理性檢查蓋錯欄位（Task 5，Critical）

`mv` 有上界檢查、`percent` 完全沒有——而 spec 明訂低電量門檻**用 SOC 判定**。`soc_raw_to_percent()` 把 ≥100 夾到 100（那是為充飽超衝設計的，不是異常偵測），所以垃圾 `raw_soc` 會變成「100% 滿電、valid=true」，把已鎖存的低電量警示直接解除且不重新提醒。

### ⑥ 死區設計雙向失效（Task 2，三個面向獨立判 Critical）

原設計用 SOC(%) 做趨勢推導。`push()` 吃 `uint8_t`，delta 恆為整數，死區 `0.5` 等價於「delta ≥ 1」：靜置時 SOC 在 53/54/55 抖動 → 反覆誤判；USB-C 500mA 充電 30 秒窗只變 0.42% → 量化後 delta=0 → 判 Idle（漏報）。**訊號比雜訊還小，調參數救不了。** 已改用 VCELL(mV)，30 秒窗變化 5mV = 4 個 LSB。spec §3.4.1 有完整推導。

### ⑦ 測試存在但守不住關鍵路徑（每個 task 都出現過）

- Task 2：6 個測試全用單調數列，環形緩衝索引 off-by-one 仍全綠
- Task 3：邊界只測單向；`entry_pending_` 的清除線刪掉，26 個測試照樣全綠
- Task 5：SOC 上界鬆動一格，41 個測試全綠
- Task 6：**哨兵常數本身從 255 改成 90**（落在合法電量範圍內），46 個測試全綠——已加 `static_assert(BATTERY_PERCENT_ABSENT > SOC_PERCENT_MAX)` 編譯期擋死
- Task 7：把 `batteryPercent` 與 `batteryChargeState` **互換寫入**，45 個測試全綠。根因是那批測試只斷言「兩份 snapshot 不相等」，欄位寫到哪裡完全不驗——而「不相等」在互換後依然成立。已補具名欄位值斷言（2026-08-23）

### ⑧ 同一份狀態有第二套比較邏輯（Task 7，Critical，2026-08-23 補跑 review 才抓到）

`DisplaySnapshot` 的去重有兩處：`updateDisplay()` STEP 01 的整包 `memcmp`，以及 partial update
判斷裡**另一份手寫的 10 欄位清單**。新增欄位時第一處自動涵蓋、第二處必須手動同步——而
「四處一起改」的 checklist 只列到 `captureSnapshot`，第二處從來不在清單上。

這也是 `EXTRACT-SHARED-HELPER` 的 `guard-placement` 條目講的同一件事：**把正確性押在
「每個維護者都記得同步另一處」上，遲早會輸**。已改成整包 `memcmp` 消除該邊界情況。

---

## 6. `verify-the-observer` 在這個 wave 命中六次

形狀完全相同：**觀測手段涵蓋不到宣稱要衡量的對象，而輸出看起來完全正常。**

1. Task 4 實作者把自己打壞的兩個測試套件當成「既有失敗」（沒跟 BASE 對照）
2. `pio run SUCCESS` 對新 lib 是空驗證（LDF 沒把 lib 拉進 build）
3. 連 controller 的「強制編譯」也只證明編得過——reviewer 用 `nm` 才證明連不進
4. controller 自己跑 mutation 得到「41/41 全綠」而誤判測試無鑑別力——實為 worktree 檢出的 HEAD 尚未含該測試
5. 實作者把 `git rebase` 的 stdout 當成分支已移動，未用 `git rev-parse HEAD` 核對，產生 dangling object 卻回報成已生效
6. controller 的 Task 7 鑑別力檢查只驗了「拿掉欄位拷貝 → 變紅」一個方向，**沒驗「拷貝到錯欄位」**——後者在原版測試下 45/45 全綠（2026-08-23 補跑 review 時實測確認）

**共同解法**：下結論前先確認觀測手段真的碰得到要衡量的東西——對照 BASE、看編譯清單、看連結符號、看測試數、看 `rev-parse`，不要只看 exit code 或摘要行。

---

## 7. 給 W2（Task 7–14）的硬性約定

1. **UI 端一律用 `ems::is_battery_absent()`，不要各自比對 255。** types 面向舉的具體 bug：四格圖示常見寫法 `frame = percent / 25`，255 會算出 `frame = 10`，直接 index 到圖示陣列外。這個 helper 已經加好了，`fuel_gauge_logic.h` 裡。
2. **不要用 `is_present()` 決定畫不畫圖示。** 它是 `begin()` 當下的 probe 快取，硬體事後物理斷線時會繼續回 `true`（`read()` 仍正確回 invalid，未違反契約）。要判斷畫不畫，看 `g_battery_percent` 是不是哨兵。
3. **新增 UI state 必須同步加進 `DisplaySnapshot`。** 這個 repo 已連踩 5 次（historyCursor / summarySubmenuCursor / endCheckCursor / Phase G 設定選單 / Phase H 電池欄位），spec §4.4 有 5 步驟 checklist。
   > ⚠️ **那份 checklist 本身有漏**：它只涵蓋 `DisplaySnapshot` struct、`DisplaySnapshotInputs`、
   > `captureSnapshot()` 拷貝、flag bit 四處，**沒有涵蓋 `updateDisplay()` 的 partial update 判斷**——
   > Phase H 就是漏在第五處（見 §5 ⑧）。該處已於 2026-08-23 改成整包 `memcmp`，往後新增欄位
   > **不需要**再手動同步它；但如果有人日後又在別處寫第二套逐欄位比較，這個坑會原樣回來。
4. **`FuelReading` 是 `constexpr explicit`**，不能用 `{a, b, c}` brace-init。

---

## 8. 殘餘風險（未解決，刻意記錄）

### Task 10 收工時的前五名（2026-08-24，依「會不會咬到人」排序）

**① 整個 Phase H 一次都沒在實機跑過。** §3-B 累積 11 條上機驗收全部未執行，所有結論都建立在
native test 加靜態推理上。最可能出事的三處：panel 幾何（`textWidth()`/`fontHeight()` 的實際
回傳值只有真機才知道，目前只驗到「代數上必然包住」）、3 秒計時的實際觀感、以及 panel 只有
約 232×88 而 OHCA 大時間約 290×96，**四周會露出巨大數字的殘片**（與既有 `drawConfirmDialog()`
同類但更明顯）。**這一條比其他四條加起來都重。**

**② 字型字集沒有任何自動守門，同一類 bug 已經咬過兩次。** Task 10 fix round 3 缺「低議行源」
（提示文字）、round 5/6 缺「恢預命」（設定 UI）。兩次都是靠人工審查在上機前攔下，不是靠工具。
現在的防線只有 `regen_vlw.sh` 裡的一段註解——而註解 enforce 不了任何事。**下一個在新檔案加
中文標籤的人會靜默出 ▯，要到上機才看得到。**

驗法（新增任何會上 TFT 的中文字串後都該跑，對**已 commit 的 blob**）：

```bash
python3 - <<'EOF'
import struct, subprocess, re
blob = subprocess.run(['git','show','HEAD:firmware/data/fonts/ems_zh_24.vlw'],capture_output=True).stdout
n = struct.unpack('>i', blob[:4])[0]
cps = {struct.unpack('>i', blob[24+i*28:28+i*28])[0] for i in range(n)}
for s in ["系統設定", "低電量", "你新增的字串"]:          # ← 填要驗的 UI 字串
    print(s, [c for c in s if ord(c) > 0x2000 and ord(c) not in cps] or "零缺字")
h = subprocess.run(['git','show','HEAD:firmware/src/ems_zh_24_vlw.h'],capture_output=True).stdout.decode('utf-8','replace')
print("_len 一致:", str(len(blob)) == re.search(r'_len\s*=\s*(\d+)', h).group(1))
EOF
```

該補而未補的是「字型流程 fixture-based 回歸測試」（在 `.h` 與 `.cpp` 各放唯一中文字，跑掃描
邏輯確認兩者都被納入、生成物被排除）。park 的理由是它屬新增能力而非修缺陷，且 Task 10 已跑六輪。

**③ `consume_first_entry()` 仍是 public 的一次性消費 API。** 守衛已移進 lib、寫回缺口也補了，
但「公開 API 被誤呼叫一次就不可逆丟事件、且無錯誤訊號」這個底層危險還在。目前生產路徑有
兩個入口：`low_battery_notice_tick()`（§13.16 執行中一次性提示）與
`ems::try_request_low_battery_start_confirm()`（§20.3 低電量開案確認框核心進場判斷，
`fuel_gauge_logic.h/.cpp`；2026-08-30 Task 11 fix round 3 P 取代 round 2 的
`apply_low_battery_start_confirm_request()`——舊版無條件執行、「是否真的低電量」的判斷
仍留在呼叫端；新版把這個守衛也收進函式本身，唯一權威是 `latch.is_low()`，回傳 `bool`
告知呼叫端有沒有攔截，native test 鎖住「低電量時攔截並正確設定/消費」「非低電量時完全
不攔截（不動 target_out、不消費 latch）」兩種情境）。`main.cpp` 的
`requestLowBatteryStartConfirm()` 現在只是薄 wrapper（同樣回傳 `bool`），且參數型別是不含
`None` 的 `LowBatteryStartTarget`（round 2 CRITICAL 修正），編譯期排除「誤傳未顯示狀態」
這個曾經存在的漏洞。兩個入口各自把「是否該消費」的守衛完全收斂在函式內部——前者的守衛
是情境判斷（`is_low_battery_notice_context()`），後者的守衛是 `latch.is_low()`。但仍然
**沒有編譯期強制**兩個入口以外不會有第三方直接呼叫 `consume_first_entry()`。若未來有人
為別的功能直接呼叫它，Phase H 的提示會靜默失效。改 private + friend 需要動 Task 3 已完成
的 9 個測試，因此 park。

**④ `LowBatteryNoticeState` 的「整包替換」只是約定。** 欄位仍 public，任何人都能寫
`g_low_battery_notice.active = true;` 繞過。註解已如實說明這一點（不再宣稱「型別上保證」）。
與 ③ 同源：**兩者都是用約定與註解取代型別保證，而這條線上已經證明過註解攔不住人。**

**⑤ 程式碼裡累積了 review 過程的考古式註解。** `（fix round N XX 裁決）`、`原本誤寫成…`
這類描述**已不存在的舊程式碼**。判準：「為什麼是這個設計」留（那是知識），「前一版寫錯什麼」
刪（那屬於 commit message）。排定在 Phase H 收尾一次掃：
`grep -rn "fix round\|原本誤寫" firmware/`。不會咬到功能，純維護性。

**⑥ `input_handler.cpp`／`main.cpp` 的整合層完全沒有回歸測試覆蓋。** §20.3 低電量開案
確認框從按鍵到啟動流程的完整接線（`onShortPress()` 三個入口攔截、`handleButtons()` 的
modal 吞按鍵邏輯）、`g_lowBatteryConfirmTarget` → `DisplaySnapshotInputs.
lowBatteryStartConfirmShown` 的映射、`updateDisplay()` 確認框早退分支相對於各
`globalState` 分派的優先序——這些全部活在 `input_handler.cpp`／`main.cpp`，而 native
測試環境的 `build_src_filter` 不編譯 `src/`（只編譯 `lib/` 底下抽出的純邏輯）。Task 11
的三輪 fix round（round 1 M、round 2 tests finding、round 3 這次）codex 每輪重跑都會
點名這個缺口——這不是巧合，是這個架構下的結構性限制：只要修改留在 `src/`，就不會有
native test 鎖住。要徹底解決需要把三個啟動入口與 modal controller 抽成可注入的
coordinator 層（依賴注入 `globalState`/`g_battery_low` 等全域，讓決策邏輯能在 native
環境重放），屬於比 Task 11 任何一輪修正都大的架構決策，目前 park——依此 repo 對
`main.cpp` wiring 缺口的既定做法，留給 code review + 上機驗收把關（見 §3-B 累積清單）。

**⑦ `handleButtons()` 的吞鍵是「以掃描索引為界」，不是「以時間為界」的完整鎖定。**
（承接 ⑥，同一個整合層、同一段程式碼的另一個面向。）modal 開啟或關閉時，吞鍵迴圈只掃
`j = i+1 .. BTN_COUNT-1`——也就是**本輪尚未掃到**的按鍵。索引小於轉換鍵（`j < i`）的按鍵
在轉換發生前就已經掃過，若它這輪剛好因 `now - lastPressMs[j] < DEBOUNCE_MS` 被 `continue`
跳過（`continue` 刻意不更新 `lastBtnState[j]`，把邊緣留到下一輪重驗），那個邊緣會在下一輪
被當成新事件處理，穿透到 modal 轉換後的新畫面。

已知未覆蓋，**刻意 park**。觸發需要三個條件同時成立：轉換由較高索引的按鍵觸發（實務上是
`BTN_BACK`=5 取消 modal）、**且**某個較低索引的按鍵同輪落在防抖窗內、**且**該按鍵確實有
待處理的邊緣。機率遠低於 round 3/4 修掉的那兩條（「兩鍵幾乎同時放開」是救護人員在緊急
狀況下完全做得出來的動作，這條不是）。

**若之後要徹底解決，方向不是繼續擴大吞鍵迴圈的索引範圍**（那還會多出「`j < i` 已經分派過
事件、清掉它的追蹤是否正確」這個新問題），而是在 modal 轉換當下另開一個獨立的「輸入鎖定
至 X ms 後」計時器，讓 `handleButtons()` 在函式開頭就跳過整輪處理，完全不依賴掃描順序。
那需要一個新的全域計時器狀態與到期判斷，屬於比 Task 11 任何一輪修正都大的架構決策——
與 ⑥ 的 coordinator 層重構是同一個量級，建議兩者一起評估，不要單獨為這條動手。

相關：round 5 已補上吞鍵迴圈同步 `lastPressMs[j] = now`，關掉的是另一個更窄的縫（被吞按鍵
之後若發生機械彈跳，舊時戳會讓彈跳邊緣繞過防抖）。那條已修，這條沒修，兩者不要混淆。

> 🔁 **2026-08-30 最終 confirmatory review 二次獨立確認**：codex `code-review` 面向不知情
> 於本項已 park 的情況下，重新描述了完全同一個缺口（以 `BTN_BACK`=5 取消 modal 為具體案例），
> 佐證了「觸發需要三條件同時成立」的判斷路徑是真實存在、可被獨立觀測到的，不是理論假設。
> 裁決不變：仍 park，理由同上。

**⑧ 低電量確認框「是否顯示中」的判斷式重複散落在 5 個呼叫點。** `g_lowBatteryConfirmTarget
!= ems::LowBatteryConfirmTarget::None`（或其鏡像 `isBlockingModalActive()` 內的同一子運算式）
分別出現在 `input_handler.cpp:143`（modal 聚合判斷）、`:391`（`onShortPress()` 分派）、
`:1376`（`onLongPress()` guard）、`main.cpp:945`（`captureSnapshot` 映射）、`:1057`
（`updateDisplay()` 早退路由）。依 `EXTRACT-SHARED-HELPER` 判準，同一概念判斷出現在 2+
呼叫點就該抽共用 helper——2026-08-30 最終 confirmatory review 的 `code-review` 面向新抓到，
非前五輪已知項目。

**刻意 park，不在收工當下動手**：這 5 個呼叫點橫跨 `input_handler.cpp`／`main.cpp` 兩個檔案，
且正是 Task 11 五輪 fix 反覆出包的同一塊 modal 狀態表面（guard-placement 類 CRITICAL 出現
過 3 次）。剛結束 round 5、韌體已編譯驗證通過的當下，為了收斂一個純語法重複而立刻再碰這塊
程式碼，regression 風險高於現狀的維護成本——5 處都是同一句子面等值判斷，語意不會分歧
（不像過去幾次「兩處各自演化出不同守衛」的教訓）。與 ⑥ 的 coordinator 層重構屬同一量級
決策，建議兩者一起評估，不要單獨為這條動手。

**⑨ 設定選單反白框覆蓋不足，全部 5 個項目共通。**（Task 12，2026-08-31）
`ui_settings.cpp` 的游標高亮用小方框局部反白（`SETTINGS_CURSOR_WIDTH×HEIGHT` =
80×20px）+ `setCursor`+`print`，但 `ems_zh_24`（24px vlw 字型）在 `SETTINGS_FONT_SIZE=2`
下實際渲染的 4 字標籤約 192×48px，高亮框只蓋到約 17% 面積。螢幕底色黑
（`main.cpp:483,490` `fillScreen(0x0000)`），選取時該項目仍有約 83% 面積是「白字黑底」
（可見）、17% 是「白字白底」（看不見）——裝置名稱／螢幕亮度／系統音量／通氣音量／電池資訊
五項全部同款。**這條在 Task 12 round 1 被誤修過一次**（改選取時黑字，經幾何驗證證實讓
83% 可見面積反而變成「黑字黑底」看不見，比原本更糟，round 3 已撤銷回無條件白字，詳見
§3-A8）。正確長期修法：比照 `ui_screens.cpp` 的 `drawMainMenu()`（約 29-49 行），改成
**整列** `fillRect(0, y, SCREEN_W, ROW_H, COLOR_TEXT_PRIMARY)` + `setTextDatum(top_left)`
+ `drawString`（不是 baseline-relative 的 `setCursor`+`print`）。牽動共用的
`_settings_text_fn`/`_settings_fill_rect_fn`（`main.cpp`）與全部 5 個項目的繪製，超出
單一 task 範圍，park 為獨立未來 task。**上機驗收清單應新增「選取狀態下選單文字是否可讀」
這個明確檢查項**（不只是「文字有沒有出現」）——這是 native mock 測不出視覺可讀性（只能斷言
顏色數值，測不出相對背景是否看得見）的直接案例，見 §6 `verify-the-observer`。

**⑩ 設定選單資料結構的型別設計債：`settings_menu_item_t` 缺 action-kind 欄位、
`wrapSettingsCursor()` 的 delta 參數缺型別約束。**（Task 12，2026-08-31）兩個獨立但
同源的缺口，皆屬 Phase G 全域型別設計決策層級，非「加一個選單項」範圍：
- `settings_menu_item_t` 沒有欄位區分「可調值」vs「導覽」語意——電池資訊（導覽項）能否
  被編輯，目前完全靠 `input_handler.cpp` 用 cursor 數值範圍（1~3）推斷，選單資料與按鍵
  分派因此是兩份真相來源，日後重排或插入項目可能讓導覽項誤入編輯模式且編譯器無法檢出。
  codex 在 Task 12 三輪（round 1/2/4）都提出改用 `enum class` + discriminated kind 的
  建議，因牽動全 firmware 所有 `SETTINGS_CURSOR_*` 使用點與 snapshot 序列化邊界，維持 park。
- `wrapSettingsCursor()` 的 `delta` 參數是裸 `int8_t`，但公式只對 ±1 成立——誤傳 -128
  等合法 `int8_t` 會讓運算產生負餘數，下一行 `uint8_t` 強制轉型會把它變成 253~255，
  直接違反函式宣告的回傳範圍。兩個生產呼叫點目前都只傳具名常數
  （`SETTINGS_CURSOR_DELTA_UP`/`SETTINGS_CURSOR_DELTA_DOWN`），無外部輸入能觸發，
  real but not load-bearing，park。
  
  改成 `enum class` 會動到公開函式簽章與全部 5 個呼叫點（2 生產 + 3 測試），建議與上一條
  一起評估、一次重構。

**⑪ 設定畫面子模式仍是平行布林值，不是 discriminated 狀態。**（Task 13，2026-08-31）
`settingsEditorMode`／`settingsRestoreConfirm`／`settingsBatteryInfoMode` 三個全域 bool
互斥表達「設定畫面目前顯示哪個子畫面」，但型別上允許同時為真——
`updateDisplay()`（`main.cpp`）與 `onShortPress()`（`input_handler.cpp`）分別用各自的
if/else-if 鏈判斷優先序，兩處優先序目前手動維持一致，沒有編譯器保證。三個獨立管道
（codex Tier 3 `types` 面向、`/simplify` 的 simplification 與 altitude 兩個角度）在
Task 13 review 時各自收斂到同一個結論：改用單一 `enum class SettingsViewMode { Menu,
Editor, RestoreConfirm, BatteryInfo }`，輸入處理／繪製／snapshot 都依同一個 discriminant
切換。這是與 ⑩ 同源的架構債（型別無法表達互斥不變量），但範圍更大——牽動三個既有全域
（不只是新增的那一個），比照 Task 9/12 對同類建議的既有裁決，超出單一 task 範圍，park。
與 ⑩ 建議一起評估、一次重構（`settingsCursor` 已用 `enum` + `wrapSettingsCursor()`
妥善抽象，「選單項目」與「子畫面模式」是同一個 UI 層兩個對稱的問題，值得一起解）。

### W1 讀取層的既有項目

- **`PLAUSIBLE_SOC_WHOLE_MAX = 110` 與趨勢死區 `±3mV` 都是推導初值**，需要 Step 6.4 的實測資料校正。
- **低電量區精度未驗**：只在 3.84V 中段比對過電表，而接近 3.0V 正是低電量警告最需要準的區間。W3（Task 10–11）動工前建議補驗。
- **失敗原因無法細分**：`read()` 的五條失敗路徑都收斂成同一個 `valid=false`，沒有計數器。目前緩解是 `pollBattery()` 在狀態翻轉時印一行概括性 log。若現場除錯真的需要細分，再補診斷列舉（比照 `ems_rtc` 的 `SetResult` 先例）。
- **合理性檢查只擋超界值，擋不掉界內腐化**：真實 SOC 15 因單一位元翻轉變成 79，兩者都 ≤110 會通過，而 79 ≥ 25 足以誤清已鎖存的低電量警示。
- **`src_fuel_gauge_check/main.cpp` 重複實作了一份 I2C 讀取邏輯**，且測試裡的實機黃金值（VCELL `0xC030` / SOC `0x366A`）來自那支舊工具、**從未經過 `Max17043Backend`**。使用者已裁示**不改**（改完需接實機重驗），且 code-reviewer 證實 `src_rtc_demo` 同樣不依賴 production lib，屬既有慣例。這是**已接受的現況，不是待辦**。
- **I2C 時脈 100kHz 靠預設值成立**：全 repo 無 `setClock()` 呼叫，`Wire.begin(SDA, SCL)` 的 ESP32 Arduino 預設值剛好是 100kHz。該行屬 Dev-Phase 3 RTC 既有程式碼，沒有任何一行把 spec 的約束釘住。
- **放電測試的取捨**：要跑真實放電曲線就得拔 USB，一拔就沒 serial。需要另建一個把讀數寫進 LittleFS 的環境，事後 dump。

---

## 9. Ruling

ledger 內共 **48 條 `Ruling:`** 與 **14 個 `parked`**，完整上下文（含每條的代價評估）在 `.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md`。**每一條都可以推翻**——它們是無人可問時代為決定的事項，不是不可變的規則。

較可能影響後續 task 的幾條：

| 裁定 | 錯了的代價 |
|---|---|
| 趨勢輸入改用 VCELL(mV)、死區 ±3mV（已獲使用者確認） | 需實測校正 |
| 低電量門檻仍用 SOC——門檻要絕對電量、趨勢要變化方向，不為型別統一而全改 | 兩種訊號尺度並存 |
| 合理性上界只設上界不設下界——0mV / 0% 可能是「真的沒電」的合法讀數 | 界內腐化擋不掉（見 §8） |
| `255` 契約防呆用 return 而非 clamp——clamp 等同把「不在線」偽裝成「電量 100%」 | — |
| 不加 MAX17043 型號校驗——合理 VERSION 值域需實機資料，猜錯會把好晶片判成不在線 | probe 仍只驗 ACK |
| `make_reading()` 不加 tagged type 區分兩個 `uint16_t` 參數——單一呼叫點、變數名一致，成本換不到對應風險下降 | 參數傳反無編譯期信號（症狀見 §3） |
| `STEP 06.6` 小數位插入不改編碼——本檔既有慣例 8+ 處先例，統一改是全檔範圍決策 | 該規則在本檔持續被局部豁免 |
| 三個電池全域維持裸 `extern`——`g_rtc` / `events[]` 等既有一貫做法，非本次退化 | 「單一寫入者」約定無型別強制 |
