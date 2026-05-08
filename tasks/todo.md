# EMS Timer 開發進度

> **2026-04-27 重要更新**：V1 規格封版並完成手機互動 Demo。
> 韌體下次工作從 **Phase A 開工**，既有韌體（`MED_PHASE` / `ems_countdown` / `vent_metronome` / 5 鍵 / 4 模式切換）視為 throwaway prototype 全砍重寫。
> 詳見 `docs/pm-dev-spec.md` v2.0。

---

## 🎯 下次工作起點：Phase A 韌體重寫（OHCA 核心）

對齊 `docs/EMS_DoseSync_Pro_Prototype_V1.md` SoT 與 `docs/pm-dev-spec.md` v2.0。

### Phase A 開工 Checklist

#### Phase A 開工前先刪（pm-dev-spec §五）

- [ ] `firmware/lib/ems_logic/ems_countdown.{h,cpp}` — 舊三階段藥物倒數
- [ ] `firmware/lib/ems_logic/vent_metronome.{h,cpp}` — 舊 6 秒節拍器（Phase C 重寫）
- [ ] `MED_PHASE_*` enum 與相關常數
- [ ] 5 鍵 / 4 模式切換邏輯（給藥/通氣/自訂/設定）
- [ ] `firmware/test/test_countdown/test_med_countdown.cpp` — 舊測試
- [ ] `tasks/` 內舊版煙霧測試清單（A~F 27 項，不適用新狀態機）

#### Phase A 開工保留可重用

- `firmware/lib/ems_logic/ems_time.h` `computeTaskElapsedMs()`
- `firmware/lib/ble_nus` — Phase F 過渡使用
- 按鍵 debounce / fire-on-release 框架（timing 常數重新定義）
- PlatformIO native 測試環境
- OLED 驅動 / I2C bus / 蜂鳴器 PWM 驅動

#### Phase A 新建模組

- [ ] `firmware/lib/ems_ohca/ems_ohca_state.{h,cpp}` — OHCA 子狀態機（待本機 EPI / 倒數 / 預警 / 警報 / 超時 / 結束鎖定）
- [ ] `firmware/lib/ems_ohca/ems_ohca_countdown.{h,cpp}` — EPI 4 分鐘倒數引擎（純函式 `decideOhcaOutput`）
- [ ] `firmware/lib/ems_ohca/ems_two_step_confirm.{h,cpp}` — 兩段確認模組（EPI / 電擊 / Amio 共用）
- [ ] `firmware/lib/ems_supp/` — 補登資料模型（Phase B 用，Phase A 預留 type）

#### Phase A 驗收

- [ ] `decideOhcaOutput()` 單元測試 ≥ 30 案例全綠
- [ ] 實機跑完 1 case：待本機 EPI → EPI×2 確認 → 倒數 → WARNING → ALARMING → EPI×2 重啟 → 長按主鍵結束 → 鎖定
- [ ] EPI 到期 5 秒未確認自動進入 OVERTIME，顯示自上次 EPI 起算的累計時間
- [ ] 案件鎖定後不可再新增事件（API 拒絕）
- [ ] 蜂鳴 / LED / 震動三模態輸出對齊 V1 §6.6（warn 黃慢閃 / alarm 紅快閃 / overtime 紅慢閃）

### Phase B~H 概覽

| Phase | 範圍 | 關鍵驗收 |
|-------|------|---------|
| B | 補登 + Amiodarone + 案件總覽 + Timeline | 補登成立後不可撤銷；總覽欄位完整對齊 V1 §11 |
| C | 6 秒通氣節奏（獨立 + OHCA 切入 + EPI 高優先打斷） | EPI ALARMING 觸發時通氣音 ≤ 50ms 內停止 |
| D | Training 模式（30s / 1m / 4m） | Training 與 OHCA 列表完全分離 |
| E | LittleFS 持久化 + 歷史紀錄 | 寫滿 51 筆 OHCA 後最舊一筆自動覆蓋；重啟後總覽完整 |
| F | App 配對碼同步（4 位數 / 120s TTL） | 同案件同步 2 次 App 端不重複；中斷後重試成功 |
| G | 系統設定 + Type-C 工具 | 恢復預設不影響案件 / Training / 裝置名稱 |
| H | 電源管理 | 插拔 Type-C 期間 OHCA 計時連續，不丟事件 |

### Phase A 開工建議步驟

1. **新 branch**：`feat/phase-a-ohca-rewrite`
2. **先寫 unit tests**（TDD）：`decideOhcaOutput` 30 案例先紅
3. **刪舊檔**：上面 Checklist 一併移除（含 commit 紀錄）
4. **新建純函式**：`ems_ohca_countdown.{h,cpp}` 讓 tests 變綠
5. **狀態機接線**：`main.cpp` 改用新 OHCA 子狀態機
6. **OLED render**：對應 V1 §6.4 / §6.7 五種畫面變體
7. **實機煙霧測試**：5 個關鍵 path 跑過

---

## 🎨 TFT 整合（Phase A → B 過渡）

> **2026-05-08 立案**：Phase A 已完成（OLED 跑通），TFT smoke test 也跑通（commit `f1a5792`）。下一步把 TFT 整進主韌體，UI 對齊 `docs/demo/index.html`（決策見 memory `project_tft_ui_design_target.md`）。
> **lib 演進**：Step 1 起用 Adafruit_ST7789 + GFX（避開 TFT_eSPI 在 N16R8 init crash）→ Step 2d 換成 **LovyanGFX + DMA**（Adafruit byte-swap CPU 是視覺瓶頸，DMA pushSprite 才能瞬切）。詳見 memory `feedback_lovyangfx_dma_for_tft.md`。
> **硬體**：ESP32-S3 N16R8、ST7789 蝦皮紅板、SPI GPIO 2/3、DC GPIO 1（避開 GPIO 48 板上 WS2812）、`tft.setRotation(3)`（LGFX 跟 Adafruit 差 180）、panel cfg `invert=false`。
>
> **進度（2026-05-08 持續中）**：branch `feat/tft-integration` 已 ~12 commits（5b2d009 → 5be48fa）。Step 1 / 2a / 2b / 2c / 2d 全部實機驗收通過。下次接 Step 2 後續其他畫面 + Step 3 字體放大方案（PM 反饋）。

