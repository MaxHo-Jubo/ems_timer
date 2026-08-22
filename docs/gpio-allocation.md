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

主顯示為 2.8" ST7789 TFT（SPI bus）—— 完整腳位見 §5.2。

| 用途 | GPIO | 介面 | 模組 | 狀態 |
|------|------|------|------|------|
| I2C SDA | **42** | I2C | — | 🟡 預留 |
| I2C SCL | **41** | I2C | — | 🟡 預留 |

> 📌 原 1.3" SH1106 OLED（I2C，GPIO 41/42）於 2026-05-08 拆除改 SPI TFT，I2C bus 整段釋出。GPIO 41/42 目前無裝置，預留給 §5.4 DS3231 RTC（Dev-Phase 3）與 §5.3 CO 感測器（電化學型 I2C）擴充。

---

## 3. 提醒輸出

| 用途 | GPIO | 模組 | 狀態 |
|------|------|------|------|
| 蜂鳴器 PWM | **14** | 主動式蜂鳴器 | ✅ 啟用 |
| 震動馬達 | **21（與 TFT CS 互斥）** | S8050 NPN + 1kΩ 基極 | ❌ `ENABLE_VIBRATION = 0` 停用 |

> 🔴 **GPIO 21 互斥約束（2026-05-08 標註）**：本欄震動馬達 GPIO 21 與 §5.2 TFT CS GPIO 21 為同一支腳，**依專案進度擇一啟用**：
> - **TFT 整合前**：可啟用震動馬達（`ENABLE_VIBRATION = 1`），TFT CS 暫無物件占用
> - **TFT 整合後（Impl-Phase B+）**：TFT CS 正式占用 GPIO 21，震動馬達必須搬走或保持停用
> - 若 Prod-Phase 量產同時需要 TFT 顯示 + 震動回饋，震動馬達需重新分配 GPIO（候選：剩餘空閒中找 PWM 能力腳位，需重做 §5.1 盤點）

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
- GPIO 43, 44 — UART0 預設腳位（U0TXD/U0RXD），**目前完全空閒可用**。本韌體 `Serial` 走 USB-CDC（晶片內建 USB，GPIO 19/20，見上方禁用清單），不經過 43/44，USB 除錯與 43/44 互不相關。註：開機瞬間 ROM bootloader 會對 UART0 印 boot log，app 啟動後即釋出，不影響後續挪用
- GPIO 47, 48 — 通用 GPIO

### 5.2 TFT SPI bus 腳位（2026-05-08 實機修正：GPIO 35/36/37 不可用）

> 🔴 **2026-05-08 實機踩雷**：採購到的 GOOUUU TECH ESP32-S3 開發板實際為 **N16R8 octal PSRAM 模組**（金屬遮蔽印 `ESP32-S3-N16R8`），GPIO 35/36/37 內部給 PSRAM，當 IO 用即 `Guru Meditation StoreProhibited`。原 §5.1 「N16R8 模組可能被佔」的警告由「保守提示」升級為「強制限制」。**MOSI/SCK 必須改用 ADC1（GPIO 2/3）**，犧牲兩支 ADC。詳見 `tft-migration-plan.md §3.1` 與 `firmware/platformio.ini [env:tft-smoke-test]` 註解。

| 用途 | GPIO | 備註 |
|------|------|------|
| SPI MOSI | **2** | 原 35 → 改 2（ADC1_CH1，已取消 ADC 候選） |
| SPI SCK | **3** | 原 36 → 改 3（ADC1_CH2，已取消 ADC 候選） |
| SPI MISO | **不接**（TFT 純寫入） / MicroSD 加入時用 **44** | 原 37 不可用、GPIO 1 已改派 TFT DC；MicroSD MISO=44 與 CS=43 配對，整組佔用 UART0 腳 43/44 |
| TFT CS | **21** | 獨立，PWM 能力 |
| TFT DC | **1**（2026-05-08 改） | 原 48 與 GOOUUU 板上 WS2812 RGB LED 衝突 → DC 切換時 LED 會把訊號吃成色彩資料 |
| TFT RST | **47** | Hardware reset |
| TFT BL（背光） | **接 3.3V 常亮** | 原計畫 PWM 走 GPIO 1，現 GPIO 1 給 TFT DC，BL PWM 能力放棄 |
| MicroSD CS | **43**（UART0 腳，與 USB-CDC 無關，可直接用）或不接 | 候選 GPIO 2 已給 SPI MOSI、GPIO 47 與 TFT RST 衝突，43 為唯一可用腳；但與 §5.3 CO-UART 候選同腳，兩者擇一 |

