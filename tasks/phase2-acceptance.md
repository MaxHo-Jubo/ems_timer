# Phase 2 整合驗收清單（2A 計時邏輯 + 2B BLE NUS）

> **狀態**：可行性測試。完整驗收待換單行程按鍵後再跑。
> 目前按鈕為開關式（toggle），按下不彈回，視覺/節奏感與實際救護情境有差異。

## 韌體資訊

| 項目 | 值 |
|------|---|
| 目標板 | ESP32-S3-DevKitC-1 |
| 主要檔案 | `firmware/src/main.cpp` |
| 編譯結果 | RAM 14.0%（45756/327680）/ Flash 28.9%（965045/3342336） |
| BLE 名稱 | `EMS Timer` |
| Service UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`（Nordic UART Service） |
| TX UUID (Notify, Device→App) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX UUID (Write, App→Device) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| ATT MTU | 517 |

## 測試工具

- **序列埠監看**：`pio device monitor -b 115200`
- **手機 App**：nRF Connect（iOS/Android 免費，Nordic 官方）
  1. Scan → 找 `EMS Timer` → Connect
  2. 展開 `Nordic UART Service`
  3. TX Characteristic：點三向下箭頭訂閱 Notify
  4. RX Characteristic：點鉛筆 icon → 選 UTF-8 → 輸入 JSON → Send

## 驗收項目

### 1. 開機與硬體自檢

- [ ] 開機 OLED 顯示「EMS Timer / Phase 2 Ready / Press any button」
- [ ] 開機蜂鳴器嗶 2 聲
- [ ] Serial 印 `[EMS Timer] Phase 2 boot`
- [ ] Serial 印 `Session ID: 0x........`（8 碼 hex）
- [ ] Serial 印 `[BLE] NUS ready, advertising as 'EMS Timer'`

### 2. BLE 連線建立

- [ ] 手機 nRF Connect Scan 畫面看得到 `EMS Timer`（訊號值合理）
- [ ] Connect 成功，Serial 印 `[BLE] client connected`
- [ ] 展開 Nordic UART Service 看得到 TX / RX 兩個 characteristic
- [ ] 訂閱 TX Notify 後，立即收到 hello：
  ```json
  {"t":"hello","ver":"phase2","sid":"0x........","count":0}
  ```

### 3. 時間同步（sync）

- [ ] 寫入 RX：`{"cmd":"sync","ts":1713416000000}`（用當下 epoch ms，Unix timestamp × 1000）
- [ ] Serial 印 `[BLE] RX: {"cmd":"sync",...}` 與 `time synced, epoch_ms=...`
- [ ] 收到 TX Notify ack：`{"t":"ack","cmd":"sync","offset":...}`

> 快速取 epoch ms：瀏覽器 console 打 `Date.now()`

### 4. 按鈕事件推送（evt Notify）

- [ ] 按 BTN3（CPR Start）→ 立即收到 Notify：
  ```json
  {"t":"evt","idx":0,"type":2,"label":"CPR Start","ts":<epoch>,"el":0}
  ```
- [ ] `ts` 欄位為 sync 後的真實 epoch ms（而非接近 0 的 offset=0 值）
- [ ] OLED 同步切換顯示 `CPR Start #1`，中間大字從 `00:00` 開始遞增
- [ ] 底部顯示 `Elapsed`

### 5. 倒數計時 + 區間提醒

- [ ] 按 BTN1（Epi）→ 收到 Notify，OLED 顯示 `Epi #N`
- [ ] 大字從 `03:00` 往下倒數，底部 `Countdown`
- [ ] 倒數到 `02:00`：短嗶 1 聲（100ms），Serial 印 `interval beep at 60s`
- [ ] 倒數到 `01:00`：再嗶 1 聲，Serial 印 `interval beep at 120s`
- [ ] 倒數到 `00:00`：連嗶 3 聲（每聲 200ms），畫面閃爍，底部變 `DONE!`
- [ ] Serial 印 `[TIMER] Epi countdown expired`

### 6. 事件切換

- [ ] DONE 狀態下按其他按鈕 → 立即切換新事件、新計時、閃爍停止
- [ ] UP 計時中按別按鈕 → 立即切換到新事件
- [ ] 連按同一按鈕 → `#` 計數遞增，計時每次從新起點算
- [ ] 每次切換都有對應的 Notify 事件推送

