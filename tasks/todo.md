# EMS Timer 開發進度

> **2026-05-13 重整**：todo.md 大瘦身。Impl-Phase A 韌體已完成（2026-05-04），網頁端 Phase F 移至 [`tasks/phase-f-todo.md`](phase-f-todo.md) 為權威來源。本檔聚焦 TFT 整合進行中 + 近期硬體/Demo 工作 + 後續階段 backlog。

---

## 🎯 當前焦點（2026-05-13）

- **網頁端 Phase F**：見 [`tasks/phase-f-todo.md`](phase-f-todo.md)（權威來源，不在此重複）
  - F-5 / F-6 / F-9 已完成；下一步 F-1 韌體 `ems_pairing` TDD（BLE 鏈路第一棒）
- **TFT 整合**：Step 2 後續其他畫面 + Step 3 字體放大 + Demo 對齊 batch 1~3 待實機測試（見下）
- **Phase E review-pr Batch 2**：工程層全部完成（2A/2B/2C 工程 4 commits，2026-05-14）；剩 Group 2C UI 反饋等 PM 對齊失敗哲學 A/B/C（見下方 🔧 章節）
- **22 commits 待跑 POST-COMMIT-REVIEW**（baseline `3d44950`，rate-limit 恢復後）；本次新增 4 commits（`40bad66`/`97e38fc`/`9426004`/`4b91e3c`）為 review 行動本身，可選擇略過 5 步驟
- **韌體 Phase B~H 規劃**：見 [`docs/pm-dev-spec.md §四`](../docs/pm-dev-spec.md)

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

> demo 用「下次給藥/請準備給藥/請給藥」中文。efontCN/TW 字型 ~600KB Flash 開銷可接受。

- [x] 主選單 efontCN_24 PoC（commit 3f759d6）→ 缺字（給/氣/訓/練/歷/紀/錄/統/設）→ 換 efontTW_24 解（commit 0ab4060）
- [x] OHCA 倒數標籤中文化 efontTW_24（commit 9a9575d）：「下次給藥/準備給藥/請給藥！/逾時」
- [x] STEP 06 計數行漏 setFont(Font0) bug + label gap 48→60 修正（commit 7133d53）
- [x] 標籤字級 setTextSize(1.2f, 1.2f) ≈ 29px（PM 反饋再大）+ 計數行 Shock→電擊（commit 3ea8ca5）
- [x] Flash 11→27.6%（efontTW_24 fontmap ~600KB），3.3MB 還夠
- [x] WaitFirstEpi 中文化（待本機 EPI / 按兩次 EPI 確認 / 電擊計數，commit e936caa）
- [x] 兩段確認 overlay + StartFlash 中文化（EPI/電擊？再按一次 / OHCA 啟動，commit 7ffbf97）
- [x] 6 項用詞對齊 demo.html（主選單 / 倒數標籤 / 啟動 flash / 兩段確認 / 計數行，commit 61e6899）
- [x] 4 題決策對齊 demo（OVERTIME→請給藥 / 確認已給 EPI？ / 待 EPI 副標移除 / 啟動 flash 加副行 OHCA，含 commit 61e6899）
- [x] EndCheck / Locked / Summary 中文化（commit 8fced41）
- [x] VentStandalone / VentEndCheck / VentOverlay（VENT→通氣）/ Placeholder 中文化（commit 8fced41）
- [x] Phase B 8 個畫面 320×240 中文化（DrugMenu / Backfill ×4 / Amio / Timeline / QuickMenu，commit 147f2a6）
- [ ] efontTW_24 字型品質不一致（如「電擊」兩字視覺不平衡）若不可接受 → 採 vlw 自訂字型

### 實機驗收狀態（2026-05-08 進行中）

