# docs/ 文件索引

> **本檔是專案文件的唯一索引**。`CLAUDE.md` 與根目錄 `README.md` 只指回這裡，不另立清單。
> 新增或廢止文件時更新本檔；狀態欄以**專案實際進度**為準，文件內自己寫的「尚未動工」等狀態列可能過時，以本表註記為主。
> 建立：2026-09-06。

**狀態圖例**：🟢 現行（實作依據或持續維護）｜🟡 現行但有已知過時處｜⚪ 已執行完成，保留供追溯｜📜 歷史（決策或評估紀錄，不再更新）｜❌ 廢止

**衝突優先序**：SoT V1 > pm-dev-spec > 其餘工程文件 > 計畫與交接文件。同一主題的 `.html` 是給 PM 看的排版版本，內容以 `.md` 為準。

---

## 1. 規格（Source of Truth）

| 狀態 | 文件 | 用途 | 最後動 |
|:---:|---|---|---|
| 🟢 | [`EMS_DoseSync_Pro_Prototype_V1.md`](EMS_DoseSync_Pro_Prototype_V1.md) | **產品規格唯一真實來源**（PM 封版）。§2.3／§3.1 註記、§20.4／§20.5、§21.1～§21.3 為工程端擴充段落，套用 PM 新版時必須保留 | 2026-09-06 |
| 🟢 | [`EMS_DoseSync_Pro_Prototype_V1_flow.html`](EMS_DoseSync_Pro_Prototype_V1_flow.html) | SoT 的視覺化版本（18 章節） | — |
| 🟢 | [`pm-dev-spec.md`](pm-dev-spec.md) | 工程實作規格 v2.0：模組 API、資料結構、Impl-Phase A～H 範圍與狀態標記 | 2026-09-06 |
| 🟢 | [`EMS_DoseSync_Button_Icon_Label_Revision.md`](EMS_DoseSync_Button_Icon_Label_Revision.md) | 按鍵圖示標示修正規格（針筒／閃電、外殼工藝、定案 #483～#487），補充 SoT §4.1 | 2026-04-30 |
| 🟡 | [`EMS_DoseSync_Pro_Test_Plan_V1.md`](EMS_DoseSync_Pro_Test_Plan_V1.md) / [`.html`](EMS_DoseSync_Pro_Test_Plan_V1.html) | PM 驗收測試計畫 v1.0。**設定值域已過時**（音量已改 0/1、螢幕亮度已移除），同步工作見 `tasks/todo.md` T7 | 2026-05-05 |
| 🟢 | [`ble-time-sync-protocol.md`](ble-time-sync-protocol.md) | BLE 對時協定的訊息層定義（Dev-Phase 2 軟體對時＋Dev-Phase 3 DS3231 校正共用），韌體與 web 端實作依據 | 2026-05-15 |

## 2. 硬體

| 狀態 | 文件 | 用途 | 最後動 |
|:---:|---|---|---|
| 🟢 | [`gpio-allocation.md`](gpio-allocation.md) / [`.html`](gpio-allocation.html) | **GPIO 分配單一真相來源**：速查表、按鍵、顯示、I2C bus 位址表、互斥約束、變更歷程 | 2026-09-05 |
| 🟢 | [`v1-hardware-spec.html`](v1-hardware-spec.html) | V1 硬體與晶片規格彙整（僅 HTML）：各晶片關鍵規格＋規格來源標記（實測／標稱／未驗證）、腳位對照總表、互斥約束與踩坑、非 V1 備料、功耗預算。**MAX17048 換裝方案全章標記未驗證**。腳位以 `gpio-allocation.md` 為準，本檔為彙整視圖 | 2026-09-07 |
| 🟢 | [`hardware-procurement-v2.md`](hardware-procurement-v2.md) / [`.html`](hardware-procurement-v2.html) | 硬體採購清單 v2：13 項品項與採購／接線狀態快照、升級路徑、給 PM 下單參考 | 2026-09-05 |
| 🟡 | [`power-module-purchase.md`](power-module-purchase.md) / [`.html`](power-module-purchase.html) | 電源模組採購與組裝 SOP、TP4056 校壓、MAX17043 接線與驗收（§10.7／§10.8）。**§10.7 待依 MAX17048 換裝更新**，見 `tasks/todo.md` T9／T10 | 2026-09-05 |
| 🟢 | [`pcb-outsourcing-guide.md`](pcb-outsourcing-guide.md) | 量產 PCB 外包指南：外包範圍、管道、7 項準備清單、預算、報價基準、**§11 量產零件對映與設計規則** | 2026-09-06 |
| ⚪ | [`tft-migration-plan.md`](tft-migration-plan.md) | OLED → 2.8" TFT 遷移計畫。已於 2026-05-08 執行完成，文件內「草案」狀態列已過時；§3.1 接線驗證表仍是 TFT 走線的參考 | 2026-09-05 |
| ⚪ | [`ds3231-integration-plan.md`](ds3231-integration-plan.md) | DS3231 RTC 整合計畫（runtime 偵測＋雙模式 backend）。2026-05-24 完成上機，文件內「規劃中」已過時 | 2026-05-24 |
| 📜 | [`power-vendor-verification.html`](power-vendor-verification.html) | 電源模組廠商諮詢清單驗證報告（2026-05-25，僅 HTML） | 2026-05-25 |

