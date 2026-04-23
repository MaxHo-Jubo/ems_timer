# EMS DoseSync 工程實作規格

版本：v1.1
日期：2026/04/23

---

## 📘 本文件定位

**本文件管什麼：**
- 工程實作細節：MCU 型號、RTOS、Task 分工、C struct 型別與 bytes
- **timing 常數**（`SHORT_PRESS_MAX_MS = 1500`、`LONG_PRESS_MIN_MS = 2000`、`VERY_LONG_PRESS_MIN_MS = 5000` 等）
- **精度實作**：節律 ±50ms 的實作方式（RTOS timer / 計時補正）
- BLE GATT Service / Characteristic / UUID 規格
- App 功能模組、SQL schema、REST API 端點
- Phase 開發順序、系統整合架構

**本文件不管什麼：**
- 使用者可感知的行為描述（啟動流程、狀態轉換、按鍵互動）→ 詳見 `pm-flow-spec.md`
- 硬體模組清單（電源 / 輸入 / 輸出 / 通訊 / 儲存 / 音訊 / 介面）→ 詳見 `pm-flow-spec.md §7`
- 主副機 BLE 配對流程 → 詳見 `pm-flow-spec.md §4`

當兩份文件在同一主題上有衝突時，以 **Source of Truth 標註的那一份為準**（每節標示 SoT 歸屬）。

### 版本紀錄

**v1.1（2026-04-23）**：文件整合 — 方案 B（職責邊界分離）
- 新增頂部「文件定位」聲明
- §3 狀態機、§4.2 節律引擎、§4.3 按鍵事件、§4.5 event_t、§4.6 BLE Service 明確標示 SoT
- §4.3 按鍵表加入 timing 常數值（對應韌體 `main.cpp` 宣告）

**v1.0（2026-04-22 及之前）**：PM 初版工程開發規格（拆分版）

---

## 一、Firmware（韌體規格）

> 目標：即時任務控制 + 精準時間記錄 + 穩定資料儲存 + BLE 通訊

---

### 1. 系統架構

**MCU**

- ESP32-S3
- RTOS：FreeRTOS（建議）

> 硬體模組清單（電源 / 輸入 / 輸出 / 通訊 / 儲存 / 音訊 / 介面）詳見 `pm-flow-spec.md §7`（SoT）。本文件不重複列表，僅在需要實作細節時引用。

---

### 2. 任務模組（Task Design）

| Task 名稱 | 功能 |
|-----------|------|
| Main Task | 狀態機（IDLE / RUNNING / PAUSE）|
| Timer Task | 節律提醒（6秒 / 4分鐘）|
| Input Task | 按鍵偵測（含 debounce）|
| BLE Task | 主副機通訊 |
| Storage Task | Flash 寫入 |
| UI Task | 顯示更新 |
| Audio Task | 蜂鳴 / 語音 |
| Power Task | 電源監控 |

---

### 3. 狀態機設計（**SoT**：timing 常數與轉換規則的權威來源）

```
IDLE
  ↓（長按 ≥ 2s）
RUNNING ─────────────┐
  ↓（長按 ≥ 2s）      │（超長按 ≥ 5s）
PAUSE ───────────────┤
  ↑（長按 ≥ 2s）      ↓
  └── RUNNING         END → IDLE（END_DISPLAY_MS = 2000ms 後自動返回）
```

**Timing 常數**（對應 `firmware/src/main.cpp`）：
- `SHORT_PRESS_MAX_MS = 1500`
- `LONG_PRESS_MIN_MS = 2000`
- `VERY_LONG_PRESS_MIN_MS = 5000`
- 1500~1999ms 為**灰色地帶**（既非短按也非長按），直接忽略
- 長按採 **fire-on-release** 模式（放開才結算 action），避免 5s 超長按觸發前誤轉狀態

> 2026-04-23 PM 確認：
> - **長按（≥ 2s）**：IDLE↔RUNNING↔PAUSE 之間循環切換（與 `pm-flow-spec.md §3` 一致）
> - **超長按（≥ 5s）**：RUNNING 或 PAUSE 直接結束任務（→ END → IDLE）
> - 兩段式長按設計避免救護現場誤觸結束正在進行中的任務

---

### 4. 核心功能模組

#### 4.1 任務控制

- 任務建立（Task ID）
- 計時器（毫秒級）
- 任務狀態保存

#### 4.2 節律提醒引擎（**SoT**：精度規格與實作常數的權威來源）

