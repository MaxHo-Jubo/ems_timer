# EMS Timer 韌體單元測試補齊評估與 Test Plan

> 產出日期：2026-04-22
> 目標程式：`firmware/src/main.cpp`（約 1800 行，Arduino/ESP32-S3）
> 工具鏈：PlatformIO + Arduino framework
> 原則：救護裝置韌體，重構必須保留既有行為；不能為了測試而引入 regression。

---

## Section 1. 可測純邏輯函式識別

走讀整份 `main.cpp` 後，把函式依「純邏輯 vs. 硬體相依」分類：

### 1.1 高純度候選（邏輯為主，硬體接觸點可抽離）

| 函式 | 行號 | 邏輯內容 | 硬體相依點 |
|------|------|---------|----------|
| `getTaskElapsedMs()` | 911 | 暫停補正的已進行時間計算 | `millis()`、讀 `deviceState` |
| `updateMedCountdown()` | 942 | 剩餘時間、1 分鐘警示、到時強提醒、重複提醒週期判斷 | `millis()`、`triggerBeep`、`triggerOledFlash`、`Serial` |
| `recordEvent()` | 869 | event_id 流水號、timestamp（epoch offset + millis）、elapsed_ms 填欄位、MAX_EVENTS 上限 | `millis()`、`Serial` |
| `transitionState()` | 787 | 狀態機移轉（IDLE→RUNNING→PAUSE→END），totalPausedMs 累計、暫停補正 medCountdownStartMs | `millis()`、`saveSession`、`triggerBeep`、`Serial` |
| 藥物選單游標循環 | 710/738（`onShortPress` 內 `BTN_UP`/`BTN_DOWN`） | `(cursor + DRUG_COUNT - 1) % DRUG_COUNT` / `(cursor + 1) % DRUG_COUNT` | 無（純算）— 但嵌在大 switch 內，需先抽出 |
| 模式選單循環 | 同上 `else if` | `(currentMode + MODE_COUNT - 1) % MODE_COUNT` | 無（純算）— 同樣嵌在 switch |
| Debounce / 長短按分類 | 556（`handleButtons`）、607（`checkLongPresses`） | `held < SHORT_PRESS_MAX_MS`、`now - lastPressMs >= DEBOUNCE_MS`、`held >= LONG_PRESS_MIN_MS` | `digitalRead`、`millis()`、`Serial` |
| `handleRxCommand()` 的 sync 分支 | 1517 | `sessionEpochOffset = ts - millis()` 計算 | `millis()`、ArduinoJson、BLE TX |
| `sendDump()` 序列化欄位組裝 | 1473 | idx/id/ts/ts_ms/el/type/src/mode/sync/extra 欄位對應 | BLE TX、`delay` |
| `saveSession()` JSON 串接邏輯 | 1652 | header + events 陣列 + 關閉 | LittleFS File、`Serial` |

### 1.2 低純度（高度硬體相依，不建議單元測試）

- `setup()` / `loop()`：框架入口。
- `setupBLE` / `setupLittleFS` / `setupI2S`：純初始化。
- `drawIdleScreen` / `drawRunningScreen` / `drawPauseScreen` / `drawEndScreen` / `drawDrugMenuScreen` / `updateDisplay`：全是 `display.print()`，改用 screenshot/視覺回歸更合適。
- `triggerBeep` / `updateBeepMachine` / `triggerOledFlash` / `updateOledFlashMachine` / `triggerVibration`：小，但幾乎就是 IO；邏輯可覆蓋但 ROI 不高。
- `handleButtons` / `checkLongPresses`：雖有邏輯，但和 `digitalRead` 綁死，必須先抽出「給定輸入序列 → 輸出事件」的 pure state 物件。
- `updateBleAdvertising`：幾乎只是兩個 BLE API 呼叫 + delay。

### 1.3 本專案的結構性問題（阻擋可測性的根因）

- 所有邏輯都是 **free functions + static 全域變數**，沒有 class、沒有介面。
- 時間來源 `millis()` 直接散落在函式內，不是注入的 clock。
- Log (`Serial.print`) 和行為 (`recordEvent`) 同一函式。
- 邊界狀態（medReminderActive、drugMenuOpen、pauseStartMs）全是 file-scope static，測試要 reset 就得寫 friend setter 或加 reset API。

