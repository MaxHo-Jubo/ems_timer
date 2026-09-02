# EMS DoseSync Pro 工程實作規格

版本：v2.0
日期：2026-04-27
對齊 SoT：`docs/EMS_DoseSync_Pro_Prototype_V1.md`

---

## 📘 文件定位

**SoT 階層**

1. `docs/EMS_DoseSync_Pro_Prototype_V1.md`（PM 規格，所有產品行為的權威來源）
2. `docs/EMS_DoseSync_Pro_Prototype_V1_flow.html`（同等內容的視覺化版本）
3. **本文件**（工程實作細節：模組分工、API、資料結構、Phase 計畫）

**v2.0 重要變更（2026-04-27）**

- 完全對齊 V1 SoT；舊 `pm-flow-spec.md` 已 `git rm`
- 既有韌體（`MED_PHASE` / `VentMetronome` / 5 鍵 / 4 模式切換）視為 throwaway prototype，全部砍掉重寫
- App backend 章節完全刪除（V1 §2.2 排除雲端 / 帳號 / 即時院端同步）
- 範圍從「自由按鍵記錄 + 6 秒節拍」改為「OHCA 案件 lifecycle 為核心」

### 版本紀錄

- **v2.0（2026-04-27）**：對齊 V1 SoT 全面重寫；廢止 pm-flow-spec.md；砍 backend；重新分階段 Phase A~H
- **v1.2 及以前**：基於舊 pm-flow-spec 的工程規格（已廢止）

---

## 一、Firmware

### 1. 系統架構

| 項目 | 規格 |
|------|------|
| MCU | ESP32-S3 |
| RTOS | FreeRTOS |
| 顯示 | 2.4 吋螢幕（V1 §21.1） |
| 持久化 | SPI Flash + LittleFS |
| 通訊 | BLE 5.0 + Type-C CDC |
| 開發框架 | PlatformIO + Arduino |

> 完整硬體模組清單見 V1.md §21（SoT）。

---

### 2. 全域狀態機

最頂層狀態（對應 V1 §3 主功能表）：

```
BOOT → MAIN_MENU
         ├─ OHCA              （V1 §5–11）
         ├─ VENT_STANDALONE   （V1 §13）
         ├─ TRAINING          （V1 §15）
         ├─ HISTORY           （V1 §12）
         └─ SETTINGS          （V1 §19）
```

各模式為獨立子狀態機，回主功能表必經 `MAIN_MENU`。

---

### 3. OHCA 案件子狀態機

對應 V1 §5–11：

```
MAIN_MENU
   │ 短按主鍵
   ▼
OHCA_START_FLASH（顯示「案件開始 OHCA」1 秒）
   ▼
OHCA_WAIT_FIRST_EPI（待本機 EPI；允許即時/補登/電擊/Amio）
   │ EPI 二段確認
   ▼
OHCA_COUNTDOWN（4 分鐘倒數中）
   │ 剩餘 ≤ 60s
   ▼
OHCA_WARNING（剩 1 分鐘；每 15s 短嗶）
   │ 倒數歸零
   ▼
OHCA_ALARMING（請給藥；高優先連續 5s）
   │ 超過 5s 仍未確認
   ▼
OHCA_OVERTIME（顯示累計時間；每 15s 短提醒）
   │ EPI 二段確認 → 重新進入 OHCA_COUNTDOWN
   │
   │ 長按主鍵 ≥ 3s
   ▼
OHCA_END_CHECK（完成並結束 / 前往補登 / 返回案件）
   │ 確認結束
   ▼
OHCA_LOCKED（不可修改）→ OHCA_SUMMARY（案件總覽）
```

子狀態與頂層狀態機正交，僅在 OHCA 子樹內生效。

---

### 4. EPI 倒數引擎（精度 ±50ms）

**新檔**：`firmware/lib/ems_ohca/ems_ohca_countdown.h`

**常數**

```c
#define EPI_CYCLE_MS                  240000  // 4 分鐘
#define EPI_WARNING_MS                 60000  // 剩 1 分鐘
#define EPI_ALARM_INITIAL_MS            5000  // 連續發報 5 秒
#define EPI_ALARM_REMIND_INTERVAL_MS   15000  // 超時後每 15s 短提醒
#define EPI_WARN_BEEP_INTERVAL_MS      15000  // 預警每 15s 短嗶
```

