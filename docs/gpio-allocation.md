# GPIO 分配總表（Single Source of Truth）

> **狀態**：本文件為 GPIO 分配的唯一真相來源
> **建立日期**：2026-05-04
> **依據**：SoT V1 `§4.1 主要按鍵`（按鍵命名）、`§21.3 擴充預留腳位`（擴充策略）
> **目標硬體**：ESP32-S3 GOOUUU 開發板（標準 N8 模組，非 octal PSRAM 版）
> **同步原則**：本文件變更後，`main.cpp` 註解、`SoT V1 §21.3`、`tft-migration-plan.md` 必須對齊

---

## 1. 按鍵腳位（依 SoT V1 §4.1，8 鍵封版）

| SoT 名稱 | 韌體 ID（建議） | GPIO | 韌體狀態 | 物理狀態 | 備註 |
|---------|----------------|------|---------|---------|------|
| **主按鍵** | `BTN_PRIMARY` | **4** | ✅ 已實作 | ✅ 接線 | 紅色大鍵 |
| **上鍵** | `BTN_UP` | **5** | ✅ 已實作 | ✅ 接線 | 選單上移 / 音量 + |
| **下鍵** | `BTN_DOWN` | **6** | ✅ 已實作 | ✅ 接線 | 選單下移 / 音量 - |
| **Power 鍵** | `BTN_POWER` | **7** | ✅ 已實作 | ✅ 接線 | 開關機 |
| **錄音鍵** | `BTN_RECORD` | **15** | ⚠️ 佔位 noop | ✅ 接線 | INMP441 到貨後啟用 |
| **返回鍵** | `BTN_BACK` | **16** | ❌ 未實作 | ✅ 接線 | Impl-Phase B 必補 |
| **EPI 鍵**（針筒圖案） | `BTN_EPI` | **17** | ❌ 未實作 | ✅ 接線 | Impl-Phase B 必補 |
| **電擊鍵**（閃電圖案） | `BTN_SHOCK` | **18** | ❌ 未實作 | ✅ 接線 | Impl-Phase B 必補 |

> 📌 **GPIO 16/17/18 已封給按鍵**：原 main.cpp 註解標記為「獨立 I2C 候選」的 GPIO 17/18 **無效**，CO 感測器擴充必須改用其他腳位（見第 5 節）。

---

## 2. 顯示

| 用途 | GPIO | 介面 | 模組 | 狀態 |
|------|------|------|------|------|
| OLED SDA | **42** | I2C | SSD1306 0.96" | ✅ 啟用 |
| OLED SCL | **41** | I2C | SSD1306 0.96" | ✅ 啟用 |

> 📌 若採用 `tft-migration-plan.md` 升級 2.4" TFT，OLED 拆除，GPIO 41/42 釋出可作其他 I2C 用途（如 DS3231 RTC）。TFT SPI 腳位見第 5.2 節。

---

## 3. 提醒輸出

| 用途 | GPIO | 模組 | 狀態 |
|------|------|------|------|
| 蜂鳴器 PWM | **14** | 主動式蜂鳴器 | ✅ 啟用 |
| 震動馬達 | **待定** | S8050 NPN + 1kΩ 基極 | ❌ `ENABLE_VIBRATION = 0` 停用 |

> 📌 震動馬達若啟用，建議用 GPIO 21（PWM 能力 + 不衝突按鍵）。

---

## 4. 錄音（Dev-Phase 1.5 待啟用）

| 用途 | GPIO | 介面 | 模組 | 狀態 |
|------|------|------|------|------|
| I2S SCK | **40** | I2S | INMP441 麥克風 | ⚠️ 接線但 `ENABLE_MIC_MONITOR = 0` |
| I2S WS | **39** | I2S | INMP441 麥克風 | ⚠️ 同上 |
| I2S SD | **38** | I2S | INMP441 麥克風 | ⚠️ 同上 |
| MicroSD 共用 SCK | **與 TFT 共用 → GPIO 36**（見 5.2） | SPI | MicroSD 卡模組 | ❌ 待選型 |
| MicroSD 共用 MOSI | **與 TFT 共用 → GPIO 35** | SPI | MicroSD 卡模組 | ❌ 待選型 |
| MicroSD 共用 MISO | **與 TFT 共用 → GPIO 37** | SPI | MicroSD 卡模組 | ❌ 待選型 |
| MicroSD CS | **47**（獨立） | SPI | MicroSD 卡模組 | ❌ 待選型 |

---

## 5. 擴充預留（重新規劃，覆寫 SoT V1 §21.3.3 舊版）

### 5.1 ESP32-S3 N8 模組可用 GPIO 盤點

**已用**（14 支）：4, 5, 6, 7, 14, 15, 16, 17, 18, 38, 39, 40, 41, 42
**禁用**（不可作 I/O）：
- GPIO 0, 45, 46 — strapping pin
- GPIO 8~13 — 內建 SPI flash
- GPIO 19, 20 — USB D-/D+
- GPIO 26~32 — 連接 SPI flash bonding pads（依模組型號可能可用，**保守禁用**）

**可用空閒**（11 支）：
- GPIO 1, 2, 3 — ADC1（建議保留給類比輸入，如 CO 感測器 MEMS 型）
- GPIO 21 — 通用 GPIO（建議保留給震動馬達 PWM）
- GPIO 35, 36, 37 — 通用 GPIO（**N8 模組可用，N16R8 octal PSRAM 模組被佔**，採購時務必確認模組型號）
- GPIO 43, 44 — 預設 USB-CDC TX/RX（**僅在 USB-CDC 不啟用時可用**，目前韌體用 USB-CDC 做 Serial Monitor，預設禁用）
- GPIO 47, 48 — 通用 GPIO