- [x] **OHCA 主流程已測試 OK**：主選單進入 / 待 EPI / EPI 兩段確認 / 倒數 4 階段 / 兩段確認 overlay 中文 / 計數行
- [ ] **以下尚待實機驗證**：
  - EndCheck（結束案件？對話）/ Locked（已鎖定）/ Summary（案件總覽）三畫面中文 layout
  - 通氣獨立模式（drawVentStandalone 已暫停 / 主鍵繼續 / 結束 hint）
  - 通氣結束確認（drawVentEndCheck 結束通氣節奏？）
  - OHCA 內 vent overlay（通氣 N / 通氣暫停）
  - Placeholder（訓練模式 D 階段 / 歷史紀錄 E 階段 / 系統設定 G 階段）
  - **Phase B 全部 8 畫面**：DrugMenu / Backfill 4 步驟 / AmioConfirm / Timeline / QuickMenu

### Demo vs 韌體對齊修正（2026-05-08 PM 反饋）

#### Batch 1：核心對話流程 ✅ 已完成

- [x] **demo 主選單 title 字級調小**（16px ASCII，對齊韌體 Font0 size 2）
- [x] **demo 移除所有 menu cursor `▶`**（韌體只有反白 highlight）
- [x] **A1：OHCA 兩段確認改全螢幕對話**（取代 44px bar overlay）— `drawOhcaConfirmDialog(EVT_EPI_LOCAL/SHOCK_LOCAL)`
  - 全螢幕黑底 + 琥珀邊框 + 標題 36px + 內文 29px × 2 行 + 分隔線 + hint
  - EPI: 「確認已給 EPI？/ 確認後將建立時間戳 / 並重啟 4 分鐘倒數 / 再按 EPI 鍵確認 返回取消」
  - SHOCK: 「確認已電擊？/ 確認後將建立時間戳 / 不影響 EPI 倒數 / 再按電擊鍵確認 返回取消」
  - BACK 鍵取消對話（modal 行為）
- [x] **B1：END_CHECK 改 3 項**（demo 對齊）
  - 完成並結束案件 / 前往補登 / 返回案件
  - 上下鍵 cycle 3 項（mod 3）
- [x] **A7：END_CONFIRM 二次確認對話**（END_CHECK 選「完成並結束」後彈出）
  - 全螢幕黑底 + 紅色邊框 + 標題「確認結束案件？」+ 「結束後不可修改」+ 「主鍵確認 返回取消」
  - 主鍵 → LOCKED；返回 → 退回 END_CHECK 主畫面
- [x] flags 欄位擴成 uint16（容納 bit 8 endConfirmShown）

#### Batch 1 待實機測試項目

- [ ] 主選單字級看起來符合預期（韌體 size 2 ≈ 16px ASCII）
- [ ] 短按 EPI/SHOCK → 應彈出全螢幕對話（不是底部 bar）
- [ ] 對話內按 EPI/SHOCK 同鍵 → 確認成立
- [ ] 對話內按返回鍵 → 取消對話
- [ ] OHCA 進行中長按主鍵 3s → END_CHECK 看到 3 項（完成並結束 / 前往補登 / 返回案件）
- [ ] END_CHECK cursor 0「完成並結束」→ 主鍵 → 紅色邊框「確認結束案件？」對話
- [ ] 對話內主鍵 → LOCKED；對話內返回 → 退回 END_CHECK 3 項
- [ ] END_CHECK cursor 1「前往補登」→ 主鍵 → 進入藥物選單（補登 EPI / Amiodarone）

#### Batch 2：Flash 訊息（5 個）✅ 已完成

- [x] **Flash helper 實作**：`triggerFlash(title, subtitle, duration_ms, titleColor)` + drawFlashOverlay + tick timeout + snapshot bit 9 + partial path 排除 modal flags
- [x] A2：「EPI 已紀錄」+「重新倒數 4 分鐘」（COLOR_ACCENT_OK，1200ms）
- [x] A3：「電擊已紀錄」（COLOR_ACCENT_OK，1200ms）
- [x] A4：「Amiodarone 已紀錄」（COLOR_ACCENT_OK，1200ms）
- [x] A5：「案件結束並鎖定」+「已存入歷史紀錄」（COLOR_ACCENT_ALERT，1200ms）
- [x] A6：「6 秒給氣」+「已開啟/已繼續/已暫停/已關閉」（800ms）

#### Batch 2 待實機測試項目