**結論**：沒有任何函式是「原封不動可以在 native 跑」的。要做單元測試必定伴隨重構。問題不是「能不能測」而是「改動多少才值得」。

---

## Section 2. 抽離成本評估

| 函式 | 相依 | 難度 | 建議抽離策略 |
|------|------|------|------------|
| `getTaskElapsedMs` | `millis()`、全域 `taskStartMs/pauseStartMs/totalPausedMs/deviceState` | **low** | 抽成 `computeTaskElapsedMs(now, taskStartMs, pauseStartMs, totalPausedMs, state)` pure function，原函式只負責注入現值 |
| 藥物選單游標循環 | 全域 `drugMenuCursor` | **low** | 抽成 `nextCursor(cur, count)` / `prevCursor(cur, count)`，原 switch 呼叫新函式 |
| 模式選單循環 | 全域 `currentMode` | **low** | 同上 → `cycleMode(mode, delta, count)` |
| `recordEvent` 欄位組裝 | `millis()`、eventCount、nextEventId、events[] | **medium** | 分離成「計算 event 資料」+「寫入 buffer」兩個函式。前者 pure，後者含 side effect |
| `updateMedCountdown` 的時間判斷 | `millis()` + 5 個全域旗標 | **medium** | 抽 `MedCountdownDecision(now, startMs, reminderActive, warn1MinTriggered, lastReminderMs) -> { fireWarn1Min, fireReminderStart, fireReminderRepeat }`，用 struct 回傳要做什麼，讓主函式去執行 side effect |
| 暫停補正數學（transitionState 內部） | `millis()` | **medium** | 抽 `computePauseCorrection(now, pauseStartMs, prevTotalPaused, medCountdownStart) -> { newTotalPaused, newMedCountdownStart }` |
| debounce + 長短按分類 | `digitalRead`、`millis()`、5 顆按鈕 state array | **high** | 抽成 `ButtonFsm` class（5 顆共用 or 單顆 × 5），API：`feed(now, levels[5]) -> events[]`。原 `handleButtons` 變成 adapter |
| `handleRxCommand` 的 sync 計算 | ArduinoJson + `millis()` | **medium** | 抽 `computeEpochOffset(tsFromApp, nowMs)`，原函式只處理解析與分派 |
| `sendDump` 欄位組裝 | `EmsEvent` → JSON | **medium** | 抽 `serializeDumpItem(e, idx, jsonDoc&)`，主函式負責 iterate + BLE send |
| `saveSession` JSON 串接 | LittleFS File | **medium** | 抽 `writeSessionJson(IWriter&, taskId, events, count)`，用介面注入 File。測試時用 `StringWriter` 拼字串驗 |
| `transitionState` 完整 | 全域一坨 + `saveSession`/`triggerBeep` | **high** | 若要測 state machine，需把 side effect 外移（`StateActions` struct 回傳），或只測 `nextState(cur, trigger)` 的純遷移表 |

**抽離難度排序**（低→高）：
- Low：`getTaskElapsedMs`、選單游標、模式循環（這三個是 free lunch）
- Medium：`recordEvent`、`updateMedCountdown`、`handleRxCommand.sync`、暫停補正、JSON 序列化
- High：`ButtonFsm`、`transitionState` 完整版

---

## Section 3. 測試框架建議

### 3.1 環境：PlatformIO `platform = native`

在 `firmware/platformio.ini` 新增：

```ini
[env:native]
platform = native
test_framework = unity  ; 或 googletest
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -I test/mocks       ; 放 Arduino stub header
```

### 3.2 Unity vs. Google Test

**選 Unity**。理由：
- PlatformIO 原生 first-class 支援，`pio test` 開箱即用。
- C 語言起家，和 Arduino/ESP-IDF 慣用風格一致；巨集輕量、編譯快。
- Google Test 需要 C++14+ RTTI、輸出較重，在 native + embedded dual target 場景下，Unity 的阻力最小。
- 本專案沒有 mock framework 需求（GMock 的主要賣點），手寫 stub 足矣。

如果後續要做 behaviour verification（「預期呼叫了 triggerBeep 幾次」），再評估是否升 GoogleTest + GMock。

### 3.3 目錄結構

