# EMS Timer — 救護計時器

給救護人員使用的手持計時裝置。按下按鈕記錄事件（注射、CPR、電擊等）的時間戳與經過時長，事後透過藍牙傳輸至手機 App 檢視與保存。

## 功能特色

- 8 顆實體按鈕，對應不同救護事件
- OLED 螢幕即時顯示事件紀錄與倒數計時
- 蜂鳴器提醒（倒數結束 / 區間提醒）
- 麥克風錄音，口述備註存入 SD 卡
- BLE 藍牙連線手機 App，傳輸與檢視歷史紀錄

## 硬體架構

| 元件 | 型號 | 介面 |
|------|------|------|
| 主控板 | ESP32-WROOM-32 DevKit | — |
| 螢幕 | SSD1306 OLED 0.96" | I2C |
| 按鈕 | 觸覺按鈕 × 8 | GPIO INPUT_PULLUP |
| 蜂鳴器 | 被動式蜂鳴器 | PWM |
| 麥克風 | INMP441 數位麥克風 | I2S |
| 儲存 | MicroSD 卡模組 | SPI |

## 文件索引

### 專案文件

| 文件 | 說明 |
|------|------|
| [CLAUDE.md](CLAUDE.md) | 專案需求規格、資料模型、開發階段定義 |
| [design-philosophy.md](design-philosophy.md) | 視覺設計哲學（Technical Cartography） |

### 概念設計圖

| 圖片 | 說明 |
|------|------|
| [ems_timer_schematic.png](ems_timer_schematic.png) | 電路配置示意圖 — GPIO 接線、元件連接關係 |
| [ems_timer_exterior.png](ems_timer_exterior.png) | 外觀概念圖 — 產品俯視圖、側面圖、尺寸標註 |

### 工具腳本

| 腳本 | 說明 |
|------|------|
| [generate_diagram.py](generate_diagram.py) | 電路配置示意圖產生器（Python + Pillow） |
| [generate_exterior.py](generate_exterior.py) | 外觀概念圖產生器（Python + Pillow） |

## 開發階段

- [ ] **Phase 1** — 硬體原型：ESP32 + 按鈕 + 螢幕基本功能
- [ ] **Phase 2** — BLE 通訊：裝置與手機配對、數據傳輸
- [ ] **Phase 3** — 手機 App：接收數據、顯示時間軸、歷史紀錄
- [ ] **Phase 4** — 整合測試與優化

## 概念設計預覽

### 電路配置

![電路配置示意圖](ems_timer_schematic.png)

### 外觀概念

![外觀概念圖](ems_timer_exterior.png)
