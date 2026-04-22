# EMS DoseSync 工程開發規格（拆分版）

---

## 一、Firmware（韌體規格）

> 目標：即時任務控制 + 精準時間記錄 + 穩定資料儲存 + BLE 通訊

---

### 1. 系統架構

**MCU**

- ESP32-S3
- RTOS：FreeRTOS（建議）

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

### 3. 狀態機設計

```
IDLE
  ↓（長按）
RUNNING
  ↓（短按）
PAUSE
  ↓（短按）
RUNNING
  ↓（長按）
END → IDLE
```

---

### 4. 核心功能模組

#### 4.1 任務控制

- 任務建立（Task ID）
- 計時器（毫秒級）
- 任務狀態保存

#### 4.2 節律提醒引擎

- 6秒節拍（CPR節律）
- 4分鐘高提醒（給藥）

**需求：**

- 誤差 < ±50ms
- 可切換模式（通氣 / 給藥）

#### 4.3 按鍵事件處理

| 按鍵 | 行為 |
|------|------|
| 主鍵短按 | 記錄事件 |
| 主鍵長按 | 開始/結束 |
| 上下鍵 | 切模式 |
| 電源鍵 | 開關機 |
| 靜音鍵 | 音效控制 |

#### 4.4 資料記錄系統

**RAM Buffer（Ring Buffer）**

- 大小：建議 100~500 events
- 防止寫入阻塞

**Flash 寫入策略**

- 批次寫入（每 5 秒）
- Wear leveling
- Fail-safe（斷電保護）

#### 4.5 資料結構（C Struct）

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

#### 4.6 BLE 通訊

**Role**

- 主機：Central
- 副機：Peripheral

**Service 設計**

- Device Info Service
- Sync Service
- Command Service
- Event Upload Service

#### 4.7 低功耗設計

- Light sleep（待機）
- 螢幕自動關閉
- BLE 廣播間隔調整

#### 4.8 錯誤處理

- Flash 寫入失敗 retry
- BLE reconnect
- RTC 校正

---

## 二、App（行動端規格）

> 目標：資料接收、分析、呈現、匯出

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
