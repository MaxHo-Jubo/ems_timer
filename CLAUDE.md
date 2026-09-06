# EMS DoseSync Pro — 救護計時器（repo 目錄沿用 ems_timer）

> 本檔與實際狀態最後校準：2026-07-03。發現段落與 git/實碼不符時，依 `~/.claude/harness/knowledge-protocol.md` 黃區流程提案修正本檔。

## 專案概述

給救護人員使用的計時器裝置。透過按鈕觸發不同事件的計時（注射、處置等），並以藍牙傳輸數據到手機 App。

## 硬體規格

- **主控板**: ESP32 開發板
- **輸入**: 實體按鈕（麵包板接線）
- **顯示**: 2.8 吋 TFT 320×240（ST7789，SPI，LovyanGFX + DMA）
- **通訊**: BLE（Bluetooth Low Energy）連接手機 App
- **錄音**: INMP441 I2S 數位麥克風 + MicroSD 卡模組（存 WAV）
- **提醒**: 蜂鳴器（主動式或被動式）

## 使用情境

1. 救護人員到場，啟動裝置
2. 按下對應按鈕記錄事件（如：注射藥物、CPR 開始/結束、到院等）
3. 裝置記錄每筆事件：**當下時間戳** + **從啟動起的經過時長**
4. 事件資料儲存在裝置本地（不依賴即時連線）
5. 事後透過 BLE 將完整事件紀錄傳輸至手機 App
6. App 端可檢視當次紀錄與**歷史紀錄**

## 資料模型

每筆事件紀錄包含：

| 欄位 | 說明 |
|------|------|
| session_id | 本次出勤 ID |
| event_type | 按鈕對應的事件類型（注射、CPR 等） |
| timestamp | 按下按鈕的絕對時間（年月日時分秒） |
| elapsed_ms | 從本次出勤啟動起的經過毫秒數 |

## 錄音功能

- 按下錄音按鈕 → ESP32 透過 I2S 收音 → 存 WAV 到 MicroSD 卡
- 錄音檔與事件紀錄關聯（同一 session）
- 事後透過 BLE 或直接讀 SD 卡取得音檔

## 倒數計時與提醒

- 特定事件按鈕觸發後啟動倒數計時（如：注射後需觀察 N 分鐘）
- **倒數結束提醒**：時間到 → 蜂鳴器響 + 螢幕閃爍提示
- **區間提醒**：倒數過程中每隔固定間隔提醒（如每 30 秒嗶一聲）
- 提醒模式可依事件類型預設不同的倒數時長與區間

## App 功能

- 透過 BLE 接收裝置端的事件紀錄
- 顯示當次出勤的事件時間軸
- 歷史紀錄列表：依出勤（session）分組，可回顧過去的紀錄
- 本地持久化儲存（SQLite / AsyncStorage）

## 技術棧

- **韌體**: Arduino Framework（ESP32）或 ESP-IDF
- **App**: 手機 App 待定（React Native / Flutter / 原生）；已有 Web demo（`web/` 原始碼、`dist/` 產物，Cloudflare Pages: ems-dosesync-demo.pages.dev）
- **通訊協定**: BLE GATT

## 硬體腳位規劃

GPIO 分配以 [`docs/gpio-allocation.md`](docs/gpio-allocation.md) 為**單一真相來源**（Single Source of Truth）。

- 按鍵命名與分配依 SoT V1 §4.1（8 鍵封版）
- 擴充策略覆寫 SoT V1 §21.3.3 舊版（CO 感測器候選腳位調整）
- 其他文件（`main.cpp` 註解、SoT V1 §21.3、`tft-migration-plan.md`）皆需向此文件對齊

### 相關文件

完整文件索引（含現行／歷史分類）見 [`docs/README.md`](docs/README.md)。本節只點名兩份：
GPIO 以 [`docs/gpio-allocation.md`](docs/gpio-allocation.md) 為單一真相來源；Phase H 接手前必讀
[`docs/superpowers/phase-h-handover.md`](docs/superpowers/phase-h-handover.md)。

### 韌體與規格對齊狀態

✅ **已對齊**（Impl-Phase A 完成，2026-05-04）：
- 8 按鍵全部接線：主/上/下/Power/錄音/返回/EPI/電擊
- 震動馬達 GPIO 21（原 GPIO 16 已封給返回鍵）
- 舊 lib：`ems_countdown` 已移除；`ems_vent` 的 source/test 仍在編譯（與 pm-dev-spec §五「已廢止」不一致，待確認去留——2026-07-03 校準）
- 主功能表 5 項（OHCA case 入口可進，其他顯示「Phase X 待實作」）
- OHCA 子狀態機 + EPI 倒數 + 兩段確認串接完成
- 95 unit tests 全綠 + 韌體編譯通過（Flash 8.9% / RAM 6%）

