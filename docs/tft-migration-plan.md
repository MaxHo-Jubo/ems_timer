# 2.4" TFT 顯示器遷移計劃（OLED → TFT）

> **狀態**：草案（待硬體到貨後實測校正）
> **建立日期**：2026-05-04
> **目標**：將現有 0.96" SSD1306 I2C OLED 升級為 2.4" SPI TFT，提升顯示面積與資訊密度
> **關聯文件**：SoT V1 §20.5（擴充預留）、§21.1（硬體清單）、§21.2（外殼規格）

---

## 1. 採購清單

| 模組 | 用途 | 介面 | 預估價格（NTD） |
|------|------|------|----------------|
| 2.4" ST7789V SPI TFT（無觸控） | 主選方案 | SPI 4-wire | 120~180 |
| 2.4" ILI9341 SPI TFT（無觸控） | 備選方案 | SPI 4-wire | 130~200 |

**選型理由**：
- **無觸控**：救護員戴手套操作，實體按鈕優於螢幕點擊；省 GPIO（XPT2046 需 T_CS / T_IRQ 兩支）。
- **SPI 介面**：避免並列介面（ILI9325/SPFD5408）佔用 16+ GPIO。
- **兩款並買**：實測比較刷新速度、色彩飽和度、可視角度後再決定量產款。

---

## 2. 現有 GPIO 配置（ESP32-S3 GOOUUU）

| GPIO | 用途 | 模組 |
|------|------|------|
| 4, 5, 6, 7 | 按鈕 1~4 | 左側按鈕（前 4 顆） |
| 15, 16, 17, 18 | 按鈕 5~8 | 左側按鈕（後 4 顆） |
| 14 | 蜂鳴器 PWM | 主動式蜂鳴器 |
| 38, 39, 40 | I2S（WS / SD / SCK） | INMP441 麥克風 |
| 41, 42 | I2C（SCL / SDA） | **SSD1306 OLED**（待移除） |

**已用 GPIO 數量**：14 支
**ESP32-S3 GOOUUU 可用 GPIO**：約 35 支（扣除 strapping pin 與 USB/Flash 專用腳）
**剩餘可用**：約 21 支

---

## 3. TFT 接線規劃（建議配置）

> ⚠️ **2026-05-04 修正**：原計劃使用 GPIO 9~13，與 ESP32-S3 內建 SPI flash 禁用區衝突，已改用高號 GPIO。最終腳位以 [`gpio-allocation.md`](gpio-allocation.md) 為準。

### 3.1 TFT 主訊號（5 支必要 + 1 支選用）

| GPIO | TFT pin | 說明 |
|------|---------|------|
| **36** | SCK | SPI clock（與 MicroSD 共用） |
| **35** | MOSI | SPI MOSI（與 MicroSD 共用） |
| **21** | CS | TFT 片選（獨立） |
| **48** | DC | Data/Command 切換 |
| **47** | RST | Hardware reset |
| **3.3V** 直供（預設）或 **GPIO 1**（PWM 調亮度，會占用 ADC1_CH0） | BL | 背光 |

**為什麼選 35~37 + 21, 47, 48**：
- 避開 ESP32-S3 內建 SPI flash 禁用區（GPIO 8~13）。
- 避開按鍵已封版的 GPIO 4~7, 14~18（SoT V1 §4.1 主要按鍵）。
- 避開 strapping pin（GPIO 0, 45, 46）與 USB D±（GPIO 19, 20）。
- 高號 GPIO（35~48）連續且方便焊接。

⚠️ **採購注意**：GPIO 35/36/37 在 **N16R8 octal PSRAM 模組**會被佔用，務必確認採購的 ESP32-S3 GOOUUU 為 **N8 / 非 octal PSRAM 版本**。

### 3.2 預留 SPI bus 共用（未來 MicroSD）

MicroSD 模組與 TFT 共用 SCK / MOSI，再加 MISO + 獨立 CS：

| GPIO | 用途 |
|------|------|
| 36 | SCK（與 TFT 共用） |
| 35 | MOSI（與 TFT 共用） |
| **37** | MISO（TFT 不需要，MicroSD 讀檔需要） |
| **2** | MicroSD CS（獨立，犧牲 ADC1_CH1） |

> ⚠️ **避開原本 GPIO 47 衝突**：原規劃 MicroSD CS 用 GPIO 47，與 TFT RST 衝突，改用 GPIO 2（ADC1）。若 CO 感測器走 ADC 路線，需重新配置。

**注意**：TFT 通常只需 MOSI（單向寫入），但若 SD 卡共用 bus，整條 bus 必須加 MISO 線。

### 3.3 移除項目

- ~~GPIO 41, 42（I2C OLED）~~ → 釋出 2 支，但**仍建議留作 I2C bus**（DS3231 RTC、CO 感測器電化學型可掛同 bus，避免再開新 bus）

---

## 4. 影響評估

### 4.1 韌體層