```
firmware/
├── platformio.ini
├── src/
│   └── main.cpp              ; 加 #ifndef UNIT_TEST 包住 setup/loop
├── lib/
│   └── ems_logic/            ; 抽出來的 pure functions
│       ├── ems_time.h/cpp    ; computeTaskElapsedMs, pause correction
│       ├── ems_event.h/cpp   ; event 欄位組裝
│       ├── ems_countdown.h/cpp ; MedCountdownDecision
│       ├── ems_menu.h/cpp    ; cursor/mode cycle
│       └── ems_button_fsm.h/cpp
└── test/
    ├── mocks/
    │   ├── Arduino.h         ; stub millis/digitalRead/Serial
    │   └── ArduinoFake.h     ; optional，用 fabiobatsilva/ArduinoFake
    ├── test_time/
    │   └── test_task_elapsed.cpp
    ├── test_countdown/
    │   └── test_med_countdown.cpp
    ├── test_menu/
    │   └── test_menu_cycle.cpp
    ├── test_event/
    │   └── test_record_event.cpp
    └── test_button/
        └── test_button_fsm.cpp
```

### 3.4 Arduino 層 stub 策略

三選一（由輕到重）：

1. **手寫最小 stub**：`Arduino.h` 只宣告 `millis()`、`digitalRead()`、`Serial.print()` 為 no-op / 可覆寫的 global。適用於邏輯完全抽離、不碰 Arduino API 的 pure function 測試。
2. **ArduinoFake**（bblanchon/ArduinoFake）：PlatformIO 生態常見，可 mock millis。中量級。
3. **無需 stub**：如果 pure function 完全不 import Arduino.h（只吃 `uint32_t now` 參數），連 stub 都不需要 → 這是最理想的狀態。

**建議策略**：Phase 1 走路徑 3（純函式直接注入 now），連 Arduino.h 都不 include；只有在測 `ButtonFsm` 才需要 stub。

---

## Section 4. Test Plan

### 4.1 優先測的 5 個函式（ROI 最高）

| 優先序 | 函式 | 理由 | Effort |
|--------|------|------|--------|
| 1 | `computeTaskElapsedMs`（抽自 getTaskElapsedMs） | 邏輯最簡單、全韌體顯示依賴、暫停補正是 silent bug 溫床 | 0.5h 重構 + 0.5h 測試 |
| 2 | `nextCursor/prevCursor` + `cycleMode` | 免費（無硬體相依）、覆蓋游標 wrap around 邊界 | 0.5h |
| 3 | `MedCountdownDecision`（抽自 updateMedCountdown） | 核心醫療功能、1 分鐘警示 / 倒數到時 / 重複提醒三條 path 易 regress | 1.5h 重構 + 1h 測試 |
| 4 | `computePauseCorrection`（抽自 transitionState） | 暫停→繼續的 medCountdownStartMs 補正，算錯直接影響用藥安全 | 1h 重構 + 0.5h 測試 |
| 5 | `recordEvent` 欄位組裝 | MAX_EVENTS 上限、event_id 流水號、elapsed 計算一次到位 | 1h 重構 + 1h 測試 |

**後補**：`handleRxCommand.sync` 的 epoch offset 計算、`saveSession` JSON 串接（改用 StringWriter 驗）、`ButtonFsm`。

### 4.2 Test Cases 明細

#### (1) `computeTaskElapsedMs(now, taskStartMs, pauseStartMs, totalPausedMs, state)`

- happy: taskStartMs=1000, now=5000, totalPausedMs=0, state=RUNNING → 4000
- paused: state=PAUSE, pauseStartMs=3000, taskStartMs=1000, totalPausedMs=0 → 2000
- paused after prior pauses: pauseStartMs=10000, taskStartMs=1000, totalPausedMs=2000 → 7000
- not started: taskStartMs=0 → 0
- running after one pause: totalPausedMs=2000, now=10000, taskStartMs=1000 → 7000
- **edge**: millis 溢位（taskStartMs > now 的 wrap-around，uint32 減法結果仍正確） → 驗算符合 rollover

#### (2) 選單游標與模式循環

- `nextCursor(0, 6)` → 1
- `nextCursor(5, 6)` → 0
- `prevCursor(0, 6)` → 5
- `prevCursor(3, 6)` → 2
- `cycleMode(MED=0, +1, 4)` → VENT=1
- `cycleMode(MED=0, -1, 4)` → SET=3
- **edge**: count=1 → 任何循環都回傳 0；count=0 → 定義為 0（或 assert，二選一）