⏳ **待實作**：見 `docs/pm-dev-spec.md §四 Phase B~H`

🔋 **Impl-Phase H（電量顯示）進行中**（2026-08-24）：W1 讀取層 Task 1–6、W2 顯示層 Task 7–9
（`94bc3fb` / `3333235` / `5634b52`）皆完成且 review clean。**W3 的 Task 10 完成**
（§13.16 執行中低電量一次性提示，`dc4aaf1`）——經 6 輪 fix、4 個 CRITICAL，詳見 handover §3-A4。
Task 11–14 未開工。全套 599 cases / 598 通過（唯一未過的 `test_storage_hw` 是既有編譯錯誤，
與 Phase H 無關），韌體 Flash 71.4%。

**整個 Phase H 一次都沒在實機跑過**——本階段全程無硬體，handover §3-B 累積了 11 條上機驗收
全部未執行，所有「已完成」的結論都建立在 native test 加靜態推理上。

> ⚠️ commit hash 變過兩次。2026-08-23 rebase：`df33d97` → `94bc3fb`（Task 1–6 不受影響）。
> 2026-08-24 Task 10 六輪 fix 每輪折回同一個 feat commit，中間版本的 hash 全部脫離分支。
> 分支尚未推送。

> 🔤 **新增任何會上 TFT 的中文字串後，必須重跑 `bash scripts/regen_vlw.sh` 並驗字集**——
> 字型是從原始碼掃出來的子集，新字沒重生就會在實機顯示 ▯，而編譯與 native test 都不會報錯。
> 這個坑在 Task 10 咬了兩次（缺新字、以及重生時把舊字 union 掉）。驗法見 handover §8 第 ② 條。

> **接手前必讀** [`docs/superpowers/phase-h-handover.md`](docs/superpowers/phase-h-handover.md)。
> **下一步是 Task 10**，但 §3-A3 列了四條 dispatch 前必須處理的計畫缺陷（最重要的一條：低電量提示
> 狀態沒進 DisplaySnapshot，會是同型 bug 的第 6 次），先讀完再派工。
> 所有上機驗收累積在 §3-B，需要實體硬體。

## 韌體交付與燒錄

提供其他工程師快速燒錄環境的 release 包模板（無需安裝 PlatformIO，只需 Python + esptool）。

### 文件索引

模板檔案在 [`firmware/release-template/`](firmware/release-template/)：`README.txt`、`flash.sh`、`flash.bat` 給收 release 包的工程師；
`HOW_TO_BUILD_RELEASE.md` **僅開發者**。各檔用途見 [`docs/README.md`](docs/README.md) §5。

### 打包流程速查

```bash
cd firmware
pio run -e esp32-s3-devkitc-1
cp firmware-merged.bin release-template/
zip -r ems-timer-firmware-$(date +%Y%m%d).zip release-template/ \
    -x "release-template/HOW_TO_BUILD_RELEASE.md"
```

> ⚠️ `-x` 排除 `HOW_TO_BUILD_RELEASE.md`，避免內部 SOP 外流。

### Prod-Phase 量產燒錄策略

量產階段的 4 種燒錄方案（USB 直燒 / Pogo Pin 治具 / PCBA 預燒 / BLE OTA）與選擇建議，後續整理進 [`docs/pcb-outsourcing-guide.md`](docs/pcb-outsourcing-guide.md) 的 PCB 預留要求章節（待補）。

## Phase 編號對照表

專案內共三套 Phase 編號並存，各自含意不同。為避免混淆，後續文件統一加前綴：

| 前綴 | 含意 | 編號範圍 | 出處 |
|------|------|---------|------|
| **Dev-Phase** | 開發進度時間軸（硬體 + 韌體 + App 整體里程碑） | 1 / 1.5 / 2 / 3 / 4 | 本檔「## 開發階段」 |
| **Impl-Phase** | 韌體實作子階段（功能模組實作順序） | A / B / C / D / E / F / G / H | `README.md` + `docs/pm-dev-spec.md §四` |
| **Prod-Phase** | 量產階段切換時機（電源拓樸、外殼定型等硬體封版決策） | 單一階段（量產） | 本檔「## 電源供應規劃」+ SoT V1 §20.4.4 |

> 📌 三套互不對映。例如 Dev-Phase 4「整合測試與優化」與 Prod-Phase「量產」是兩件事；Impl-Phase A~H 在 Dev-Phase 2~3 內逐步完成。

> 📌 既有文件（SoT V1、pm-dev-spec、incremental-impl-plan、README）內的 Phase 編號暫保留原樣，新增/修改文件時使用前綴。歷史 commit 與既有對話紀錄不回填。

## 開發階段