> 📌 **2026-05-08 實機驗證通過配置**：
> - Library：**Adafruit_ST7789 + Adafruit_GFX**（不用 TFT_eSPI 2.5.43，原因見下方註記）
> - 必加：`tft.invertDisplay(false)`（蝦皮買的 ST7789 紅板出廠 polarity 與 Adafruit 預設相反）
> - VCC：**接 5V/VIN**（板上有 LDO + level shifter，吃 3.3V VCC 會 brown out）
> - LED：接 3.3V 常亮
> - 訊號線（CS/DC/RST/MOSI/SCK）：3.3V 邏輯，不需 level shift（板子自帶）

> 🚫 **TFT_eSPI 2.5.43 在此模組踩雷**：lib 對 ESP32-S3 SPI controller 做暫存器級存取（`SPI_CMD_REG(SPI_PORT)`），在 N16R8 模組 init() 即 StoreProhibited（lib 自己原始碼註解 "Draws once then freezes"）。換 GPIO 無解，必須換 lib。

### 5.3 CO 感測器擴充腳位（取代舊版 GPIO 17/18 獨立 I2C）

| 候選方案 | 介面 | GPIO | 備註 |
|---------|------|------|------|
| 電化學型 + I2C | SDA / SCL | **GPIO 42 / 41**（I2C bus，可與 §5.4 DS3231 共用） | I2C 多裝置 bus；DS3231 地址 0x68，CO 感測器選用不同地址即可同 bus 並存 |
| 電化學型 + UART | TX / RX | **GPIO 43 / 44** | 佔用 UART0 預設腳，不影響 USB-CDC Serial Monitor（走內建 USB 19/20）；但與 §5.2 MicroSD 同腳，CO-UART 與 MicroSD 兩者擇一 |
| MEMS 半導體型 + ADC | 類比輸入 | **❌ 已無 ADC 可用**（GPIO 1/2/3 全部給 TFT） | 2026-05-08 起 N16R8 + TFT 整合後 ADC 全用完；CO 必須走 I2C 或 UART |
| 加熱半導體型（MQ-7/MQ-9） | — | **🚫 禁用** | 功耗超出 USB-C 供電上限（SoT V1 §20.5） |

### 5.4 RTC 模組擴充（DS3231，Dev-Phase 3 已上機）

> ✅ **2026-05-24 實機驗證通過**：DS3231 模組接 GPIO 42/41 I2C bus，`main.cpp` setup STEP 06.5 runtime 偵測 0x68 → 掛 `ems::DS3231Backend`；不在線時掛 `ems::NullRtcBackend` 降級。對齊 `docs/ds3231-integration-plan.md §4.1`。後續若再加 I2C 感測器（如 CO 電化學型 §5.3）才會變共用 bus。
>
> ⏳ **永續性測試待跑**：對時 → 斷電 30 秒 → 重開 → boot log 應顯示 seeded（不需 BLE）。CR2032 紐扣電池備援能力尚未實測。

| 用途 | GPIO | 備註 |
|------|------|------|
| I2C SDA | **GPIO 42** | 原 OLED bus，TFT 升級後釋出；DS3231 地址 0x68、MAX17043 燃料計 0x36；boot 時 `Wire.begin(42, 41)` |
| I2C SCL | **GPIO 41** | 原 OLED bus，TFT 升級後釋出 |
| INT（選用） | **GPIO 43** 或不接 | 目前未接；用於秒中斷喚醒；GPIO 43 為 UART0 腳、與 USB-CDC 無關，但與 §5.2 MicroSD CS、§5.3 CO-UART 同腳，多者並用需重新盤點 |

