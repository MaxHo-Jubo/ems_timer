# EMS Timer 開發進度

## 已完成階段

### Phase 0 — 開發環境（2026-04-13）
- [x] PlatformIO + VS Code 環境建立
- [x] 基本韌體 Build / Upload / Serial monitor

### Phase 1 — 硬體原型（2026-04-17 驗收通過）
- [x] ESP32-S3-DevKitC-1 平台升級（從 ESP32 換到 S3，方案 B 接線）
- [x] 8 按鈕（GPIO 4/5/6/7/15/16/17/18）+ INPUT_PULLUP
- [x] SSD1306 OLED（I2C 42/41）
- [x] 蜂鳴器（GPIO 14）
- [x] INMP441 I2S 麥克風（SCK/WS/SD = 40/39/38）— 通訊協定層驗證通過，靈敏度待換模組
- [x] Phase 1 基線 commit（`4dba88e`）

### Phase 2A — 計時邏輯 + OLED 顯示（2026-04-18 編譯通過）
- [x] EmsEvent 資料結構 + 事件陣列（MAX_EVENTS = 30）
- [x] Session 狀態（開機隨機 ID + 首次按鈕鎖定起算點）
- [x] EventConfig 計時配置表（per 按鈕 TIMER_UP/DOWN + 倒數時長 + 區間提醒）
- [x] 倒數提醒 state machine（區間短嗶 + 結束 3 聲 + 畫面閃爍）
- [x] 非 blocking 蜂鳴器 state machine
- [x] OLED 計時顯示（頂部事件名+計數 / 中間大字 mm:ss / 底部模式）
- [x] 編譯通過：RAM 5.9% / Flash 9.2%

### Phase 2B — BLE NUS 通訊（2026-04-18 編譯通過，2026-04-21 實機驗收）
- [x] ArduinoJson 依賴
- [x] BLE NUS service（TX Notify + RX Write，MTU 升至 517）
- [x] 連線 hello 訊息（含 session_id）
- [x] `sync` 命令：App 下發 epoch ms 做軟體對時
- [x] `dump` 命令：批次回傳 events[] 陣列（dump_start / dump_item × N / dump_end）
- [x] `clear` 命令：清空事件
- [x] 連線/斷線自動管理 advertising 重啟
- [x] 修正：`ARDUINO_USB_CDC_ON_BOOT=1`（native USB CDC Serial）
- [x] 修正：hello delay 100ms → 3000ms（給 App 足夠時間訂閱 Notify）
- [x] 即時 evt Notify 暫時停用（Phase 3 再確認是否保留）

### Phase 2C — 按鈕重構與事件資料強化（2026-04-21）
- [x] BTN1~4 改為藥物計時器（Group 0: Epi/Amio/Atropine/Adenosine；Group 1: Naloxone/Nitro/D50/Morphine）
- [x] BTN5~8 改為系統功能（Menu/Next/Prev/Power），不觸發事件記錄
- [x] `getButtonLabel()` / `getEventConfig()` 統一查詢介面，支援群組切換
- [x] EmsEvent 新增 `elapsed_end_ms`：記錄計時中斷或倒數自然結束的時間點
- [x] 反覆點擊或切換按鈕皆自動封存前一筆計時結束時間
- [x] BLE dump/evt JSON 新增 `end` 欄位
- [x] BTN5 Menu 功能實作（開啟選單 / 確認切換群組；同時中斷進行中的藥物計時）
- [x] BTN6 Next / BTN7 Prev 選單導航實作（游標循環移動 + 重置 5s 超時）
- [x] OLED 選單畫面（drawMenuScreen：群組列表反白游標 + 5s 無操作自動關閉）
- [ ] BTN8 Power 開關機實作（暫停：換單行程按鍵後加長按偵測再啟用 deep sleep）

## 待上機驗證

- [x] **Phase 2 可行性測試通過**（2026-04-21）：§1~4、§7~8、§10、§A~C 全通過
  - §5 倒數計時、§6 事件切換、§9 容量上限、§11~12 待補測（換單行程按鍵後）