**API（純函式）**

```c
typedef enum {
  OHCA_PHASE_WAIT_FIRST_EPI,
  OHCA_PHASE_COUNTDOWN,
  OHCA_PHASE_WARNING,
  OHCA_PHASE_ALARMING,
  OHCA_PHASE_OVERTIME,
} ohca_phase_t;

typedef struct {
  bool        buzz_short;            // 短嗶
  bool        buzz_alarm_continuous; // 高優先連續發報
  bool        led_yellow_slow;       // 黃燈慢閃
  bool        led_red_fast;          // 紅燈快閃
  bool        led_red_slow;          // 紅燈慢閃
  bool        vibrate;
  bool        screen_flash;
  const char* display_label;         // "請準備給藥" / "請給藥" / NULL
  uint32_t    display_remaining_ms;  // 倒數剩餘；超時則為自上次 EPI 起算累計
} ohca_output_t;

ohca_output_t decideOhcaOutput(ohca_phase_t phase,
                                uint32_t since_last_epi_ms,
                                uint32_t now_ms);
```

純函式可被單元測試覆蓋（沿用既有 PlatformIO native 環境）。

---

### 5. 兩段確認模組

V1 §6.2 / §7.2 / §8.2 共用模式。

**新檔**：`firmware/lib/ems_ohca/ems_two_step_confirm.h`

```c
typedef struct {
  uint32_t first_press_ms;
  bool     armed;
  uint32_t timeout_ms;     // 預設 5000
} two_step_confirm_t;

bool twoStepConfirm_press(two_step_confirm_t* s, uint32_t now_ms);
// 第一次按 → armed=true，回 false（顯示確認畫面）
// 第二次按（≤ timeout）→ armed=false，回 true（成立）
// 逾時自動清除 armed
```

每個事件型別獨立持有 `two_step_confirm_t` instance：
- `confirm_epi_t`
- `confirm_shock_t`
- `confirm_amio_t`

EPI 到期警報（ALARMING / OVERTIME）下，**主鍵只消音不建立紀錄**；EPI 鍵才能進入確認畫面（V1 §6.7）。

---

### 6. 補登資料模型

V1 §9。

```c
typedef enum {
  // ===== 0x00~0x7F：V1 核心事件段位（封版固定） =====
  EVT_EPI_LOCAL          = 0x01,  // 本機 EPI（有時間戳）
  EVT_SHOCK_LOCAL        = 0x02,  // 本機電擊（有時間戳）
  EVT_AMIODARONE         = 0x03,
  EVT_EPI_PRE_HANDOVER   = 0x04,  // 接手前 EPI（無時間戳）
  EVT_EPI_PURE_SUPP      = 0x05,  // 純補登 EPI（無時間戳）
  EVT_SHOCK_PRE_HANDOVER = 0x06,
  EVT_SHOCK_PURE_SUPP    = 0x07,

  // ===== 0x80~0xBF：未來擴充藥物紀錄段位（V1 §8A.5） =====
  // EVT_DRUG_D50W       = 0x80,
  // EVT_DRUG_TXA        = 0x81,
  // EVT_DRUG_ATROPINE   = 0x82,
  // ...

  // ===== 0xC0~0xDF：未來擴充感測器事件段位 =====
  // EVT_CO_ALERT        = 0xC0,  // CO 濃度警報
  // EVT_CO_READING      = 0xC1,  // CO 數值定期記錄
  // ...

  // ===== 0xE0~0xEF：未來擴充錄音相關事件段位 =====
  // EVT_RECORD_START    = 0xE0,
  // EVT_RECORD_STOP     = 0xE1,
  // ...

  // ===== 0xF0~0xFF：保留給系統事件 =====
  // EVT_SYS_LOW_BATTERY = 0xF0,
} ems_event_type_t;

typedef struct {
  uint32_t          event_id;
  ems_event_type_t  type;
  uint64_t          timestamp_ms;     // 補登事件 = recorded_at（不是實際發生時間）
  uint32_t          elapsed_ms;       // 自案件起點扣除 PAUSE
  uint8_t           count;            // 補登批量（1~5 / 1~3）；本機事件固定 1
  bool              actual_time_null; // 補登事件 = true
} ems_event_t;
```

