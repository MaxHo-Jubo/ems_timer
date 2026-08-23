# Impl-Phase H 電量顯示 — 交接文件

- **最後更新**：2026-08-23（W2 完成收工）
- **狀態**：W1 完成（Task 1–6，review clean）。**W2 完成**：Task 7（review 已補跑，修掉 1 Critical）、Task 8、Task 9（各 review clean）。**Task 10 已完成 pre-flight 查證但尚未 dispatch**（見 §3-A3）。所有上機驗收累積待硬體；Task 11–14 未開工
- **branch**：`feat/phase-g-system-settings`（未推送）

> 本文件是單一時間線，取代先前三層疊加的版本。裡面所有數字與 commit 都在 2026-08-23 收工時實測過。
>
> ⚠️ **2026-08-23 補跑 Task 7 review 時 rebase 過，Phase H 的 commit hash 全部變了。**
> 舊文件與舊對話紀錄裡的 `df33d97`（Task 7）已不存在，現為 `94bc3fb`。

---

## 1. 三十秒看懂現況

MAX17043 燃料計硬體驗收通過，主韌體每 10 秒輪詢一次寫進四個全域。**W2 顯示層已完成——程式碼上右上角四格電量圖示與低電量閃爍都寫好了，但一次都沒在實機上看過**（本階段全程無硬體，所有上機驗收累積到最後）。W3（低電量提示與開案確認框）與 W4（電池資訊畫面）未開工。

| 項目 | 狀態 |
|---|---|
| 硬體 | ✅ 驗收通過（I2C `0x36`、3.844V vs 電表 3.88V） |
| spec | ✅ `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md` |
| 實作計畫 | ✅ `docs/superpowers/plans/2026-08-22-phase-h-battery-display.md`（14 task） |
| W1 讀取層 | ✅ Task 1–6 review clean；**上機驗收剩兩項待硬體** |
| W2 顯示層 | ✅ 完成：Task 7（`94bc3fb`）、Task 8（`3333235`）、Task 9（`5634b52`）review clean |
| W3 低電量行為 | 🔄 Task 10 pre-flight 查證完成、未 dispatch（見 §3-A3）；Task 11 未開工 |
| W4 電池資訊畫面 | ⬜ Task 12–14 |

**實測數字**：`firmware/lib/ems_fuel_gauge/` 69 個、`test_display_snapshot` 50 個 native test 全綠；全套 579 cases / 578 通過。唯一未過的 `test_storage_hw` 是**既有**編譯錯誤（已用 worktree checkout 到本工作起點驗證過，與 Phase H 無關）。ESP32-S3 韌體編譯 SUCCESS。

---

## 2. 如何接手

```bash
# 1. 讀 spec 與計畫（計畫是 spec 的論證，兩者一起讀）
docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md
docs/superpowers/plans/2026-08-22-phase-h-battery-display.md

# 2. 確認目前狀態
cd firmware && pio test -e native -f test_fuel_gauge_logic   # 應為 69/69
cd firmware && pio test -e native                            # 應為 579/578，唯一 ERRORED = test_storage_hw
cd firmware && pio run -e esp32-s3-devkitc-1                 # 應為 SUCCESS
git log --oneline 7fdf1ee..HEAD                              # Phase H 的全部 commit
```

**續跑方式**：用 `superpowers:subagent-driven-development`。它會偵測 `.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md` 這個 ledger 並從第一個沒有 `complete` 記號的 task 接續。**Task 1–9 全部 complete，下一個是 Task 10**——但 §3-A3 有四條 dispatch 前必須處理的計畫缺陷，先讀完再派工。

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

### 3-A3. Task 9 完成；Task 10 的 pre-flight 已做完但**還沒 dispatch**（2026-08-23 收工點）

**Task 9（`5634b52`）**：四格電量圖示、低電量閃爍、幾何閃電符號。經 2 輪 fix。
新增到 lib 的純函式：`should_draw_battery_icon(percent, low, lowBlinkOn)`、
`battery_segments_for_percent(percent)`，都有 native test。

> ⚠️ Task 9 過程中 controller 抓到一個 Critical：`drawBatteryIcon()` 原本寫成
> `if (!lowBlinkOn) return;`，而 `compute_low_battery_blink_on()` 在非低電量時恆回 false
> ——**電量正常時圖示完全不畫**。根因是 controller 自己 dispatch 的措辭誤導。
> 這個缺陷編譯會過、native 抓不到（`ui_screens.cpp` 不進 native build）、也沒有硬體可上機發現，
> 三層觀測全部涵蓋不到。修法是抽出 `should_draw_battery_icon()` 三分支純函式並加測試。

#### Task 10 接手包：查證結論（不必重查）

