# Impl-Phase H 電量顯示 — 交接文件

- **建立**：2026-08-22
- **狀態**：W1 進行中（Task 3/14 完成，Task 4 尚未開工）
- **branch**：`feat/phase-g-system-settings`

---

## 1. 三十秒看懂現況

MAX17043 燃料計硬體驗收通過，主韌體的讀取層做到一半。已完成純邏輯層的三個元件（換算、趨勢推導、低電量閂鎖），全部有 native test。尚未接上主韌體，畫面上還看不到任何電量資訊。

| 項目 | 狀態 |
|---|---|
| 硬體 | ✅ 驗收通過（I2C `0x36`、3.844V vs 電表 3.88V） |
| spec | ✅ `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md` |
| 實作計畫 | ✅ `docs/superpowers/plans/2026-08-22-phase-h-battery-display.md`（14 task） |
| W1 讀取層 | 🔄 Task 1–3 完成，Task 4–6 待做 |
| W2 顯示層 | ⬜ Task 7–9 |
| W3 低電量行為 | ⬜ Task 10–11 |
| W4 電池資訊畫面 | ⬜ Task 12–14 |

`firmware/lib/ems_fuel_gauge/` 目前 30 個 native test 全綠。全套 531 cases / 530 通過（唯一未過的 `test_storage_hw` 是**既有**的編譯錯誤，已用 worktree checkout 到本工作起點驗證過，與本 wave 無關）。

---

## 2. 如何接手

```bash
# 1. 讀 spec 與計畫（計畫是 spec 的論證，兩者一起讀）
docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md
docs/superpowers/plans/2026-08-22-phase-h-battery-display.md

# 2. 讀 SDD ledger（含全部 19 條 ruling 與每個 task 的完整經過）
.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md

# 3. 確認目前狀態
cd firmware && pio test -e native -f test_fuel_gauge_logic   # 應為 30/30
git log --oneline 7fdf1ee..HEAD                              # 本 wave 的 11 個 commit
```

> ⚠️ `.superpowers/` 是 git-ignored 的本機工作區。換機器就沒有了——本文件是它的持久化摘要，但 ledger 內的逐輪細節（每個 review 面向的原始 findings）只在本機。

**續跑方式**：用 `superpowers:subagent-driven-development`，它會偵測 ledger 並從第一個沒有 `complete` 記號的 task 接續。下一個是 **Task 4**（backend 介面與 Null 降級實作）。

---

## 3. 已完成的 task

| Task | commit | 產出 | fix 輪數 | review |
|---|---|---|---|---|
| 1 | `a3f2715` | `vcell_raw_to_mv()` / `soc_raw_to_percent()` | 2 | clean |
| 2 | `00e3be3` | `ChargeState` / `ChargeTrendTracker` | 2（含一次設計變更） | clean |
| 3 | `f465b66` | `LowBatteryLatch` | 1 | clean |

每個 task 都跑了 SDD spec review + 專案 Tier 3 六面向（共 7 個 reviewer），全部 findings 已處理或 parked，三個 task 的 pending-review 閘門皆已正常解鎖（`--aspects-done=6`，未使用 `--force`）。

### 這三個 task 抓到的實質問題

**① 死區設計雙向失效**（三個面向獨立判 Critical）

原設計用 SOC(%) 做趨勢推導。`push()` 吃 `uint8_t`，delta 恆為整數，死區 `0.5` 等價於「delta ≥ 1」：

- 靜置時 SOC 在 53/54/55 抖動 → 反覆誤判 Charging/Discharging
- USB-C 500mA 充電，30 秒窗只變 0.42% → 量化後 delta=0 → 判 Idle（漏報）

訊號比雜訊還小，調參數救不了。**已改用 VCELL(mV)**，30 秒窗變化 5mV = 4 個 LSB。spec §3.4.1 有完整推導。

**② `255` 哨兵會清除低電量鎖存**（CRITICAL）

spec 定義 `batteryPercent == 255` 為「燃料計不在線」，而解除判斷是 `percent >= 25`——`255 >= 25` 成立。感測器讀取失敗一次，**已觸發的低電量警示就被靜默吃掉**。已加契約防呆（超出 0~100 直接 return，**不可 clamp**——夾成 100 一樣會清掉鎖存）。

**③ 測試存在但守不住關鍵路徑**（三個 task 每次都出現）