- **6 秒節拍（CPR 節律）— 僅通氣模式啟用**（PM 2026-04-23 確認）
- **4 分鐘高提醒（給藥）— 僅給藥模式啟用**

**精度需求：**

- 節拍/倒數誤差 < ±50ms
- 可切換模式（通氣 / 給藥），模式切換時節拍器停止並重置

**實作常數**（對應 `firmware/lib/ems_logic/ems_countdown.h`）：
- `DEFAULT_MED_COUNTDOWN_MS = 240000`（4 分鐘）
- `DEFAULT_MED_WARN_1MIN_MS = 60000`（剩 1 分鐘中途警示）
- `DEFAULT_MED_REMINDER_REPEAT_MS = 30000`（到時後每 30 秒重複提醒）
- VENT 6 秒節拍常數：Phase 2 抽 `VentMetronome` 模組時補齊（見 `docs/gap-analysis.md` Phase 2 待辦）

**實作方式**：純函式 `decideMedCountdownAction()` 決定本 cycle 要觸發的行為，主迴圈執行 side effect（beep / OLED flash / event record）。單元測試見 `firmware/test/test_countdown/test_med_countdown.cpp`。

#### 4.3 按鍵事件處理（**SoT**：timing 常數與判定流程的權威來源）

**Timing 常數**（對應 `firmware/src/main.cpp`）：

| 常數 | 值 | 用途 |
|------|-----|------|
| `DEBOUNCE_MS` | 80 | 下降緣 debounce |
| `SHORT_PRESS_MAX_MS` | 1500 | 短按上限 |
| `LONG_PRESS_MIN_MS` | 2000 | 長按下限（fire-on-release） |
| `VERY_LONG_PRESS_MIN_MS` | 5000 | 超長按下限（fire-on-hold，即刻觸發） |

**按鍵行為對照表**：

| 按鍵 | 行為 |
|------|------|
| 主鍵短按（< 1500ms） | 記錄事件（依模式：給藥 / 通氣 / 自訂） |
| 主鍵灰色地帶（1500~1999ms） | 忽略（避免短/長按混淆） |
| 主鍵長按（2000ms ≤ t < 5000ms） | 狀態循環：IDLE→RUNNING→PAUSE→RUNNING→PAUSE→…（放開時結算） |
| 主鍵超長按（≥ 5000ms） | 結束任務：RUNNING 或 PAUSE 直接進入 END（即刻觸發） |
| 上下鍵短按 | 切模式（給藥 / 通氣 / 自訂 / 設定，共 4 種） |
| 電源鍵 | 短按螢幕亮滅 / 長按開關機 |
| 錄音鍵長按（≥ 2000ms） | INMP441 錄音啟停，錄音檔存 SD 卡（Phase 1.5 到貨後啟用） |

**實作模式**：
- 長按採 **fire-on-release**：使用者放開時才根據累計 held 時間分派 short / long / gray
- 超長按採 **fire-on-hold**：達 5000ms 當下立即 fire，放開時 noop
- 目的：避免使用者按到 3 秒放棄（實際按超過 2s）時誤觸發 PAUSE，再按 5s 又觸發 END 的雙重事件

> 2026-04-23 PM 確認：
> - 只提供**錄音鍵**，**無靜音鍵**（原規格靜音鍵條款移除）
> - **模式 4 種**：給藥 / 通氣 / 自訂 / 設定（對齊 PM 流程圖）
> - **錄音鍵採長按觸發**（對齊 PM 流程圖），短按目前保留

#### 4.4 資料記錄系統

**RAM Buffer（Ring Buffer）**

- 大小：建議 100~500 events
- 防止寫入阻塞

**Flash 寫入策略**

- 批次寫入（每 5 秒）
- Wear leveling
- Fail-safe（斷電保護）

#### 4.5 資料結構（**SoT**：C struct 欄位、型別、bytes 的權威來源）

```c
typedef struct {
    uint32_t event_id;
    uint64_t timestamp;
    uint32_t elapsed_ms;    // 自任務起點扣除所有暫停後的毫秒數（2026-04-23 PM 採方案 A 正式納入）
    uint8_t  event_type;
    uint8_t  source;
    uint8_t  device_id;     // 單機階段固定 0；Phase 3 主副機時區分 0x01 主機 / 0x02 副機
    uint8_t  mode;
    char     extra_data[32];
    uint8_t  sync_flag;
} event_t;
```

