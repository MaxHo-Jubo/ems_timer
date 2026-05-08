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

## 3. TFT 接線規劃（2026-05-08 實機驗證版）

> ✅ **2026-05-08 實機跑通**：2.8" ST7789 紅板（蝦皮）+ ESP32-S3 GOOUUU N16R8 + Adafruit_ST7789 lib，紅綠藍三色 + 文字計數器全部正確顯示。本節為實機驗證後的 SoT。
> ❌ **2026-05-04 舊計劃**（GPIO 35/36 + TFT_eSPI）已作廢，原因見 §3.4。

### 3.1 TFT 主訊號（實機接線）

| GPIO | TFT pin | 實機驗證 |
|------|---------|---------|
| **2** | MOSI / SDI | ✅（原 35 → 2，避開 N16R8 octal PSRAM） |
| **3** | SCK | ✅（原 36 → 3，同上原因） |
| **21** | CS | ✅ |
| **48** | DC | ✅ |
| **47** | RST | ✅ |
| **3.3V** 直供 | LED（背光） | ✅ 常亮，未啟用 PWM |
| **5V / VIN** | VCC | ✅ 板上有 LDO + level shifter，3.3V VCC 會 brown out |
| GND | GND | ✅ |
| MISO / 觸控腳（T_IRQ/T_DO/T_DIN/T_CS/T_CLK） | 不接 | TFT 純寫入；無觸控 |

**腳位選擇邏輯**：
- 避開 GPIO 8~13（SPI flash）、19/20（USB）、0/45/46（strapping）。
- 避開按鍵已封版 GPIO 4~7, 14~18。
- **避開 GPIO 35~37**（N16R8 octal PSRAM 內部佔用，2026-05-08 實機踩雷確認）。
- MOSI/SCK 用 GPIO 2/3（原為 ADC1 候選），CS/DC/RST 用 GPIO 21/48/47。

> 🔴 **採購警示升級**：先前文件警告「採購務必選 N8 / 非 octal PSRAM 版本」**未生效** — 採購到的 GOOUUU TECH 板實際就是 N16R8（金屬遮蔽印 `ESP32-S3-N16R8`）。本節腳位以「拿到 N16R8」為前提；若日後改用 N8 模組，可釋放 GPIO 2/3 回 ADC、SPI 改回 35/36。

### 3.2 預留 SPI bus 共用（未來 MicroSD）

MicroSD 模組與 TFT 共用 SCK / MOSI，再加 MISO + 獨立 CS：

| GPIO | 用途 |
|------|------|
| **3** | SCK（與 TFT 共用，原 36 → 3） |
| **2** | MOSI（與 TFT 共用，原 35 → 2） |
| **1** | MISO（原 37 不可用 → 1，犧牲 ADC1_CH0；TFT BL PWM 互斥） |
| **43** | MicroSD CS（USB-CDC TX，需放棄 USB Serial Monitor） |

> ⚠️ **CS 候選惡化**：原 §3.2 候選 GPIO 2 已給 SPI MOSI、GPIO 47 與 TFT RST 衝突。剩 GPIO 43（USB-CDC）或 44。Prod-Phase 量產不需 USB-CDC 時切換。Dev-Phase 期間若需同時保留 USB Serial 與 MicroSD，需重新評估或加 IO expander。

**注意**：TFT 通常只需 MOSI（單向寫入），但若 SD 卡共用 bus，整條 bus 必須加 MISO 線。

### 3.3 移除項目

- ~~GPIO 41, 42（I2C OLED）~~ → 釋出 2 支，但**仍建議留作 I2C bus**（DS3231 RTC、CO 感測器電化學型可掛同 bus，避免再開新 bus）

### 3.4 為什麼放棄 TFT_eSPI 改 Adafruit_ST7789（2026-05-08）

| 項目 | TFT_eSPI 2.5.43 | Adafruit_ST7789 + GFX |
|------|----------------|----------------------|
| 在 N16R8 模組 init() 行為 | ❌ `Guru Meditation StoreProhibited`，crash 在 SPI 暫存器寫入（`SPI_CMD_REG(SPI_PORT)` 被 octal PSRAM 旁路寫到 0x10） | ✅ 走標準 Arduino SPI driver，正常 init |
| 換 GPIO 是否解決 | ❌ 從 35/36 換到 2/3 仍 crash（不是腳位問題，是 lib 對 ESP32-S3 的 SPI 暫存器假設不成立） | n/a |
| Lib 自身備註 | `// Draws once then freezes`（`TFT_eSPI_ESP32.c` SPI3_HOST 配置處） | — |
| 速度 | 較快（DMA + 暫存器級） | 較慢（標準 SPI），但 EMS Timer 顯示頻率（≤2 fps）感覺不到 |
| ESP32-S3 N16R8 穩定性 | 已驗證不可用 | 已驗證可用 |