**補登成立後韌體層拒絕修改 / 撤銷 / 刪除**（V1 §9.7）。

**event_type 編號段位規範：**
- V1 韌體只實作 0x01~0x07 核心段位
- 0x80~0xFF 為擴充保留段位，App 端解析時遇到未知 type 應 fallback 為「未支援事件」並保留原始 byte，不可丟棄
- BLE NUS JSON 與 LittleFS `/sessions/*.json` 序列化時，type 欄位輸出為十進位整數（如 `"type": 1`），App 端依編號段位判讀
- 新增事件類型時，於本表登記後同步更新 App 解析表與 V1 §11/§17 案件總覽顯示規則

---

### 7. 6 秒通氣節奏

#### 7.1 獨立模式（V1 §13）

**新檔**：`firmware/lib/ems_vent/ems_vent_metronome.h`

進入後立即啟動 1~6 循環：

| 秒 | 蜂鳴 | LED | 畫面 |
|----|------|-----|------|
| 1 | 加強提示音 | 紅閃 | 反紅 |
| 2~6 | 短提示音 | — | 數字 2~6 |

通氣音量 0~5 獨立記憶（NVS key 與系統音量分開）。
靜音時：聲音關閉，畫面反紅 + LED 紅閃保留（V1 §13.10 靜音規則）。

#### 7.2 OHCA 中切入（V1 §14）

OHCA 中按返回鍵 → 快速功能選單 → 進入通氣節奏。
EPI 倒數背景持續；畫面同時顯示 `EPI MM:SS` 小提示。

**EPI 高優先打斷**（V1 §14.8 EPI 到期優先權）：

```c
if (ohca_phase == OHCA_PHASE_ALARMING && in_vent_overlay) {
  vent_metronome.silence();          // 立即停止通氣音
  ui.switch_to(OHCA_ALARM_SCREEN);
  // 必須完成 EPI 二段確認才能返回
  // 確認後彈出「返回通氣節奏？」
}
```

---

### 8. Training 模式（V1 §15）

倒數可選 30s / 1min / 4min。
全程顯示「訓練模式」浮水印。
重置功能僅 Training 提供（OHCA 不提供）。
結束後選擇保存 / 不保存：
- 保存 → 寫入 Training 紀錄區（最近 20 筆，FIFO）
- 不保存 → 不占用儲存

---

### 9. 案件總覽 + Timeline（V1 §11）

案件鎖定後產生：

```c
typedef struct {
  uint32_t epi_total;
  uint32_t epi_local;
  uint32_t epi_pre_handover;
  uint32_t epi_pure_supp;
  uint64_t first_epi_local_ms;       // 0 = 無
  uint64_t last_epi_local_ms;
  uint32_t shock_total;
  uint32_t shock_local;
  uint32_t shock_pre_handover;
  uint32_t shock_pure_supp;
  uint64_t last_shock_local_ms;      // 不顯示「第一次本機電擊」
  uint32_t amio_total;
  uint64_t last_amio_ms;
  bool     synced_to_app;
  uint64_t synced_at_ms;
} ohca_case_summary_t;
```

Timeline 補登事件時間欄位顯示 `-`。

---

### 10. 持久化

**LittleFS partition 規劃**

| 區域 | 上限 | 覆蓋策略 |
|------|------|---------|
| `/cases/ohca/*.dat` | 50 | FIFO，覆蓋最舊 |
| `/cases/training/*.dat` | 20 | FIFO |
| `/config/system.json` | 1 | 覆寫 |
| `/config/device_name.txt` | 1 | App 寫入 |
| `/sync_state.json` | 1 | Case ID 同步狀態索引 |

**裝置端不提供刪除 API**，僅 Type-C 管理工具可清除（V1 §18.2 / §18.3）。

---

### 11. 按鍵事件處理

V1 §4.1：主鍵 / 返回鍵 / 上鍵 / 下鍵 / EPI 鍵 / 電擊鍵 / Power / 錄音鍵（V1 不啟用）。

**Timing 常數**（最終由實機調整）

