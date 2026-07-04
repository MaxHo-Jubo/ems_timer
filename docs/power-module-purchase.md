# EMS Timer 電源模組採購與組裝指南

> **適用階段**：Dev-Phase 2 起（脫離 USB Type-C 直供，改鋰電池供電）
> **建立日期**：2026-05-06
> **負責人**：Max Ho
> **對應 SoT 章節**：V1 §20.4（充放電拓樸）、§21.1（主要硬體清單）

## 1. 目的

提供 EMS Timer 從「USB-C 直供」過渡到「3.7V 鋰電池 + 充電升壓模組 + 邊充邊用」的具體採購與組裝 SOP，供工程端執行與後續複製。

## 2. 整體架構

```
                    ┌─────────────────┐
USB-C 5V 輸入 ─────►│ TP4056 充電升壓 │
                    │   模組（聯騰）  │
                    └────┬──────┬─────┘
                         │      │
                  B+/B-  │      │  OUT+/OUT-
                         ▼      ▼
              ┌──────────────┐  └──► ESP32-S3 GOOUUU VIN/GND
              │  PH2.0 母座  │
              │（焊在板上）  │
              └──────┬───────┘
                     │ PH2.0 公頭對接
                     ▼
         ┌────────────────────────┐
         │ 1000mAh LiPo 523450    │
         │ 3.7V + 內建保護板      │
         │ （DW01 + 8205A）       │
         └────────────────────────┘
```

**核心特性**：
- USB-C 接上時：邊充電邊供電（pass-through）
- USB-C 拔除時：電池升壓 5V 持續供電 ESP32
- 電池內建保護板：過充/過放/過流/短路全包

## 3. 採購清單

