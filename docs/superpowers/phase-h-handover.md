# Impl-Phase H 電量顯示 — 交接文件

- **最後更新**：2026-08-22
- **狀態**：**W1 讀取層程式碼全數完成**（Task 1–6，review 皆 clean）。Task 6 的上機驗收待硬體；W2–W4（Task 7–14）未開工
- **branch**：`feat/phase-g-system-settings`

> 本文件是單一時間線，取代先前三層疊加的版本。裡面所有數字與 commit 都在 2026-08-22 收工時實測過。

---

## 1. 三十秒看懂現況

MAX17043 燃料計硬體驗收通過，**主韌體已經真的在讀電量了**——開機 probe I2C `0x36`，每 10 秒輪詢一次，結果寫進四個全域供 UI 唯讀。但**畫面上還看不到任何東西**：UI 層（Task 7–14）尚未實作。

| 項目 | 狀態 |
|---|---|
| 硬體 | ✅ 驗收通過（I2C `0x36`、3.844V vs 電表 3.88V） |
| spec | ✅ `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md` |
| 實作計畫 | ✅ `docs/superpowers/plans/2026-08-22-phase-h-battery-display.md`（14 task） |
| W1 讀取層 | ✅ Task 1–6 程式碼完成、review clean；**Task 6 上機驗收待硬體** |
| W2 顯示層 | ⬜ Task 7–9 |
| W3 低電量行為 | ⬜ Task 10–11 |
| W4 電池資訊畫面 | ⬜ Task 12–14 |

**實測數字**：`firmware/lib/ems_fuel_gauge/` 51 個 native test 全綠；全套 552 cases / 551 通過。唯一未過的 `test_storage_hw` 是**既有**編譯錯誤（已用 worktree checkout 到本工作起點驗證過，與 Phase H 無關）。

---

## 2. 如何接手

```bash
# 1. 讀 spec 與計畫（計畫是 spec 的論證，兩者一起讀）
docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md
docs/superpowers/plans/2026-08-22-phase-h-battery-display.md

# 2. 確認目前狀態
cd firmware && pio test -e native -f test_fuel_gauge_logic   # 應為 51/51
cd firmware && pio test -e native                            # 應為 552/551，唯一 ERRORED = test_storage_hw
git log --oneline 7fdf1ee..HEAD                              # Phase H 的全部 commit
```

**續跑方式**：用 `superpowers:subagent-driven-development`。它會偵測 `.superpowers/sdd/2026-08-22-phase-h-battery-display/progress.md` 這個 ledger 並從第一個沒有 `complete` 記號的 task 接續（下一個是 **Task 7**）。

> ⚠️ `.superpowers/` 是 git-ignored 的本機工作區，換機器就沒有了。本文件是它的持久化摘要；ledger 內的逐輪細節（每個 review 面向的原始 findings、48 條 ruling 的完整上下文）只在本機。

---

## 3. ⚠️ 接手第一件事：Task 6 的上機驗收（需要硬體）

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

每個 task 都跑 SDD task review + 專案 Tier 3 六面向（共 7 個 reviewer），全部 findings 已處理或 parked，pending-review 閘門皆正常解鎖（`--aspects-done=6`，**全程未使用 `--force`**）。

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

---

## 6. `verify-the-observer` 在這個 wave 命中五次

形狀完全相同：**觀測手段涵蓋不到宣稱要衡量的對象，而輸出看起來完全正常。**

1. Task 4 實作者把自己打壞的兩個測試套件當成「既有失敗」（沒跟 BASE 對照）
2. `pio run SUCCESS` 對新 lib 是空驗證（LDF 沒把 lib 拉進 build）
3. 連 controller 的「強制編譯」也只證明編得過——reviewer 用 `nm` 才證明連不進
4. controller 自己跑 mutation 得到「41/41 全綠」而誤判測試無鑑別力——實為 worktree 檢出的 HEAD 尚未含該測試
5. 實作者把 `git rebase` 的 stdout 當成分支已移動，未用 `git rev-parse HEAD` 核對，產生 dangling object 卻回報成已生效

**共同解法**：下結論前先確認觀測手段真的碰得到要衡量的東西——對照 BASE、看編譯清單、看連結符號、看測試數、看 `rev-parse`，不要只看 exit code 或摘要行。

---

## 7. 給 W2（Task 7–14）的硬性約定

1. **UI 端一律用 `ems::is_battery_absent()`，不要各自比對 255。** types 面向舉的具體 bug：四格圖示常見寫法 `frame = percent / 25`，255 會算出 `frame = 10`，直接 index 到圖示陣列外。這個 helper 已經加好了，`fuel_gauge_logic.h` 裡。
2. **不要用 `is_present()` 決定畫不畫圖示。** 它是 `begin()` 當下的 probe 快取，硬體事後物理斷線時會繼續回 `true`（`read()` 仍正確回 invalid，未違反契約）。要判斷畫不畫，看 `g_battery_percent` 是不是哨兵。
3. **新增 UI state 必須同步加進 `DisplaySnapshot`。** 這個 repo 已連踩 4 次（historyCursor / summarySubmenuCursor / endCheckCursor / Phase G 設定選單），spec §4.4 有 5 步驟 checklist。
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
