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

### Phase 2B — BLE NUS 通訊（2026-04-18 編譯通過）
- [x] ArduinoJson 依賴
- [x] BLE NUS service（TX Notify + RX Write，MTU 升至 517）
- [x] 連線 hello 訊息（含 session_id）
- [x] `sync` 命令：App 下發 epoch ms 做軟體對時
- [x] `dump` 命令：批次回傳 events[] 陣列（dump_start / dump_item × N / dump_end）
- [x] `clear` 命令：清空事件
- [x] 按鈕按下即時 Notify 推送新事件
- [x] 連線/斷線自動管理 advertising 重啟
- [x] 編譯通過：RAM 14.0% / Flash 28.9%

## 待上機驗證

- [ ] **Phase 2 整合驗收**：驗收清單見 `tasks/phase2-acceptance.md`（可行性測試為主，完整驗收延後到換單行程按鍵）
  - nRF Connect 手機 App 當測試工具
  - 12 大項 + 3 項異常處理 + 4 項換鍵後補測

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

### Phase 4 — 整合測試與優化
- [ ] 電源方案選型（實測耗電後決定 18650 / LiPo / 乾電池）
- [ ] 外殼設計
- [ ] 長時間穩定性測試（連續 > 2 小時出勤情境）
- [ ] 最終驗收

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