- `COLOR_ACCENT_WARN = 0xFDE0`（`app_globals.h:114`）、`SCREEN_H = 240`（`:99`）
- `LowBatteryLatch::consume_first_entry()` 在 `fuel_gauge_logic.h:314`
- **Training 進行中的 `globalState` 也是 `GLOBAL_OHCA`**（`input_handler.cpp:485` 開案時設定，
  模式用 `g_case_mode == CASE_MODE_TRAINING` 區分）。所以計畫寫的
  `in_active_case = (globalState == GLOBAL_OHCA) || (globalState == GLOBAL_VENT)`
  **已經涵蓋 Training**，不需要再加條件；`GLOBAL_TRAINING_SETUP` 是開案前的設定畫面，不該觸發（正確）。
- 下一個可用 flag bit 是 **`0x00080000`**（第 20 個；`SNAP_FLAG_BATTERY_LOW_BLINK = 0x00040000` 是目前最高位）

#### Task 10 接手包：dispatch 前必須處理的四條計畫缺陷

1. 🔴 **提示狀態沒有進 `DisplaySnapshot`——這會是同型 bug 的第 6 次。**
   計畫的 `drawLowBatteryNotice()` 用 `millis() > g_low_battery_notice_until_ms` 在**繪製時**
   判斷是否還在顯示期間，但「提示顯示中」這個狀態不在 snapshot 裡 → `memcmp` 相等 → 不重繪
   → 提示不會出現、3 秒到期也不會消失。
   目前「碰巧」會動的唯一原因是低電量時 `SNAP_FLAG_BATTERY_LOW_BLINK` 每 500ms 翻轉、
   順帶提供了重繪節奏——**這是依賴巧合**，出現／消失時機有最多 500ms 誤差，且 Task 11
   一改閃爍邏輯就會卡住。
   **裁決方向**（與 Task 9 的相位處理一致）：`captureDisplaySnapshot()` 算好
   `lowBatteryNoticeVisible` → `DisplaySnapshotInputs` → 新 flag bit `0x00080000` →
   `presentFrame(snap)` 取 bit 傳給 `drawLowBatteryNotice(bool visible)`，繪製函式不自算 `millis()`。
   spec §4.4 的 5 步驟 checklist 全部要跑。
2. 「是否在顯示期間」是純邏輯（輸入 `until_ms`、`now_ms`），比照 `compute_low_battery_blink_on()`
   抽進 `ems_fuel_gauge` lib 加 native test，否則又是 `ui_screens.cpp` 的 0 覆蓋率。
3. `presentFrame()` 簽名已是 `presentFrame(const DisplaySnapshot& snap)`（Task 9 改的），
   計畫寫的「在 STEP 01 之後、STEP 02 之前插入」STEP 編號已漂移，插入後要整段重排。
4. Step 4 上機驗證降級（無硬體）。計畫提到的「暫時把 `LOW_BATTERY_ENTER_PERCENT` 改成 90
   驗證後改回」與 §3-B Task 6 的做法相同，上機時一併安排。

#### Task 10–14 的 dispatch 都要帶的一條約定

**呼叫 `ems::battery_segments_for_percent()` 前必須先用 `ems::is_battery_absent()` 擋掉哨兵。**
該函式對 255 會回滿格（255 ≥ 75）。Task 9 review 時 codex 要求在函式內加 guard，
但 implementer 論證「呼叫端本來就得先做不在線的早退判斷，加第二個判斷點不消除第一個」，
controller 採納該論證（不可達的 guard 是死碼＋誤導註解）。
**代價已知**：這是把正確性押在呼叫端記憶力上，正是 `guard-placement` 原則反對的模式。
**若 Task 12–14 出現第二個呼叫點，應改採 `enum class BatteryIconState { Absent, Normal, LowOn, LowOff }`**
（見 §9 的 park 條目）。

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

### ⏳ 待做：失敗不造警示（與靜置擷取一起做）

需要 `low=0` 的前置狀態，但目前電池只有 2%，永遠 `low=1`。做法：把
`firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h:196` 的 `LOW_BATTERY_ENTER_PERCENT` 從 `20`
**暫時**改成 `1`（讓 2% 不算低電量），燒錄後應看到 `low=0`，再拔一次 SDA，確認後續 `low=`
維持 0。**測完改回 20，不要 commit。**

### Step 6.4 順便收兩筆資料

機器既然接上了就一起收，省一次拆裝。這兩筆分別是兩個 park 決定的前提：

1. **VERSION 暫存器實際值** —— 決定要不要加 MAX17043 型號校驗。目前 probe 只驗 ACK 不驗值，因為合理值域需要實機觀測才能定，**猜錯會把好晶片判成不在線**。記進 `docs/power-module-purchase.md §10.8`。
2. **靜置 10 分鐘的 VCELL 抖動幅度** —— 校正趨勢死區 `±3mV` 與 `PLAUSIBLE_SOC_WHOLE_MAX = 110`，兩者都是推導初值不是量測值。

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

> Task 1–6 的 hash 不受 2026-08-23 那次 rebase 影響（rebase 基底是 `c180cb3`，都在它之前）。

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