#### (3) `MedCountdownDecision(now, startMs, reminderActive, warnTriggered, lastReminderMs)`

- 尚未到 180s：回傳 `{no-op}`
- 剩 60s、warnTriggered=false：`{fireWarn1Min=true}`
- 剩 60s、warnTriggered=true：`{no-op}`
- 到達 240s、reminderActive=false：`{fireReminderStart=true}`
- reminderActive=true、距 lastReminderMs < 30s：`{no-op}`
- reminderActive=true、距 lastReminderMs >= 30s：`{fireReminderRepeat=true}`
- **edge**: elapsed = 0（剛啟動，防止 1 分警示誤觸發）
- **edge**: elapsed = MED_COUNTDOWN_MS - 1（剛好差 1ms 沒到時）

#### (4) `computePauseCorrection(now, pauseStartMs, prevTotalPaused, medCountdownStart, isMed)`

- pauseStartMs=10000, now=13000, prev=0 → totalPaused=3000
- pauseStartMs=10000, now=13000, prev=2000 → totalPaused=5000
- isMed=true, medCountdownStart=5000 → 補正為 8000
- isMed=false → medCountdownStart 不變
- **edge**: 暫停 0ms（now == pauseStartMs）→ totalPaused 不變

#### (5) `recordEvent` 欄位組裝 / 溢位

- happy：eventCount=0 → events[0] 填入正確 event_id=1、timestamp=offset+now、elapsed 正確
- 連續記 3 筆：event_id 為 1/2/3、nextEventId 推進到 4
- **edge**: eventCount=MAX_EVENTS → 應該拒絕寫入（當前程式碼有沒有檢查？**需要 grep 確認**，若無則是 bug）
- extra_data 超長：strncpy 正確截斷、結尾 `\0`
- 暫停中呼叫：elapsed 應該用 pauseStartMs 而非 now（需確認現有行為，若用 now 則是潛在 bug）

### 4.3 Effort 估算

| 項目 | 時數 |
|------|------|
| Phase 1（低風險抽離 + 測）：時間、選單、倒數決策 | 4–5h |
| Phase 2（含 state 耦合重構）：暫停補正、recordEvent | 3–4h |
| Phase 3（高風險）：ButtonFsm、transitionState 純化 | 6–8h |
| 基礎設施（native env、mocks、Unity 設定、CI） | 2–3h |
| **合計** | **15–20h** |

### 4.4 分階段執行計畫

**Phase 1（本週可做）— 低成本抽離**
- 建 `firmware/lib/ems_logic/` 子 library（PlatformIO 會自動編進 firmware 與 test 兩個環境）
- 抽：`computeTaskElapsedMs`、`nextCursor`/`prevCursor`、`cycleMode`
- 設 `platformio.ini` 的 `[env:native]` + Unity
- 寫 10~15 個測試，跑 `pio test -e native`
- 原始 `main.cpp` 改為呼叫新函式，**編譯韌體確認行為未變 + 實機煙霧測試**

**Phase 2（下週）— 中度重構**
- 抽 `MedCountdownDecision`（回傳 struct，不做 side effect）
- 抽 `computePauseCorrection`
- 抽 `recordEvent` 的 event data 組裝部分（buffer 寫入保留）
- 寫 15~20 個測試
- 實機回歸：跑一次完整任務（IDLE→RUN→PAUSE→RUN→END）驗儲存檔

**Phase 3（可選）— 高風險才做**
- 只在 Phase 1/2 發現 bug 或有具體需求時才進場
- `ButtonFsm` 單獨 class、完整 state machine test
- `transitionState` 的 transition table 純化

---

## Section 5. 風險評估

### 5.1 高風險重構點