### Phase 2.x — BTN5~8 選單系統（2026-04-21）
- [x] MenuState 狀態機（MENU_NONE / MENU_OPEN）
- [x] BTN5 開選單 / 確認，BTN6/7 導航，OLED 反白顯示
- [x] 5 秒無操作自動關閉
- [x] 開機 lastBtnState 修正（toggle switch 不誤觸）
- [x] BTN5 開選單時中斷進行中的藥物計時

## 後續階段

### Phase 1.5 — INMP441 重試（等硬體）
- [ ] 新 INMP441 模組到貨
- [ ] `ENABLE_MIC_MONITOR` 設 1 重編
- [ ] 驗證靈敏度（呼吸、說話、背景音）
- [ ] 錄音檔 WAV + SD 卡模組（GPIO 10~13，目前僅保留腳位）

### Phase 3 — 手機 App + 硬體 RTC
- [ ] DS3231 RTC 模組升級（與 OLED 共用 I2C bus）
- [ ] 離線時間戳保存（斷電不失憶）
- [ ] 手機 App（React Native / Flutter，待定）
  - [ ] BLE 掃描/連線
  - [ ] 接收 Notify 即時更新時間軸
  - [ ] SQLite 本地歷史儲存
  - [ ] App 分發策略（TestFlight / 自架 APK 等，細節留存於舊版 todo）
  - [ ] **按鈕設定編輯**：App 可編輯 BTN1~4 各群組的計時配置，透過 BLE `config` 命令推送至 ESP32（群組數量可擴充，不限於兩組）
    - 可設定項目：
      - `label`：藥物名稱（自訂顯示文字）
      - `mode`：計時模式（`up` 正數 / `down` 倒數）
      - `duration`：倒數總時長（秒，`up` 模式忽略）
      - 蜂鳴器觸發條件（可複選）：
        - `beep_on_expire`：計時結束時嗶聲
        - `beep_interval`：固定區間提醒（每 N 秒嗶一聲）
        - `beep_at`：指定時間點提醒（倒數剩 N 秒時嗶）
    - ESP32 端用 `Preferences`（NVS）持久化，重開機不遺失
    - 不需要 OTA，純資料層操作

### Phase 4 — 整合測試、優化與量產化

#### 整合測試
- [ ] 電源方案選型（實測耗電後決定 18650 / LiPo / 乾電池）
- [ ] 長時間穩定性測試（連續 > 2 小時出勤情境）
- [ ] 最終驗收

#### 量產化路線
採漸進式硬體升級，韌體不需改動：

| 階段 | 硬體方案 | 適用時機 |
|------|---------|---------|
| 現在 | 麵包板 + 杜邦線 | 韌體驗證（當前） |
| 原型機 | 洞洞板手焊 / 杜邦線固定 | 使用者試用、功能測試 |
| 小批量 | 客製 PCB + 手焊（KiCad → JLCPCB） | 5~10 台，確認設計定版 |
| 正式版 | PCB + SMT 代焊（JLCPCB 焊被動元件，自焊模組） | 設計定版後降低組裝成本 |

- [ ] **PCB 設計**（KiCad）：ESP32-S3-MINI 模組 + 8 按鈕座 + OLED 連接器 + 蜂鳴器 + 電源管理
  - 按鈕改 JST 連接器（插拔式，方便換位置或更換開關）
  - 換單行程 tactile 按鍵（同時啟用 BTN8 長按 deep sleep）
- [ ] **外殼設計**（3D 列印）：開孔對齊 OLED 視窗、8 顆按鈕、USB-C 口、蜂鳴器孔
- [ ] 洞洞板原型機組裝與使用者測試
- [ ] PCB 打樣（JLCPCB，5 片起）
- [ ] **OTA 韌體更新**：WiFi STA 模式（ESP32 從 server 拉 `.bin`，手機 App 觸發）
  - `.bin` 初期放 GitHub Releases
  - 使用場景：非出勤時段，於辦公室/家中連 WiFi 更新

## 設計決策參考