### 5.2 TFT SPI bus 腳位（取代 tft-migration-plan.md 舊版 GPIO 9~13）

| 用途 | GPIO | 備註 |
|------|------|------|
| SPI SCK | **36** | TFT + MicroSD 共用 |
| SPI MOSI | **35** | TFT + MicroSD 共用 |
| SPI MISO | **37** | MicroSD 需要，TFT 純寫入不需要 |
| TFT CS | **21** | 獨立 |
| TFT DC | **48** | Data/Command 切換 |
| TFT RST | **47** | Hardware reset |
| TFT BL（背光 PWM） | **接 3.3V 常亮 或 GPIO 1**（PWM 調亮度，會吃掉 ADC1_CH0） | 二擇一 |
| MicroSD CS | **47** ← **衝突！** | 與 TFT RST 衝突 |

> ⚠️ **MicroSD CS 修正**：上表 MicroSD CS 改用 **GPIO 2**（ADC1_CH1，犧牲一支 ADC）或 **GPIO 43**（USB-CDC 替代腳，需放棄 USB Serial Monitor）。建議優先 GPIO 2，因 USB-CDC 對開發階段除錯重要。

### 5.3 CO 感測器擴充腳位（取代舊版 GPIO 17/18 獨立 I2C）

| 候選方案 | 介面 | GPIO | 備註 |
|---------|------|------|------|
| 電化學型 + I2C | SDA / SCL | **與 OLED 共用 GPIO 42 / 41** | I2C 多裝置 bus，OLED + CO 感測器同 bus 並存 |
| 電化學型 + UART | TX / RX | **GPIO 43 / 44** | 必須放棄 USB-CDC Serial Monitor |
| MEMS 半導體型 + ADC | 類比輸入 | **GPIO 1 / 2 / 3 擇一** | 與 TFT 背光 PWM、MicroSD CS 互斥 |
| 加熱半導體型（MQ-7/MQ-9） | — | **🚫 禁用** | 功耗超出 USB-C 供電上限（SoT V1 §20.5） |

### 5.4 RTC 模組擴充（DS3231，Dev-Phase 3 計畫）

| 用途 | GPIO | 備註 |
|------|------|------|
| I2C SDA | **與 OLED 共用 GPIO 42** | DS3231 + OLED 同 bus，地址不衝突（DS3231=0x68, SSD1306=0x3C） |
| I2C SCL | **與 OLED 共用 GPIO 41** | 同上 |
| INT（選用） | **GPIO 43**（若 USB-CDC 停用）或不接 | 用於秒中斷喚醒 |

---

## 6. 互斥約束總覽

| 衝突組合 | 互斥原因 | 解法 |
|---------|---------|------|
| TFT BL（PWM） vs CO 感測器 ADC | 都搶 GPIO 1/2/3 | 二擇一；TFT BL 可接 3.3V 常亮，省下 GPIO |
| MicroSD CS vs USB-CDC Serial | GPIO 43/44 衝突 | 量產不需 USB-CDC 時切換 |
| 獨立 I2C vs 按鍵 16/17/18 | 物理腳位衝突 | 按鍵已封版，獨立 I2C 改與 OLED bus 共用 |
| GPIO 35/36/37 vs N16R8 octal PSRAM | 模組型號限制 | 採購務必選 N8 / N16 非 octal 版 |

---

## 7. 對齊狀態（其他文件同步進度）

| 文件 | 同步狀態 | 待修正項目 |
|------|---------|-----------|
| `firmware/src/main.cpp` 8 按鍵定義 | ✅ 已同步（2026-05-04） | Impl-Phase A 重寫；`BTN_COUNT=8`，`BTN_BACK` / `BTN_EPI` / `BTN_SHOCK` 已加入 |
| `firmware/src/main.cpp` 震動馬達 GPIO | ✅ 已同步（2026-05-04） | `VIBRATION_PIN=21`，原 GPIO 16 釋放給返回鍵 |
| `docs/EMS_DoseSync_Pro_Prototype_V1.md` §21.3.1 | ✅ 已同步（2026-05-04） | 補上返回/EPI/電擊鍵；震動馬達 GPIO 16 → 21；新增狀態欄 |
| `docs/EMS_DoseSync_Pro_Prototype_V1.md` §21.3.3 | ✅ 已同步（2026-05-04） | 移除「獨立 I2C：GPIO 17/18」候選 |
| `docs/EMS_DoseSync_Pro_Prototype_V1_flow.html` | ✅ 已同步（2026-05-04） | 對齊 §21.3.1 與 §21.3.3 |
| `docs/gap-analysis.md` 震動馬達列 | ✅ 已同步（2026-05-04） | GPIO 16 → 21 |
| `docs/tft-migration-plan.md` 第 3.1 節 | ✅ 已同步（2026-05-04） | TFT GPIO 從 9~13 改為 35~37, 21, 47, 48 |
| `CLAUDE.md` | ✅ 已同步（2026-05-04） | 加入本文件索引 |

---

## 8. 變更歷程

| 日期 | 變更 | 原因 |
|------|------|------|
| 2026-05-04 | 建立本文件 | 解決 main.cpp / SoT V1 / tft-migration-plan 三方 GPIO 矛盾 |
| 2026-05-04 | 確認 SoT §4.1 8 按鍵封版 | GPIO 16/17/18 歸屬返回 / EPI / 電擊鍵 |
| 2026-05-04 | TFT GPIO 重配 | 原 9~13 與 SPI flash 禁用區衝突，改 35~37 + 21, 47, 48 |
| 2026-05-04 | Impl-Phase A 韌體對齊 | main.cpp 重寫：`BTN_COUNT=8`、新增 `BTN_BACK/EPI/SHOCK`、震動 GPIO 21；舊 lib `ems_countdown` / `ems_vent` 廢止 |