**結論**：放棄 TFT_eSPI。Adafruit 速度差異對救護現場顯示需求（倒數計時 + 大字事件）可忽略。

### 3.5 顏色反相設定（必加）

蝦皮買的 ST7789 紅板出廠 panel polarity 與 Adafruit_ST7789 預設相反：
- Adafruit init 預設送 `INVON`（IPS 主流）
- 此模組需要 `INVOFF` 才正確
- **必須**在 `tft.init()` 後呼叫 `tft.invertDisplay(false)`

不加會看到：白變黑、紅變藍系、綠變紫、藍變黃（每位元反相）。

### 3.3 移除項目

- ~~GPIO 41, 42（I2C OLED）~~ → 釋出 2 支，但**仍建議留作 I2C bus**（DS3231 RTC、CO 感測器電化學型可掛同 bus，避免再開新 bus）

---

## 4. 影響評估

### 4.1 韌體層

| 項目 | 變更內容 |
|------|---------|
| 函式庫 | `Adafruit_SH110X` + `Adafruit_GFX` → **`Adafruit_ST7789` + `Adafruit_GFX`**（2026-05-08 實機驗證後從 TFT_eSPI 改回 Adafruit；原因見 §3.4） |
| 初始化 | `display.begin(0x3C, true)` → `tft.init(240, 320)` + `tft.setRotation(1)` + **`tft.invertDisplay(false)`**（必加，見 §3.5） |
| 繪圖 API | `display.drawPixel/drawLine/print` → 同名 API（Adafruit_GFX 共用基底，遷移最小） |
| 字型 | SSD1306 內建 5x7 → Adafruit_GFX 預設 5x7 + `setTextSize()` 放大 + Free Fonts（救護現場可選大字型如 24pt 以上） |
| 色彩 | 單色 → RGB565（16-bit），可用顏色區分倒數狀態（綠/黃/紅） |
| 更新策略 | 全螢幕重繪 → 建議改 partial update（只重繪變動區域），避免閃爍 |
| 配置檔 | 不需 `User_Setup.h`，建構子直接傳 GPIO；色彩反相寫在程式碼 |

**影響範圍**：
- `firmware/src/main.cpp`：所有 `display.xxx()` 呼叫需改寫，但 Adafruit_GFX API 與現有 SH110X API 高度相容（都繼承 Adafruit_GFX 基底）
- `firmware/lib/`：若有自訂 OLED 顯示模組，需重構為 TFT 版本
- `platformio.ini`：移除 `adafruit/Adafruit SH110X`，新增 `adafruit/Adafruit ST7735 and ST7789 Library`

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
- 韌體：當前 main 已切換為 SH110X driver；若需回到 0.96" SSD1306 baseline，從 git 歷史取回
- 硬體：GPIO 41/42 仍保留 I2C 走線，OLED 模組不丟棄
- 中繼方案：1.2~1.3" SH1106 OLED（128×64，比 0.96" 大 70%，功耗仍低，SH110X driver 已驗證可用）

---

## 7. 待辦清單

- [ ] 採購 ST7789 + ILI9341 各 1 片
- [ ] 採購 6 條 20cm 母對公杜邦線
- [ ] 到貨後執行第 5 節驗證流程
- [ ] 選定款式後更新 SoT V1 對應章節
- [ ] 韌體分支 `feat/tft-migration` 開發新顯示驅動
- [ ] 重新計算 1000mAh 電池續航，決定是否升級電池容量
- [ ] **顯示器 init 失敗處理**：當前 `display.begin()` 失敗僅 `Serial.println` 後 fall-through，後續 `display.xxx()` 每幀對 I2C bus 拋 NACK 拖慢 main loop（救護現場螢幕死機=裝置失能但無感警示）。換 TFT 時連同新 driver 設計 fatal handler：retry 3 次仍失敗 → buzzer 連續發報 + halt loop（pre-existing issue，2026-05-04 silent-failure 審查發現，刻意延後至 driver 重寫時一併處理）