| 項目 | 變更內容 |
|------|---------|
| 函式庫 | `Adafruit_SSD1306` + `Adafruit_GFX` → **`TFT_eSPI`**（推薦，ESP32 最佳化） |
| 初始化 | `display.begin(SSD1306_SWITCHCAPVCC, 0x3C)` → `tft.init()` + `tft.setRotation()` |
| 繪圖 API | `display.drawPixel/drawLine/print` → `tft.drawPixel/drawLine/drawString`（API 類似但函式名不同） |
| 字型 | SSD1306 內建 5x7 → TFT_eSPI 提供多種字型 + Free Fonts（救護現場可選大字型如 24pt 以上） |
| 色彩 | 單色 → RGB565（16-bit），可用顏色區分倒數狀態（綠/黃/紅） |
| 更新策略 | 全螢幕重繪 → 建議改 partial update（只重繪變動區域），避免閃爍 |
| 配置檔 | `User_Setup.h` 需配置 ST7789 或 ILI9341 driver、SPI pin、頻率（建議 40MHz） |

**影響範圍**：
- `firmware/src/main.cpp`：所有 `display.xxx()` 呼叫需改寫
- `firmware/lib/`：若有自訂 OLED 顯示模組，需重構為 TFT 版本
- `platformio.ini`：移除 `adafruit/Adafruit SSD1306`，新增 `bodmer/TFT_eSPI`

### 4.2 硬體層

| 項目 | 影響 |
|------|------|
| 麵包板配線 | 新增 6 條杜邦線（SCK/MOSI/CS/DC/RST/BL） |
| 外殼開孔 | **SoT V1 §21.2 需重畫**：0.96" OLED（25×15mm）→ 2.4" TFT（50×38mm 顯示區，外框約 60×45mm） |
| PCB（Prod-Phase 量產階段） | 需重新 layout TFT 連接器位置 |

### 4.3 功耗（影響 SoT V1 §20.4 電源預算）

| 模組 | 電流 | 備註 |
|------|------|------|
| SSD1306 OLED（現有） | ~15~20 mA | 全亮 |
| ST7789 / ILI9341 TFT + 背光 | **~80~120 mA** | 背光佔大宗（~60~100mA） |

**續航衝擊**：
- 1000mAh 電池原預估（OLED 配置）：待 Phase 1 實測
- 換 TFT 後續航**可能掉 30~50%**
- **緩解方案**：
  1. 背光 PWM 調暗（50% duty 可省 ~30mA）
  2. 閒置 30 秒後背光自動關閉，按鈕喚醒
  3. 量產階段升級電池容量至 1500~2000mAh

### 4.4 SoT V1 規格文件需同步更新章節

- [ ] §20.4 電源預算表 → 加入 TFT 電流估算與續航重算
- [ ] §20.5 擴充預留 → 標註 TFT SPI bus 已使用
- [ ] §21.1 硬體清單 → SSD1306 移除，新增 ST7789 / ILI9341
- [ ] §21.2 外殼規格 → 顯示開孔尺寸更新
- [ ] §21.3 GPIO 分配表 → 同步本文件第 3 節

---

## 5. 採購到貨後驗證流程

### Step 1：基本顯示測試
1. 按本文件第 3.1 節接線
2. 安裝 `TFT_eSPI` 函式庫，配置 `User_Setup.h`
3. 跑官方範例 `TFT_graphicstest`
4. 確認：開機顯示正常、無雪花、刷新無撕裂

### Step 2：兩款比較
| 比較項目 | ST7789 | ILI9341 | 勝出 |
|---------|--------|---------|------|
| 刷新速度（fillScreen） | 待測 ms | 待測 ms | ? |
| 色彩飽和度（目視） | | | ? |
| 可視角度（30° 斜視） | | | ? |
| 背光均勻度 | | | ? |
| 實際電流（multimeter） | 待測 mA | 待測 mA | ? |

### Step 3：救護場景模擬
- 倒數計時畫面（4:00 → 0:00）大字顯示，車內螢光燈下可視性
- OHCA 主畫面 + 6 秒給氣輔助區塊（SoT V1 §14）排版測試
- 紅/黃/綠狀態色切換是否清晰

### Step 4：定型決策
- 選定其中一款 → 更新 SoT V1 §21.1
- 確認外殼開孔尺寸 → 更新 §21.2
- 重新計算電源預算 → 更新 §20.4

---

## 6. 回滾方案

若 TFT 實測不符預期（如功耗過高、刷新延遲、字型不清），可保留 OLED 配置：
- 韌體：保留 SSD1306 分支於 git tag `v1-oled-baseline`
- 硬體：GPIO 41/42 仍保留 I2C 走線，OLED 模組不丟棄
- 替代升級路徑：考慮 1.3" SH1106 OLED（128×64，比 0.96" 大 70%，功耗仍低）

---

## 7. 待辦清單

- [ ] 採購 ST7789 + ILI9341 各 1 片
- [ ] 採購 6 條 20cm 母對公杜邦線
- [ ] 到貨後執行第 5 節驗證流程
- [ ] 選定款式後更新 SoT V1 對應章節
- [ ] 韌體分支 `feat/tft-migration` 開發新顯示驅動
- [ ] 重新計算 1000mAh 電池續航，決定是否升級電池容量