- Phase 2 設計決策：`CLAUDE.md` § Phase 2 設計決策（2026-04-18）
- 按鈕互動：單行程按鍵計畫、開關店員/模組切換功能延後
- BLE 協議：NUS + JSON（先求通，省電化延後）
- 時間同步：Phase 2 軟體對時 → Phase 3 DS3231 RTC
- 電源方案 E（乾電池）已記錄為候選

## 硬體備註

- 主控：ESP32-S3-DevKitC-1（從早期 ESP32 WROOM-32 仿製板升級）
- 按鈕：目前為開關式（toggle），未來改單行程（tactile momentary）
- 共地：所有按鈕 GND 接同一軌

---

# Phase 2A~2E+3A 實機煙霧測試清單（2026-04-22）

基於 `firmware/src/main.cpp` 實際程式碼（commit `ee5b3d7` + OLED 反色閃爍 + 按鍵 debug log）。

**索引**：BTN 0=PRIMARY(GPIO4)、1=UP(GPIO5)、2=DOWN(GPIO6)、3=POWER(GPIO7)、4=RECORD(GPIO15)
**模式循環**：MED → VENT → CUST → SET → MED
**狀態碼**：0=IDLE、1=RUNNING、2=PAUSE、3=END
**關鍵常數**：SHORT<1500ms、LONG>=2000ms、1500~2000ms=GRAY、MED 倒數 240s、剩 60s 警示、到時每 30s 重複、END 停 2s 自動回 IDLE、選單 8s timeout

## P0 開機與基本輸入

- [x] **P0-1 上電** → OLED 顯示 IDLE 畫面；Serial BOOT + /sessions 掃描
- [x] **P0-2 BTN0 快按放** → SHORT log，IDLE 下 noop
- [x] **P0-3 BTN0 按 1.7 秒放** → GRAY ignored，狀態不變
- [x] **P0-4 BTN0 按 2.5 秒放** → LONG fired → IDLE→RUNNING，MED countdown 啟動
- [x] **P0-5 BTN1/BTN2 短按** → 模式循環 MED↔SET↔CUST↔VENT（P5-6 測試中驗證）
- [ ] **P0-6 BTN3 短/長按** — 待 tactile 按鍵
- [ ] **P0-7 BTN4 短/長按** — 待 tactile 按鍵

## P1 狀態機四態轉換

- [x] **P1-1 RUNNING → PAUSE**：BTN0 單次長按通過
- [x] **P1-2 PAUSE 短按繼續**：驗證通過
- [x] **P1-3 PAUSE 長按結束**：單次長按 PAUSE→END→自動回 IDLE 通過（修復 END 鎖死 bug 後）
- [ ] **P1-4 暫停時間補正** — 待 tactile 按鍵（需連續長按串流）
- [ ] **P1-5 BTN1/BTN2 在 PAUSE 下無反應** — 待 tactile 按鍵

## P2 模式切換與 MED 倒數

- [x] **P2-1 模式循環**：MED↔SET↔CUST↔VENT 四擋循環通過
- [x] **P2-2 MED 倒數啟動**：`[MED] 240s countdown start/reset` 通過
- [x] **P2-3 剩 60 秒警示**：Serial + 2 短嗶（聽覺確認通過）
- [x] **P2-4 倒數歸零**：3 嗶 + OLED 整螢幕反色 200ms + `GIVE MED!` 閃爍（全部確認通過）
- [x] **P2-5 每 30 秒重複**：log + 3 嗶 + 反色通過
- [x] **P2-6 BTN0 短按確認給藥**：extra="epi" + countdown 重置 + 1 短嗶通過
- [ ] **P2-7 VENT 短按** — 快測項，待 tactile 或下次
- [ ] **P2-8 CUST 短按** — 同上

## P3 藥物選單（Phase 2E）

- [x] **P3-1 BTN0 短按開選單**：log + 游標在 Amiodarone 通過
- [x] **P3-2 BTN2 游標**：部分驗證（Atropine 被記錄）
- [ ] **P3-3 BTN1 游標反向循環** — 待補測
- [x] **P3-4 BTN0 短按確認**：`[DRUG] recorded: Atropine` + EVT 通過
- [ ] **P3-5 選單 8 秒 timeout** — 待補測
- [ ] **P3-6 上/下鍵重置 timeout** — 待補測
- [ ] **P3-7 選單開啟時進 PAUSE** — 待 tactile 按鍵