- [x] Dev-Phase 1: 硬體原型 — ESP32-S3 + 8 按鈕 + OLED + 蜂鳴器（2026-04-17 驗收通過）
- [ ] Dev-Phase 1.5: INMP441 麥克風重試（換新模組後啟用 `ENABLE_MIC_MONITOR`）
- [x] Dev-Phase 2: BLE 通訊 — 裝置與手機配對、數據傳輸（Impl-Phase F BLE 鏈路完成，2026-05）
- [ ] Dev-Phase 3: 手機 App — 接收數據、顯示時間軸 + 升級硬體 RTC（DS3231）
  - [x] DS3231 RTC 已整合上機（commit c83f1c2）
  - [ ] 手機 App 未開始（現有 Web demo 見「技術棧」）
- [ ] Dev-Phase 4: 整合測試與優化

## Dev-Phase 2 設計決策（2026-04-18）

### 按鈕互動
- 採單行程按鈕（single-action）：一次下降緣 = 一筆事件，Dev-Phase 1 的 debounce 邏輯可直接沿用。
- 按鈕事件名稱後期會全改，目前配置以驗證可行性為主。

### 資料模型（統一）
```
EmsEvent { event_type: uint8, timestamp: uint64 (epoch ms), elapsed_ms: uint32 }
```
- 裝置端不做配對（CPR Start/End、Record on/off 都是獨立 event）。
- 同 event_type 允許多筆（陣列累積）。
- `MAX_EVENTS = 30`（單次出勤容量）。
- Session 開機自動產生，首次按鈕按下鎖定 `session_start_ms`，不做手動切換。

### BLE 協議
- Dev-Phase 2 採 Nordic UART Service (NUS) + JSON（先求通，不求省電）。
- 後期 App 穩定後，若要省頻寬/功耗再切自訂 GATT。

### 時間同步
- **Dev-Phase 2**：App 連線時下發 epoch ms（軟體對時，免硬體成本）。
- **Dev-Phase 3**：升級 DS3231 RTC 模組（I2C，TFT 已轉 SPI 後 GPIO 41/42 釋出供 RTC 獨立 bus）— 離線不失憶、救護現場免等 App 連線、晶振精度 ±2ppm 符合醫療紀錄可信度要求。

### 按鈕功能擴展計畫（未來）
- **開關電源功能**：特定按鈕長按 → 軟關機/啟動（進入 deep sleep 或重置）。
- **選擇模組功能**：按鈕在不同模式下可有不同語意（例如「CPR 模式」下 8 顆按鈕定義與「注射模式」不同）。
- 目前 Dev-Phase 1/2 先不實作，按鈕配置以驗證可行性為主，結構上保留擴展空間（`event_type` 可擴增，不限於 8 種）。

## 電源供應規劃

### 相關文件

採購清單 [`docs/hardware-procurement-v2.md`](docs/hardware-procurement-v2.md)、電源模組 SOP
[`docs/power-module-purchase.md`](docs/power-module-purchase.md)、量產規劃 [`docs/pcb-outsourcing-guide.md`](docs/pcb-outsourcing-guide.md)；
各自的 HTML 版與其餘硬體文件見 [`docs/README.md`](docs/README.md) §2。

### Dev-Phase 2~3 開發階段
- 原規劃 USB Type-C 直供；**實際已提前進入鋰電池供電驗證**（1000mAh LiPo + TP4056 + 升壓，2026-06 起）。
- 電池供電 TFT 白屏（2026-06-14 根因定位 → 2026-07-04 修復）：GND 回流接觸壓降 ~0.5V（ground bounce），正極與升壓板輸出正常；已只修地線接點焊死，實測通過。

### Prod-Phase 量產階段（封版方向）
- 切換到 3.7V 鋰電池 1000mAh + TP4056 充電 IC + 升壓 IC + 單節保護板。
- 對齊 SoT V1 §20.4 充放電拓樸與 §21.1 主要硬體清單。
- 功耗預算與 1000mAh 連續使用時數預估見 SoT V1 §20.4，待 Dev-Phase 1 實測校正。
- 擴充模組（INMP441/MicroSD/CO 感測器）功耗預留見 SoT V1 §20.5；CO 感測器禁用加熱半導體型（MQ-7/MQ-9）。

### 候選方案備忘
- **方案 E — 乾電池供電**（AA × 3 或 × 4 + LDO）
  - **優點**：便利商店買得到、無充電設計、救護車備料簡單、沒電即換新。
  - **缺點**：容量低（鹼性 ~2500mAh 但放電曲線差）、體積重量大、環保壓力。
  - **適用**：極端可靠性場景（例如偏遠救護站無法穩定充電）。
- 其他方案（18650 / LiPo / USB-C + 內置電池 / Power Bank）Prod-Phase 做外殼時再依實測耗電決定。
