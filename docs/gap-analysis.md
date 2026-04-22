# PM 規格 vs 現有韌體 Gap Analysis

> 基準：`docs/pm-flow-spec.md` + `docs/pm-dev-spec.md`（PM 目標）
> vs **Phase 2A~2E+3A + Phase 1 單元測試框架**（commit `38ea2cb`，2026-04-22）
> 前次更新：2026-04-22（Phase 2C 版本）
> 本次更新：2026-04-22（Phase 2A~2E+3A + Phase 1 單元測試完成後）

---

## 總覽（更新後）

| 層級 | PM 目標 | 現況 | 差距等級 |
|------|---------|------|---------|
| 按鍵配置 | 5 鍵語意化（主鍵 / 上下 / 電源 / 靜音） | 5 鍵（BTN_PRIMARY/UP/DOWN/POWER/RECORD），電源與錄音/靜音鍵為 noop 佔位 | 🟡 中（硬體已縮減至 5 鍵，但電源 / 靜音功能未實作） |
| 狀態機 | IDLE → RUNNING → PAUSE → END | 4 態狀態機 ✅ 完整實作 | 🟢 一致 |
| 任務控制 | 長按主鍵開始 / 結束任務 | 長按主鍵 ≥2000ms 驅動 IDLE→RUNNING / RUNNING→PAUSE / PAUSE→END | 🟢 一致 |
| RUNNING↔PAUSE 切換 | **規格矛盾**：pm-flow §3 寫「長按」、pm-dev §3 狀態圖寫「短按」 | RUNNING→PAUSE 長按 / PAUSE→RUNNING 短按 | 🟠 規格自打架，**等 PM 確認** |
| 節律提醒 | 6 秒 CPR 節拍 + 4 分鐘給藥提醒（±50ms） | MED 模式 240 秒給藥倒數 ✅；VENT 模式僅記錄事件，**無 6 秒節拍** ❌ | 🟡 中（VENT 節拍待實作） |
| 資料結構 | event_t（8 欄位） | EmsEvent（9 欄位，多 `elapsed_ms`、缺 `device_id`） | 🟠 韌體擴充，**等 PM 確認 elapsed_ms 是否正式納入規格** |
| 容量 | Ring Buffer 100~500 events | MAX_EVENTS = 100 | 🟢 符合下限 |
| 持久儲存 | SPI Flash 批次寫入（每 5 秒）+ wear leveling | **LittleFS**，任務 END 時整批寫 `/sessions/<epoch>.json` | 🟡 中（用 LittleFS 替代 SPI Flash raw；寫入時機非每 5 秒而是 END 時） |
| BLE 架構 | 主機（Central）掃描副機（Peripheral）+ GATT 4 Service | 裝置為 Peripheral；**無主副機架構**；使用 NUS + JSON 單一 service | 🔴 大（**主副機定義等 PM 釐清**：兩台裝置互聯 vs 裝置+App？） |
| 電源鍵功能 | 短按螢幕亮滅 / 長按關機 | 僅 log 輸出（noop） | 🟡 中（硬體到位後可補） |
| 靜音鍵功能 | 短按標記靜音事件 / 長按切換靜音 ON/OFF | BTN_RECORD 佔位（原設計為 INMP441 錄音，非靜音） | 🟠 用途需對齊：PM 規格的靜音鍵跟現有 BTN_RECORD 是不同功能 |
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

**目前決議**：保留現況（韌體保留 `elapsed_ms`），`ems_time.h` 與 `test_task_elapsed.cpp` 已加註解標明此為「BLE 協定承諾的欄位」，**等 PM 確認是否正式擴充規格**。

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

**目前決議**：使用者將與 PM 確認兩份規格應以哪份為準。韌體暫不動。

---

### C. `device_id` 欄位未實作 🟡

PM 規格 `event_t` 有 `uint8_t device_id`。韌體 EmsEvent 目前無此欄位。

**原因**：Phase 3 主副機架構才需要區分裝置。目前單機運作不需要。

**決議**：等 Phase 3 實作主副機時再補。短期不影響 MVP。

---

### D. VENT 6 秒節拍器未實作 🟡

PM 規格 `pm-flow-spec §2 節律提醒循環`：每 6 秒 BEEP（CPR 節拍），需要 ±50ms 精度（pm-dev §4.2）。

韌體現況：VENT 模式只記錄按鍵事件，無節拍器機制。

**影響**：這是 PM 規格核心要求之一（「OHCA 數據化紀錄 + CPR 品質監測」核心價值）。

**決議**：列為 Phase 2 後續擴充項。需抽 `VentMetronome` 類似的模組，以便 unit test 覆蓋 ±50ms 節拍精度。

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

## 等 PM 確認的開放問題（優先順序）

1. **【最高】RUNNING↔PAUSE 觸發方式**：pm-flow 寫長按、pm-dev 寫短按，兩份自相矛盾。韌體目前採「RUNNING→PAUSE 長按 / PAUSE→RUNNING 短按」。PM 要選一份為準，另一份同步更正。
2. **【高】`elapsed_ms` 是否正式納入 `event_t`**：韌體已擴充且 App 依賴。PM 選方向 A（規格追實作）最簡。
3. **【高】「主機 / 副機」定義**：是兩台 EMS Timer 硬體互聯（PM 規格字面意思），還是「裝置 + 手機 App」？影響整個 BLE 架構方向。
4. **【中】靜音鍵 vs 錄音鍵**：現有 BTN_RECORD 硬體是為 INMP441 錄音保留，PM 規格要求的靜音鍵需另規劃（或取消，或重新映射 BTN_RECORD 的雙用途）。
5. **【中】VENT 6 秒節拍器**：PM §4.2 明確要求，但裝置定位上是否真的每個場域都需要（或只在「通氣模式」開啟時才跑）？
6. **【低】`device_id` 欄位**：Phase 3 主副機時需要，現階段單機可忽略。

---

## 小結

**Phase 2A~2E+3A + Phase 1 單元測試起步後，跟 PM 規格最主要的差距已經從「骨架缺失」變成「對齊細節」**。三個原本列為 🔴 的大缺口（狀態機、任務控制、持久儲存）都已補齊或有功能等價替代。

剩下的主要是：
- **規格矛盾 / 規格擴充待 PM 拍板**（PAUSE 觸發、`elapsed_ms`、主副機定義）
- **尚未實作的 PM 條款**（VENT 6 秒節拍、靜音鍵、電源鍵、震動馬達、RTC 硬體、App、Backend）
- **單元測試覆蓋對齊 PM 規格**（優先抽 `MedCountdownDecision` 對應 ±50ms 要求）

建議下一波開發先釐清「等 PM 確認」的 6 個開放問題，再決定 Phase 2 單元測試範圍與 Phase 3 實作優先順序。