> ✅ **2026-08-22 實機驗證通過**：MAX17043 燃料計（I2C 地址 `0x36`）已上機接線，與 DS3231（`0x68`）、模組附掛 EEPROM（`0x57`）三者同時掛在本 41/42 bus 上，位址不衝突、連掃 5 輪無掉線。VCELL 讀數 3.844V 對比電表實測 3.88V（差 36mV，在電表誤差帶內），確認 VDD 吃到真實電池電壓。韌體 UI 整合仍排入 Impl-Phase H（採購決策見 `power-module-purchase.md §10.4/§10.6`，接線與安全測試 SOP 見 §10.7，**驗收紀錄與暫存器換算公式見 §10.8**）。
>
> 驗收工具：`pio run -e i2c-scan -t upload`（掃 bus）與 `pio run -e fuel-gauge-check -t upload`（讀 VCELL/SOC），兩者皆為獨立環境，不影響主韌體。

#### 當前 I2C bus 位址表（GPIO 42=SDA / 41=SCL）

> 📌 **這是本專案實際使用的 I2C bus，查現況以本表為準。** `hardware-procurement-v2.md` 與 `power-module-purchase.md §9.8.3` 另有兩張 I2C 位址表，那是 SparkFun Thing Plus 方案（**未採用**）的 Qwiic bus（GPIO 8/9）規劃推算，不是現況。

| 位址 | 裝置 | 狀態 |
|------|------|------|
| `0x36` | MAX17043 燃料計 | ✅ 已上機（2026-08-22 驗收） |
| `0x57` | AT24C32 EEPROM | ✅ 已上機（DS3231 模組附掛，非獨立採購） |
| `0x68` | DS3231 RTC | ✅ 已上機（2026-05-24） |

2026-08-22 實機掃描確認 bus 上僅此三個位址回應，無衝突。

**已不在此 bus 上**：SH1106 OLED（`0x3C`）——2026-05-08 顯示器改用 TFT ST7789 走 SPI，OLED 移除，GPIO 41/42 因此釋出給 I2C 周邊。**尚未接入**：MCP23017（`0x20`）按鈕擴充候選，尚未採購。

---

## 6. 互斥約束總覽

| 衝突組合 | 互斥原因 | 解法 |
|---------|---------|------|
| **TFT CS（GPIO 21） vs 震動馬達（GPIO 21）** | 同一支腳 | 依進度擇一啟用；Prod-Phase 同需則震動搬腳（見 §3 註記） |
| **TFT DC（GPIO 48 → 1） vs 板上 WS2812 RGB LED（GPIO 48）** | GOOUUU 板出廠 GPIO 48 接 WS2812，DC 切換頻率高，LED 把訊號吃成資料一直亂亮 | 2026-05-08 改 TFT DC → GPIO 1；GPIO 48 留給 WS2812（未來可當狀態指示燈使用） |
| TFT SPI MOSI/SCK（GPIO 2/3）vs CO 感測器 ADC | 2026-05-08 起 GPIO 2/3 已給 SPI | ADC 全部用完（1/2/3 全給 TFT）；CO 感測器改走 I2C 或 UART |
| ~~TFT BL（PWM, GPIO 1） vs CO 感測器 ADC~~ | 已失效：2026-05-08 後 GPIO 1 已給 TFT DC | TFT BL 維持 3.3V 常亮（無 PWM 能力）；CO 感測器走 I2C 或 UART |
| MicroSD（43/44） vs CO 感測器 UART（43/44） vs DS3231 INT（43） | GPIO 43/44 為三者唯一候選腳，物理腳位重疊 | 三者擇一；需並用須改用其他空閒腳或外接 I2C 擴充。**與 USB-CDC 無關**：USB 除錯走內建 USB（GPIO 19/20），不佔 43/44 |
| 獨立 I2C vs 按鍵 16/17/18 | 物理腳位衝突 | 按鍵已封版；I2C 改用釋出的 GPIO 41/42 bus（原 OLED 用） |
| **GPIO 35/36/37 vs N16R8 octal PSRAM** | **2026-05-08 實機踩雷確認**：採購到的就是 N16R8 模組 | TFT SPI 改 GPIO 2/3；採購若能選 N8 可釋放 GPIO 35/36/37 |

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
| `firmware/lib/ems_rtc/` + `main.cpp` setup STEP 06.5 | ✅ 已同步（2026-05-24） | DS3231Backend runtime 偵測，`Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)` 使用 §5.4 的 GPIO 42/41 |

---

## 8. 變更歷程

