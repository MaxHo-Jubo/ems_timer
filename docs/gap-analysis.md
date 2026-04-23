# PM 規格 vs 現有韌體 Gap Analysis

> 基準：`docs/pm-flow-spec.md` + `docs/pm-dev-spec.md`（PM 目標）
> vs **Phase 2A~2E+3A + Phase 1 單元測試框架**（commit `38ea2cb`，2026-04-22）
> 前次更新：2026-04-22（Phase 2A~2E+3A + Phase 1 單元測試完成後）
> 本次更新：2026-04-23（PM 回覆 6 題開放問題，全數拍板）

---

## 總覽（更新後）

| 層級 | PM 目標 | 現況 | 差距等級 |
|------|---------|------|---------|
| 按鍵配置 | 5 鍵語意化（主鍵 / 上下 / 電源 / 靜音） | 5 鍵（BTN_PRIMARY/UP/DOWN/POWER/RECORD），電源與錄音/靜音鍵為 noop 佔位 | 🟡 中（硬體已縮減至 5 鍵，但電源 / 靜音功能未實作） |
| 狀態機 | IDLE → RUNNING → PAUSE → END | 4 態狀態機 ✅ 完整實作 | 🟢 一致 |
| 任務控制 | 長按主鍵開始 / 結束任務 | 長按主鍵 ≥2000ms 驅動 IDLE→RUNNING / RUNNING→PAUSE / PAUSE→END | 🟢 一致 |
| RUNNING↔PAUSE 切換 | **PM 2026-04-23 拍板：長按 2s 循環；超長按 5s 直接結束任務** | RUNNING→PAUSE 長按 ✅ / PAUSE→RUNNING 目前短按 ❌；RUNNING→END / PAUSE→END 目前用長按 2s（需改 5s） | 🟠 韌體按鍵處理需重構為兩段式長按 |
| 節律提醒 | 6 秒 CPR 節拍（僅通氣模式）+ 4 分鐘給藥提醒（±50ms） | MED 模式 240 秒給藥倒數 ✅；VENT 模式僅記錄事件，**無 6 秒節拍** ❌ | 🟡 中（VENT 節拍待實作，PM 2026-04-23 確認「通氣模式才有」） |
| 資料結構 | event_t（8 欄位 → 9 欄位，PM 2026-04-23 採方案 A 納入 `elapsed_ms`） | EmsEvent（9 欄位，多 `elapsed_ms`、缺 `device_id`） | 🟢 `elapsed_ms` 已對齊（PM 規格追實作）；`device_id` 待 Phase 3 補 |
| 容量 | Ring Buffer 100~500 events | MAX_EVENTS = 100 | 🟢 符合下限 |
| 持久儲存 | SPI Flash 批次寫入（每 5 秒）+ wear leveling | **LittleFS**，任務 END 時整批寫 `/sessions/<epoch>.json` | 🟡 中（用 LittleFS 替代 SPI Flash raw；寫入時機非每 5 秒而是 END 時） |
| BLE 架構 | **兩台 EMS Timer 硬體互聯**（PM 2026-04-23 拍板）— 主機 Central 掃描副機 Peripheral + GATT 4 Service | 裝置為 Peripheral；**無主副機架構**；使用 NUS + JSON 單一 service | 🔴 大（Phase 3 實作主副機互聯） |
| 電源鍵功能 | 短按螢幕亮滅 / 長按關機 | 僅 log 輸出（noop） | 🟡 中（硬體到位後可補） |
| 錄音鍵功能 | **PM 2026-04-23 確認只有錄音鍵，無靜音鍵** — INMP441 錄音用 | BTN_RECORD 佔位（Phase 1.5 換模組後啟用） | 🟢 定義對齊；待 INMP441 到貨啟用 |
| 震動馬達 | PM §4.3 要求震動提醒 | 硬體預留 GPIO 16 + `ENABLE_VIBRATION=0`；**OLED 整螢幕反色 200ms** 作視覺替代 | 🟡 中（硬體到貨後啟用） |
| RTOS | FreeRTOS 8 task | Arduino loop 單執行緒 | 🟡 中（可延後） |
| 電源管理 | Li-ion + TP4056 + light sleep | USB-C 直供，無電源管理 | 🟡 中（硬體層） |
| RTC | 硬體 RTC（PM §4.8 要求 RTC 校正） | 軟體 epoch offset（BLE sync 下發） | 🟡 中（Phase 3 DS3231 升級） |
| App | 完整 BLE App + SQLite + 視覺化 | 無 App（Phase 3 目標） | 🔴 大（依計畫推進） |
| Backend | Cloud + REST API + 分析引擎 | 無 Backend | 🔴 大（Phase 3+） |
| 單元測試 | （PM 規格未明確要求） | Phase 1 框架起步 — `lib/ems_logic` + Unity + `computeTaskElapsedMs` 9/9 tests 綠 | 🟢 基礎建立，Phase 2 擴充中 |