- [x] A2 — EPI 確認後 flash「EPI 已紀錄」+「重新倒數 4 分鐘」
- [ ] A3 — 電擊確認後 flash「電擊已紀錄」
- [ ] A4 — 長按 EPI → Amiodarone → 確認後 flash「Amiodarone 已紀錄」
- [ ] A5 — END_CHECK 完成並結束 → END_CONFIRM → 主鍵 → flash「案件結束並鎖定」+「已存入歷史紀錄」
- [ ] A6 — OHCA 內按返回 → QuickMenu 4 個動作對應 flash（已開啟/已繼續/已暫停/已關閉）
- [x] partial update 已排除 modal flags，dialog/flash 不會被倒數時間穿透

#### Batch 3：次要補完 ✅ 已完成（待燒錄實機驗證）

- [x] **A8：VENT_PRE preview 畫面**（drawVentPre）
  - 進入 Vent 後先顯示「按主鍵開始」預備畫面
  - 主鍵 → 啟動倒數；返回鍵 → 回主功能表
  - 上下鍵預備時也可調音量
  - 加 ventPreShown 旗標 + snapshot bit 10 (0x400)
- [x] **B3：QUICK_MENU 加「案件簡版總覽」**（demo 對齊）
  - 未開啟給氣：3 項（開啟給氣 / 案件簡版總覽 / 返回 OHCA）
  - 已開啟給氣：4 項（暫停或繼續 / 關閉給氣 / 案件簡版總覽 / 返回 OHCA）
  - 簡版總覽選項 → flash「簡版總覽 / 結束案件後看完整總覽」（2 秒）

#### Batch 3 待實機測試項目

- [ ] A8 — 主功能表選「6 秒通氣節奏」→ 看到「按主鍵開始」預備畫面（不立即開始倒數）
- [ ] A8 — 預備畫面按上下鍵 → 音量改變
- [ ] A8 — 預備畫面按返回鍵 → 回主功能表
- [ ] A8 — 預備畫面按主鍵 → 開始倒數 1, 2, 3, 4, 5, 6
- [ ] B3 — OHCA 進行中按返回鍵 → QuickMenu 看到「案件簡版總覽」項目
- [ ] B3 — 選「案件簡版總覽」→ flash「簡版總覽 / 結束案件後看完整總覽」（2 秒）
- [ ] B3 — 開啟給氣後再進 QuickMenu → 4 項（pause / disable / summary / back）

### 未跑 review 的 commits（rate limit 恢復後補跑）

依 CLAUDE.md POST-COMMIT-REVIEW，以下 commits 已 commit 未走 5 步驟 review：

**🎯 下次 review 起點 baseline：`3d44950`（demo.html 字級對齊韌體 PM 反饋放大調整，2026-05-09 16:45 GMT+8）**
- 預期 7:40pm rate limit 重置後從此 hash 起跑 POST-COMMIT-REVIEW 五步驟
- step 1 eslint 跳過（無 .eslintrc.json）｜step 2 /simplify 立即跑｜step 3 pr-reviewer / step 4 review-pr / step 5 通知 由 schedule 接續