## P4 事件紀錄 + LittleFS（Phase 3A）

- [x] **P4-1 任務存檔**：`[FS] saved: /sessions/15.json` 通過
- [ ] **P4-2 重開機掃描** — 待補測（簡單，1 分鐘）
- [ ] **P4-3 JSON 結構** — 需要 BLE dump 或 LittleFS uploader
- [x] **P4-4 event_id 連續遞增**：log 觀察 id=1,2,3,4... 通過
- [ ] **P4-5 source 分類** — 需要看 JSON
- [ ] **P4-6 mode 欄位** — 需要看 JSON
- [x] **P4-7 extra_data**：Atropine、epi 均正確寫入
- [ ] **P4-8 容量 > 100 筆** — 待補測（需大量按鍵，等 tactile）

## P5 邊界與穩定性

- [x] **P5-1 Debounce**：多次 `debounce reject` 正常運作
- [x] **P5-2 灰色地帶**：多次 `GRAY (ignored)` 通過
- [x] **P5-3 長按半途放開**：GRAY 不觸發 LONG 通過
- [ ] **P5-4 雙鍵同時** — 待 tactile
- [x] **P5-5 長任務 > 5 分鐘**：實測跑 > 500 秒 mm:ss 顯示正確
- [x] **P5-6 倒數中切模式**（修復後）：切離 MED 清 countdown、切回 MED 重啟通過
- [x] **P5-7 PAUSE 中 MED 倒數順延**：補正邏輯通過（240023ms 到時）
- [x] **P5-8 END 期間按鍵**（修復後）：2 秒自動切 IDLE 不被鎖死

## Review

### 2026-04-22 測試通過項

- **P0-1~P0-5**（P0-6/7 待 tactile）
- **P1-1~P1-3**（P1-4/5 待 tactile）
- **P2-1~P2-6**（P2-7/8 待補測）
- **P3-1, P3-2, P3-4**（其他待補測或 tactile）
- **P4-1, P4-4, P4-7**（P4-2/3/5/6/8 待補測）
- **P5-1, P5-2, P5-3, P5-5, P5-6, P5-7, P5-8**（P5-4 待 tactile）

### Session 中修復的 bug

1. **END 跨狀態邊界誤觸**：PAUSE 長按觸發 PAUSE→END 後，若使用者繼續按或快速再按，2 秒後 END→IDLE 自動切換時會被當成 IDLE 的長按觸發 IDLE→RUNNING。修法：END 2 秒到自動切 IDLE 時清除所有按鍵 `btnPressStartMs` 並設 `btnLongFired=true`，使用者放開時以 `RELEASE (long already fired)` 重置。
2. **END 鎖死**：前一版修法要求「所有按鍵放開」才切 IDLE，toggle 下使用者持續操作無 2 秒空窗則永遠切不出 END。改回 2 秒直接切 + 清按鍵狀態。
3. **P5-6 切模式 countdown 不重置**：MED 倒數中切其他模式後切回 MED，會繼續用舊 `medCountdownStartMs`（切離的時間被算進倒數）。修法：切離 MED 時清 `medCountdownStartMs=0, medReminderActive=false, medOneMinWarningTriggered=false`；切入 MED + RUNNING 時呼叫 `startMedCountdown()`。

### 按鈕硬體註記

當前為有段式 toggle（按到底鎖定，沒按到底回彈），連續長按測試不便。待換 tactile momentary 後重跑：P0-6/7、P1-4/5、P2-7/8、P3-3/5/6/7、P4-2/3/5/6/8、P5-4。

### 後續修改（已 commit）

