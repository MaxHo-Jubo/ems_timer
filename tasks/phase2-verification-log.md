# Phase 2 驗證進度快照

> 暫停時間：2026-04-18 ~16:30
> 下次繼續時讀這份文件恢復 context，然後接著 `tasks/phase2-acceptance.md` 的勾選。

## 已完成的驗證項目（2026-04-18）

### 環境與工具
- [x] 韌體編譯通過：RAM 14.0% / Flash 28.9%
- [x] PlatformIO 燒錄成功（ESP32-S3-DevKitC-1）
- [x] 測試工具：iPhone + nRF Connect
- [x] 確認 iOS 智慧型標點會把 `"` 變成 `" "` U+201C/U+201D（3 bytes UTF-8），ArduinoJson 會 reject
  - 解法：iOS 設定 → 一般 → 鍵盤 → 關閉「智慧型標點符號」，或從電腦複製貼上

### BLE 基礎
- [x] 廣播可見：手機 Scan 到 `EMS Timer`
- [x] Connect 成功
- [x] Service Discovery：手機認到 Nordic UART Service（6E400001）
- [x] 發現 TX Characteristic（6E400003）+ CCCD 描述符
- [x] 發現 RX Characteristic（6E400002）

### 修復過的 bug
- [x] **dump 命令造成斷線**：根因為 `onWrite` callback 在 BLE GATT task 執行，
      直接呼叫 `sendDump()`（含多次 notify + 每筆 10ms delay）阻塞 BLE 心跳，
      central 超時斷線。
  - 修復：callback 只 copy 到 `pendingCmdBuf` + 設 `pendingCmdReady` flag，
    實際處理搬到 `loop()` 的 main task context。
  - 驗證結果：寫入 `{"cmd":"dump"}` 後連線維持不斷，修復成功。

## 下次要繼續的驗證

### 立即要確認（銜接上次）
- [ ] **TX Notify 實際收到 dump 回應**
  - 回到 nRF Connect service 展開畫面
  - 確認 UART TX Characteristic 的 Notify 訂閱（三向下箭頭 icon）是**實心啟用**
  - 若未啟用：點一下訂閱，然後再送一次 `{"cmd":"dump"}`
  - 應收到 4 則 Notify 序列：
    ```
    {"t":"dump_start","count":N}
    {"t":"dump_item","idx":0,...}
    ...
    {"t":"dump_end"}
    ```
  - 順便貼 Serial log 確認裝置端有印 `[BLE] RX: {"cmd":"dump"}`

### 建議驗證順序（照 phase2-acceptance.md 章節）

1. **hello 訊息**（章節 2）— 連線建立時的第一個 Notify
2. **按鈕事件 Notify**（章節 4）— 按 BTN3 → 立即收到 `evt` Notify + OLED 切換到 `CPR Start`
3. **倒數計時**（章節 5）— 按 BTN1（Epi）→ 3 分鐘倒數 + 每分嗶一聲 + 結束 3 聲 + 畫面閃爍
4. **事件切換**（章節 6）— UP/DOWN 互切、連按同顆按鈕累計
5. **sync 對時**（章節 3）— `{"cmd":"sync","ts":<epoch_ms>}`
6. **dump**（章節 7）— 已部分驗證，確認 Notify 完整
7. **clear**（章節 8）— `{"cmd":"clear"}`
8. **容量上限 30 筆**（章節 9）
9. **斷線重連**（章節 10）
10. **蜂鳴器非 blocking**（章節 11）
11. **異常處理 A/B/C**

## 當前韌體狀態

- 檔案：`firmware/src/main.cpp`
- 已包含：Phase 2A 計時 + Phase 2B BLE NUS + BLE callback 阻塞修復
- `ENABLE_MIC_MONITOR = 0`（Phase 1.5 麥克風待換料）
- 未 commit（建議驗證完 Phase 2 所有項目後一次 commit）

## 下次恢復流程

```bash
# 1. 進專案目錄
cd /Users/maxhero/Documents/MaxHero/Projects/ems_timer/firmware

# 2. 重新燒錄（確保晶片上跑的是最新版）
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload -t monitor

# 3. 手機開 nRF Connect → 連 EMS Timer
# 4. 展開 Nordic UART Service
# 5. 訂閱 TX Notify
# 6. 照「下次要繼續的驗證」逐項跑
```

## 驗證中要留意的已知行為

- 開關式按鈕（尚未換單行程）按下不彈回，測切換要放開再按
- OLED 250ms 節流，秒數顯示可能慢半拍
- 倒數結束 500ms 閃爍週期，文字會短暫消失屬正常
- dump 每筆間隔 10ms，30 筆需約 300ms+