| 常數 | 預設 | 用途 |
|------|------|------|
| `DEBOUNCE_MS` | 30 | 下降緣 debounce |
| `LONG_PRESS_END_MS` | 3000 | OHCA 主鍵長按 → 結束前檢查 |
| `LONG_PRESS_DRUG_MENU_MS` | 1000 | EPI 鍵長按 → 藥物選單 |
| `LONG_PRESS_SHOCK_SUPP_MS` | 1000 | 電擊鍵長按 → 電擊補登 |
| `LONG_PRESS_POWER_MS` | 2000 | Power 開關機 |
| `CONFIRM_TIMEOUT_MS` | 5000 | 兩段確認 timeout |

**核心口訣**（V1 §4.2）
- 即時事件按事件鍵
- 選單確認按主鍵
- 返回取消按返回鍵

---

### 12. 顯示模組

OLED 畫面分類（對應 V1 §6.4 / §6.7 / §11 / §13.6 等）：

- `IDLE`：主功能表
- `OHCA_*`：倒數 / 預警 / 請給藥 / 超時
- `CONFIRM_DIALOG`：兩段確認彈窗
- `OHCA_SUMMARY` / `OHCA_TIMELINE`
- `VENT_BEAT_1~6`（含 OHCA 中切入版本，含 EPI 小提示）
- `TRAINING_*`（含「訓練模式」浮水印）
- `SETTINGS_*`

統一畫面框架 + 狀態驅動 render。

---

### 13. 蜂鳴 / LED / 震動輸出

| 場景 | 蜂鳴 | LED | 震動 |
|------|------|-----|------|
| EPI WARNING | 每 15s 短嗶 | 黃慢閃 | — |
| EPI ALARMING | 高優先連續 5s | 紅快閃 | 持續 |
| EPI OVERTIME | 每 15s 短嗶 | 紅慢閃 | — |
| 通氣第 1 秒 | 加強提示音 | 紅閃 | — |
| 通氣 2~6 秒 | 短提示音 | — | — |
| 確認成立 | 成功音 1 聲 | 綠閃 | — |
| 低電量 | 不發聲 | 電量圖示閃 | — |

通氣音量 = 0：蜂鳴關閉，畫面 + LED 保留（V1 §13.10 靜音規則）。

---

### 14. BLE 通訊

#### 14.1 Phase F 過渡：NUS + JSON

延用既有 `firmware/lib/ble_nus`。
單案同步 payload：

```json
{
  "type": "case_sync",
  "case_id": "uuid-v4",
  "mode": "ohca",
  "device_name": "安康91",
  "device_id": "DSP-0001",
  "fw_version": "v1.0.0",
  "started_at_ms": 1745740800000,
  "ended_at_ms":   1745741700000,
  "events": [
    { "event_id": 1, "type": 0, "timestamp_ms": ..., "elapsed_ms": ..., "count": 1, "actual_time_null": false },
    { "event_id": 2, "type": 3, "timestamp_ms": ..., "elapsed_ms": ..., "count": 2, "actual_time_null": true  }
  ],
  "summary": { ... ohca_case_summary_t ... }
}
```

#### 14.2 Phase F+ 自訂 GATT（可選）

V1 §16 配對碼流程：

| Service | Characteristic | Property |
|---------|----------------|----------|
| Pairing Service | `pair_code` | read / notify |
| Pairing Service | `pair_status` | notify |
| Case Sync Service | `case_meta` | read |
| Case Sync Service | `case_chunk` | notify |
| Case Sync Service | `sync_ack` | write |
| Device Info | `name` | read / write |
| Device Info | `fw_version` | read |
| Device Info | `battery` | read / notify |

UUID 與 payload schema 在 Phase F 落地時補齊。

#### 14.3 配對碼

- 4 位數字
- 有效期 120 秒（V1 §16.4）
- 逾時必須重新產生
- 同案件重複同步：App 端 Case ID 去重，覆蓋而非新增（V1 §16.8）

---

### 15. 系統設定

對應 V1 §19。

| 設定 | 範圍 | 持久化 | 預設 |
|------|------|--------|------|
| 螢幕亮度 | 1~5（最低 1） | NVS | 3 |
| 系統音量 | 1~5（不可靜音） | NVS | 3 |
| 通氣音量 | 0~5（0 = 靜音） | NVS | 3 |
| 裝置名稱 | App 寫入 | LittleFS | 「未命名」 |
| 韌體版本 | read-only | 編譯時嵌入 | — |