| 重構動作 | 可能破壞的現有行為 | 風險等級 |
|---------|-----------------|---------|
| 把 `handleButtons` 抽成 `ButtonFsm` | debounce 時序對按鈕體感極敏感；80ms debounce、1500/2000ms 長短按門檻需要逐一驗 | **高** |
| 把 `transitionState` 的 side effect 外移（triggerBeep、saveSession） | 狀態遷移順序一旦搞錯，儲存檔可能缺事件或提示音亂 | **高** |
| `updateMedCountdown` 的三條提醒 path 重構 | 救護現場要靠這個提醒時間到；1min 警示、到時、重複提醒任一錯都可能漏提醒 | **高** |
| `recordEvent` 拆 pure/impure | 影響每一筆紀錄；順序顛倒會讓 event_id/timestamp 對不上 | **中** |
| `getTaskElapsedMs` 抽純函式 | 純計算，單測可完整覆蓋行為 | **低** |
| 選單循環抽純函式 | 只是取代兩行取模運算 | **極低** |

### 5.2 降風險做法

1. **先寫 regression test 再重構**（TDD 的 characterization test）
   - 在抽任何函式前，先針對現有 `getTaskElapsedMs` / `updateMedCountdown` 寫「黑箱」測試，餵已知輸入，記錄現在的輸出。
   - 重構後這些測試必須全綠才算沒 regression。
   - 特別是 `updateMedCountdown`：寫一組「模擬 loop 呼叫 N 次、每次 now += 100ms」的整合測，確認 1min warn / 240s 到時 / 30s 重複提醒的觸發時間點跟重構前一致。

2. **每個 Phase 結束都做實機煙霧測試**
   - 完整跑一次任務週期（IDLE→RUN→記錄多筆→PAUSE→RUN→END）
   - 驗 OLED 顯示、蜂鳴器、LittleFS 檔內容
   - 這是 native unit test 無法取代的行為驗證層

3. **Git 單次 commit 單一抽離**
   - 不要一次重構多個函式；每抽一個就 commit，出事容易 bisect。

4. **保留舊函式，新舊並存一段時間**
   - 例如抽 `computeTaskElapsedMs(...)` 後，讓 `getTaskElapsedMs()` 只是 thin wrapper。
   - 避免直接改呼叫方，降低 diff 面積。

5. **暫停補正不要動邏輯，只搬位置**
   - `medCountdownStartMs += pausedDuration` 這行是 silent 邏輯（只在 MED 模式、medCountdownStartMs > 0 才跑）。重構時條件必須一字不改，用測試 pinned 住。

6. **END 狀態的 2 秒自動回 IDLE + btnLongFired 清除**（loop 的 STEP 06）
   - 這段是為了避免 toggle switch 誤觸的特殊處理，不要動。若要測，當成 `ButtonFsm` 的 edge case 加 test。

### 5.3 不建議為了測試重構的函式

- **所有 draw*Screen**：Arduino 畫面 API 沒有 headless renderer 能 asset；投入 mock 成本 >> 抓 bug 價值。
- **updateBleAdvertising**：核心邏輯就是「連上→送 hello」兩個 API call，改個兩行拆出來對可靠性沒幫助。
- **setupBLE / setupLittleFS / setupI2S**：硬體初始化，靠實機驗。

### 5.4 首要檢查項（寫測試前先 grep 確認）

- `recordEvent` 是否有 `if (eventCount >= MAX_EVENTS) return;` 防護？若沒有，先補，再寫測試釘住行為。
- `getTaskElapsedMs` 在 `STATE_PAUSE` 分支的公式 `(pauseStartMs - taskStartMs) - totalPausedMs` 是否扣過前一段暫停？需畫時序圖驗邏輯。

---

## 附錄 A：建議的最小起步 diff

1. 新增 `firmware/lib/ems_logic/ems_time.h`：宣告 `uint32_t computeTaskElapsedMs(uint32_t now, uint32_t taskStartMs, uint32_t pauseStartMs, uint32_t totalPausedMs, uint8_t state)`。
2. 新增 `firmware/lib/ems_logic/ems_time.cpp`：實作（純 C++，不 include Arduino.h）。
3. 修改 `main.cpp` 的 `getTaskElapsedMs()`：只做 `return computeTaskElapsedMs(millis(), taskStartMs, pauseStartMs, totalPausedMs, deviceState);`
4. 新增 `firmware/test/test_time/test_task_elapsed.cpp`：10 個 test case。
5. 在 `platformio.ini` 加 `[env:native]` section。
6. 跑 `pio test -e native`，綠燈。
7. 跑 `pio run -e esp32-s3-devkitc-1`，確認韌體還能編 + 實機煙霧測試。

這個 PR 的 diff 面積小、風險低，是證明 test 基礎設施能運作的最佳起點。