- Task 2：6 個測試全用單調數列，環形緩衝索引寫錯（off-by-one）仍全綠
- Task 3：邊界只測單向；`entry_pending_` 的清除線刪掉，26 個測試照樣全綠

**這是本 wave 最該帶進後續 task 的教訓**：寫測試時只想著「證明功能會動」，不會自動涵蓋「功能壞掉時會不會紅」。從 Task 4 起應把 mutation 檢查直接寫進 dispatch，而不是等 review 抓。

---

## 4. 待你決定 / 需要實測的事

| 項目 | 說明 |
|---|---|
| **死區 3mV 需校正** | 這是推導初值不是量測值。下界由訊號決定（500mA 充電 30 秒 5mV），上界由靜置雜訊決定——而**靜置時 VCELL 抖多少沒有數據**。Task 6 主韌體整合後應收一段長時間靜置讀數校正 |
| **低電量區精度未驗** | 只在 3.84V 中段比對過電表，而接近 3.0V 正是低電量警告最需要準的區間。W3（Task 10–11）動工前建議補驗 |
| **放電測試的取捨** | 要跑真實放電曲線就得拔 USB，一拔就沒 serial。需要另建一個把讀數寫進 LittleFS 的環境，事後 dump |

---

## 5. 全部 19 條 ruling

以下是我在無人可問時代為決定的事項，**每條都可以推翻**。詳細理由與代價見 ledger 對應行號。

### 流程層

1. **不另開 worktree** — 已在 feature branch 而非 main，且 8 個 task 需實機燒錄，worktree 隔離對硬體測試無益
2. **subagent 只做機器可驗步驟** — 人眼驗收（TFT 畫面）累積到 wave 邊界交付
3. **`/simplify` 不另跑** — 其涵蓋面向已被 types 與 code-reviewer 實質覆蓋
4. **round 1 不單獨 re-review，與 round 2 合併** — round 2 會改寫 round 1 的產出
5. **一個 Low 不開新 round** — 併入下個 task 的 dispatch（同一檔案）
6. **brief 是計畫的快照，計畫變更後所有已產未派的 brief 都要重產**

### 設計層

7. **趨勢輸入改用 VCELL** — 見上方 ①（已獲使用者確認）
8. **低電量門檻仍用 SOC** — 門檻要絕對電量、趨勢要變化方向，不為型別統一而全改
9. **`255` 契約防呆用 return 而非 clamp** — clamp 等同把「不在線」偽裝成「電量 100%」
10. **合理性上界只設上界不設下界** — 0mV 可能是「真的沒電」的合法讀數
11. **`reset()` 的 log 責任歸呼叫端** — 純邏輯層禁 include `Arduino.h`，log 要求已寫進 Task 6 計畫
12. **垃圾值的不可信判定留給 `FuelReading.valid` 層**（Task 5，已寫進計畫）

### 取捨層（parked，附代價）

13. **raw 與 mV 不加 tag type** — YAGNI，raw 只在 3 行內存活
14. **常數外洩 vs 消除重複，選消除重複** — 兩個 Important 衝突時的取捨
15. **`>>8` / `&0xFF` 不抽具名常數** — 採 lite 的全庫 grep 證據（既有 6 處同寫法）
16. **兩個 `bool` 不改 `enum class State`** — 非法組合目前不可達，是重構非修 bug
17. **`is_low()` 預設 false 不加第三態** — 曝險窗只有一個 main loop tick
18. **Task 1 的極端值只鎖行為不加上界** — 上界屬 Task 5 職責
19. **Task 2/3/4 brief 的預期測試數就地修正** — 計畫記估算、brief 記當下事實

---

## 6. 下一步（Task 4）

`.superpowers/sdd/.../task-4-brief.md` 已就緒（預期測試數已校正）。Task 4 是 `FuelReading` struct + `FuelGaugeBackend` 純虛介面 + `NullFuelGauge`，比照既有 `firmware/lib/ems_rtc/` 的 backend + Null 降級 pattern。

**dispatch 時務必帶上**：

- 這個檔案的重複失效模式（註解與實作不符、測試無鑑別力）
- Task 5 的合理性上界要求已寫進計畫，Task 4 只負責定義 `valid` 語意
- 常數放置慣例已寫進 `fuel_gauge_logic.h` 檔頭，新增符號照著做