### Step 1：lib 切換 + 編譯通過（半小時）✅ **已完成 2026-05-08**

- [x] 新 branch：`feat/tft-integration`
- [x] `firmware/platformio.ini` `[env:esp32-s3-devkitc-1]`：`lib_deps` 把 `Adafruit SH110X` 換成 `Adafruit ST7735 and ST7789 Library`（`Adafruit GFX Library` 留著）
- [x] `firmware/src/main.cpp` 改 include：`<Adafruit_SH110X.h>` → `<Adafruit_ST7789.h>`
- [x] `display` 物件改宣告：用 `TftAdapter` 繼承 `Adafruit_ST7789`，補 `clearDisplay()` / `display()` 給舊 SH110X-style 呼叫點 → 1300+ 行 draw code 不需改字面
- [x] `display.begin(0x3C, true)` → `tft.init(240, 320); tft.setRotation(1); tft.invertDisplay(false);`
- [x] **不改任何 `display.xxx()` 呼叫點**
- [x] `pio run -e esp32-s3-devkitc-1` 編譯通過（Flash 8.7%、RAM 6.2%）
- [x] 188 unit tests `pio test -e native` 全綠（toml 寫的 95 已過時）
- [x] 實機踩雷 + 修正：
  - DC GPIO 48 → 1（避開 GOOUUU 板上 WS2812）— commit 5c88c44
  - WS2812 boot latch 白 → `neopixelWrite(48, 0, 0, 0)` 主動 silence — commit 862b924
  - 按鍵 GND 公共線鬆動修復（硬體側）

### Step 2a：主功能表對齊 demo + design tokens ✅ **已完成 2026-05-08**

- [x] 加 design tokens（COLOR_BG/TEXT_PRIMARY/TEXT_MUTED/ACCENT_OK/WARN/ALERT/FLASH_VENT，RGB565）
- [x] 加 SCREEN_W=320 / SCREEN_H=240 邏輯解析度常數
- [x] `drawMainMenu()` 重排：黑底 + 灰色標題 + 5 大字選項 + cursor 白底黑字 highlight
- [x] 修雙 fillScreen 雙閃 bug
- [x] commit 5b2d009

### Step 2b：updateDisplay snapshot 去重（解閃爍）✅ **已完成 2026-05-08**

- [x] 加 `DisplaySnapshot` struct + `captureDisplaySnapshot()`：抓 globalState/ohcaState/ohcaSubState/cursor/countdownSec/ventBeat/flags
- [x] `updateDisplay()` 入口 memcmp 比對，無變化跳過全螢幕重畫
- [x] commit 1d4af9b
- [x] 另：按鍵 debounce 統一 press/release 門檻（修 TFT 慢渲染期間 bounce 雙觸發）— commit d825bcf

### Step 2c：OHCA 倒數畫面對齊 demo ✅ **已完成 2026-05-08**

- [x] `drawOhcaCountdownCommon` 重排 320×240：頂部 OHCA 綠 badge / 中央大時間 mm:ss / 標籤 / 底部 EPI/Shock 計數
- [x] state → 顏色：COUNTDOWN=WHITE / WARNING=AMBER / ALARMING=RED 閃爍 / OVERTIME=RED 持續
- [x] ALARMING flash phase bit 進 snapshot（300ms 半週期切深紅 bg）
- [x] 字型：`FreeMonoBold24pt7b`（~48px monospace），中央 datum-based 自動置中
- [x] commits: f39ed89 (init) / 8e428c8 (WaitFirstEpi 對齊) / 4d831ab (partial push 局部 + 殘留清除)

### Step 2d：LovyanGFX + DMA 全頁切換消掃描感 ✅ **已完成 2026-05-08**

> Adafruit_GFX byte-swap CPU overhead 撐不住，SPI 80MHz 視覺仍卡；換 LovyanGFX 走 DMA 才解。

- [x] platformio.ini：lib_deps 換 `lovyan03/LovyanGFX`，移除 Adafruit_ST7789 + Adafruit_GFX
- [x] 寫 `LGFX` class（SPI2_HOST + DMA_CH_AUTO + 80MHz + 蝦皮紅板 panel cfg invert=false）
- [x] `FrameSprite` 繼承 `LGFX_Sprite`，補 SH110X 相容 `clearDisplay()` / `display()` no-op
- [x] `setPsram(true)` PSRAM 優先 alloc 153KB framebuffer（N16R8 8MB）
- [x] updateDisplay 結尾 + sub-state early return + partial path 統一 `display.pushSprite(0, 0)` DMA push
- [x] API 替換：`getTextBounds + setCursor` → `setTextDatum + drawString`；setFont 走 `&fonts::xxx` namespace
- [x] commits: 9ed9a63 (lib 切換) / 5be48fa (rotation 1→3 修對齊)
- [x] 實機驗收：所有畫面切換 ~4ms 接近瞬完成、倒數無殘留 ✅

### Step 2 後續：其他畫面（每塊 1 commit）

> demo 320×240，原 OLED 128×64。每個畫面要重新排版。Step 2c/2d 已驗證新 layout 流程，剩下走相同 pattern。

- [x] **OHCA 主畫面 — 待本機 EPI** — `drawOhcaWaitFirstEpi()`（commit 8e428c8）
- [x] **EPI 成立 1 秒提示** — `drawOhcaStartFlash()`（commit 8e428c8 + LGFX datum）
- [x] **兩段確認 overlay** — `drawTwoStepArmedOverlay()`（commit 96ef727，琥珀反色 40px bar）
- [x] **案件結束流程畫面** — `drawOhcaEndCheck()` / `drawOhcaLocked()` / `drawOhcaSummary()`（commit 96ef727）
- [x] **6 秒通氣**（OHCA 內輔助 + 獨立模式）— `drawOhcaVentOverlay()` / `drawVentStandalone()`（commit 96ef727 + 7a85f64 大字 y 修正）
- [x] **VentEndCheck / Placeholder** 一併對齊新 layout（commit 96ef727）
- [ ] 補登/Amiodarone 等 Phase B 畫面：本階段不做，Phase B 才畫
- [ ] 移除 `OLED_WIDTH/HEIGHT` 別名 + 殘留 SH110X_WHITE/BLACK 替換為 COLOR_TEXT_PRIMARY/COLOR_BG（全部畫面重排完後）
- [ ] 考慮抽出 `firmware/lib/ems_display/ems_display.{h,cpp}` 模組（多畫面穩定後）