## 3. Impl-Phase 計畫與交接

| 狀態 | 文件 | 用途 | 最後動 |
|:---:|---|---|---|
| ⚪ | [`phase-d-training-plan.md`](phase-d-training-plan.md) | Phase D 訓練模式實作計畫。Phase D 韌體已完成（見 `phase-g-system-settings-plan.md` 前置），文件內「尚未開工」已過時 | 2026-07-14 |
| ⚪ | [`phase-f-web-validation-plan.md`](phase-f-web-validation-plan.md) | Phase F「先網頁後 App」BLE 驗證計畫。BLE 鏈路已完成，web 測試工具在 `ble-tester/` | 2026-05-15 |
| 🟡 | [`phase-g-system-settings-plan.md`](phase-g-system-settings-plan.md) | Phase G 計畫：系統設定（已完成、上機驗收中）＋ Type-C 管理工具（**未開工**） | 2026-07-18 |
| 🟢 | [`superpowers/phase-h-handover.md`](superpowers/phase-h-handover.md) | Impl-Phase H 電量顯示交接文件：13 個 task 完成、§3-B 上機驗收清單待硬體 | 2026-09-01 |
| 🟢 | [`superpowers/phase-g-device-info-handover.md`](superpowers/phase-g-device-info-handover.md) | Impl-Phase G 裝置資訊畫面交接文件：程式完成、待 §3-B 上機驗收 | 2026-09-02 |
| ⚪ | [`superpowers/specs/2026-08-22-phase-h-battery-display-design.md`](superpowers/specs/2026-08-22-phase-h-battery-display-design.md) | Phase H 設計 spec；§10 已知限制仍被程式註解引用 | 2026-08-30 |
| ⚪ | [`superpowers/plans/2026-08-22-phase-h-battery-display.md`](superpowers/plans/2026-08-22-phase-h-battery-display.md) | Phase H 實作計畫（SDD 產物） | 2026-08-30 |
| ⚪ | [`superpowers/specs/2026-09-01-phase-g-device-info-design.md`](superpowers/specs/2026-09-01-phase-g-device-info-design.md) | Phase G 裝置資訊畫面設計 spec | 2026-09-01 |
| ⚪ | [`superpowers/plans/2026-09-01-phase-g-device-info.md`](superpowers/plans/2026-09-01-phase-g-device-info.md) | Phase G 裝置資訊畫面實作計畫（SDD 產物） | 2026-09-02 |

## 4. 給 PM 的報告

| 狀態 | 文件 | 用途 | 最後動 |
|:---:|---|---|---|
| 🟢 | [`progress.md`](progress.md) / [`.html`](progress.html) | 累加式進度報告（最新在上，目前 10 條到 2026-08-24）。新增用 `/pm-html-report` | 2026-08-24 |

## 5. 工具與 Demo

| 狀態 | 路徑 | 用途 |
|:---:|---|---|
| 🟢 | [`demo/index.html`](demo/index.html) | 手機互動 Demo 原始碼（single file），部署於 Cloudflare Pages `ems-dosesync-demo.pages.dev`；TFT UI 的美學藍本 |
| 🟢 | [`ble-tester/index.html`](ble-tester/index.html) | Phase F web BLE 測試工具：連線、time_sync、案件 dump |
| 🟢 | [`../firmware/release-template/`](../firmware/release-template/) | 韌體交付包模板：`README.txt`（對方燒錄說明）、`flash.sh`／`flash.bat`（一鍵燒錄）、`HOW_TO_BUILD_RELEASE.md`（**僅開發者**，打包 SOP） |
| 🟢 | [`../tasks/todo.md`](../tasks/todo.md) | 跨 session 的待辦與上機驗收紀錄（目前 T6～T10） |

## 6. 歷史紀錄（不再更新，保留追溯）

| 狀態 | 文件 | 內容 | 最後動 |
|:---:|---|---|---|
| 📜 | [`gap-analysis.md`](gap-analysis.md) | 2026-04 PM 規格 vs 舊韌體落差分析，基準是已廢止的 `pm-flow-spec.md` | 2026-05-04 |
| 📜 | [`incremental-impl-plan.md`](incremental-impl-plan.md) | 2026-04 增量實作計畫，用的是舊 Phase 2／3 編號（對照表見 `CLAUDE.md`「Phase 編號對照表」） | 2026-05-15 |
| 📜 | [`font-vlw-evaluation.md`](font-vlw-evaluation.md) | efontTW_24 → vlw 自訂字型評估。方案已採用，字集重生流程見 `scripts/regen_vlw.sh` 與 `CLAUDE.md` | 2026-05-09 |
| 📜 | [`wave2-g21-briefing.md`](wave2-g21-briefing.md) | 2026-07-15 G2.1／G2.2 BLE 裝置名稱寫入的一次性技術簡報，功能已完成 | 2026-07-15 |

## 7. 已廢止

| 文件 | 說明 |
|---|---|
| `pm-flow-spec.md` | 2026-04-27 被 SoT V1 取代並刪除；`gap-analysis.md` 仍引用它，僅供追溯 |
