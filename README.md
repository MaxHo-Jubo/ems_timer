# EMS Timer / EMS DoseSync — 救護計時器

給救護人員使用的手持計時裝置。長按主鍵啟動任務，短按記錄給藥 / 通氣事件，裝置本機儲存完整任務紀錄（LittleFS），事後可透過藍牙傳輸至手機 App。

> 產品對外名稱 **EMS Timer**；PM 規格層使用 **EMS DoseSync**。兩者指同一個專案。

---

## Source of Truth 排序（權威順序）

本專案文件分層，越上層越權威；底層文件不得與上層衝突。

**兩份 PM 規格文件採職責邊界分離（2026-04-23 整合，方案 B）**：`pm-flow-spec.md` 管使用者可感知的行為；`pm-dev-spec.md` 管工程實作常數與型別。當兩份在同一主題上有衝突時，以各節標註的 **SoT** 為準。

| 優先 | 層級 | 文件 | 說明 |
|:---:|------|------|------|
| 1a | 行為規格 | [`docs/pm-flow-spec.md`](docs/pm-flow-spec.md) | 產品行為規格 — 啟動流程、狀態轉換、按鍵互動（含秒數 / 精度）、硬體模組清單（**SoT**：§7 硬體模組） |
| 1b | 工程實作規格 | [`docs/pm-dev-spec.md`](docs/pm-dev-spec.md) | 工程實作規格 — timing 常數、C struct、GATT Service、SQL schema、REST API、FreeRTOS Task 分工（**SoT**：§3 狀態機 timing、§4.2 節律精度、§4.3 按鍵常數、§4.5 event_t、§4.6 GATT）|
| 2 | 差距分析 | [`docs/gap-analysis.md`](docs/gap-analysis.md) | PM 規格 vs 當前韌體實作的差距盤點 |
| 2 | 執行計畫 | [`docs/incremental-impl-plan.md`](docs/incremental-impl-plan.md) | 對齊 PM 規格的增量實作計畫 |
| 3 | AI 協作規則 | [`CLAUDE.md`](CLAUDE.md) | Phase 設計決策、資料模型、開發約定 |
| 4 | 執行追蹤 | [`tasks/todo.md`](tasks/todo.md) | 開發進度、測試勾選清單 |
| 4 | 執行追蹤 | [`tasks/unit-test-plan.md`](tasks/unit-test-plan.md) | 單元測試規劃（Phase 1 起步） |
| 5 | 對外介紹 | `README.md`（本檔） | 現況快速導覽；規格細節以上述文件為準 |

若發現 README 與 `docs/` 不一致，以 `docs/` 為準。README 定期同步（最新：2026-04-22）。

---

## 當前功能（Phase 2A~2E + 3A 完成，已 commit）

- **5 鍵狀態機**：IDLE / RUNNING / PAUSE / END 四態，主鍵短按/長按分流
- **四種操作模式**：MED（給藥）/ VENT（通氣）/ CUSTOM / SETTINGS（後兩者 Phase 3 補強）
- **MED 給藥倒數**：240 秒倒數、剩 60 秒中途警示、到時 3 嗶 + OLED 整螢幕反色、每 30 秒重複提醒、短按確認給藥（記錄 "epi"）重置倒數
- **藥物子選單**：6 種藥物（Amiodarone / TXA / D50 / Atropine / Adenosine / Naloxone），8 秒無操作自動關閉
- **OLED 畫面**：狀態驅動（IDLE / RUNNING 大字倒數 / PAUSED 閃爍 / Saved）
- **LittleFS 持久儲存**：任務結束自動寫 `/sessions/<epoch>.json`，開機掃描列出未同步檔
- **事件紀錄**：單任務最多 100 筆，含 event_id / timestamp / elapsed_ms / source / mode / extra_data / sync_flag
- **按鍵 debug log**：完整按鍵生命週期 Serial 輸出（PRESS / RELEASE / GRAY / LONG fired / debounce reject）

---

## 硬體架構

| 元件 | 型號 / 規格 | 介面 |
|------|------------|------|
| 主控板 | ESP32-S3-DevKitC-1 | — |
| 螢幕 | SSD1306 OLED 0.96" | I2C（SDA/SCL = GPIO 42/41） |
| 按鈕 | 有段式 × 5（計畫換 tactile momentary） | GPIO INPUT_PULLUP |
| 蜂鳴器 | 主動式蜂鳴器 | GPIO 14 |
| 震動馬達 | 1027 硬幣馬達 + S8050 NPN | GPIO 16（`ENABLE_VIBRATION=0` 預設關，OLED 反色取代視覺） |
| 麥克風 | INMP441 數位麥克風 | I2S（SCK/WS/SD = 40/39/38，Phase 1.5 待換模組） |
| 儲存 | LittleFS（內建 Flash）| — |

## 按鍵配置（5 鍵）