### Step 3：字體放大改進方案（PM 反饋 2026-05-08）

> PM 覺得目前介面字體不夠大（demo 也是）。已對 OHCA 系列已改 layout 畫面套用 ~1.5x 放大（commit 39b544a）。

- [x] OHCA 已改 layout 畫面套放大尺度（commit 39b544a）：
  - drawOhcaStartFlash: size 3→4
  - drawOhcaWaitFirstEpi: badge 2→3 / Awaiting EPI 3→4 / 副標 1→2 / counter 1→2
  - drawOhcaCountdownCommon: badge 2→3 / **大時間 size 1→2（96px tall）** / label 2→3 / counter 1→2
  - 常數調整：OHCA_TIME_VISUAL_UP 8→20 / OHCA_LABEL_GAP_PX 16→48 / partial bbox 加大
- [x] 大時間 pt 升級替代方案：用 setTextSize(2) FreeMonoBold24pt7b 達 96px（接近 demo 84px 視覺份量）。GFX bitmap font 沒 36/48pt7b，這條已用 size 2 解
- [ ] 剩餘畫面（EndCheck/Locked/Summary/TwoStepArmedOverlay/VentOverlay/VentStandalone）對齊新 layout 時套用同 size pattern
- [ ] 主功能表已滿版不改

### 中文支援評估（2026-05-08 PM 詢問）

> demo 用「下次給藥/請準備給藥/請給藥」中文。目前韌體用英文 (Next dose/Prepare EPI/GIVE EPI!)。要不要支援中文待定。

- [x] 試 LGFX `fonts::efontCN_24`（24px CJK）— 主選單做 PoC：「OHCA 急救/6 秒給氣/訓練模式/歷史紀錄/系統設定」
  - Flash 11→27.6%（efontCN_24 fontmap ~600KB），3.3MB 還夠
  - 編譯通過，待實機驗收 cover 率（commit 待加）
- [ ] 主選單實機驗收：所有字是否正常顯示，字型清晰度可接受
- [ ] 缺字評估：擴展到 OHCA 倒數標籤「下次給藥/請準備給藥/請給藥」常用字
- [ ] 若 efontCN cover 不夠或字型粗細不滿意，採用 vlw 自訂字型（自選繁中字集生成）
- [ ] 決策：英文保持 / 全 CJK / 雙語切換

### Step 4：硬體層 + 文件同步（半小時）

- [ ] SoT V1 §21.2 外殼開孔尺寸：0.96" OLED 25×15mm → 2.8" TFT 60×45mm（記得改 §21.2 跟 `flow.html` 對應段）
- [ ] `CLAUDE.md` 更新：把「OLED」字眼換 TFT 描述
- [x] memory `project_oled_sh1106_interim.md` 結案（2026-05-08 已更新為「TFT + LGFX 封版」）

### Step 5：實機驗收

- [ ] 主功能表 cursor 上下、進入子選單、返回
- [ ] OHCA 完整 flow：待 EPI → EPI 確認 → 倒數 → warning → alarming → overtime → 結束 → 鎖定
- [ ] 兩段確認 timeout（5 秒未確認自動取消）
- [ ] 6 秒通氣（基本切換能進）
- [ ] 跟 demo screenshot 並排對照無視覺落差

### 風險與決策點

- 🟡 **字型大小調校**：demo 84px 在 320×240 占 35% 高度。實機 ST7789 用 GFX FreeMono 24pt 約 48px，可能要改 36pt 或 48pt 才接近 demo 視覺份量
- 🟡 **動畫頻率**：demo CSS keyframes 寫死的閃爍頻率（黃慢、紅快、紅慢），韌體要照搬數字 → 抽出 demo CSS 的 `animation-duration` 數字當韌體常數
- 🔴 **GPIO 21 衝突**：TFT CS 跟震動馬達同腳。整合時 `ENABLE_VIBRATION = 0` 必須維持；之後 Prod-Phase 要震動回饋的話必須先解 GPIO 衝突（見 `gpio-allocation.md` §3 註記）
- 🟡 **Refresh strategy**：demo 用 React diff 重繪 DOM，TFT 沒 diff，全螢幕重繪會閃。要實作 partial update（只重繪變動區域）或 double buffer（耗 RAM）

---

## GitLab 遷移設定（2026-04-24 完成）

**背景**：repo 從 GitHub 移植到 GitLab（`https://gitlab.webotopia.work/maxhero/ems_timer.git`）。

### 完成項目

- [x] **1. Git credential 設定**
  - `osxkeychain` helper 已預設啟用（macOS）
  - PAT 產生步驟已文件化於 `README.md` § Repository & CI/CD
- [x] **2. 保留 GitHub 當備份**
  - 新增 remote `github`（`git push github main` 手動同步）
- [x] **3. GitLab CI** — test job 已移除
  - 2026-04-24：此 GitLab 實例無可用 runner，test job 取消
  - 單元測試改本機執行：`pio test -e native -d firmware`
  - 未來若有 runner，再把 test job 加回 `.gitlab-ci.yml`