### 7. 批次讀取歷史（dump）

- [ ] 按幾個按鈕累積 3~5 筆事件
- [ ] 寫入 RX：`{"cmd":"dump"}`
- [ ] 收到 Notify 序列：
  ```
  {"t":"dump_start","count":N}
  {"t":"dump_item","idx":0,"type":...,"label":"...","ts":...,"el":...}
  {"t":"dump_item","idx":1,...}
  ...
  {"t":"dump_end"}
  ```
- [ ] `dump_item` 數量等於 `dump_start.count` 等於先前按鈕次數

### 8. 清空事件（clear）

- [ ] 寫入 RX：`{"cmd":"clear"}`
- [ ] Serial 印 `[BLE] events cleared`
- [ ] 收到 Notify ack：`{"t":"ack","cmd":"clear"}`
- [ ] OLED 回到待機畫面「Press any button」
- [ ] 下次按鈕事件 `idx` 從 0 重新開始

### 9. 容量上限（30 筆）

- [ ] 連按按鈕累積到 30 筆
- [ ] 按第 31 次：Serial 印 `[WARN] events[] full, dropping new event`
- [ ] 後續按鈕仍可切換 OLED 顯示但不累積到陣列
- [ ] `dump` 回傳筆數停在 30

### 10. 斷線重連

- [ ] App 端 Disconnect
- [ ] Serial 印 `[BLE] client disconnected` 與 `advertising restarted`
- [ ] 手機重新 Scan 應該再次看到 `EMS Timer`
- [ ] 重新 Connect 後再次收到 hello（`count` 欄位反映目前累積事件數）

### 11. 蜂鳴器非 blocking

- [ ] 倒數結束的 3 聲嗶響過程中，按其他按鈕仍能即時切換 OLED
- [ ] BLE Notify 推送不延遲
- [ ] 嗶響不卡住 loop（按鈕偵測不遲鈍）

### 12. 大訊息不分包

- [ ] `dump` 回傳的 `dump_item` 訊息完整（不會被 App 收到破碎的 JSON 片段）
  - 如果有分包跡象 → MTU 協商未成功，需排查

## 異常處理驗證

### A. JSON 格式錯誤
- [ ] 寫入 RX：`not a json` → Serial 印 `[BLE] parse err: ...`，裝置不當機
- [ ] 寫入 RX：`{"foo":"bar"}`（無 cmd 欄位）→ Serial 印 `missing 'cmd' field`

### B. 未知命令
- [ ] 寫入 RX：`{"cmd":"unknown"}` → Serial 印 `[BLE] unknown cmd: unknown`

### C. 未連線狀態的 notify
- [ ] 未連線時按鈕按下 → OLED 正常顯示，`sendEvent` 內部 early return 不當機

## 已知與預期行為

1. **開關式按鈕限制**：目前按鈕按下不彈回，需要手動放開才能再次按下。驗證切換流程時記得放開再按。
2. **OLED 節流 250ms**：秒數切換可能慢半拍，屬預期設計（避免 I2C 壅塞）。
3. **倒數結束閃爍**：500ms 週期，若 OLED 重繪對到 off 半週期，文字會短暫消失屬正常。
4. **dump 每筆 10ms 間隔**：30 筆 dump 約需 300ms + notify stack 處理時間，屬預期。

## 換單行程按鍵後應補測的項目

- [ ] 快速連按（100ms 內）→ debounce 200ms 是否正確擋下
- [ ] 連續切換不同按鈕的視覺流暢度
- [ ] 長按（> 1 秒）→ 僅觸發一次（下降緣偵測）
- [ ] 真實救護節奏模擬（CPR 開始 → 多次 Epi/Atropine → 電擊 → 插管 → 到院）

## 結論（待填）

- [ ] **Phase 2 可行性測試通過**：硬體 → 計時 → OLED → BLE → App 接收整條鏈路打通
- [ ] 發現的問題：
- [ ] 效能觀察（BLE notify 延遲、OLED 更新順暢度、蜂鳴器響應）：
- [ ] 下一步（Phase 1.5 麥克風 / Phase 3 App 開發）決策：