**恢復預設值**（V1 §19.6）：清除前三項。**不清** 裝置名稱 / 案件 / Training / 同步狀態。

---

### 16. 電源管理

V1 §20：

- OHCA / 通氣模式螢幕常亮，**禁止**自動進入 deep sleep
- 案件中**不強制**自動關機，只提示低電量
- 插拔 Type-C **不重啟、不中斷** case

---

## 二、App（行動端）

對齊 V1 §17。

### 1. 平台

iOS / Android（暫定 React Native）。

### 2. 範圍

**做：**
- BLE 配對（輸入 4 位配對碼）
- 接收單案資料
- 4 頁籤顯示（交班摘要 / 完整總覽 / Timeline / 備註）
- 一鍵複製
- 案件刪除（僅手機端，不影響裝置）
- OHCA / Training 分開列表

**不做（V1 §2.2 明令排除）：**
- 雲端同步
- 帳號登入
- 病患個資輸入
- 即時院端同步
- PDF 報表（V1 排除，未來再議）

### 3. 本地儲存

SQLite。

```sql
CREATE TABLE cases (
  case_id     TEXT PRIMARY KEY,
  mode        TEXT,            -- 'ohca' / 'training'
  device_name TEXT,
  device_id   TEXT,
  fw_version  TEXT,
  started_at  INTEGER,
  ended_at    INTEGER,
  synced_at   INTEGER,
  raw_json    TEXT             -- 原始 payload，避免欄位變更
);

CREATE TABLE notes (
  case_id              TEXT PRIMARY KEY,
  hospital_arrival_at  INTEGER,
  rosc                 INTEGER,    -- bool
  handover_to          TEXT,
  special_situation    TEXT,
  other                TEXT,
  FOREIGN KEY (case_id) REFERENCES cases(case_id)
);
```

備註欄位**不回寫裝置**（V1 §17.5）。

### 4. Case ID 去重

App 端以 `case_id` 為主鍵；重複同步時覆蓋。

---

## 三、Type-C 電腦端管理工具

V1 §18.3。

獨立 Python 或 Electron 工具，連線 ESP32 USB CDC：

- 列出案件
- 匯出（CSV / JSON）
- 二次確認後清除案件區
- **不可修改**既有案件內容

協定可重用 NUS payload 格式（透過 USB CDC 傳輸而非 BLE）。

---

## 四、實作階段

### Phase A — OHCA 核心（最高優先）

**目標**：跑完一個 case，從待本機 EPI → 倒數 → 預警 → 警報 → 超時 → 結束鎖定。

**範圍**
- 重寫主功能表 + OHCA 子狀態機
- EPI 4 分鐘倒數引擎（純函式 + 單元測試）
- EPI / 電擊兩段確認
- OLED 主畫面 / 倒數 / 預警 / 警報 / 超時
- 蜂鳴 / LED / 震動三模態輸出對齊 V1 §6.6

**驗收**
- `decideOhcaOutput()` 單元測試 ≥ 30 案例全綠
- 實機跑完 1 case 並結束鎖定，不可再新增事件
- EPI 到期 5 秒未確認 → 自動進入 OVERTIME，顯示累計時間

**廢止**：既有 `MED_PHASE` enum、`ems_countdown.cpp`、`vent_metronome.cpp` 全砍。

---

### Phase B — 補登 + Amiodarone + 案件總覽

- 長按 EPI 鍵 → 藥物選單（補登 EPI / Amiodarone）
- 長按電擊鍵 → 電擊補登（接手前 / 純補登）
- 補登次數選擇 UI（1~5 / 1~3）
- Amiodarone 兩段確認
- 結束前檢查（V1 §10）
- 案件總覽（V1 §11）+ Timeline（補登時間欄 = `-`）

**驗收**：補登成立後不可撤銷；總覽欄位完整對齊 V1 §11。

---

### Phase C — 6 秒通氣節奏

- 獨立模式（從主功能表進入）
- 通氣音量 0~5 獨立 NVS
- OHCA 中快速功能進入（返回鍵）
- EPI 高優先打斷邏輯
- EPI 完成後「返回通氣節奏？」詢問

**驗收**：EPI ALARMING 觸發時通氣音 ≤ 50ms 內停止。