```
7ffbf97  兩段確認 overlay + StartFlash 中文化
e936caa  WaitFirstEpi 中文化（待本機 EPI / 按兩次 EPI 確認 / 電擊）
3ea8ca5  OHCA 倒數標籤 1.2x + 電擊中文
7133d53  STEP 06 setFont fix + label gap 60
9a9575d  OHCA 倒數標籤中文化
0ab4060  efontCN→efontTW（解繁中缺字）
3f759d6  主選單 efontCN_24 中文 PoC
7a85f64  vent 大字 y 下移避撞 Vol
96ef727  剩餘畫面（EndCheck/Locked/Summary/TwoStepArmed/Vent*/Placeholder）
e242f37  todo Step 3 字體放大同步
39b544a  字體放大 1.5x（OHCA 系列已改 layout 畫面）
b7f9382  todo + memory sync
5be48fa  setRotation 1→3 修 LGFX vs Adafruit 差 180
9ed9a63  LovyanGFX + DMA pushSprite
c2155a4  SPI 80MHz（穩定但 lib overhead 才是視覺瓶頸）
34838f9  GFXcanvas16 全頁緩衝（被 9ed9a63 取代）
4d831ab  partial 局部 push + canvas 殘留清除
b675a45  SPI 40MHz + setTextColor(fg,bg) 試解掃描感
8e428c8  WaitFirstEpi 對齊新 layout + partial path
147f2a6  Phase B 補登/Timeline/QuickMenu/Amio 中文化 320×240 layout
8fced41  EndCheck/Locked/Summary/Vent/Placeholder 中文化
61e6899  中文用詞對齊 demo.html
7ffd2cf  demo 模擬 320×240 TFT + 字級對齊韌體
c6a92b4  batch1 韌體 vs demo 對齊（A1+B1+A7）
9e76ccb  batch1 確認對話框外框放大
57d043b  batch2 5 個 flash 過場提示 + partial update 排除 modal
e02e017  batch3 VENT_PRE 預備畫面 + QuickMenu 案件簡版總覽
578bc1e  清 OLED/SH110X 別名 + SoT/CLAUDE.md TFT 同步
6308d3e  TFT 中文 VLW 字型整合 + UI 字級與排版調整
3d44950  demo.html 字級對齊韌體 PM 反饋放大調整 ← 🎯 review baseline
```

**baseline 之後新增的 Phase E review-pr Batch 2 工程修復（2026-05-14，不另外 review，本身就是 review 行動）**：
```
40bad66  Phase E 批次 2A — 5 處 silent failure 加 log
97e38fc  Phase E 批次 2B — C-1 FIFO bug 修 + D7 翻轉 + D8 新增
9426004  Phase E 批次 2C/I-7 — storage_delete 順序反轉 + rollback
4b91e3c  Phase E 批次 2C/C-2+C-4 — save persist rollback + LOCKED auto-retry + buffer 16KB
```

### Rate limit 恢復後接續清單（按優先序）

1. ~~EndCheck/Locked/Summary 中文化~~ ✅ commit 8fced41
2. ~~VentStandalone / VentEndCheck / VentOverlay 中文化~~ ✅ commit 8fced41
3. ~~Placeholder 中文化~~ ✅ commit 8fced41
4. **drawOhcaCountdownCommon 的 OHCA badge 是否中文化** — B 不動（demo 仍用英文）
5. **drawMainMenu 標題「EMS DOSESYNC PRO」是否本地化** — B 不動（demo 也用英文）
6. ~~Phase B 8 畫面 320×240 中文化~~ ✅ commit 147f2a6
7. **完整實機 flow 驗收剩餘項目**（見上方「實機驗收狀態」清單）
8. ~~demo.html 對齊韌體 320×240 layout + 字級~~ ✅ commit 7ffd2cf（#screen 固定 320×240 + 字級對齊 efontTW_24/FreeMonoBold24pt7b + cursor 反白）
9. **補跑 22 個 commits 的 5 步驟 review**
10. **推 origin（GitLab + GitHub backup）**

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

## 🔧 Phase E review-pr Batch 2 待處理（2026-05-13 立案）

> **背景**：Phase E 持久化（commit `5a2027e` + `a0c5b9c`）跑 `/pr-review-toolkit:review-pr`
> 5-agent 審查，找到 13 CRITICAL + 23 IMPORTANT + 15 SUGGESTION 共 51 項。低風險批次已處理：
> - `7ebec92` /simplify 8 項清理
> - `3ea5884` Batch 1：註解 + magic number 13 項
> - `8844e9f` Batch 3：補測 13 case + 強化 4 既有
>
> **Batch 2 是行為改動 + 醫療安全相關**，需要設計決策 + 實機驗證，故獨立追蹤。

### Group 2A — 純 log 加固 ✅ **已完成（2026-05-14 commit `40bad66`）**

- [x] **C-3** `enforce_fifo_cap` 加 `[STORAGE] FIFO evict type=N id=X` audit log（fs 層 delete_file always return true，logic 層補語意層 trace）
- [x] **I-3** `storage_init` persist_index 失敗加 warn（含「下次 save 會修復」備註）
- [x] **I-4** rebuild 路徑 `fill_meta_from_bin` 損毀檔加 warn（OHCA / Training 各一條）
- [x] **I-5** `fs_read_file` short read 加 warn 印 `path/expected/actual`
- [x] **I-6** `fs_rename_file` remove-to 失敗加 warn
- [x] 新增 `EMS_STORAGE_LOG` macro（`#ifdef ARDUINO` 包 Serial.printf；native test 0 開銷）