---

## 本次新增 / 變更的 Gap

### A. `elapsed_ms` 欄位：韌體擴充，PM 規格未定義 🟠

**PM 規格** (`pm-dev-spec.md §4.5 event_t`)：
```c
typedef struct {
    uint32_t event_id;
    uint64_t timestamp;
    uint8_t  event_type;
    uint8_t  source;
    uint8_t  device_id;
    uint8_t  mode;
    char     extra_data[32];
    uint8_t  sync_flag;
} event_t;
```
**無 `elapsed_ms`。**

**韌體現況** (`firmware/src/main.cpp` EmsEvent)：
除 PM 8 欄位外（另缺 `device_id`），多一個 `uint32_t elapsed_ms`（從任務起點扣除所有暫停後的經過毫秒數）。

**影響範圍**：
- `firmware/src/main.cpp` EmsEvent struct、`recordEvent()`、`saveSession()` JSON 序列化
- `firmware/lib/ems_logic/ems_time.h` `computeTaskElapsedMs`
- `firmware/test/test_time/test_task_elapsed.cpp` 9 個 test case
- BLE NUS `dump` 協定回傳欄位（App 依賴此欄位繪時間軸）
- LittleFS `/sessions/*.json` 持久檔格式

**兩個處理方向**：

| 方向 | 內容 | 代價 |
|------|------|------|
| **A（規格追實作，推薦）** | PM 擴充 `event_t` 加入 `elapsed_ms: uint32` | 僅 PM 規格文件改 1 行 |
| **B（實作追規格）** | 韌體移除 `elapsed_ms` 欄位 | 改動 5 個位置 + BLE 協定升版 + `/sessions/*.json` 舊檔不相容 + App 端自行從 `timestamp - task_start_timestamp - sum(pauses)` 重算（容易錯） |

**PM 決議（2026-04-23）**：採方案 A — PM 擴充 `event_t` 納入 `uint32_t elapsed_ms`。韌體不動，改 `pm-dev-spec.md §4.5` 規格文件 1 行即可對齊。待辦：更新 `docs/pm-dev-spec.md §4.5` event_t 定義，補上 `elapsed_ms` 欄位。

---

### B. RUNNING↔PAUSE 觸發方式：PM 兩份規格自打架 🟠

**`pm-flow-spec.md §3`**：
> 主鍵長按（≥ 2s）：開始 / 結束任務、**暫停 / 繼續任務**

**`pm-dev-spec.md §3 狀態機圖**：
```
IDLE
  ↓（長按）
RUNNING
  ↓（短按）   ← 與 pm-flow 矛盾
PAUSE
  ↓（短按）
RUNNING
  ↓（長按）