---

### Phase D — Training 模式

- 30s / 1min / 4min 倒數可選
- 全程「訓練模式」浮水印
- 重置 / 結束 / 保存選項
- 保存進 Training 區（不混入 OHCA）

**驗收**：Training 與 OHCA 列表完全分離。

---

### Phase E — 持久化 + 歷史紀錄

- LittleFS partition 規劃
- 50 OHCA + 20 Training FIFO 覆蓋
- 從歷史紀錄重新進入案件總覽
- 重啟後資料不遺失

**驗收**：寫滿 51 筆 OHCA 後最舊一筆自動覆蓋；重啟後總覽完整。

---

### Phase F — App 配對碼同步

- 配對碼 4 位數 + 120s TTL
- 單案傳輸（NUS + JSON 過渡）
- Case ID 去重
- 已同步 / 未同步狀態
- 中斷重試 = 整筆重傳

**驗收**：同案件同步 2 次，App 端不重複；中斷後重試成功。

---

### Phase G — 系統設定 + Type-C 工具

- 螢幕亮度 / 系統音量 / 通氣音量 NVS ✅ 已完成
- 裝置名稱由 App 寫入 ✅ 已完成
- **裝置資訊畫面**（2026-08-30 擴充，原「韌體版本 read-only」升級為完整畫面，並併入原 Impl-Phase H
  Task 14 的電池／充電狀態接線）：名稱 / 型號 / 序號 / 韌體版本 / 電池 % / 充電狀態，
  SoT V1 §19.7。🟡 **程式碼與 review 全數完成，僅待上機驗收**（2026-09-02，
  `feat/phase-g-device-info` 分支 HEAD `62bf11f`）——6 個 task 完成後，SDD 流程最後
  一步「全分支整合 review」抓到 1 個 CRITICAL（`drawDeviceInfo()`／`drawBatteryInfo()`
  共用的 `drawCenteredText()` 沒還原繪圖狀態，畫面文字錯色＋裁切）+ 4 個 Important，
  已全數修復並通過 repo Tier 3 codex 兩輪 re-review（6/6 面向、0 CRITICAL），詳見
  `docs/superpowers/phase-g-device-info-handover.md`「✅ 已解決」段落。下一步是該文件
  §3-B 的上機驗收清單（需要實體硬體），沒有剩餘的程式碼工作。
- Type-C 管理工具 MVP（列案件 / 匯出 / 清除）未開工

**驗收**：恢復預設不影響案件 / Training / 裝置名稱。

---

### Phase H — 電源管理

- OHCA / 通氣螢幕常亮
- 邊充邊用測試
- 低電量警告（不強制關機）
- Type-C 插拔不中斷案件

**驗收**：插拔 Type-C 期間 OHCA 計時連續，不丟事件。

---

## 五、現有韌體：廢棄 vs 保留

### Phase A 開工時刪除

- `firmware/lib/ems_logic/ems_countdown.{h,cpp}` — 舊三階段藥物倒數
- `firmware/lib/ems_logic/vent_metronome.{h,cpp}` — 舊 6 秒節拍器
- `MED_PHASE_*` enum 與相關常數
- 5 鍵 / 4 模式切換邏輯（給藥/通氣/自訂/設定）
- `firmware/test/test_countdown/test_med_countdown.cpp` — 舊測試
- `tasks/` 內舊版煙霧測試清單（A~F 27 項，不適用新狀態機）

### 保留可重用

- `firmware/lib/ems_logic/ems_time.h` `computeTaskElapsedMs()`
- `firmware/lib/ble_nus` — Phase F 過渡使用
- 按鍵 debounce / fire-on-release 框架（timing 常數重新定義）
- PlatformIO native 測試環境
- OLED 驅動 / I2C bus / 蜂鳴器 PWM 驅動

### 新建模組

- `firmware/lib/ems_ohca/` — OHCA 子狀態機 + 倒數 + 兩段確認
- `firmware/lib/ems_supp/` — 補登模型
- `firmware/lib/ems_persist/` — LittleFS 案件儲存
- `firmware/lib/ems_pairing/` — 配對碼產生 / 驗證
- `firmware/lib/ems_vent/` — 6 秒通氣節拍器（重寫）
- `firmware/lib/ems_training/` — Training 模式