本 session 的 commit（push 完成）：
- `95d7511` fix: END 邊界誤觸 + 切模式 MED 倒數 bug
- `b09ed2c` refactor: 套用 PR review 5 項 MINOR/INFO
- `eed7fb1` feat: Phase 1 單元測試框架（computeTaskElapsedMs）
- `38ea2cb` test: 修正 computeTaskElapsedMs 多段暫停測試預期值
- `12fa47b` docs: Source of Truth 架構 + gap-analysis 對齊 Phase 2A~2E+3A
- `4fcfb36` feat: Phase 2 單元測試 Step A — MedCountdownDecision

---

# Phase 2 單元測試進度（2026-04-22）

依 `tasks/unit-test-plan.md` 分 3 phase；對齊 PM 規格 `docs/pm-dev-spec.md §4.2` 要求。

## Phase 1 — 低成本抽離 ✅ 完成

- [x] **computeTaskElapsedMs**（`lib/ems_logic/ems_time`）— 9/9 tests 綠
  - 注意：`elapsed_ms` 是韌體擴充欄位，**不在 PM 規格條款中**（見 `docs/gap-analysis.md` §A）
  - 已加 Source-of-Truth 註解於 `ems_time.h` 與 `test_task_elapsed.cpp`

## Phase 2 — 中度重構（進行中）

### MedCountdownDecision — Step A ✅ 完成（commit `4fcfb36`）

- [x] 新增 `lib/ems_logic/ems_countdown.h/cpp` — `decideMedCountdownAction()` 純函式
- [x] 新增 `test/test_countdown/test_med_countdown.cpp` — 12 tests 全綠
- [x] 對應 PM 規格 §4.2「4 分鐘給藥高提醒 ±50ms」

### MedCountdownDecision — Step B ⏳ 待處理（下次實機驗證時做）

- [ ] 修改 `main.cpp` 的 `updateMedCountdown()` 為 thin wrapper
  - 呼叫 `ems::decideMedCountdownAction()` 取得 action
  - 依 `fireWarn1Min` / `fireReminderStart` / `fireReminderRepeat` 三旗標執行 side effect：
    - `triggerBeep(MIN1_BEEP_PULSES, MIN1_BEEP_ON_MS, MIN1_BEEP_OFF_MS)` + `Serial.println("[MED] 1-min warning")`
    - `recordEvent(EVT_MEDICATION, SRC_SYSTEM, "reminder")` + `triggerBeep(EXPIRE_*)` + `triggerOledFlash` + `Serial.println("[MED] 240s expired...")`
    - `triggerBeep(EXPIRE_*)` + `triggerOledFlash` + `Serial.println("[MED] reminder repeat")`
  - 呼叫後 set 狀態：`medOneMinWarningTriggered=true` / `medReminderActive=true`, `lastReminderBeepMs=now` / `lastReminderBeepMs=now`
- [ ] **實機驗證**：跑一次完整 MED 4 分鐘倒數流程（P2-2~P2-6），確認無 regression
  - 剩 60 秒時 2 短嗶
  - 歸零時 3 嗶 + OLED 反色 + `GIVE MED!` 閃爍
  - 30 秒後重複提醒
  - 短按 BTN0 確認給藥，倒數重置

### 其他 Phase 2 候選（尚未排期）

- [ ] `computePauseCorrection`（抽自 `transitionState` PAUSE→RUNNING 的補正數學）
- [ ] `recordEvent` 欄位組裝（MAX_EVENTS 上限、event_id 遞增、extra_data 截斷）
- [ ] `cycleMode` / `nextCursor` / `prevCursor`（低難度純取模，可順便補）

## Phase 3 — 高風險（未排期）

- [ ] `ButtonFsm` 抽成 class（debounce + 長短按分類）
- [ ] `transitionState` 純化（side effect 外移）
- [ ] `VentMetronome` 6 秒節拍器（PM §4.2 要求，韌體尚未實作，待 PM 確認定位）

## 環境與工具現況

- PlatformIO `[env:native]` 已設（`platform=native`, `test_framework=unity`, `build_src_filter=-<*>`）
- Unity 2.6.1 已自動下載
- 測試目錄結構：`firmware/test/test_time/` + `firmware/test/test_countdown/`
- 執行：`pio test -e native -d firmware`（跑全部）或 `-f test_time` / `-f test_countdown`（指定）