### Group 2B — C-1 FIFO bug 修 ✅ **已完成（2026-05-14 commit `97e38fc`）**

- [x] **C-1** `storage_save_case` STEP 01.5 加 pre-evict + `enforce_fifo_cap` while 條件 `>` → `>=`
  - 修法：把 enforce_fifo_cap call 從 STEP 07 搬到 STEP 01.5（pre-write），拿掉 STEP 01 `case_count >= kTotalCapacity` guard（cap 算術保證 case_count + 1 <= kTotalCapacity）
- [x] **TDD**：D7 翻轉 `TEST_ASSERT_FALSE` → `TEST_ASSERT_TRUE`（OHCA seq=1 被擠 + Training 不動 + 新 seq=71 存在）；新增 D8 對稱情境（Training 被擠 + OHCA 不動）
- [ ] **實機驗證**：50 OHCA + 20 Training + 1 OHCA → 應成功 + 最舊 OHCA seq 1 被擠掉、Training 不動（native test 已 cover，實機留 Phase F 整合驗收）

### Group 2C — 醫療失敗哲學重設計（部分完成）

> agent 建議的失敗哲學頂層原則：「**資料遺失優於不一致**」→ 寫盤失敗就拒絕進 LOCKED
> 完成態，強制使用者重試或承認失敗。當前實作三選一全沒選清楚。

#### ✅ 工程層 invariant 修復（不等 PM）

- [x] **I-7** `storage_delete` 順序反轉 + persist 失敗 rollback（2026-05-14 commit `9426004`）
  - snapshot case_meta → shift left RAM → persist_index 失敗 shift right + 寫回 snapshot + return false → 成功才砍檔
  - 新增 G6 test 注入 `rename_file = fail_rename` 驗 rollback
- [x] **C-2** `storage_save_case` persist 失敗 rollback（2026-05-14 commit `4b91e3c`）
  - case_count-- + delete events.bin + return false + log；`next_seq` 不退（保 D6 單調 invariant + Phase F BLE 增量同步安全），失敗的 seq 成 hole
  - 新增 G7 test 驗 rollback + hole 行為（restore 後下一筆拿 seq+1 不重用）
- [x] **C-4 最小修** `main.cpp:1393` `g_locked_saved = ok`（2026-05-14 commit `4b91e3c`）
  - 只在成功設 true → 下個 tick 自動 retry；失敗印「will auto-retry」trace
  - 完整 UI 反饋（紅色警告 + 蜂鳴 + 重試按鈕）留下方 PM 對齊
- [x] **EMS_STORAGE_INDEX_BUFFER_SIZE 12288 → 16384** follow-up：C-2 改成 check persist return 後暴露既有 buffer overflow bug（header 註解明示 TODO），bump 解 70 cases 場景

#### ⏳ 需 PM 對齊（暫緩到 Phase E 整合驗收會議）

- [ ] **PM 對齊**：失敗該硬擋（拒絕 LOCKED）還是 fallback（先保住 in-RAM）？三選一：
  - **A. 硬擋**：寫失敗就拒絕 LOCKED，使用者必須重試或承認失敗（一致性最強，但 partition 真壞 → 卡死無法結案）
  - **B. fallback**：寫失敗印 warning + 繼續 LOCKED，下次或重啟試 retry（救護不中斷，但突然斷電 → 紀錄全失）
  - **C. 妥協**：LOCKED 仍進入但 SUMMARY 紅色警告 + 蜂鳴；提供「重試」按鈕；3 次失敗才放棄（平衡，但實作複雜度最高）
- [ ] **C-4 完整修** UI 反饋（依 PM 對齊結果實作）：
  - SUMMARY 畫面加紅色「保存失敗」橫條
  - 蜂鳴器 pattern（連續 vs 間歇）
  - 重試按鈕 / 三次失敗才放棄