| 品項 | 賣場 | 單價 | 數量 | 小計 |
|---|---|---|---|---|
| 1000mAh 523450 LiPo PH2.0（左紅右黑、含保護板） | [Shopee](https://reurl.cc/R28yZr) | NT$130 | 1 | 130 |
| TP4056 充電升壓模組 Type-C | [聯騰-蝦皮賣場](https://reurl.cc/O6zjZr) | NT$25 | 2（1 用 1 備） | 50 |
| JST PH2.0 母座 直插 2P | 蝦皮搜「PH2.0 母座 2P 直插」 | NT$5 | 5 | 25 |
| **總計** | | | | **NT$205** |

### 3.1 規格驗證（已確認）

**電池**：
- ✅ 容量 1000mAh（對齊 SoT V1 §20.4）
- ✅ JST PH2.0 公頭已壓好
- ✅ 內建保護板（黃色 Kapton 膠帶下可見小 PCB 與 DW01/8205A）
- ✅ 尺寸 5.2mm × 34mm × 50mm（外殼設計依此預留）
- ✅ 極性「左紅右黑」（接頭正面、卡榫朝上時）

**充電升壓模組**（聯騰 MTARDTP4056S）：
- ✅ TP4056 充電 IC（充電電流預設 1A = 1C）
- ✅ Type-C 輸入
- ✅ 內建升壓電路（電感 + 可調電位器，輸出 5V）
- ⚠️ 規格頁未明確標示輸出電流上限（推測 ~1A，實測為準）
- ⚠️ 板上無 JST 座，B+/B- 為焊盤

## 4. 收貨驗收 SOP

### 4.1 電池驗收（拿到當天執行）

| 步驟 | 動作 | 通過標準 | 不通過處理 |
|---|---|---|---|
| 1 | 三用電表量公頭兩 pin 電壓 | 3.6~4.2V，紅線正、黑線負 | 反接 → 退貨 |
| 2 | 確認黃膠帶下有保護板小 PCB | 肉眼可見元件 | 無保護板 → 退貨 |
| 3 | 充飽（接 TP4056 板）後再量 | ≥ 4.18V | < 4.0V → 電池老化退貨 |
| 4 | 充電過程用 USB 電流計監控 | 累積容量 ≥ 800mAh | < 800mAh → 容量衰減退貨 |
| 5 | 量電池外觀 | 無鼓脹、無漏液 | 任一異常 → 立即退貨並安全處理 |

### 4.2 充電升壓模組驗收

| 步驟 | 動作 | 通過標準 | 不通過處理 |
|---|---|---|---|
| 1 | 目視 IC 標示 | 「TC4056A」或「TP4056」字樣可辨識 | 標示模糊 → 換貨 |
| 2 | USB-C 插入 + B+/B- 接電池後 | 紅燈亮（充電中）/ 綠燈亮（充飽） | 無 LED 反應 → 換貨 |
| 3 | 量 OUT+/OUT- 之間電壓（無負載） | 預設可能 ~5V，可調電位器轉到 **5.0V ± 0.1V** | 調不到 → 換貨 |
| 4 | 接假負載（100Ω 電阻）量 OUT 電壓 | 仍維持 4.8~5.2V | 大幅下降 → 升壓電流不足 |

### 4.3 PH2.0 母座驗收

- 公母對插順暢、卡榫扣得住
- 兩 pin 焊腳間距 2.0mm（用游標卡尺確認）

## 5. 組裝步驟

### 5.1 焊接 PH2.0 母座到聯騰板

1. **決定方向**：母座**卡榫朝外**（朝向板子邊緣），方便插拔
2. **對齊 B+/B-**：
   - 電池公頭「左紅右黑」（卡榫朝上看）
   - 母座焊到板子上時，**左 pin → B+ 焊盤、右 pin → B- 焊盤**
3. **焊接**：先點固一腳調位置，再正式焊兩腳
4. **目視檢查**：無短路、無虛焊

### 5.2 升壓電壓校正

1. USB-C 接上 5V（不接電池）
2. 三用電表跨在 OUT+ 和 OUT- 之間
3. 用小一字起子轉可調電位器，**轉到輸出 5.00V ± 0.05V**
4. 用熱熔膠或指甲油**鎖住電位器**（防震動移位）

### 5.3 接 ESP32-S3

| 聯騰板 | ESP32-S3 GOOUUU |
|---|---|
| OUT+ | VIN / **V5IN**（5V 軌，視板子絲印）|
| OUT- | GND |

> 📌 **GOOUUU N16R8 板絲印是 `V5IN`**，與一般 dev board 的 `VIN` 是**同一條 5V 軌**，名字不同節點相同。本專案 TFT 螢幕也掛在 V5IN 上，OUT+ 接 V5IN 後 ESP32 與 TFT 一起吃這條 5V，不需另拉線。

🚨 **接線前兩道強制檢查（跳過任一件可能燒晶片）**：

1. **萬用表確認 V5IN 是 5V 不是 3V3** — 接到 3V3 pin 會直接燒晶片。USB 插上量 V5IN 對 GND 必須 ~5V。
2. **校壓 + 帶 TFT 量帶載不掉壓** — 電位器轉 5.00V ± 0.05V 後，接上 TFT 再量一次；加載後 V5IN 不可掉到 4.7V 以下，否則 ESP32 brown-out 反覆重開機（見 §6）。

⚠️ **千萬不要接到 3V3 pin** — 會燒晶片。

### 5.4 整合測試

1. **滿電測試**：電池充飽，拔 USB-C → ESP32 正常開機運作 ≥ 30 分鐘
2. **耗電測試**：放電到 3.4V → ESP32 仍正常（驗證升壓有效）
3. **過放保護測試**：放電到 2.5V → 保護板應自動切斷負載（OUT 電壓歸零），重新接 USB-C 充電後恢復
4. **邊充邊用測試**：USB-C 與電池同時接 → ESP32 連續運作不重開機

## 6. 故障排除

| 症狀 | 可能原因 | 排查 |
|---|---|---|
| ESP32 一插電池就重開機 | 升壓電壓沒調到 5V，VIN < 4.7V | 重新校正電位器到 5.0V |
| 充電指示燈不亮 | 電池極性反、保護板已切斷、TP4056 IC 燒毀 | 量電池電壓、量 B+/B- 是否短路 |
| 充電燈一直紅燈不轉綠 | 充電電流太小（電池容量大）、TP4056 R3 焊盤接觸不良 | 量充電電流，正常 ~1A |
| 拔 USB-C 後 ESP32 立即斷電 | 升壓功能失效、電池電壓過低觸發保護 | 量 OUT 電壓，量電池電壓 |
| 電池發熱 / 鼓脹 | 過流、內部短路 | **立即拔除、隔離、報廢處理** |

## 7. 安全提醒

- **LiPo 鋰聚合物電池為危險品**：禁止穿刺、擠壓、丟入火中、短路
- **充電時人在場**：首次充電必須在場監控
- **儲存環境**：陰涼乾燥處（< 30°C），長期儲存保持 50% 電量
- **報廢處理**：用鹽水浸泡 24 小時放電後送電池回收

## 8. 後續整合到 SoT

採購驗證通過後，需更新以下文件：

- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §20.4.3 補充充電 IC 為「聯騰 MTARDTP4056S（TP4056 + 升壓二合一）」
- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §21.1 主要硬體清單新增本電源模組
- [ ] `docs/pcb-outsourcing-guide.md` PCB 預留要求補充 PH2.0 母座焊盤位置
- [ ] `docs/gpio-allocation.md` 確認 VIN 接腳對應正確
- [ ] `tasks/production-roadmap.md` BOM 段落同步更新單價與賣場

## 9. 未來升級路線（Prod-Phase 候選方案）

> 此章節記錄「現行 DIY 方案」之外的兩個整合度更高的商用方案，供未來 Prod-Phase 量產或 DIY 失敗時切換參考。**目前不採購，僅作技術備案。**

### 9.1 觸發升級的時機

當以下情況之一發生時，建議重新評估升級到 §9.2 / §9.3 方案：

- 聯騰板 USB-C 切換時 ESP32 反覆重開機（pass-through 不真實）
- 進入 Prod-Phase 量產階段（5~10 台以上）
- 想在 OLED 顯示電量百分比 / 剩餘時間
- 同時規劃整合 RTC（Phase 3 DS3231）

### 9.2 候選方案 A：Adafruit PowerBoost 1000C

**官方產品介紹**（節錄自 [adafruit.com/product/2465](https://www.adafruit.com/product/2465)）：

> PowerBoost 1000C is the perfect power supply for your portable project! With a built-in **load-sharing battery charger circuit**, you'll be able to keep your power-hungry project running even while recharging the battery! This little DC/DC boost converter module can be powered by any 3.7V LiIon/LiPoly battery, and convert the battery output to 5.2V DC for running your 5V projects.

**核心規格**：

| 項目 | 規格 |
|---|---|
| 充電 IC | MCP73871（Microchip，工業級）|
| 升壓 IC | TPS61090（TI，工業級）|
| 輸入 | Micro USB 5V |
| 輸出 | **5.2V**（特意做高補償 USB 線損）|
| 連續電流 | ~1A（內部 switch 2A）|
| 充電電流 | 1A（可調至 100mA）|
| 電池接頭 | **JST PH 2.0**（與你目前電池相容）|
| 特殊功能 | Load-sharing、低電量偵測 LED、EN pin（軟關機）|
| 尺寸 | 23 × 45 × 10mm |
| 重量 | 6g |
| 價格 | $19.95（≈ NT$650）+ 國際運費 |
| 含電池 | ❌ 需另購 |

**優點**：
- TPS61090 是 TI 工業級升壓 IC，可靠度遠高於 TP4056 山寨版
- **真正的 load-sharing**（負載與電池分離，切換無 noise）
- EN pin 可實作軟關機（對應 EMS Timer 規劃中的長按關機功能）
- JST PH 2.0 接頭與現有電池相容，**升級無需換電池**

**缺點**：
- 價格約現行方案 3 倍
- Micro USB 不是 Type-C（除非買 v2 版）
- 海外購買，運費 + 等待時間

**適用情境**：Prod-Phase 量產首選，或現行方案出現切換 noise 問題時

### 9.3 候選方案 B：PiSugar 3

**官方產品介紹**（節錄自 [pisugar.com](https://www.pisugar.com/products/pisugar-3-raspberry-pi-zero-battery)）：

> PiSugar 3 is the third generation of PiSugar, Raspberry Pi zero battery series, making Raspberry Pi a portable device. With the on-board RTC, the Raspberry Pi can get the correct time whether it's powered or networked.

**核心規格**：

| 項目 | 規格 |
|---|---|
| 內建電池 | **1200mAh LiPo**（可充電鋰聚合物，非水銀電池）|
| 輸入 | Type-C / Micro USB 雙介面 5V-3A max |
| 輸出 | 5V-3A max |
| 充電保護 | 硬體電池保護電路 |
| 通訊介面 | **I2C**（位址 0x57 / 0x68，可自訂）|
| RTC | 板載 RTC + 寫入保護 + **CR1220 鈕扣電池備份**（不是主電源）|
| 特殊功能 | 軟關機、防誤觸開關、軟體看門狗、WebUI/APP、OTA 韌體升級 |
| 形狀 | 為 Raspberry Pi Zero（65 × 30mm）設計 |
| 價格 | $39.99（≈ NT$1,300）+ 國際運費 |
| 含電池 | ✅ 1200mAh 內建 |

**優點**：
- **真正一片解決**：充電 + 升壓 + 保護 + RTC + 電量計 + 軟關機
- I2C 電量計可在 OLED 顯示電量百分比 / 剩餘時間
- 內建 RTC **直接取代 Phase 3 規劃的 DS3231**（一石二鳥）
- 1200mAh 電池容量比現行 1000mAh 略大
- WebUI / APP 可遠端管理電池狀態

**缺點**：
- 價格約現行方案 6 倍
- 為 Pi Zero 形狀設計，**外殼空間需重新評估**（65 × 30mm × 約 10mm 厚）
- I2C 0x57 / 0x68 位址需與其他 I2C 裝置（OLED、未來感測器）協調避開
- **板上小鈕扣電池是 RTC 備用**（CR 系列鋰錳，不可充），用完需更換

**適用情境**：Prod-Phase 量產且想一次整合 RTC、電量顯示、APP 管理的進階方案

### 9.4 候選方案 C：SparkFun Thing Plus - ESP32-S3

> ⚠️ **此方案性質與 §9.2/§9.3 不同**：A/B 是「電源附加模組」（外掛在主控板上），C 是「**主控板替換方案**」（整片取代目前 GOOUUU ESP32-S3 + 充電模組 + microSD 模組三件套）。切換 C 需重做 GPIO mapping 並調整韌體 partition table。

**官方產品介紹**（節錄自 [sparkfun.com/sparkfun-thing-plus-esp32-s3.html](https://www.sparkfun.com/sparkfun-thing-plus-esp32-s3.html)）：

> The board we rely on for demanding IoT projects with the ESP32-S3, this Feather-compatible board features a 240MHz dual-core processor, 2MB PSRAM, WiFi, and Bluetooth 5 LE. With on-board MCP73831 LiPo charger, MAX17048 fuel gauge, USB-C, microSD slot, and Qwiic connector, it integrates everything you need for a battery-powered project on a single board.

**核心規格**：

| 項目 | 規格 |
|---|---|
| 主控 | ESP32-S3-MINI1-**N4R2**（4MB Flash + 2MB PSRAM）|
| 充電 IC | **MCP73831**（Microchip 工業級，預設 214mA@3.3V）|
| 電量計 | **MAX17048**（I2C @ 0x36，可讀電量百分比 + 電壓）|
| 升壓 IC | 不需要（LiPo 3.7V 直驅板載 RT9080 LDO 輸出 3.3V@500mA）|
| 輸入 | USB-C 5V / 2-pin JST LiPo 3.7V |
| 輸出 | 3.3V@500mA（板載 LDO）|
| 電池接頭 | **JST PH 2.0**（與你目前 1000mAh 523450 電池相容）|
| microSD | **板載卡槽**（共用 SPI bus）|
| I2C 介面 | **Qwiic 連接器**（4-pin SH 1.0，免焊接）|
| 系統按鈕 | BOOT / RESET（不可作 user button）|
| LED | WS2812 RGB（GPIO 46）+ 藍色 STAT |
| GPIO 引出 | 21 個（其中 17 個可 ADC）|
| 形狀 | Feather form factor，**65 × 23 × ~10mm** |
| 價格 | **$24.95 USD（≈ NT$800）** + 國際運費 |
| 含電池 | ❌ 需另購 |

**核心優勢**：
- **一片解決電源 + microSD + I2C bus**：充電 IC + 燃料計 + LiPo JST + microSD + Qwiic + USB-C 全板載，省下 §9.2/§9.3 額外模組
- **MAX17048 燃料計**直接讀電量百分比，可在 OLED 顯示「XX% / 剩 N 小時」（比 PowerBoost LBO 二元警告精準）
- **板載 microSD 直接支援 SD 卡 OTA**：拔卡更新韌體，~5 秒燒完，比 BLE OTA 快 6~12 倍且不需拆殼（見 §9.8.2）
- **Feather form factor 體積小**（65 × 23mm），外殼設計簡化、走線變少
- ESP32-S3 chip 與目前 GOOUUU 同型號 → **韌體幾乎可平移**，只需改 GPIO mapping

**核心限制**：
- ❌ **板上沒有 user button / OLED / 蜂鳴器 / 震動 / 麥克風 / TFT** — 全部仍要外接（這片是主控板，不是完整裝置）
- ⚠️ **Flash 縮水 N8 → N4**（8MB → 4MB）— 完整韌體預估 1.58MB > 預設 OTA partition 1.5MB，需自訂 partition table 拉到 1.94MB（見 §9.8.2 + §9.8.4）
- ⚠️ **GPIO 21 支對外接需求 22 支不夠** — 需 MCP23017 I2C IO Expander 把 8 顆按鈕走 I2C（見 §9.8.3）
- ⚠️ **BOOT 鍵是 strapping pin**（GPIO 0），不可挪用為 user button
- ⚠️ **Qwiic 預設 I2C 腳位**（GPIO 8/9 推測）需查 schematic 確認，採購後實機驗證

**適用情境**：Prod-Phase pilot 量產（5~20 台），需要高整合度、體積要小、想用 SD 卡 OTA 取代 BLE OTA、且接受重做 GPIO mapping 的場景

### 9.5 四方案對照

| 項目 | **現行（聯騰 + 523450 + DIY）** | **A: PowerBoost 1000C** | **B: PiSugar 3** | **C: Thing Plus ESP32-S3** |
|---|---|---|---|---|
| 性質 | 電源模組 | 電源附加模組 | 電源附加模組（含 RTC）| **主控板替換** |
| 含電池 | ❌（單獨採購）| ❌（單獨採購）| ✅ 1200mAh 內建 | ❌（單獨採購）|
| 充電 IC | 山寨 TP4056 | Microchip MCP73871（工業級）| 整合方案 | **Microchip MCP73831（工業級）**|
| 升壓 IC | 山寨版 SX1308 推測 | TI TPS61090（工業級）| 未公開（整合方案）| 不需要（LiPo 直驅板載 LDO 3.3V）|
| Load-sharing | ⚠️ 可能不真實 | ✅ 真實作 | ✅ UPS 等級 | ✅ MCP73831 自帶 |
| RTC | ❌ 需外加 DS3231 | ❌ | ✅ 內建 + 寫入保護 | ❌ 需外加 DS3231 |
| I2C 電量計 | ❌ | ❌（僅 LBO 二元低電警告）| ✅ 0x57 / 0x68 | ✅ MAX17048 @ 0x36 |
| 軟關機 | ❌ | ✅（EN pin）| ✅（按鈕觸發）| 可由 GPIO 切 LDO 模擬 |
| 電池接頭 | PH 2.0 焊盤需自焊母座 | PH 2.0 母座已焊 | 內建免接線 | **PH 2.0 母座已焊** |
| microSD | ❌ 需外加模組 | ❌ | ❌ | **✅ 板載卡槽（共用 SPI bus）** |
| OTA 通道 | USB 直燒 | USB 直燒 / BLE | USB 直燒 / BLE | **USB / SD 卡 / BLE** |
| 主控板需求 | 維持 GOOUUU | 維持 GOOUUU | 維持 GOOUUU | **本身即主控板（替換 GOOUUU）** |
| Flash 容量 | N8（8MB，無壓力）| N8（8MB）| N8（8MB）| ⚠️ N4（4MB，需 partition 微調）|
| 總成本（含 1000mAh 電池）| **NT$205** | NT$650 + NT$130 + 運費 ≈ **NT$900+** | NT$1,300 + 運費 ≈ **NT$1,500** | NT$800 + NT$130 + MCP23017 NT$50 ≈ **NT$1,000** |
| 取得難度 | 蝦皮現貨 1~3 天 | 海外代購 7~14 天 | 海外運送 14~21 天 | 海外代購 7~14 天 |
| 適用階段 | Dev-Phase 2 原型 | Prod-Phase 量產 | Prod-Phase 量產 + 進階功能 | **Prod-Phase pilot 整合替換** |

### 9.6 決策建議

| 情境 | 推薦方案 |
|---|---|
| **現在（Dev-Phase 2 原型驗證）** | 走現行 DIY，沒必要花大錢 |
| **單台原型出現電源穩定度問題** | 升級 A（PowerBoost 1000C）|
| **Prod-Phase 5~10 台量產（維持 GOOUUU 主控）** | 升級 A（成本可控、工業級可靠度）|
| **Prod-Phase + 想要 RTC + 電量顯示 + APP** | 升級 B（PiSugar 3）|
| **Prod-Phase pilot + 想要 SD 卡 OTA + 體積要小 + 整合度高** | **升級 C（SparkFun Thing Plus ESP32-S3）** |
| **量產 50+ 台** | 自製 PCB + ESP32-S3-WROOM-1-N16R8（16MB Flash 永遠不擔心）|

### 9.7 接線參考（候選方案 A：PowerBoost 1000C）

> 此節為未來切換到 PowerBoost 1000C 時的接線指南，目前僅作備案，不影響現行 §5 組裝流程。

#### 9.7.1 必接（最小可動）

| PowerBoost 1000C | → | ESP32-S3 GOOUUU | 說明 |
|---|---|---|---|
| BAT（JST PH 2.0 母座） | → | LiPo 3.7V 1000mAh 公頭 | 紅(+) 黑(-)；反接會炸 |
| 5V | → | VIN（**不是 3V3**）| 走板載 LDO 轉 3.3V |
| GND | → | GND | 共地 |
| Micro USB | → | 對外充電孔 | 插入時走 load-sharing：USB 直供 + 同時充電池 |

#### 9.7.2 強烈建議接

| PowerBoost 1000C | → | ESP32-S3 GPIO | 說明 |
|---|---|---|---|
| LBO | → | 任一輸入腳（內部 pull-up）| 電池 < 3.2V 時拉低，作為低電量警告 / OLED 提示 |
| EN | → | 滑動開關到 GND（或 GPIO 程控）| 拉低 = 軟關機；浮接 = 輸出常開 |

#### 9.7.3 接線注意事項

1. **VIN ≠ 3V3**：5.2V 必須接 VIN 或 5V pin；接 3V3 會直接燒板載 LDO
2. **LBO 是 open-drain**：必須有 pull-up（用 ESP32 內部 pull-up 即可），否則讀不到 LOW
3. **EN 預設高電位**：浮接時輸出常開；要做關機開關必須接到 GND
4. **JST PH 2.0 極性**：Adafruit 標準是紅(+) 黑(-)；自製電池接頭前用三用電表確認
5. **load-sharing 行為**：插上 Micro USB 時系統由 USB 供電同時充電池，**不會** brown-out；拔 USB 才走電池放電
6. **輸出 5.2V 不是 5.0V**：TPS61090 特意拉高補償 USB 線損，ESP32-S3 VIN 可接受 5.0~5.5V 區間

#### 9.7.4 電流預算驗證（1A 額定）

| 元件 | 峰值電流 |
|---|---|
| ESP32-S3（WiFi / BLE TX 峰值）| ~250mA |
| OLED 1.3"（SH1106）| ~20mA |
| 蜂鳴器（主動式）| ~30mA |
| 8 × 按鈕 pull-up | < 1mA |
| INMP441 麥克風（Phase 1.5）| ~1.4mA |
| MicroSD 寫入（Phase 1.5 峰值）| ~100mA |
| **合計** | **< 500mA** |

PowerBoost 1000C 額定 1A，餘量約 2 倍，符合 SoT V1 §20.5 擴充模組功耗預算。

#### 9.7.5 切換時需回填的文件

LBO 偵測腳位實際選定後，需同步更新：

- [ ] `docs/gpio-allocation.md`：新增 LBO 偵測腳位、EN 控制腳位（若用 GPIO 而非開關）
- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §20.4.3：充電 IC 改寫為「Adafruit PowerBoost 1000C（MCP73871 + TPS61090）」
- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §21.1：主要硬體清單替換電源模組
- [ ] `docs/pcb-outsourcing-guide.md`：PCB 預留 PowerBoost 1000C（23 × 45 × 10mm）腳位與固定孔

### 9.8 接線參考（候選方案 C：SparkFun Thing Plus-ESP32-S3）

> 此節為未來切換到 SparkFun Thing Plus-ESP32-S3 時的接線指南，目前僅作備案，不影響現行 §5 組裝流程。對應 §9.4 候選方案 C 的所有 §9.8.x 引用。

#### 9.8.1 板載周邊整合（免外接）

切換到方案 C 後，以下三件原本要外接的模組可省略：

| 原方案需外接 | 方案 C 板載 | I2C / 介面 |
|---|---|---|
| TP4056 充電板 | ✅ MCP73831 充電 IC | — |
| 電量監控（無）| ✅ MAX17048 燃料計 | I2C `0x36` |
| MicroSD 模組 | ✅ 板載 microSD 卡槽 | SPI（共用 bus）|
| I2C bus 接線 | ✅ Qwiic 連接器 | 4-pin SH 1.0 免焊 |

**仍需外接**：8 顆按鈕、OLED、蜂鳴器、震動、INMP441 麥克風、MCP23017。

#### 9.8.2 SD 卡 OTA 與 Partition Table

**為何要 SD 卡 OTA**：
- BLE OTA 通過 NUS 傳 1.58MB 韌體 ~30~60 秒，且要 App 端配合
- SD 卡 OTA：拔卡 → 換新 firmware.bin → 插回開機 → bootloader 自動 flash，~5 秒搞定
- 救護現場不需拆殼、不需配對 App，現場能換版

**自訂 Partition Table**（4MB Flash 規劃）：

```csv
# Name,    Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1F0000,  # 1.94MB（拉大）
app1,     app,  ota_1,   ,        0x1F0000,  # 1.94MB（拉大）
spiffs,   data, spiffs,  ,        0x10000,   # 64KB 設定檔
```

- `app0 + app1 = 3.88MB`，剩 0.12MB 給 nvs / spiffs
- 完整韌體 1.58MB < 1.94MB，**符合**
- 比預設 partition（OTA 各 1.5MB）多 0.44MB，未來韌體擴充有餘量

**OTA 流程程式碼骨架**：
```cpp
#include <SD.h>
#include <Update.h>

void checkSdOta() {
  if (!SD.exists("/firmware.bin")) return;
  File f = SD.open("/firmware.bin");
  if (!Update.begin(f.size())) return;
  Update.writeStream(f);
  if (Update.end(true)) {
    SD.remove("/firmware.bin");  // 刷完刪檔避免重刷
    ESP.restart();
  }
}
```

#### 9.8.3 MCP23017 I/O 擴充接線

**為何需要**：SparkFun Thing Plus-ESP32-S3 對外 GPIO 21 支，扣掉 I2S（3）+ OLED（共用 Qwiic）+ 蜂鳴器（1）+ 震動（1）+ INTA（1）後，剩約 12~14 支自由分配。8 顆按鈕全走本體會太緊，PM 也明確要求**主鍵 / EPI / 電擊保留本體 GPIO**，其餘 5 顆走 MCP23017。

**MCP23017 接線**：

| MCP23017 pin | → | ESP32-S3 | 備註 |
|---|---|---|---|
| VDD (9) | → | 3V3 | |
| VSS (10) | → | GND | |
| SDA (13) | → | GPIO 8（Qwiic SDA）| Wire bus 共用 |
| SCL (12) | → | GPIO 9（Qwiic SCL）| 同上 |
| RESET (18) | → | 3V3 | **不可浮接** |
| A0/A1/A2 (15/16/17) | → | GND × 3 | 位址 = `0x20` |
| INTA (20) | → | 任一 GPIO | 5 顆按鈕中斷通知 |

**I2C 位址表**（避衝突）：

| 裝置 | 位址 | 來源 |
|---|---|---|
| MCP23017 | `0x20` | 新採購 |
| MAX17048 燃料計 | `0x36` | 板載 |
| SH1106 OLED | `0x3C` | 現行 |
| DS3231 RTC（Dev-Phase 3）| `0x68` | 預留 |

✅ 無衝突。

**按鍵分配**（對齊 PM #6）：

| 按鍵 | 走哪 | 理由 |
|---|---|---|
| 主鍵（長按 3s OHCA 入口）| ESP32-S3 本體 GPIO | 救命路徑、零延遲 |
| EPI 鍵 | ESP32-S3 本體 GPIO | 倒數計時即時觸發 |
| 電擊鍵 | ESP32-S3 本體 GPIO | 同上 |
| 上 / 下 / Power / 錄音 / 返回 | MCP23017 | 選單操作慢 1ms 無感 |

> 詳細採購與焊接清單見 [`docs/hardware-procurement-v2.md`](hardware-procurement-v2.md)。

#### 9.8.4 Flash 預算驗證

| 區塊 | 大小 | 累計 |
|---|---|---|
| Bootloader | ~32KB | 32KB |
| Partition table | 4KB | 36KB |
| NVS | 20KB | 56KB |
| OTA data | 8KB | 64KB |
| **app0**（OTA slot 0）| 1.94MB | 2.00MB |
| **app1**（OTA slot 1）| 1.94MB | 3.94MB |
| SPIFFS | 64KB | 4.00MB |

✅ **剛好填滿 4MB，無餘量**。未來韌體超過 1.94MB 時必須：
- 升級到 ESP32-S3-WROOM-1-**N16R8**（16MB Flash）— 但要自製 PCB
- 或放棄 OTA，改全燒 factory partition（破壞 SD 卡 OTA 功能）

#### 9.8.5 MAX17048 燃料計使用

**Library**：
```ini
# platformio.ini
lib_deps = sparkfun/SparkFun MAX1704x Fuel Gauge Arduino Library@^1.0.4
```

**程式碼骨架**：
```cpp
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
SFE_MAX1704X lipo(MAX1704X_MAX17048);

void setup() {
  Wire.begin();
  lipo.begin();
  lipo.quickStart();  // 校正
}

void loop() {
  float voltage = lipo.getVoltage();      // 3.0~4.2V
  float percent = lipo.getSOC();          // 0~100%
  float change_rate = lipo.getChangeRate(); // %/hr，正=充電 負=放電
  // OLED 顯示
}
```

**OLED 顯示建議**：
```
Battery: 78% (3.85V)
Time left: ~3h 20m (放電中)
```

#### 9.8.6 切換時需回填的文件

切換到方案 C 後，需同步更新：

- [ ] `docs/gpio-allocation.md`：全部重編（SparkFun GPIO 編號跟 GOOUUU 完全不同）
- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §20.4.3：充電 IC 改寫為「SparkFun Thing Plus-ESP32-S3 板載 MCP73831」
- [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §21.1：主要硬體清單替換主控板
- [ ] `docs/pcb-outsourcing-guide.md`：PCB 預留 Feather form factor（65 × 23mm）
- [ ] `firmware/platformio.ini`：board 改 `sparkfun_esp32s3_thing_plus`
- [ ] `firmware/partitions/custom_4MB_ota.csv`：新增自訂 partition table
- [ ] `firmware/src/main.cpp`：所有 pin 常數重定義
- [ ] `firmware/src/button_manager/`：抽 IButtonSource 介面 + Mcp23017ButtonSource 實作

## 10. 目前採購決策（2026-05-14 快照）

### 10.1 Pilot 階段雙軌策略

| 模組 | 角色 | 狀態 | 用途 |
|---|---|---|---|
| **聯騰 MTARDTP4056S 一體版** | 主驗證模組 | 即將採購（蝦皮 1~3 天）| 馬上驗證 BLE / TFT / button 功能、確認電源切換穩定度 |
| **PowerBoost 1000C** | 對照組 + 量產候選 | 已下單，到貨需 3~4 週 | 對比聯騰山寨板，驗證真實 load-sharing 行為 |
| **MAX17048 燃料計** | UX 加值 | **延後採購** | 等電源穩定 + PM 確認 UX 後再買（見 §10.4）|

### 10.2 處理順序

```
[Step 1] 採購聯騰一體版（買 2 片：1 用 1 備）
[Step 2] 焊接 PH2.0 母座 + 校壓 5.00V ± 0.05V + 鎖膠
[Step 3] 接 ESP32-S3 VIN，跑 §5.4 四項整合測試
[Step 4] 觀察「USB-C 拔插時 ESP32 是否 reset」（山寨板最常見的雷）
[Step 5] 救護現場跑半天～一天，確認穩定無 reset
[Step 6] PowerBoost 到貨後重做 Step 3~4，對比 load-sharing 行為差異
[Step 7] 確認電源拓樸後 → 採購 MAX17048（時機見 §10.4）
```

### 10.3 為何選聯騰一體版而非「TP4056 純充電板 + MT3608 升壓板」兩片串接

| 維度 | 聯騰一體版 | TP4056 + MT3608 兩片串接 |
|---|---|---|
| IC 顆數 | 4 顆整合一片 | 4 顆分兩片 |
| 焊接點 | 4 點 + 1 trim pot 校壓 | 8 點 + 1 trim pot 校壓 |
| 雜訊耦合 | 一片內走線、乾淨 | 兩片間靠杜邦/飛線、容易吃雜訊 |
| 失敗模式 | 少 | 多一個「串接線壓降觸發 MT3608 brown-out 重啟 ESP32」雷 |
| Load-sharing | IC 內部協調（不完美但有）| 兩片獨立工作（更差）|

→ **聯騰一體版焊接工少一倍、debug 風險低**，量產之前都用這個。

### 10.4 MAX17048 採購觸發點

不急著買，**等下面任一條件滿足再下單**：

| 觸發條件 | 為何要等 |
|---|---|
| 聯騰版整合測試全綠 | 不穩定的電源會干擾 MAX17048 量測，先確認電源沒問題 |
| PM 確認 UX 要顯示百分比 / 剩餘時間 | 若 PM 說「電量圖示閃就夠」（對齊 SoT §13.17 字面），MAX17048 可省 |
| 開始實作 Impl-Phase H（電源管理）| 那時才會真正寫到 fuel gauge UI 程式碼 |

**推薦型號**：Adafruit MAX17048（PID 5580）— 雙 JST-PH 接口可直接串在電池與 PowerBoost 之間、STEMMA QT 免焊 I2C、Adafruit_MAX1704X lib 直接套。

### 10.5 聯騰版未支援的功能（要記在工程已知缺陷）

| 功能 | 聯騰能做嗎 | 解法 |
|---|---|---|
| 長按 Power 鍵硬體關機 | ❌ 沒 EN pin | 軟體 `esp_deep_sleep_start()` 暫代 |
| 低電量警告 | ❌ 沒 LBO pin | 需補訊號源（詳見 §10.6 三選項；**ADC 腳已用盡**，走燃料計或飛 TP4056 狀態腳，非走 ADC）|
| 真實 load-sharing | ⚠️ 不完美 | PowerBoost 取代或量產自製 PCB 解決 |

---

### 10.6 電池顯示能力現況與選項（Phase H 韌體參照）

> 📌 **做 SoT V1 Phase H（電源管理 / 電池顯示 / 低電量警告）前先讀本節。** 結論：現有硬體（N16R8 + TFT + 聯騰一體板）韌體對電池「零訊號路徑」，要顯示任何電池資訊都得**先補硬體訊號源**。

#### 為什麼現在什麼都讀不到

兩條常見路徑都斷：

1. **電壓 ADC 粗估 → 無 ADC 腳可用**：ESP32-S3 的 ADC 只在 GPIO 1–20；本專案 GPIO 1/2/3（唯三 ADC1 腳）因 N16R8 模組把 SPI MOSI/SCK 逼上 GPIO 2/3、DC 上 GPIO 1，**三支 ADC 全給 TFT**（見 `gpio-allocation.md §5.1`）。空閒的 GPIO 43/44 是 UART0 腳、**不具 ADC 能力**。
2. **充電狀態數位讀取 → 聯騰板沒拉狀態腳**：聯騰一體板只有板上紅/綠充電 LED（韌體看不到），**未拉出 CHRG/STDBY，也無 LBO 低電腳**（見 §10.5）。

→ 「沒有燃料計」不等於省事，目前是**根本沒有訊號來源**。

#### 三個選項（由省到不省）

| 選項 | 訊號來源 | 韌體能顯示 | 硬體代價 | 需 ADC？ |
|---|---|---|---|---|
| **A 數位狀態腳** | TP4056 CHRG/STDBY（+ 理想上 LBO）→ GPIO 43/44 | ⚡充電中 / ✅充飽 / 🔋低電量圖示閃（**二元**）| 換/飛一片有拉出這些腳的板子（PowerBoost 有 LBO；或現板 IC CHRG 腳飛線）| ❌ 不需 |
| **B 電壓 ADC** | 電池電壓經分壓 → ADC 腳 | **4 格粗略電量條**（校準後靜態 ±10–15%，帶載飄；鋰電 3.7–3.9V 平坦區難分 40–80%）| 要從 TFT 挪一支 ADC 腳出來，**動 GPIO 重規劃** | ✅ 需（現無）|
| **C MAX17048 燃料計** | I2C `0x36`（掛現有 41/42 bus，DS3231 在 0x68）| 精準 %（±1%）+ 剩餘時間估算 | 同 I2C bus 再拉兩線 + 一顆晶片 | ❌（走 I2C）|

#### 對 SoT 規格的滿足度

- SoT §2.1「電池 / 充電狀態顯示」+ 提醒行為表「低電量 → 圖示閃、不發聲」要的是**圖示 + 低電警告**，不是精準 %。**選項 A 即可合規**（充電狀態 + 低電二元閃）。
- 但「**4 格電量圖示**」的分級（滿 / 中 / 低）需要**電壓或燃料計的 level 資訊**：選項 A 只給「二元低電 + 充電」，畫不出漸變格數；要真正 4 格條得走 B 或 C。
- MAX17048 在採購清單（`hardware-procurement-v2.md #13`）標的是「**選配 / UX 加值**」，非 V1 硬需求。

#### 反直覺結論與路線建議

- **只要合規**（充電 + 低電閃）→ **選項 A**，最小硬體改動，純數位接 GPIO 43/44，不碰 ADC。
- **要電量百分比 / 4 格條**（UX 加值）→ **別走 B，直接上 C（MAX17048）**。因為 ADC 已被 TFT 佔滿，為電壓法動 TFT 腳位比「同 I2C bus 掛一顆燃料計」還麻煩。「沒燃料計」反而是最尷尬的中間態。

#### 動工前要先確認 / 補的事

- [ ] **現板 TP4056 CHRG 腳是否有可焊 pad**（本節從「無 LBO pin」推斷狀態腳也沒拉出，但 CHRG 有可能在 IC 腳飛得到）→ 決定選項 A 是飛線還是換板
- [ ] 選定選項後，同步更新 `gpio-allocation.md`（新增狀態腳 / ADC 腳分配）與韌體 `main.cpp` 讀取邏輯
- [ ] 韌體端電量圖示 UI 對齊 SoT 提醒行為表「低電量 → 圖示閃、不發聲」；4 格圖示規格落點見 memory「四格電量圖示規格來源」

---

## 11. 量產 PCB 設計建議（Prod-Phase）

> 目標：當 pilot 跑滿 5~10 台、Dev-Phase 1~4 全綠後，自製 PCB 時的電源拓樸與 IC 選型建議。

### 11.1 推薦拓樸：Buck-Boost 3.3V 直驅

**核心 insight**：EMS Timer 全系統元件（ESP32-S3 / ST7789 TFT / SD 卡 / INMP441 / 蜂鳴器 / 震動）**沒有真正需要 5V 的**。量產不必沿用 GOOUUU 開發板「升壓到 5V 再降回 3.3V」的繞路設計。

```
USB-C ─► [充電 IC] ─► LiPo 3.7V ─► [Buck-Boost 3.3V] ─► ESP32-S3 3V3 pin + 全周邊
            │              │
            └──► [保護 IC]  └──► [燃料計] ──► I2C ──► ESP32
```

### 11.2 三條候選拓樸對比

| | **A: 5V 路線**（沿用 GOOUUU 思路）| **B: Buck-Boost 3.3V** ★ 推薦 | **C: PMIC 一片解決** |
|---|---|---|---|
| 拓樸 | 充電 + 升壓 5V + LDO 3.3V | 充電 + Buck-Boost 3.3V | PMIC 三合一 |
| IC 顆數 | 3 顆主動 | **2 顆主動** | **1 顆主動** |
| PCB 面積 | ~25×30mm | ~20×20mm | ~15×15mm |
| 效率 | ~80%（兩級轉換）| **~93%**（一級）| ~85% |
| Noise | 高（LDO 散熱）| 低 | 高（IP5306 行動電源 IC）|
| 待機行為 | 正常 | 正常 | ❌ IP5306 無負載 10 秒自動關機，救護待機災難 |
| 工程風險 | 低（業界主流）| 中（buck-boost layout 較敏感）| 高（待機問題 + noise）|
| BOM 成本 | NT$60/台 | NT$70/台 | NT$50/台 |

→ **B 路線是 EMS Timer 量產正解**。C 雖然便宜，但 IP5306 待機自動關機對救護場景是 deal-breaker。

### 11.3 拓樸 B 具體 IC 選型

| 角色 | 推薦 IC | 為何選它 | 單價 |
|---|---|---|---|
| **充電 IC** | **MCP73831** | Microchip 工業級、SOT-23-5 小封裝、單電阻調充電電流、PowerBoost 同款 | NT$20 |
| **電池保護** | **DW01 + 8205A** | 經典組合、便宜可靠；或直接買含保護板的電池 | NT$10 |
| **Buck-Boost** | **TPS63802** | TI、輸入 1.3~5.5V、輸出 1.8~5.5V、2A、95% 效率、開關 2.5MHz（電感小）| NT$60 |
| **燃料計**（選配）| **MAX17048** | I2C `0x36`、能讀 0~100% + 充放電速率、Adafruit/SparkFun 同款 lib | NT$50 |
| **靜電保護** | **PRTR5V0U2X** | USB-C D+/D- ESD 保護、量產必加 | NT$5 |
| **被動元件** | 電感 1.5μH + Caps + Rs | TDK / Murata、不要買淘寶散裝 | NT$15 |

**單台電源 BOM 約 NT$160**（含燃料計）/ **NT$110**（不含）。

### 11.4 PCB Layout 三大關鍵

Buck-Boost IC 是高頻開關電路（2.5MHz），layout 錯了會 EMI 爆炸、自激震盪、過熱。

#### 1. 開關迴路面積最小化
```
[IC SW pin] ─► [電感] ─► [輸出 cap] ─► GND
                                      ↑
                                  這條迴路要短到讓你心痛
```
- 電感、輸入 cap、輸出 cap **三個元件貼著 IC**，距離 < 2mm
- 迴路面積大 = 天線 = EMI 過 EMC 不了

#### 2. GND Plane 不切斷
- 4 層板（建議）：Top（信號）+ GND + Power + Bottom（信號）
- 開關電路下方 GND **必須完整不切斷**，否則迴流路徑變長 → 雜訊
- 2 層板（省錢）：Bottom 全 GND plane、不要走訊號

#### 3. 充電 IC 散熱
- MCP73831 充 500mA 時會發燙（~3°C 上升）
- IC 下方鋪銅、用 thermal via 連到 bottom GND plane
- 不做散熱 = 充電速度降速、IC 壽命短

### 11.5 進入量產前的 Pilot 必驗測試（為 PCB 鋪路）

**現在 / PowerBoost 階段就要做**，這些測試直接決定量產 PCB 設計：

| 測試 | 方法 | 量產決策影響 |
|---|---|---|
| TFT 在 3.3V 夠亮嗎 | 拔掉 5V，飛線從 GOOUUU 3V3 pin 拉到 TFT VIN | 夠亮 → 拓樸 B；不夠 → 加 5V 升壓專餵 TFT |
| 蜂鳴器 3.3V 夠大聲 | 同上條件下測救護現場噪音中聽不聽得到 | 不夠 → 加 5V 升壓 or 換更大聲的蜂鳴器 |
| SD 卡 3.3V 寫入 | 同上 | SD 本來就 3.3V，這是 sanity check |
| LiPo 4.2V 餵 3V3 pin | 直接 LiPo → 3V3 pin、不接 LDO，觀察是否異常 | 確認 ESP32-S3 3V3 規格上限是否真的 3.6V hard limit |

→ 測試結果直接決定量產 PCB 要不要保留 5V 升壓電路，**做這個測試的 ROI 比什麼都高**。

### 11.6 進入量產的觸發條件

5 個條件全部滿足才進量產 PCB 打樣：

| 條件 | 判準 |
|---|---|
| 功能凍結 | Dev-Phase 1~4 全綠、PM 不再加需求 |
| Pilot 跑滿 5~10 台 | 救護現場至少跑過 1 個月、收 ≥ 3 次 bug feedback |
| BOM 穩定 6 個月 | 所有零件無 EOL 風險、有第二供應商 |
| 韌體 OTA 機制驗證 | SD 卡或 BLE OTA 能讓救護員自己升級 |
| 外殼設計凍結 | 開模費（~NT$50k~150k）能攤平 |

任一條件未滿足就回 pilot 多跑幾台，**不要急著進 SMT 量產**。

### 11.7 量產拓樸選擇決策流程

```
Pilot Step 5「TFT/蜂鳴器/SD 卡 3.3V 驗證」結果：
   │
   ├─► 全 PASS → 拓樸 B（Buck-Boost 3.3V 直驅）
   │              IC: MCP73831 + TPS63802 + (MAX17048)
   │              BOM: ~NT$110~160/台
   │
   ├─► 蜂鳴器不夠大聲 → 拓樸 A 變體（保留 5V 升壓專餵蜂鳴器）
   │                    主邏輯仍走 3.3V，5V 只供蜂鳴器
   │
   └─► TFT 背光不夠亮 → 評估換 TFT 或加 5V 升壓專餵 LED
                          量產 PCB 加 small boost IC（TPS61023）
```

---

## 12. 版本紀錄

| 日期 | 版本 | 變更 | 作者 |
|---|---|---|---|
| 2026-05-06 | v1.0 | 初版建立，採購清單與驗收 SOP | Max Ho |
| 2026-05-06 | v1.1 | 新增 §9 未來升級路線（PowerBoost 1000C / PiSugar 3 候選方案）| Max Ho |
| 2026-05-06 | v1.2 | 新增 §9.7 PowerBoost 1000C 接線參考（必接腳位、EN/LBO、電流預算、切換回填清單）| Max Ho |
| 2026-05-07 | v1.3 | 新增 §9.4 候選方案 C（SparkFun Thing Plus ESP32-S3 主控板級整合方案）+ §9.5/§9.6 對照表與決策建議擴充為四方案 | Max Ho |
| 2026-05-07 | v1.4 | **補回 §9.8 缺漏章節**：板載周邊整合、SD 卡 OTA + Partition table、MCP23017 接線、Flash 預算、MAX17048 使用、切換回填清單。對齊 hardware-procurement-v2.md | Max Ho |
| 2026-05-14 | v1.5 | 修正製造商名稱「蓮騰」→「聯騰」（LIAN TENG ELECTRONIC）；新增 §10 目前採購決策快照（雙軌 pilot 策略、MAX17048 延後採購觸發點、聯騰版未支援功能清單）；新增 §11 量產 PCB 設計建議（buck-boost 3.3V 拓樸、IC 選型、PCB layout 三大關鍵、量產觸發條件）| Max Ho |