| 按鍵 | 常數名 | GPIO | 短按（<1500ms） | 長按（≥2000ms） |
|------|--------|------|----------------|----------------|
| 正下方大鍵 | BTN_PRIMARY | 4 | 依 state + mode 分派：MED 提醒時 = 確認 epi、MED 一般 = 開藥物選單、VENT = 記通氣事件、PAUSE = 繼續任務 | 任務狀態轉換：IDLE→RUNNING / RUNNING→PAUSE / PAUSE→END |
| 左側上鍵 | BTN_UP | 5 | 選單游標上移 / 模式反向切換 | noop |
| 左側下鍵 | BTN_DOWN | 6 | 選單游標下移 / 模式正向切換 | noop |
| 右側電源鍵 | BTN_POWER | 7 | log 輸出（screen wake 待實作） | log 輸出（shutdown 待實作） |
| 左上錄音鍵 | BTN_RECORD | 15 | log 輸出（INMP441 到貨後啟用） | log 輸出 |

> 門檻：SHORT <1500ms、LONG ≥2000ms、1500~2000ms 為灰色地帶忽略、DEBOUNCE 80ms、END 狀態下 2 秒自動回 IDLE。

## BLE 通訊協定（Phase 2B 已驗證，Phase 2/3A 重構後升級為 phase2c）

服務：Nordic UART Service（NUS），裝置為 Peripheral。

| 命令（App → 裝置） | 說明 |
|-------------------|------|
| `{"cmd":"sync","ts":<epoch_ms>}` | 時間同步（裝置用軟體 epoch 補正）|
| `{"cmd":"dump"}` | 批次取得所有事件紀錄 |
| `{"cmd":"clear"}` | 清空事件陣列 |

事件欄位（dump 回傳）：`event_id` / `timestamp`（epoch 秒 + ms 小數）/ `elapsed_ms` / `event_type` / `source` / `mode` / `extra_data` / `sync_flag`

> **PM 規格差異注意**：PM 規格的 `event_t` 有 `device_id` 但韌體目前無（Phase 3 主副機時補）；韌體 `elapsed_ms` 為實作擴充，PM 規格未定義。

---

## 開發階段

- [x] **Phase 1**（2026-04-17）— 硬體原型：ESP32-S3 + 8 按鈕 + OLED + 蜂鳴器驗收通過
- [x] **Phase 2A~2E + 3A**（2026-04-22）— 5 鍵狀態機 + 給藥倒數 + 藥物選單 + LittleFS 持久化
  - 實機煙霧測試 25/34 項通過（剩餘 9 項等 tactile 按鍵到貨補測）
  - 3 個 bug 已修：END 邊界誤觸、END 鎖死、切模式 MED 倒數未重置
  - PR review 5 項 MINOR/INFO 修正（millis overflow、抽 switchMode helper 等）
- [x] **Phase 1 單元測試框架**（2026-04-22）— `lib/ems_logic` + `[env:native]` + `computeTaskElapsedMs` 9/9 tests 綠
- [ ] **Phase 1.5** — INMP441 麥克風重試（換新模組後啟用 `ENABLE_MIC_MONITOR`）
- [ ] **Phase 2F** — 1.3" SH1106 OLED 升級（採 U8g2 library，型號到貨先驗證）
- [ ] **Phase 2 單元測試延伸** — MedCountdownDecision / computePauseCorrection / recordEvent
- [ ] **Phase 3** — 手機 App + DS3231 RTC 升級 + 主副機架構
- [ ] **Phase 4** — 整合測試、電源方案、外殼設計、量產化

詳細進度見 [`tasks/todo.md`](tasks/todo.md)。

---

## 量產化路線

| 階段 | 硬體方案 | 說明 |
|------|---------|------|
| 0 | 麵包板 + 杜邦線 | 韌體驗證（當前） |
| 1 | 洞洞板手焊 | 使用者試用與功能測試 |
| 2 | 客製 PCB 手焊 | 5~10 台，確認設計定版 |
| 3 | PCB + SMT 代焊 | 機器焊接被動元件，降低組裝成本 |

詳細各階段說明、工具、成本估算與外殼設計指引，見 [`tasks/production-roadmap.md`](tasks/production-roadmap.md)。

---

## 文件索引（依 Source of Truth 排序）

| 層級 | 文件 | 說明 |
|------|------|------|
| 1a | [docs/pm-flow-spec.md](docs/pm-flow-spec.md) | PM 行為規格（流程 / 互動 / 硬體模組） |
| 1b | [docs/pm-dev-spec.md](docs/pm-dev-spec.md) | PM 工程實作規格（常數 / 型別 / API / schema） |
| 2 | [docs/gap-analysis.md](docs/gap-analysis.md) | PM 規格 vs 現況差距 |
| 2 | [docs/incremental-impl-plan.md](docs/incremental-impl-plan.md) | 增量實作計畫 |
| 3 | [CLAUDE.md](CLAUDE.md) | 專案規則 / Phase 設計決策 |
| 4 | [tasks/todo.md](tasks/todo.md) | 開發進度與待辦清單 |
| 4 | [tasks/unit-test-plan.md](tasks/unit-test-plan.md) | 單元測試規劃 |
| 4 | [tasks/production-roadmap.md](tasks/production-roadmap.md) | 量產化路線 |
| 4 | [tasks/phase2-acceptance.md](tasks/phase2-acceptance.md) | Phase 2 驗收清單 |
| 5 | [design-philosophy.md](design-philosophy.md) | 視覺設計哲學（Technical Cartography） |

---

## 概念設計預覽

### 電路配置

![電路配置示意圖](ems_timer_schematic.png)

### 外觀概念

![外觀概念圖](ems_timer_exterior.png)