- [ ] **實機驗證**：手動觸發 LittleFS 寫入失敗（拉滿 partition）→ UI 看到紅色警告

### Group 2D — 型別大重構（暫緩到 Phase F BLE 整合穩定後）

- [ ] **C-12** `case_meta_t` 9 metric mirror `ohca_case_summary_t` → 抽 `case_metric_counts_t` sub-struct
- [ ] **I-21** `storage_case_type_t` 升 `enum class : uint8_t` 擋 `(cast)42` 亂塞
- [ ] **I-22** `case_meta_t::id` 升 `case_id_t` newtype + factory `make_case_id(uint32_t seq)`
- [ ] **I-23** `IStorageBackend` 改 C++ virtual base 或至少在 `storage_init` 開頭 assert 全 fn ptr 非 NULL

### 對應 commits 與 review 報告

| 階段 | commit | 內容 |
|------|--------|------|
| baseline | `5a2027e` + `a0c5b9c` | Phase E 原始實作（feat + fix）|
| /simplify | `7ebec92` | 8 項清理（純函式內 lambda 抽出、static 名稱、註解修正）|
| Batch 1 | `3ea5884` | 註解 + magic number 13 項清理 |
| Batch 3 | `8844e9f` | 補測 13 case（A6-A8 / C5-C6 / D7 / E6-E7 / G1-G5）+ 強化 4 既有 |
| **Batch 2A** | `40bad66` | 5 處 silent failure 加 log + `EMS_STORAGE_LOG` macro |
| **Batch 2B** | `97e38fc` | C-1 FIFO pre-evict + D7 翻轉 + D8 新增 |
| **Batch 2C/I-7** | `9426004` | storage_delete 順序反轉 + rollback + G6 |
| **Batch 2C/C-2+C-4** | `4b91e3c` | save persist rollback + LOCKED auto-retry + index buffer 12→16KB + G7 |

**累計成果**：native test 263 cases / 262 PASSED（test_storage_hw native ERROR 為 baseline 既有）；ESP32 build SUCCESS，Flash 27.1% / RAM 19.5%（前 16.4%，+8KB BSS 給 index buffer）

### 進場順序建議

1. ✅ **Group 2A 已完成**
2. ✅ **Group 2B 已完成**（native test cover 完整，實機 50+20+1 場景留 Phase F 整合驗收）
3. ✅ **Group 2C 工程層**（I-7 + C-2 + C-4 最小修）**已完成**
4. ⏳ **Group 2C UI 層**：等 PM 對齊失敗哲學 A/B/C → 決定 C-4 完整修法 → SUMMARY 紅色警告 + 蜂鳴 pattern + 重試流程
5. **Group 2D 不建議近期做**：型別重構動序列化結構，等 Phase F BLE 整合穩定後再評估

---

## 📋 近期工作（2026-05-12 / 13）

### 已完成

- ✅ **Phase F 網頁端落地**：Cloudflare Pages + D1 部署完成（commit `19bc495`）；F-5 後端骨架 + F-6 前端骨架 + F-9 對齊 SoT §17 完整 UI 全綠（見 `tasks/phase-f-todo.md`）
- ✅ **進度報告 5 PM HTML**（`docs/progress.html` 累加進度 5：Phase F 網頁端完整落地）
- ✅ **手機 demo 320×240 文字溢出修正**（Android line-height 1.5→1.2，commit 待 review）
- ✅ **硬體採購清單 V1 功能標記**（`docs/hardware-procurement-v2.{md,html}` 同步功用欄位 + 非 V1 圖例 + SoT §2.2/§6.6 規格引用）
- ✅ **HTML 文件未定義 CSS 變數修正**

### 待跑

- [ ] 22 commits POST-COMMIT-REVIEW 五步驟（rate-limit 恢復後，baseline `3d44950`）
- [ ] push origin（GitLab + GitHub backup）
- [ ] 硬體採購清單下單（蝦皮 / 露天）— 等 PM 確認方案
- [ ] **GitLab Pages 驗證**：自架 GitLab 實例 Pages 功能是否啟用（admin 設定），或改用 GitHub Pages

---

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
