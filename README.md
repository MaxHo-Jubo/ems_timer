# EMS DoseSync Pro — Prototype V1

院前 OHCA 現場的給藥與事件時間同步輔助裝置。協助 EMT / ALS 接手人員掌握 EPI 4 分鐘節奏、紀錄電擊與藥物事件、案件結束提供完整總覽，並可透過手機 App 同步單一案件輔助登打救護紀錄表。

> 對外正式名稱 **EMS DoseSync Pro**；專案目錄沿用舊名 `ems_timer`。

---

## 🌐 線上 Demo

| 內容 | 網址 |
|------|------|
| **Landing 首頁** | https://ems-dosesync-demo.pages.dev/ |
| 產品流程規格（互動 HTML） | https://ems-dosesync-demo.pages.dev/EMS_DoseSync_Pro_Prototype_V1_flow.html |
| 互動 Demo（手機沉浸式） | https://ems-dosesync-demo.pages.dev/demo/ |
| Demo 真實速度模式 | https://ems-dosesync-demo.pages.dev/demo/?speed=1x |

部署於 Cloudflare Pages。建議在手機開啟 Demo 取得最佳互動體驗。

> **Demo 已知簡化**：Training 補登（V1 §15.10）目前僅顯示 `Demo 暫未展開` 提示後返回（`docs/demo/index.html:1474`）；完整補登流程（長按 EPI / 長按電擊 → 接手前 1~5 / 純補登 1~3）依 SoT §15.10 + §9 在韌體 Phase B 實作。

---

## 📘 文件結構（Source of Truth 由上至下）

| 優先 | 文件 | 用途 |
|:---:|------|------|
| 1 | [`docs/EMS_DoseSync_Pro_Prototype_V1.md`](docs/EMS_DoseSync_Pro_Prototype_V1.md) | **產品規格唯一真實來源**（PM 封版） — 主功能 / OHCA lifecycle / EPI 倒數 / 兩段確認 / 補登 / 案件總覽 / 6 秒通氣 / Training / App 同步 / 系統設定 / 硬體 |
| 1 | [`docs/EMS_DoseSync_Pro_Prototype_V1_flow.html`](docs/EMS_DoseSync_Pro_Prototype_V1_flow.html) | 同等內容的視覺化版本（18 章節） |
| 2 | [`docs/pm-dev-spec.md`](docs/pm-dev-spec.md) | 工程實作細節（v2.0）— 模組 API、資料結構、Phase A~H 開發階段 |
| 3 | [`docs/demo/index.html`](docs/demo/index.html) | 手機互動 Demo 原始碼（single file，~1900 行） |
| 4 | [`tasks/todo.md`](tasks/todo.md) | 開發進度與下一步待辦 |
| 5 | [`CLAUDE.md`](CLAUDE.md) | 專案約定 |
| - | [`docs/pcb-outsourcing-guide.md`](docs/pcb-outsourcing-guide.md) | **Phase 4 量產 PCB 外包指南**（外包範圍、台灣管道、7 項準備清單、風險與預算） |
| - | [`docs/EMS_DoseSync_Button_Icon_Label_Revision.md`](docs/EMS_DoseSync_Button_Icon_Label_Revision.md) | **按鍵圖示標示修正規格**（針筒 / 閃電圖案、外殼工藝注意事項、定案 #483~#487）— 與 V1 SoT §4.1 / §4.1.1 一致，補充工藝細節與同步檢查清單 |

衝突時以較高層為準。`docs/pm-flow-spec.md` 已於 2026-04-27 廢止（被 V1 SoT 取代）。

---

## 🎯 產品範圍（V1 封版）

**做：**
- OHCA 案件 lifecycle（待本機 EPI → 倒數 → 預警 → 警報 → 超時 → 結束鎖定）
- EPI 4 分鐘倒數提醒（精度 ±50ms）
- EPI / 電擊 / Amiodarone 兩段確認
- 補登（接手前 1~5 / 純補登 1~3）
- 案件總覽 + Timeline
- 獨立 6 秒通氣節奏 + OHCA 中切入（EPI 高優先打斷）
- Training 模式（30s / 1m / 4m）
- 歷史紀錄（OHCA 50 + Training 20 自動覆蓋）
- App 單案配對碼同步（4 位數 / 120s TTL）
- Type-C 電腦端管理工具（匯出 / 清除）
- 系統設定（亮度 / 音量 × 2）

**不做（V1 §2.2 明令排除）：**
雲端、帳號登入、即時院端同步、GPS、病患個資、錄音功能、PDF 報表、權限管理。

---

## 🚀 部署

### 線上 Demo（Cloudflare Pages）

一次性設定：

```bash
npm install -g wrangler
# OAuth 不通可改 API Token 路線：產生 token → export CLOUDFLARE_API_TOKEN=xxx
wrangler login
```

每次部署：

```bash
./scripts/deploy-cf-pages.sh
```

腳本會：
1. 從 `docs/` 挑出公開檔案組成 `dist/`
2. 加上 `scripts/landing.html` 當首頁
3. 跑 `wrangler pages deploy`（commit message 強制純 ASCII 規避 Cloudflare API 8000111）

只 build 不部署：