| 日期 | 變更 | 原因 |
|------|------|------|
| 2026-05-04 | 建立本文件 | 解決 main.cpp / SoT V1 / tft-migration-plan 三方 GPIO 矛盾 |
| 2026-05-04 | 確認 SoT §4.1 8 按鍵封版 | GPIO 16/17/18 歸屬返回 / EPI / 電擊鍵 |
| 2026-05-04 | TFT GPIO 重配 | 原 9~13 與 SPI flash 禁用區衝突，改 35~37 + 21, 47, 48 |
| 2026-05-04 | Impl-Phase A 韌體對齊 | main.cpp 重寫：`BTN_COUNT=8`、新增 `BTN_BACK/EPI/SHOCK`、震動 GPIO 21；舊 lib `ems_countdown` / `ems_vent` 廢止 |
| 2026-05-08 | TFT SPI 改 GPIO 2/3、library 改 Adafruit | 實機踩雷：採購到 N16R8 octal PSRAM 模組，GPIO 35/36/37 不可用；TFT_eSPI 2.5.43 暫存器級存取 crash，改 Adafruit_ST7789。詳見 §5.2 與 `tft-migration-plan.md §3.1` |
| 2026-05-08 | TFT DC 改 GPIO 48 → 1 | 主韌體整合 Step 1 燒錄後實機踩雷：GOOUUU 板上 GPIO 48 接 WS2812 RGB LED，TFT DC 高頻切換時 LED 把訊號吃成色彩資料一直亂亮。GPIO 1 原為 TFT BL PWM 候選，改為 DC，BL 改 3.3V 常亮（無 PWM）；ADC 完全用盡 |
| 2026-05-21 | 修正 GPIO 43/44 與 USB-CDC 的錯誤關聯（§5.1/§5.2/§5.3/§5.4/§6） | 原文件將 GPIO 43/44 標為「USB-CDC TX/RX」、並要求「放棄 USB-CDC」才能挪用 —— 事實錯誤：43/44 是 UART0（U0TXD/U0RXD），USB-CDC 走晶片內建 USB（GPIO 19/20，§5.1 禁用清單已正確標示）。挪用 43/44 給 MicroSD / CO-UART / RTC INT 不影響 USB 除錯。真實衝突為 43/44 在 MicroSD、CO-UART、DS3231 INT 三方重疊 |
| 2026-05-21 | 收斂 §5.2 MicroSD MISO 腳位為明確 44 | 原 §5.2 MISO 寫「用 43/44」含糊（兩腳只需一支），且 `tft-migration-plan.md` §3.2 仍寫 MISO=GPIO 1（過時：GPIO 1 已於 2026-05-08 改派 TFT DC）。確定 MicroSD MISO=44、CS=43 配對，並同步修正 `tft-migration-plan.md` |
| 2026-05-24 | §5.4 DS3231 從「計畫」→「已上機」 | Dev-Phase 3 RTC 整合 6 wave 完成（見 `docs/ds3231-integration-plan.md`），實機 boot log 確認 0x68 偵測 + seed `g_ts_state`；OHCA case 時戳改用真實 epoch；BLE time_sync Applied 反向寫回 DS3231。永續性測試（斷電 30 秒重開）待補 |
| 2026-07-22 | §5.4 新增 MAX17043 燃料計（0x36，掛 41/42 bus）| 使用者確認實際採購型號為 MAX17043（原規劃 MAX17048），已購入尚未上機接線 |
| 2026-08-22 | §5.4 MAX17043 從「尚未上機」→「實機驗證通過」 | 上機接線後 i2c-scan 確認 0x36 在線且與 0x57/0x68 共存無衝突；fuel-gauge-check 讀出 VCELL 3.844V / SOC 54.4%，對比電表 3.88V 差 36mV，確認 VDD 為真實電池電壓。驗收紀錄見 `power-module-purchase.md §10.8` |
| 2026-08-22 | §5.4 新增「當前 I2C bus 位址表」 | 專案原本沒有任何一張表記錄 42/41 bus 上實際掛了什麼——既有兩張 I2C 位址表（`hardware-procurement-v2.md`、`power-module-purchase.md §9.8.3`）都是 SparkFun Thing Plus 方案的 Qwiic bus（GPIO 8/9）規劃推算，易被誤讀為現況。新表以實機掃描為依據（`0x36`/`0x57`/`0x68`），並標明 SH1106 已隨 TFT 改 SPI 而移除、MCP23017 尚未採購 |
