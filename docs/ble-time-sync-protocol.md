# EMS DoseSync Pro — BLE 時間同步協定

> 補齊 `incremental-impl-plan.md §Phase 2E / §Phase 3B` 缺少的訊息層細節。
> 涵蓋 Dev-Phase 2（軟體對時）與 Dev-Phase 3（DS3231 + BLE 校正）兩階段共用協定。

---

## 0. 為什麼需要對時

事件資料模型有兩個時間欄位（見 CLAUDE.md「資料模型」）：

| 欄位 | 來源 | 失準後果 |
|---|---|---|
| `elapsed_ms` | 開機後 `millis()` 計時 | 不受對時影響 |
| `timestamp` | 系統時間（epoch ms）| **失準直接寫進歷史紀錄** |

裝置端沒有 GPS 也沒有 NTP，只能：
- **Dev-Phase 2**：每次 App 連線 → App 把手機當下 epoch 推給裝置（軟體對時）
- **Dev-Phase 3**：開機讀 DS3231 → 立即可寫事件；App 連線則順便校正 DS3231

兩階段 **訊息協定一樣**，差別只在裝置端拿到 epoch 後是否寫進 DS3231。

---

## 1. 傳輸層

沿用 `phase-f-web-validation-plan.md §3` 的 NUS：

| 角色 | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX（App → Device, Write） | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX（Device → App, Notify） | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

格式：UTF-8 JSON，**單筆訊息一行**（以 `\n` 結尾），不分片（時間同步訊息 < 200 bytes，遠低於 MTU 247）。

---

## 2. 訊息定義

### 2.1 `time_sync`（App → Device）

```json
{
  "type": "time_sync",
  "epoch_ms": 1713715200000,
  "tz_offset_min": 480,
  "req_id": "ts-001"
}
```

| 欄位 | 型別 | 必填 | 說明 |
|---|---|---|---|
| `type` | string | ✅ | 固定 `"time_sync"` |
| `epoch_ms` | uint64 | ✅ | UTC epoch 毫秒（App 送出當下時間，**不含**估計傳輸延遲） |
| `tz_offset_min` | int16 | ✅ | 時區偏移，分鐘為單位；台北 = `480`（UTC+8）。裝置端只用於 UI 顯示，**不影響** `timestamp` 儲存值（一律 UTC epoch） |
| `req_id` | string | 選填 | App 用於對映 ACK；長度 ≤ 16 字元；若省略則 ACK 也省略 |

### 2.2 `time_sync_ack`（Device → App）

裝置收到 `time_sync` 處理完成後立即送：

```json
{
  "type": "time_sync_ack",
  "req_id": "ts-001",
  "device_now_ms": 1713715200034,
  "applied": true,
  "source": "ble",
  "rtc_present": false
}
```

| 欄位 | 型別 | 說明 |
|---|---|---|
| `type` | string | 固定 `"time_sync_ack"` |
| `req_id` | string | 鏡射 request；若 request 沒帶則本欄位省略 |
| `device_now_ms` | uint64 | 裝置端**寫入時鐘後當下**的 epoch ms。App 用 `(收到 ACK 當下) - (送出 request 當下)` 估算 RTT，必要時做二次精修 |
| `applied` | bool | 是否成功更新系統時間（罕見情況：epoch_ms 顯然錯誤如 < 2024-01-01 會拒絕） |
| `source` | string | 對時來源：`"ble"`（Phase 2）/ `"ble+rtc"`（Phase 3 同時寫 DS3231）/ `"rejected"` |
| `rtc_present` | bool | `true` = 裝置上有 DS3231 並寫入成功；`false` = Phase 2 軟體對時或 RTC 寫入失敗 |

### 2.3 錯誤情境

App 端**不**主動送 `time_sync` 後又送一個 `time_sync_cancel`。要重對時就再送一筆 `time_sync` 即可（裝置端時間直接覆寫）。

裝置端拒絕條件（`applied: false, source: "rejected"`）：
- `epoch_ms < 1704067200000`（< 2024-01-01 UTC，明顯異常）
- `epoch_ms > 4102444800000`（> 2100-01-01 UTC，明顯異常）
- JSON 解析失敗 → 不回 ACK（App 端 timeout 後自行重送）

---

## 3. 觸發時機

| 觸發點 | 誰發起 | 備註 |
|---|---|---|
| BLE 連線建立 + Service Discovery 完成 | **App** | 必送，視為「對時 = 連線就緒」前提 |
| 使用者在 App 手動點「重新對時」 | **App** | 為長時間連線中校漂 |
| App 進前景且距上次對時 > 1 小時 | **App** | 可選，依產品決策 |
| 裝置開機 | ❌ 不主動發起 | 裝置不送 `time_sync_request`，被動等 App。Phase 3 改用 RTC 走 |

> 📌 Dev-Phase 3 後即使無 App，裝置仍能用 DS3231 寫正確 `timestamp`；App 來則順便校正 DS3231（誤差超過 30 秒才寫，避免每次連線都灼燒 EEPROM-less 寫入週期，DS3231 主時間暫存器無此問題但養成習慣）。

---

## 4. 裝置端行為

### 4.1 Dev-Phase 2（軟體對時，無 DS3231）

```
接收 time_sync
  └─ 驗證 epoch_ms 範圍
      ├─ 通過 → 記住 epoch_ms_offset = epoch_ms - millis()
      │        所有事件 timestamp = millis() + epoch_ms_offset
      │        回 ACK { applied: true, source: "ble", rtc_present: false }
      └─ 不通過 → 回 ACK { applied: false, source: "rejected" }
```

