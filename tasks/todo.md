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