```bash
./scripts/deploy-cf-pages.sh --build-only
```

### 隔離設計

`dist/` 只包含 3 個公開檔案，工程文件（`pm-dev-spec.md` / `gap-analysis.md` / `incremental-impl-plan.md` / V1.md）不會 deploy。

---

## 🔧 開發階段（Phase A~H）

對齊 `docs/pm-dev-spec.md §四`。**目前狀態（2026-09-02 校準）：Phase A~F 韌體已實作完成；
Phase G/H 部分完成，細節見下表。**

⚠️ 下表「✅ 已完成」僅代表**韌體已實作 + native test 綠燈 + 編譯通過**，不代表已完整
上機驗收——除 Dev-Phase 1 硬體原型（2026-04-17 驗收通過）與少數獨立驗證過的硬體元件
（DS3231 RTC、電池供電拓樸）外，尚未見任何 Phase 等級的完整上機端到端驗收記錄，這是
全專案累積至今的最大殘餘風險，詳見各 Phase 的 handover 文件「上機驗收清單」。

| Phase | 範圍 | 狀態 |
|-------|------|------|
| A | OHCA 核心狀態機 + EPI 倒數 + 兩段確認 | ✅ 已完成 |
| B | 補登 + Amiodarone + 案件總覽 + Timeline | ✅ 已完成 |
| C | 6 秒通氣節奏（獨立 + OHCA 切入 + EPI 高優先打斷） | ✅ 已完成 |
| D | Training 模式 | ✅ 已完成 |
| E | LittleFS 持久化 + 歷史紀錄 | ✅ 已完成 |
| F | App 配對碼同步（BLE NUS，MVP1~3 皆已實作） | ✅ 已完成 |
| G | 系統設定 + Type-C 管理工具 | 🟡 系統設定／裝置名稱／裝置資訊畫面已完成（僅待上機驗收，見 `docs/superpowers/phase-g-device-info-handover.md`）；**Type-C 管理工具（列案件/匯出/清除）未開工** |
| H | 電源管理 | 🟡 低電量警告已完成；**螢幕常亮／邊充邊用測試／Type-C 插拔不中斷案件未見專門實作或驗證**（注意：pm-dev-spec 定義的「電源管理」跟已完成的「電量顯示」UI 是不同範圍，只有低電量警告一項重疊，見 `docs/superpowers/phase-h-handover.md`） |

**另外兩塊完全未開工、不在本表 Phase A~H 範圍內**（見 `docs/pm-dev-spec.md §二/§三`）：
- **手機 App**（Dev-Phase 3）——repo 內無任何 RN/Flutter/native 專案，現有 `web/`/`docs/demo/` 僅為視覺參考 demo，不是真實 App
- **Type-C 電腦端管理工具**——同上表 Phase G 內的同名項目，完全沒有對應程式碼

**Phase A 開工時 throwaway 重寫**：既有韌體 `MED_PHASE` / `ems_countdown.cpp` / `vent_metronome.cpp` / 5 鍵 / 4 模式切換邏輯全砍。詳見 `docs/pm-dev-spec.md §五` 與 `tasks/todo.md`。

---

## 📦 Repo 結構

```
ems_timer/
├── docs/
│   ├── EMS_DoseSync_Pro_Prototype_V1.md         ← SoT
│   ├── EMS_DoseSync_Pro_Prototype_V1_flow.html  ← SoT 視覺化
│   ├── pm-dev-spec.md                           ← 工程實作
│   ├── demo/index.html                          ← 手機互動 Demo
│   ├── gap-analysis.md
│   └── incremental-impl-plan.md
├── firmware/                                    ← Phase A 開工時重寫
│   ├── src/main.cpp
│   ├── lib/ems_logic/                           ← throwaway
│   ├── lib/ble_nus/                             ← Phase F 過渡保留
│   └── test/                                    ← throwaway
├── scripts/
│   ├── landing.html
│   └── deploy-cf-pages.sh
├── tasks/
│   ├── todo.md                                  ← 進度與待辦
│   └── ...
├── .gitlab-ci.yml                               ← GitLab Pages（test job 已移除無 runner）
├── CLAUDE.md
└── README.md
```

---

## 🧪 韌體本機測試

```bash
cd firmware
pio test -e native
```

CI 不跑單元測試（GitLab webotopia 實例無 runner）。

---

## 📡 Repository

- **主要開發**：GitLab webotopia — `https://gitlab.webotopia.work/maxhero/ems_timer.git`
- **備份鏡像**：GitHub — `https://github.com/MaxHo-Jubo/ems_timer.git`（手動 `git push github main` 同步）

---

## 📝 變更紀錄重點

- **2026-04-27**：V1 規格封版；舊 `pm-flow-spec.md` 廢止；pm-dev-spec 重寫 v2.0；手機互動 Demo 上線；部署 Cloudflare Pages
- **2026-04-24**：repo 遷移至 GitLab webotopia（origin），GitHub 改備份
- **2026-04-23**：MED_PHASE 三階段倒數 + 通氣節拍三模態輸出（將在 Phase A 重寫）
- **2026-04-21**：Phase 2C 按鈕重構（將在 Phase A 重寫）
- **2026-04-17**：Phase 1 硬體原型驗收通過