- [x] **4. GitLab Pages — 發佈 docs/**（待驗證 Pages 功能是否啟用）
  - `pages` job 設定完成：部署 `docs/ems-flow-spec.html` 為首頁 + 全部 MD / HTML
  - 僅 main branch 觸發
  - ⚠️ 自架 GitLab 的 Pages 功能需 admin 啟用，目前 Settings 沒看到 Pages 項 → 待確認 Deploy 選單或請朋友開啟

### 決策結論

- Q1: 首頁 = `ems-flow-spec.html`（不另做 landing page）
- Q2: 備份 remote 名稱 = `github`
- Q3: CI 不跑 test job（無 runner）；本機手動 `pio test`

### 待驗證 / 待處理

- [ ] Pages 功能是否啟用（Deploy → Pages 或請朋友在 GitLab admin 開啟）
- [ ] 若 Pages 不可用，改用 GitHub Pages 或整個移除 pages job
- [ ] `git push github main` 備份同步成功

## 已完成階段

### Phase 0 — 開發環境（2026-04-13）
- [x] PlatformIO + VS Code 環境建立
- [x] 基本韌體 Build / Upload / Serial monitor

### Phase 1 — 硬體原型（2026-04-17 驗收通過）
- [x] ESP32-S3-DevKitC-1 平台升級（從 ESP32 換到 S3，方案 B 接線）
- [x] 8 按鈕（GPIO 4/5/6/7/15/16/17/18）+ INPUT_PULLUP
- [x] SSD1306 OLED（I2C 42/41）
- [x] 蜂鳴器（GPIO 14）
- [x] INMP441 I2S 麥克風（SCK/WS/SD = 40/39/38）— 通訊協定層驗證通過，靈敏度待換模組
- [x] Phase 1 基線 commit（`4dba88e`）

### Phase 2A — 計時邏輯 + OLED 顯示（2026-04-18 編譯通過）
- [x] EmsEvent 資料結構 + 事件陣列（MAX_EVENTS = 30）
- [x] Session 狀態（開機隨機 ID + 首次按鈕鎖定起算點）
- [x] EventConfig 計時配置表（per 按鈕 TIMER_UP/DOWN + 倒數時長 + 區間提醒）
- [x] 倒數提醒 state machine（區間短嗶 + 結束 3 聲 + 畫面閃爍）
- [x] 非 blocking 蜂鳴器 state machine
- [x] OLED 計時顯示（頂部事件名+計數 / 中間大字 mm:ss / 底部模式）
- [x] 編譯通過：RAM 5.9% / Flash 9.2%

### Phase 2B — BLE NUS 通訊（2026-04-18 編譯通過，2026-04-21 實機驗收）
- [x] ArduinoJson 依賴
- [x] BLE NUS service（TX Notify + RX Write，MTU 升至 517）
- [x] 連線 hello 訊息（含 session_id）
- [x] `sync` 命令：App 下發 epoch ms 做軟體對時
- [x] `dump` 命令：批次回傳 events[] 陣列（dump_start / dump_item × N / dump_end）
- [x] `clear` 命令：清空事件
- [x] 連線/斷線自動管理 advertising 重啟
- [x] 修正：`ARDUINO_USB_CDC_ON_BOOT=1`（native USB CDC Serial）
- [x] 修正：hello delay 100ms → 3000ms（給 App 足夠時間訂閱 Notify）
- [x] 即時 evt Notify 暫時停用（Phase 3 再確認是否保留）

### Phase 2C — 按鈕重構與事件資料強化（2026-04-21）
- [x] BTN1~4 改為藥物計時器（Group 0: Epi/Amio/Atropine/Adenosine；Group 1: Naloxone/Nitro/D50/Morphine）
- [x] BTN5~8 改為系統功能（Menu/Next/Prev/Power），不觸發事件記錄
- [x] `getButtonLabel()` / `getEventConfig()` 統一查詢介面，支援群組切換
- [x] EmsEvent 新增 `elapsed_end_ms`：記錄計時中斷或倒數自然結束的時間點
- [x] 反覆點擊或切換按鈕皆自動封存前一筆計時結束時間
- [x] BLE dump/evt JSON 新增 `end` 欄位
- [x] BTN5 Menu 功能實作（開啟選單 / 確認切換群組；同時中斷進行中的藥物計時）
- [x] BTN6 Next / BTN7 Prev 選單導航實作（游標循環移動 + 重置 5s 超時）
- [x] OLED 選單畫面（drawMenuScreen：群組列表反白游標 + 5s 無操作自動關閉）
- [ ] BTN8 Power 開關機實作（暫停：換單行程按鍵後加長按偵測再啟用 deep sleep）

## 待上機驗證

- [x] **Phase 2 可行性測試通過**（2026-04-21）：§1~4、§7~8、§10、§A~C 全通過
  - §5 倒數計時、§6 事件切換、§9 容量上限、§11~12 待補測（換單行程按鍵後）

### Phase 2.x — BTN5~8 選單系統（2026-04-21）
- [x] MenuState 狀態機（MENU_NONE / MENU_OPEN）
- [x] BTN5 開選單 / 確認，BTN6/7 導航，OLED 反白顯示
- [x] 5 秒無操作自動關閉
- [x] 開機 lastBtnState 修正（toggle switch 不誤觸）
- [x] BTN5 開選單時中斷進行中的藥物計時

## 後續階段

### Phase 1.5 — INMP441 重試（等硬體）
- [ ] 新 INMP441 模組到貨
- [ ] `ENABLE_MIC_MONITOR` 設 1 重編
- [ ] 驗證靈敏度（呼吸、說話、背景音）
- [ ] 錄音檔 WAV + SD 卡模組（GPIO 10~13，目前僅保留腳位）

### Phase 3 — 手機 App + 硬體 RTC
- [ ] DS3231 RTC 模組升級（與 OLED 共用 I2C bus）
- [ ] 離線時間戳保存（斷電不失憶）
- [ ] 手機 App（React Native / Flutter，待定）
  - [ ] BLE 掃描/連線
  - [ ] 接收 Notify 即時更新時間軸
  - [ ] SQLite 本地歷史儲存
  - [ ] App 分發策略（TestFlight / 自架 APK 等，細節留存於舊版 todo）
  - [ ] **按鈕設定編輯**：App 可編輯 BTN1~4 各群組的計時配置，透過 BLE `config` 命令推送至 ESP32（群組數量可擴充，不限於兩組）
    - 可設定項目：
      - `label`：藥物名稱（自訂顯示文字）
      - `mode`：計時模式（`up` 正數 / `down` 倒數）
      - `duration`：倒數總時長（秒，`up` 模式忽略）
      - 蜂鳴器觸發條件（可複選）：
        - `beep_on_expire`：計時結束時嗶聲
        - `beep_interval`：固定區間提醒（每 N 秒嗶一聲）
        - `beep_at`：指定時間點提醒（倒數剩 N 秒時嗶）
    - ESP32 端用 `Preferences`（NVS）持久化，重開機不遺失
    - 不需要 OTA，純資料層操作

### Phase 4 — 整合測試、優化與量產化

#### 整合測試
- [ ] 電源方案選型（實測耗電後決定 18650 / LiPo / 乾電池）
- [ ] 長時間穩定性測試（連續 > 2 小時出勤情境）
- [ ] 最終驗收

#### 電源模組採購驗收（2026-05-06 立案）

> 詳見 [`docs/power-module-purchase.md`](../docs/power-module-purchase.md)（含 HTML 版 `.html`）

- [ ] **採購下單**
  - [ ] 1000mAh 523450 LiPo PH2.0（左紅右黑、含保護板）× 1
  - [ ] TP4056 充電升壓模組 Type-C（蓮騰 MTARDTP4056S）× 2
  - [ ] JST PH2.0 母座 直插 2P × 5
- [ ] **收貨驗收**（依 §4 SOP）
  - [ ] 電池：極性、保護板、充飽電壓、容量、外觀
  - [ ] 模組：IC 標示、LED 反應、升壓電壓校正、假負載穩定度
  - [ ] PH2.0 母座：對插順暢、焊腳間距 2.0mm
- [ ] **組裝**（依 §5 步驟）
  - [ ] PH2.0 母座焊到蓮騰板 B+/B-
  - [ ] 升壓電位器校正到 5.00V ± 0.05V，鎖定
  - [ ] 接 ESP32-S3 GOOUUU VIN/GND
- [ ] **整合測試**（依 §5.4）
  - [ ] 滿電拔 USB-C 連續運作 ≥ 30 分鐘
  - [ ] 放電到 3.4V 仍正常（升壓有效）
  - [ ] 過放保護觸發測試
  - [ ] 邊充邊用不重開機
- [ ] **驗證通過後同步 SoT**（依 §8）
  - [ ] `docs/EMS_DoseSync_Pro_Prototype_V1.md` §20.4.3 / §21.1
  - [ ] `docs/pcb-outsourcing-guide.md` PCB 預留要求
  - [ ] `docs/gpio-allocation.md` VIN 接腳對應
  - [ ] `tasks/production-roadmap.md` BOM 段落

#### 量產化路線
採漸進式硬體升級，韌體不需改動：

| 階段 | 硬體方案 | 適用時機 |
|------|---------|---------|
| 現在 | 麵包板 + 杜邦線 | 韌體驗證（當前） |
| 原型機 | 洞洞板手焊 / 杜邦線固定 | 使用者試用、功能測試 |
| 小批量 | 客製 PCB + 手焊（KiCad → JLCPCB） | 5~10 台，確認設計定版 |
| 正式版 | PCB + SMT 代焊（JLCPCB 焊被動元件，自焊模組） | 設計定版後降低組裝成本 |

- [ ] **PCB 設計**（KiCad）：ESP32-S3-MINI 模組 + 8 按鈕座 + OLED 連接器 + 蜂鳴器 + 電源管理
  - 按鈕改 JST 連接器（插拔式，方便換位置或更換開關）
  - 換單行程 tactile 按鍵（同時啟用 BTN8 長按 deep sleep）
- [ ] **外殼設計**（3D 列印）：開孔對齊 OLED 視窗、8 顆按鈕、USB-C 口、蜂鳴器孔
- [ ] 洞洞板原型機組裝與使用者測試
- [ ] PCB 打樣（JLCPCB，5 片起）
- [ ] **OTA 韌體更新**：WiFi STA 模式（ESP32 從 server 拉 `.bin`，手機 App 觸發）
  - `.bin` 初期放 GitHub Releases
  - 使用場景：非出勤時段，於辦公室/家中連 WiFi 更新

## 設計決策參考

- Phase 2 設計決策：`CLAUDE.md` § Phase 2 設計決策（2026-04-18）
- 按鈕互動：單行程按鍵計畫、開關店員/模組切換功能延後
- BLE 協議：NUS + JSON（先求通，省電化延後）
- 時間同步：Phase 2 軟體對時 → Phase 3 DS3231 RTC
- 電源方案 E（乾電池）已記錄為候選

## 硬體備註

- 主控：ESP32-S3-DevKitC-1（從早期 ESP32 WROOM-32 仿製板升級）
- 按鈕：目前為開關式（toggle），未來改單行程（tactile momentary）
- 共地：所有按鈕 GND 接同一軌

---

# Phase 2A~2E+3A 實機煙霧測試清單（2026-04-22）

基於 `firmware/src/main.cpp` 實際程式碼（commit `ee5b3d7` + OLED 反色閃爍 + 按鍵 debug log）。

**索引**：BTN 0=PRIMARY(GPIO4)、1=UP(GPIO5)、2=DOWN(GPIO6)、3=POWER(GPIO7)、4=RECORD(GPIO15)
**模式循環**：MED → VENT → CUST → SET → MED
**狀態碼**：0=IDLE、1=RUNNING、2=PAUSE、3=END
**關鍵常數**：SHORT<1500ms、LONG>=2000ms、1500~2000ms=GRAY、MED 倒數 240s、剩 60s 警示、到時每 30s 重複、END 停 2s 自動回 IDLE、選單 8s timeout

## P0 開機與基本輸入

- [x] **P0-1 上電** → OLED 顯示 IDLE 畫面；Serial BOOT + /sessions 掃描
- [x] **P0-2 BTN0 快按放** → SHORT log，IDLE 下 noop
- [x] **P0-3 BTN0 按 1.7 秒放** → GRAY ignored，狀態不變
- [x] **P0-4 BTN0 按 2.5 秒放** → LONG fired → IDLE→RUNNING，MED countdown 啟動
- [x] **P0-5 BTN1/BTN2 短按** → 模式循環 MED↔SET↔CUST↔VENT（P5-6 測試中驗證）
- [ ] **P0-6 BTN3 短/長按** — 待 tactile 按鍵
- [ ] **P0-7 BTN4 短/長按** — 待 tactile 按鍵

## P1 狀態機四態轉換

- [x] **P1-1 RUNNING → PAUSE**：BTN0 單次長按通過
- [x] **P1-2 PAUSE 短按繼續**：驗證通過
- [x] **P1-3 PAUSE 長按結束**：單次長按 PAUSE→END→自動回 IDLE 通過（修復 END 鎖死 bug 後）
- [ ] **P1-4 暫停時間補正** — 待 tactile 按鍵（需連續長按串流）
- [ ] **P1-5 BTN1/BTN2 在 PAUSE 下無反應** — 待 tactile 按鍵

## P2 模式切換與 MED 倒數

- [x] **P2-1 模式循環**：MED↔SET↔CUST↔VENT 四擋循環通過
- [x] **P2-2 MED 倒數啟動**：`[MED] 240s countdown start/reset` 通過
- [x] **P2-3 剩 60 秒警示**：Serial + 2 短嗶（聽覺確認通過）
- [x] **P2-4 倒數歸零**：3 嗶 + OLED 整螢幕反色 200ms + `GIVE MED!` 閃爍（全部確認通過）
- [x] **P2-5 每 30 秒重複**：log + 3 嗶 + 反色通過
- [x] **P2-6 BTN0 短按確認給藥**：extra="epi" + countdown 重置 + 1 短嗶通過
- [ ] **P2-7 VENT 短按** — 快測項，待 tactile 或下次
- [ ] **P2-8 CUST 短按** — 同上

## P3 藥物選單（Phase 2E）

- [x] **P3-1 BTN0 短按開選單**：log + 游標在 Amiodarone 通過
- [x] **P3-2 BTN2 游標**：部分驗證（Atropine 被記錄）
- [ ] **P3-3 BTN1 游標反向循環** — 待補測
- [x] **P3-4 BTN0 短按確認**：`[DRUG] recorded: Atropine` + EVT 通過
- [ ] **P3-5 選單 8 秒 timeout** — 待補測
- [ ] **P3-6 上/下鍵重置 timeout** — 待補測
- [ ] **P3-7 選單開啟時進 PAUSE** — 待 tactile 按鍵

## P4 事件紀錄 + LittleFS（Phase 3A）

- [x] **P4-1 任務存檔**：`[FS] saved: /sessions/15.json` 通過
- [ ] **P4-2 重開機掃描** — 待補測（簡單，1 分鐘）
- [ ] **P4-3 JSON 結構** — 需要 BLE dump 或 LittleFS uploader
- [x] **P4-4 event_id 連續遞增**：log 觀察 id=1,2,3,4... 通過
- [ ] **P4-5 source 分類** — 需要看 JSON
- [ ] **P4-6 mode 欄位** — 需要看 JSON
- [x] **P4-7 extra_data**：Atropine、epi 均正確寫入
- [ ] **P4-8 容量 > 100 筆** — 待補測（需大量按鍵，等 tactile）

## P5 邊界與穩定性

- [x] **P5-1 Debounce**：多次 `debounce reject` 正常運作
- [x] **P5-2 灰色地帶**：多次 `GRAY (ignored)` 通過
- [x] **P5-3 長按半途放開**：GRAY 不觸發 LONG 通過
- [ ] **P5-4 雙鍵同時** — 待 tactile
- [x] **P5-5 長任務 > 5 分鐘**：實測跑 > 500 秒 mm:ss 顯示正確
- [x] **P5-6 倒數中切模式**（修復後）：切離 MED 清 countdown、切回 MED 重啟通過
- [x] **P5-7 PAUSE 中 MED 倒數順延**：補正邏輯通過（240023ms 到時）
- [x] **P5-8 END 期間按鍵**（修復後）：2 秒自動切 IDLE 不被鎖死

## Review

### 2026-04-22 測試通過項

- **P0-1~P0-5**（P0-6/7 待 tactile）
- **P1-1~P1-3**（P1-4/5 待 tactile）
- **P2-1~P2-6**（P2-7/8 待補測）
- **P3-1, P3-2, P3-4**（其他待補測或 tactile）
- **P4-1, P4-4, P4-7**（P4-2/3/5/6/8 待補測）
- **P5-1, P5-2, P5-3, P5-5, P5-6, P5-7, P5-8**（P5-4 待 tactile）

### Session 中修復的 bug

1. **END 跨狀態邊界誤觸**：PAUSE 長按觸發 PAUSE→END 後，若使用者繼續按或快速再按，2 秒後 END→IDLE 自動切換時會被當成 IDLE 的長按觸發 IDLE→RUNNING。修法：END 2 秒到自動切 IDLE 時清除所有按鍵 `btnPressStartMs` 並設 `btnLongFired=true`，使用者放開時以 `RELEASE (long already fired)` 重置。
2. **END 鎖死**：前一版修法要求「所有按鍵放開」才切 IDLE，toggle 下使用者持續操作無 2 秒空窗則永遠切不出 END。改回 2 秒直接切 + 清按鍵狀態。
3. **P5-6 切模式 countdown 不重置**：MED 倒數中切其他模式後切回 MED，會繼續用舊 `medCountdownStartMs`（切離的時間被算進倒數）。修法：切離 MED 時清 `medCountdownStartMs=0, medReminderActive=false, medOneMinWarningTriggered=false`；切入 MED + RUNNING 時呼叫 `startMedCountdown()`。

### 按鈕硬體註記

當前為有段式 toggle（按到底鎖定，沒按到底回彈），連續長按測試不便。待換 tactile momentary 後重跑：P0-6/7、P1-4/5、P2-7/8、P3-3/5/6/7、P4-2/3/5/6/8、P5-4。

### 後續修改（已 commit）

本 session 的 commit（push 完成）：
- `95d7511` fix: END 邊界誤觸 + 切模式 MED 倒數 bug
- `b09ed2c` refactor: 套用 PR review 5 項 MINOR/INFO
- `eed7fb1` feat: Phase 1 單元測試框架（computeTaskElapsedMs）
- `38ea2cb` test: 修正 computeTaskElapsedMs 多段暫停測試預期值
- `12fa47b` docs: Source of Truth 架構 + gap-analysis 對齊 Phase 2A~2E+3A
- `4fcfb36` feat: Phase 2 單元測試 Step A — MedCountdownDecision

---

# Phase 2 單元測試進度（2026-04-22）

依 `tasks/unit-test-plan.md` 分 3 phase；對齊 PM 規格 `docs/pm-dev-spec.md §4.2` 要求。

## Phase 1 — 低成本抽離 ✅ 完成

- [x] **computeTaskElapsedMs**（`lib/ems_logic/ems_time`）— 9/9 tests 綠
  - 注意：`elapsed_ms` 是韌體擴充欄位，**不在 PM 規格條款中**（見 `docs/gap-analysis.md` §A）
  - 已加 Source-of-Truth 註解於 `ems_time.h` 與 `test_task_elapsed.cpp`

## Phase 2 — 中度重構（進行中）

### MedCountdownDecision — Step A ✅ 完成（commit `4fcfb36`）

- [x] 新增 `lib/ems_logic/ems_countdown.h/cpp` — `decideMedCountdownAction()` 純函式
- [x] 新增 `test/test_countdown/test_med_countdown.cpp` — 12 tests 全綠
- [x] 對應 PM 規格 §4.2「4 分鐘給藥高提醒 ±50ms」

### MedCountdownDecision — Step B ✅ 完成（commit `07a0963`，2026-04-23）

- [x] `main.cpp` `updateMedCountdown()` 改為 thin wrapper，呼叫 `ems::decideMedCountdownAction()`
- [x] 同步 spec v1.4/v1.2：新增 `MedPhase` enum、`fireReminderRepeat` → `fireAlarmingPulse`（連續發報語意）
- [x] 移除 `DEFAULT_MED_REMINDER_REPEAT_MS`、新增 `DEFAULT_MED_ALARM_PULSE_MS=1500`
- [x] 補齊通氣節拍三模態輸出（蜂鳴 + 震動 guard + OLED flash 作 LED stub）
- [x] 單元測試擴充至 20 cases（phase 判定 + ALARMING 連續 pulse + wraparound）
- [ ] **實機煙霧測試待跑** → 見下方「實機煙霧測試清單（spec v1.4/v1.2 對齊）」

### 其他 Phase 2 候選（尚未排期）

- [ ] `computePauseCorrection`（抽自 `transitionState` PAUSE→RUNNING 的補正數學）
- [ ] `recordEvent` 欄位組裝（MAX_EVENTS 上限、event_id 遞增、extra_data 截斷）
- [ ] `cycleMode` / `nextCursor` / `prevCursor`（低難度純取模，可順便補）

## Phase 3 — 高風險（未排期）

- [ ] `ButtonFsm` 抽成 class（debounce + 長短按分類）
- [ ] `transitionState` 純化（side effect 外移）
- [ ] `VentMetronome` 6 秒節拍器（PM §4.2 要求，韌體尚未實作，待 PM 確認定位）

## 環境與工具現況

- PlatformIO `[env:native]` 已設（`platform=native`, `test_framework=unity`, `build_src_filter=-<*>`）
- Unity 2.6.1 已自動下載
- 測試目錄結構：`firmware/test/test_time/` + `firmware/test/test_countdown/` + `firmware/test/test_vent/`
- 執行：`pio test -e native -d firmware`（跑全部）或 `-f test_time` / `-f test_countdown` / `-f test_vent`（指定）
- Arduino build：`pio run -e esp32-s3-devkitc-1 -d firmware`

---

# 實機煙霧測試清單（spec v1.4/v1.2 對齊，2026-04-23 commit `07a0963`）

> 下次上開發板時依此清單逐項驗證。任一項 fail 就回 code 修到過為止。
> 硬體：ESP32-S3 + 5 鍵 + OLED + 蜂鳴器（震動 disabled、status LED 未接）

## A. MED_PHASE 三階段判定（核心變更）

- [ ] **A1 COUNTING 顯示**：IDLE 按主鍵進 RUNNING/MED，OLED 顯示 4 分鐘倒數，讀秒順暢（剩餘 > 1min 階段）
- [ ] **A2 WARNING 進入點**：等到剩餘 ≤ 60s 時，觸發一次「2 短嗶」中途警示（`MIN1_BEEP_PULSES` x `MIN1_BEEP_ON_MS`），Serial 印 `[MED] WARNING phase: 1-min remaining`，且不重複觸發
- [ ] **A3 ALARMING 首次觸發**：倒數歸零時立即 3 嗶 + OLED 反色 + Serial 印 `[MED] ALARMING start: continuous until main-button dismiss`，同時 `recordEvent(EVT_MEDICATION, SRC_SYSTEM, "reminder")` 寫入事件陣列

## B. ALARMING 連續發報（取代 v1.3 前的 30s 週期）

- [ ] **B1 連續 pulse 節奏**：ALARMING 進入後，每 ~1.5s 觸發一次 3 嗶 + OLED 反色，從使用者感知為連續發報（不再是 30s 一次的長間隙）
- [ ] **B2 持續時間**：放著不理 30 秒，Buzzer 仍持續吵（至少觸發 15 次以上 pulse），驗證確實「連續到按鍵為止」
- [ ] **B3 主鍵解除**：ALARMING 中短按主鍵 → 立即停嗶、OLED 停閃、記錄 `EVT_MEDICATION "epi"` 事件、倒數重置回 4:00（COUNTING 階段）
- [ ] **B4 迴圈重複**：連續做 2~3 輪「等 4 分鐘 → ALARMING → 主鍵重置」確認循環穩定、事件陣列累積正確

## C. 主鍵短按階段依賴行為（spec v1.4 新拆分）

- [ ] **C1 ALARMING 短按**：ALARMING 時短按主鍵 → 解除 + 記錄 Epi + 重置（同 B3）
- [ ] **C2 COUNTING 短按開選單**：倒數中（剩餘 > 1min）短按主鍵 → 開啟藥物選單，OLED 顯示當前藥物（游標 0 = Amiodarone），倒數持續運行不重置
- [ ] **C3 WARNING 短按開選單**：剩餘 ≤ 60s 期間短按主鍵 → 同樣開選單，Epi 倒數不受影響
- [ ] **C4 選單內選藥**：選單開啟時上下鍵切換藥物（Amiodarone / TXA / D50 / Atropine / Adenosine / Naloxone），主鍵確認 → 記錄該藥物事件、選單關閉、Epi 倒數仍在跑
- [ ] **C5 選單超時**：開啟選單後放 8s 不動，選單自動關閉，未記錄事件

## D. 通氣節拍三模態輸出（spec v1.2 補齊）

- [ ] **D1 6 秒 BEEP 節奏**：切 MODE_VENT + RUNNING，每 6 秒短嗶一聲（精度 ±50ms 人耳可接受）
- [ ] **D2 OLED flash（LED 提示替代）**：每次 BEEP 同時 OLED 短暫反色（200ms）—— Phase 3 換真 status LED 前以 OLED 作替代
- [ ] **D3 震動 skip**：`ENABLE_VIBRATION=0` 下不觸發震動馬達，Serial 無震動訊息（硬體到貨改 1 後再驗 D3b 震動觸發）
- [ ] **D4 離開重置**：VENT → 切 MED（或 RUNNING → PAUSE）後 lastVentTickMs=0，再切回 VENT+RUNNING 時立即 fire 第一聲

## E. PAUSE 行為（對齊 spec §4.2 階段轉換規則）

- [ ] **E1 COUNTING 中 PAUSE**：倒數中長按主鍵 ≥ 2s 進 PAUSE → 倒數暫停、OLED 顯示 PAUSE，主鍵短按不觸發任何行為
- [ ] **E2 ALARMING 中 PAUSE**（關鍵！）：ALARMING 中長按 ≥ 2s → 立即停止發報、不再連續 pulse，OLED 進 PAUSE 畫面
- [ ] **E3 PAUSE → RESUME 繼續**：長按 ≥ 2s 恢復 RUNNING，倒數從剩餘時間繼續（不是從 4:00 重新起算）
- [ ] **E4 VENT PAUSE**：通氣模式 PAUSE 後節拍器停止，RESUME 立即 fire 第一聲重新起算

## F. 回歸測試（確認沒打破既有功能）

- [ ] **F1 超長按結束**：任何狀態長按 ≥ 5s → END → IDLE，寫入 LittleFS 的 `/sessions/<task_id>.json`
- [ ] **F2 模式切換**：上下鍵循環 MED → VENT → CUST → SET → MED，OLED 正確顯示當前模式
- [ ] **F3 BLE 連線**：手機 App 連上後能收到 JSON 事件通知（連續給藥重置 + ALARMING 觸發事件）
- [ ] **F4 事件時間戳**：確認 `timestamp` / `elapsed_ms` 欄位在 ALARMING 觸發、主鍵解除、藥物選單選藥時都正確填入
- [ ] **F5 記憶體無泄漏**：跑 30 分鐘以上（含多輪 ALARMING + 藥物選單 + PAUSE/RESUME），Serial 觀察 heap 沒持續下降

## 驗收標準

- 所有 A~F 項目打勾完成
- Serial log 無異常錯誤訊息
- OLED 顯示無錯亂、無殘影
- LittleFS session 檔案能被 App 正常讀取
- 任一 fail → 回 code 修，不移交問題到下一輪

---

# Session Progress Snapshot

> 自動存檔於 2026-04-23 23:10

## 本次 session 完成的工作

- HTML 規格文件（`docs/ems-flow-spec.html`）`body` 加 `zoom: 1.5` 放大整體視覺
- 依 PM 流程圖（時間機制與提醒條件）比對 spec，發現通氣節拍器輸出方式不一致
- 補齊 `pm-flow-spec.md` §2、`pm-dev-spec.md` §4.2、`gap-analysis.md` 的通氣輸出為「蜂鳴 + 震動 + LED 提示」
- 採用新版本號管理策略：只比 committed 版本多 1 小版本 → `pm-flow-spec` v1.4、`pm-dev-spec` v1.2、HTML v1.0
- 合併之前累積的多個小版本變更為單一版本紀錄條目
- Spec commit `1d5a454` 推送 origin/main
- 依 spec 變更改造韌體：
  - 新增 `MedPhase` enum（NOT_STARTED / COUNTING / WARNING / ALARMING）
  - 移除 `DEFAULT_MED_REMINDER_REPEAT_MS`、新增 `DEFAULT_MED_ALARM_PULSE_MS=1500`
  - `fireReminderRepeat` → `fireAlarmingPulse`（連續發報語意）
  - `main.cpp` `updateMedCountdown` 改為純函式 thin wrapper
  - `updateVentMetronome` 補齊震動（guard）+ OLED flash（LED stub）
  - `test_med_countdown` 重寫擴充至 20 cases
- Unit test 41/41 全綠、ESP32-S3 build 成功
- 韌體 commit `07a0963` 推送 origin/main
- 煙霧測試清單（A~F 6 大類 27 項）整理進 `tasks/todo.md`

## 未完成 / 後續待處理

- 上開發板重跑實機煙霧測試（見上方 A~F 清單）
- Phase 3 實體 status LED 接線（目前以 OLED flash 代替）
- 震動馬達到貨後 `#define ENABLE_VIBRATION 1` + 接 `VIBRATION_PIN` 驗證 D3b 震動觸發