> `elapsed_ms` 為 BLE NUS `dump` 協定承諾的欄位，App 依此繪製時間軸。韌體實作見 `firmware/lib/ems_logic/ems_time.h` `computeTaskElapsedMs()`。

#### 4.6 BLE 通訊（**SoT**：GATT Service 定義的權威來源）

> 主副機**配對與同步流程**（掃描 / 連線 / 心跳）詳見 `pm-flow-spec.md §4`。本節只定義 GATT Service 技術規格。

**Role**

- 主機：Central
- 副機：Peripheral

**GATT Service 列表**

- Device Info Service
- Sync Service
- Command Service
- Event Upload Service

（Characteristic UUID 與 payload schema 於 Phase 3 主副機實作時補齊。）

#### 4.7 低功耗設計

- Light sleep（待機）
- 螢幕自動關閉
- BLE 廣播間隔調整

#### 4.8 錯誤處理

- Flash 寫入失敗 retry
- BLE reconnect
- RTC 校正

---

## 二、App（行動端規格，**SoT**）

> 目標：資料接收、分析、呈現、匯出。`pm-flow-spec.md §6` 只寫 App 資料流程概觀，實作規格以本節為準。

---

### 1. 平台

- iOS / Android（Flutter 或 React Native 建議）

---

### 2. 功能模組

#### 2.1 裝置管理

- 掃描 BLE
- 配對
- 顯示狀態（電量 / 連線）

#### 2.2 即時監控

- 任務進行中畫面
- 節律顯示（動畫）
- 事件即時更新

#### 2.3 資料同步

- 自動同步
- 手動同步
- 斷線補傳

#### 2.4 資料儲存（Local DB）

- SQLite

**Table：events**

| 欄位 | 型別 |
|------|------|
| id | int |
| timestamp | datetime |
| type | int |
| mode | int |
| device_id | int |

#### 2.5 視覺化分析

- 時間軸（Timeline）
- CPR 節律圖
- 給藥間隔圖
- 事件統計

#### 2.6 匯出功能

- PDF 報告
- CSV
- EMS 系統格式

#### 2.7 UI 頁面

| 頁面 | 功能 |
|------|------|
| 首頁 | 裝置狀態 |
| 任務頁 | 即時監控 |
| 歷史紀錄 | 列表 |
| 分析頁 | 圖表 |
| 設定 | 裝置設定 |

---

## 三、Backend（後端系統規格）

> 目標：資料集中管理 + 分析 + 醫療整合

---

### 1. 架構

- Cloud-based（AWS / GCP）
- REST API + WebSocket

---

### 2. API 設計

#### 2.1 上傳資料

```http
POST /events/upload
```

#### 2.2 查詢任務

```http
GET /tasks/{id}
```

#### 2.3 查詢事件

```http
GET /events?task_id=xxx
```

---

### 3. 資料庫設計（SQL）

**Table: tasks**

| 欄位 | 型別 |
|------|------|
| id | UUID |
| start_time | datetime |
| end_time | datetime |
| device_id | string |

**Table: events**

| 欄位 | 型別 |
|------|------|
| id | UUID |
| task_id | UUID |
| timestamp | datetime |
| type | int |
| mode | int |
| extra | JSON |

---

### 4. 核心功能

#### 4.1 資料清洗

- 去重
- 時間排序
- 異常檢測

#### 4.2 分析引擎

- CPR 品質分析（節律準確率）
- 給藥間隔
- 反應時間

#### 4.3 報告生成

- 自動生成 PDF
- OHCA 報告格式

#### 4.4 使用者系統

- 帳號登入
- 權限控管（消防 / 醫院）

---

## 四、系統整合關係

```
[Device Firmware]
      ↓ BLE / Wi-Fi
[Mobile App]
      ↓ HTTPS
[Backend Server]
      ↓
[醫療系統 / 報表]
```

---

## 五、開發優先順序（非常重要）

### Phase 1（MVP）

- Firmware：任務 + 記錄 + BLE
- App：資料接收 + 顯示
- Backend：基本儲存

### Phase 2

- 分析圖表
- 報告輸出
- 雲端同步

### Phase 3

- 多裝置同步（主副機）
- Wi-Fi
- AI 分析（CPR 品質）

---

## 六、產品關鍵技術價值（非常重要）

這不是一般裝置，實際在做的是：

- 「OHCA 數據化紀錄系統」
- 「CPR 品質監測工具」
- 「救護流程數位轉型」