END → IDLE
```

**韌體實作**：
- IDLE → RUNNING：**長按**（符合兩份）
- RUNNING → PAUSE：**長按**（符合 pm-flow，不符 pm-dev）
- PAUSE → RUNNING：**短按**（符合 pm-dev，不符 pm-flow）
- PAUSE → END：**長按**（符合兩份）

**PM 決議（2026-04-23 初版 → 流程圖檢視後補正）**：

- **長按 2s**：IDLE↔RUNNING↔PAUSE 狀態循環（不含 END）
- **超長按 5s**：RUNNING 或 PAUSE 直接進入 END（取代原先的 PAUSE→長按 2s→END）
- 兩段式長按避免誤觸結束救護現場的進行中任務

**韌體待改**：
1. 新增 `VERY_LONG_PRESS_MIN_MS = 5000` 常數
2. 重構按鍵偵測為 **fire-on-release** 模式（2s 只 arming 不立即 fire，放開或到 5s 才結算），避免中間產生誤觸發的 EVT_TASK_PAUSE 事件
3. `onShortPress(BTN_PRIMARY)` 移除 PAUSE→RUNNING 分支
4. `onLongPress(BTN_PRIMARY)` 新增 PAUSE→RUNNING，**移除 PAUSE→END**
5. 新增 `onVeryLongPress(BTN_PRIMARY)`：RUNNING→END、PAUSE→END
6. 實機煙霧測試 P2-4（PAUSE→RUNNING 短按）需改為「P2-4 長按 2s」；新增 P2-7「超長按 5s 結束任務」

---

### C. `device_id` 欄位未實作 🟡

PM 規格 `event_t` 有 `uint8_t device_id`。韌體 EmsEvent 目前無此欄位。

**原因**：Phase 3 主副機架構才需要區分裝置。目前單機運作不需要。

**PM 決議（2026-04-23）**：**Phase 3 才做**。短期不影響 MVP，配合主副機 BLE 互聯一起實作。

---

### D. VENT 6 秒節拍器未實作 🟡

PM 規格 `pm-flow-spec §2 節律提醒循環`：每 6 秒 BEEP（CPR 節拍），需要 ±50ms 精度（pm-dev §4.2）。

韌體現況：VENT 模式只記錄按鍵事件，無節拍器機制。

**影響**：這是 PM 規格核心要求之一（「OHCA 數據化紀錄 + CPR 品質監測」核心價值）。

**PM 決議（2026-04-23）**：**通氣模式才有** — 6 秒節拍僅在 VENT 模式啟用時觸發，其他模式（MED 等）不跑節拍器。

**Phase 2 待辦**：抽 `VentMetronome` 純函式模組（進入/離開 VENT 模式時啟停、tick 輸出 BEEP 事件），unit test 覆蓋 ±50ms 節拍精度。

---

### E. Phase 1 單元測試現況 🟢

| 測試檔 | 對象 | 測試數 | 狀態 | PM 規格對應 |
|--------|------|--------|------|------------|
| `test/test_time/test_task_elapsed.cpp` | `computeTaskElapsedMs` | 9 | ✅ 全綠 | ⚠️ 測的 `elapsed_ms` 為韌體擴充欄位，**不在 PM 規格條款中** |

**結論**：現有測試**邏輯正確**（對照實作），但因覆蓋對象不在 PM 規格條款中，**不能聲稱「符合 Source of Truth」**。

**Phase 2 單元測試優先順序**（對齊 PM 規格的條款）：
1. `MedCountdownDecision`（抽自 `updateMedCountdown`）→ 對應 PM §4.2「4 分鐘高提醒 ±50ms」
2. `VentMetronome`（新模組）→ 對應 PM §4.2「6 秒節拍 ±50ms」
3. `computePauseCorrection` → 確保暫停不污染計時精度
4. `recordEvent` 欄位組裝 → 確保 event_t 所有欄位正確填入（MAX_EVENTS 上限、event_id 遞增）

詳見 `tasks/unit-test-plan.md`。

---

## 歷史 Gap（Phase 2C 之前的版本，已消化）

以下是前一版 gap-analysis 列出的差距，目前**已完成**或**有替代方案**：

| 前版 Gap | 現況 |
|---------|------|
| 按鍵物理配置 8 鍵 → 5 鍵需討論 | ✅ 已改 5 鍵 |
| 無狀態機 | ✅ 4 態完整實作 |
| 無長按任務控制 | ✅ 長按 2000ms 驅動狀態轉換 |
| 無暫停機制 | ✅ PAUSE 狀態 + 時間補正 |
| 無持久儲存 | 🟡 LittleFS（非 SPI Flash raw，功能等價） |
| `event_t` 僅 3 欄位 | ✅ 已擴充至 9 欄位（多 `elapsed_ms`、缺 `device_id`） |
| MAX_EVENTS = 30 | ✅ 已擴充至 100 |

---

## PM 回覆確認結果（2026-04-23 全數拍板）

| # | 問題 | PM 答覆 | 影響 / 後續動作 |
|---|------|---------|----------------|
| 1 | RUNNING↔PAUSE 觸發方式 | **長按 2s 循環；超長按 5s 結束任務**（2026-04-23 流程圖檢視後追加） | 韌體重構兩段式長按；P2-4 測試案例更新；新增 P2-7 超長按 END |
| 2 | `elapsed_ms` 是否納入 `event_t` | **方案 A（規格追實作）** | 韌體不動；更新 `pm-dev-spec.md §4.5` event_t 加 `uint32_t elapsed_ms` |
| 3 | 「主機 / 副機」定義 | **兩台 EMS Timer 硬體互聯** | Phase 3 實作主副機 BLE 架構（Central 掃描 Peripheral + GATT 4 Service） |
| 4 | 靜音鍵 vs 錄音鍵 | **只有錄音鍵**（無靜音鍵） | `pm-flow-spec.md` / `pm-dev-spec.md` 靜音鍵條款需移除或改為錄音鍵；BTN_RECORD 維持現況 |
| 5 | VENT 6 秒節拍器 | **通氣模式才有** | Phase 2 抽 `VentMetronome` 純函式模組，僅 VENT 模式啟用 |
| 6 | `device_id` 欄位 | **Phase 3 才做** | 配合主副機 BLE 一起實作 |

## 因 PM 回覆產生的規格文件 / 韌體更新待辦

### 規格文件同步（docs/）
- [x] `pm-dev-spec.md §3` 狀態圖：RUNNING→PAUSE、PAUSE→RUNNING 改長按，並新增超長按 5s → END
- [x] `pm-dev-spec.md §4.3` 按鍵表：主鍵長按拆兩段（2s 循環 / 5s 結束），模式改 4 種，錄音鍵改長按
- [x] `pm-dev-spec.md §4.5` event_t：加 `uint32_t elapsed_ms`
- [x] `pm-flow-spec.md §2`：6 秒節拍僅通氣模式、4 分鐘僅給藥模式
- [x] `pm-flow-spec.md §3`：長按拆兩段、模式 4 種、錄音鍵長按、移除靜音鍵
- [x] `pm-flow-spec.md §7`：輸入模組改錄音鍵

### 韌體修正（firmware/）
- [ ] 新增 `VERY_LONG_PRESS_MIN_MS = 5000` 常數、`btnVeryLongFired[]` 狀態陣列
- [ ] 改 `handleButtons()` / `checkLongPresses()` 為 fire-on-release 模式（2s arming，放開或 5s 才結算）
- [ ] 移除 `onShortPress(BTN_PRIMARY)` 的 PAUSE→RUNNING 分支
- [ ] `onLongPress(BTN_PRIMARY)`：新增 PAUSE→RUNNING，移除 PAUSE→END
- [ ] 新增 `onVeryLongPress(BTN_PRIMARY)`：RUNNING→END、PAUSE→END
- [ ] 實機煙霧測試：P2-4 改長按 2s；新增 P2-7 超長按 5s → END

### 單元測試（firmware/test/）
- [ ] 若新增按鍵分類純函式（如 `classifyPressDuration(held_ms) → SHORT/LONG/VERY_LONG/GRAY`），補測試涵蓋邊界（1500/2000/5000ms）

---

## 小結

**Phase 2A~2E+3A + Phase 1 單元測試起步後，跟 PM 規格最主要的差距已經從「骨架缺失」變成「對齊細節」**。三個原本列為 🔴 的大缺口（狀態機、任務控制、持久儲存）都已補齊或有功能等價替代。

**2026-04-23 PM 6 題全數拍板後**，剩下的工作分三條線：

1. **規格文件同步（小、今天能收）**：更新 `pm-dev-spec.md` / `pm-flow-spec.md` 把長按決議、`elapsed_ms`、靜音鍵移除、VENT 節拍條件寫進去。
2. **韌體對齊（中）**：PAUSE→RUNNING 改長按（單點改動 + P2-4 測試更新）；Phase 2 繼續抽 `VentMetronome`、`MedCountdownDecision` thin wrapper、`computePauseCorrection` 單元測試對齊 ±50ms。
3. **Phase 3 架構（大）**：兩台 EMS Timer 主副機 BLE 互聯 + `device_id` 欄位 + 時間同步，這整包一起動。

建議下一步：先做「規格文件同步」+ 「韌體 PAUSE→RUNNING 長按」這兩筆小改動收束規格對齊，再推 Phase 2 單元測試（VentMetronome 是重點），Phase 3 主副機留到韌體測試穩定後再啟動。