開機後**未對時前**寫入的事件：`timestamp` 欄位設為 `0`，App 端讀到 0 顯示「未對時」並用 `elapsed_ms` 拼相對時間。

### 4.2 Dev-Phase 3（DS3231 已整合）

```
開機
  └─ Wire.begin(42, 41); rtc.begin();
      ├─ rtc.lostPower() == false → 讀 rtc.now() 設 system time（離線可用）
      └─ rtc.lostPower() == true  → system time 維持 0（同 Phase 2 未對時態）

接收 time_sync
  └─ 驗證 epoch_ms 範圍
      ├─ 通過 → 設 system time（同 Phase 2）
      │        並且 |epoch_ms - rtc_epoch_ms| > 30000 才寫 rtc.adjust()
      │        回 ACK { applied: true, source: "ble+rtc", rtc_present: true }
      └─ 不通過 → 回 ACK { applied: false, source: "rejected", rtc_present: true }
```

> ⚠️ DS3231 寫入頻率限制：晶片本身無寫入週期限制，但若 BLE 一秒連線一次就寫一次屬無謂耗能。30 秒門檻 = 比 DS3231 自身 ±2ppm 漂移（每月 ~5 秒）寬鬆兩個量級，安全。

---

## 5. 誤差預算

| 誤差來源 | 數量級 | 處理 |
|---|---|---|
| App 取 epoch → BLE Write 延遲 | 20~80 ms | 大部分情境可接受；App 端可在 ACK 後算 RTT，必要時送修正包 |
| 裝置 BLE Write Callback → 寫 system time | < 5 ms | 忽略 |
| `millis()` 漂移（ESP32 內部 RC，無 TCXO） | ~20 ppm | Dev-Phase 2 內影響 timestamp，1 小時漂 72 ms |
| DS3231 漂移 | ±2 ppm | 每月 ~5 秒，可忽略 |
| 拔電後重連直到下一次 `time_sync` 之間 | 看實際 | Phase 2 → timestamp = 0；Phase 3 → 由 RTC 接手 |

**結論：** Dev-Phase 2 在 App 連線狀態下誤差 < 100 ms，足以滿足救護現場「秒級」紀錄精度。離線場景必須等 Dev-Phase 3 才合格。

---

## 6. 主副機架構（Phase 3C）

副機（Assistant）由主機（Master）走相同的 `time_sync` 訊息對時：

- 主機 Central 連上副機 Peripheral 後立即送 `time_sync`，欄位與 App 對裝置一致
- 副機回 `time_sync_ack`
- 副機不接受兩個來源（App 與主機）同時對時：以**最後一筆**為準

實作上副機端的 `time_sync` 處理邏輯與裝置接收 App 的處理完全共用，差別只在 BLE role。

---

## 7. App 端開發建議

1. **連線完成（onServicesDiscovered）立刻送 `time_sync`**，不要等使用者操作。等待過程中收到的 `event` 訊息 `timestamp` 為 0 屬正常。
2. **送 `time_sync` 時記下本地 `t_send_ms`**，收到 ACK 計算 `rtt = t_recv_ack - t_send_ms`。若 `rtt > 500 ms` 顯示警示。
3. **不要在 BLE 訊息中送本地時區字串**（如 `"Asia/Taipei"`），裝置端不解析 IANA 名稱，只認 `tz_offset_min`。
4. **不要假設裝置端時鐘可單調遞增**：對時可能把時鐘往回拉。App 端事件 list 用 `event_id`（單調遞增）排序，不要用 `timestamp`。

---

## 8. 驗收標準（Dev-Phase 2）

- [ ] 用 nRF Connect 對 RX 寫入 §2.1 範例 JSON（含 `\n`）後收到 §2.2 範例格式的 Notify
- [ ] 對時前 dump 出的事件 `timestamp = 0`，對時後新事件 `timestamp` 為真實 epoch
- [ ] App 端送 `epoch_ms = 1000000000000`（2001 年）→ 收到 `applied: false, source: "rejected"`
- [ ] 拔 USB 重開機 → dump 出的事件 `timestamp` 重新變 0（Phase 2 預期）

## 9. 驗收標準（Dev-Phase 3 新增）

- [ ] DS3231 已對過時的情境下重開機 → 不需 BLE 對時，新事件 `timestamp` 已正確
- [ ] DS3231 電池失憶（lostPower=true）情境下開機 → 行為與 Phase 2 一致（timestamp=0 等 App）
- [ ] BLE 對時與 DS3231 時間差 > 30s → ACK 回 `source: "ble+rtc"`；差 < 30s → 仍回 `"ble+rtc"`但實際未寫 DS3231（可在 device log 觀察）

---

## 10. 不在本 spec 範圍

- 對時前後的 UI 顯示（App / 裝置 OLED）
- `event` / `dump` 訊息格式（見 `incremental-impl-plan.md §2E` 與 `pm-dev-spec.md §14`）
- BLE 配對、加密、白名單（見 PM dev spec）
- 主副機角色切換流程（見 `incremental-impl-plan.md §3C`）

---

## 變更紀錄

| 日期 | 變更 | 對應 commit |
|---|---|---|
| 2026-05-15 | 初稿，補齊 Phase 2/3 對時訊息層 | TBD |
