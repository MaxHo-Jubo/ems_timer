# Impl-Phase G 裝置資訊畫面 — 交接文件

- **最後更新**：2026-09-01（計畫已產出，尚未開工）
- **狀態**：Spec 與 implementation plan 皆已完成並 commit，**尚未派工/開工**。
- **branch**：`main`（前一階段 Phase H 電量顯示已 merge 進 `main`，本次工作預期直接在
  `main` 上開新 commit，或另開 feature branch——依 `superpowers:using-git-worktrees`
  慣例，開工時再決定，見 §2）

> 本文件是單一時間線，比照 `docs/superpowers/phase-h-handover.md` 的格式（取代分散在
> commit message 裡的追蹤方式）。

---

## 1. 三十秒看懂現況

**背景**：`docs/pm-dev-spec.md §四 Phase G` 原規劃「韌體版本 read-only」一項從未落地成 UI；
SoT V1 §19.7「裝置資訊」畫面（名稱／型號／序號／韌體／電池／充電狀態六欄）也從未實作。
2026-08-30 使用者裁決把兩者合併，一次併入 Impl-Phase G（詳見 §3-A0）。

**這次要做的事**：
1. 系統設定選單從既有 5 項（裝置名稱／亮度／音量／通氣音量／電池資訊）擴充至 SoT §19.1
   完整 8 項，新增 App連線設定／Type-C連線（placeholder）／裝置資訊
2. 8 項裝不下 240px 螢幕，加捲動機制（比照既有歷史紀錄清單模式）
3. 實作裝置資訊畫面本體（`drawDeviceInfo()`）

**已完成**：
- ✅ Brainstorming（architectural path）：序號來源／選單合併／捲動排版三個關鍵決策已與
  使用者逐項確認，見 `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md` §2
- ✅ Spec 寫入並 commit（`a1c8b1c` 初版 + `30dd2eb` 序號方案修正）
- ✅ Implementation plan 寫入並 commit：`docs/superpowers/plans/2026-09-01-phase-g-device-info.md`
  （6 個 task：捲動 clamp 共用函式 → 選單 8 項化 → 按鍵接線 → DisplaySnapshot 接線 →
  裝置資訊畫面本體 → 文件收尾）

**尚未開工**：全部 6 個 task 都還沒動手實作。

---

## 2. 如何接手

```bash
cat docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md   # 先讀設計決策
cat docs/superpowers/plans/2026-09-01-phase-g-device-info.md          # 再讀實作計畫

# 開工前先確認測試基準線
cd firmware && pio test -e native                            # 應為 623/624，唯一 ERRORED = test_storage_hw
cd firmware && pio run -e esp32-s3-devkitc-1                  # 應為 SUCCESS
git log --oneline -5                                          # 確認在 main 上，Phase H 已 merge
```

**續跑方式**：用 `superpowers:subagent-driven-development` 或 `superpowers:executing-plans`
逐 task 執行（計畫檔開頭已註明）。6 個 task 有嚴格順序依賴——Task 3 依賴 Task 1（`clampScrollOffset()`）
與 Task 2（游標常數）；Task 4 依賴 Task 3 定義的全域變數；Task 5 的 `drawDeviceInfo()` 是
Task 4 Step 8 呼叫點的前向引用（計畫內已註明，序列執行時 Task 4 完成當下編譯會失敗，
Task 5 完成後才會通過）——**不建議打散成平行 task**，依 1→2→3→4→5→6 序列執行。

> ⚠️ `firmware/src/` 整個被 `[env:native]` 的 `build_src_filter = -<*>` 排除，`input_handler.cpp`／
> `main.cpp`／`ui_screens.cpp` 的邏輯無法被 native test 直接呼叫。這也是為什麼 Task 5
> （`drawDeviceInfo()`）沒有 native test——跟既有 `drawBatteryInfo()`（Task 13）同一個
> 既有限制，不是本次新增的缺口。這些檔案的正確性驗證管道是**韌體編譯 + 上機驗收**，
> 見計畫末尾的「上機驗收清單」。

---

## 3. 接手待辦

### 3-A0. 背景裁決（2026-08-30，Phase H handover 移出）

「裝置資訊」畫面併入 Impl-Phase G 的完整裁決過程記錄在
`docs/superpowers/phase-h-handover.md §3-A7`，本文件不重複，只連結參照。

### 3-A1. Brainstorming 逐項決策記錄（2026-09-01）

完整記錄在 spec §2，這裡摘要三個最關鍵、且中途發現需要修正的決策：

1. **序號來源**：原訂 ESP32 efuse MAC 衍生，**寫計畫前查現有程式碼發現此方案有問題**——
   `app_globals.h` 已有 `SYNC_DEVICE_ID = "DSP-0001"` 常數且已用於案件同步 metadata
   （`sync_send.cpp` 的 `device_id` 欄位）。efuse 衍生會產生第二套不同來源的序號，
   跟 App 端看到的案件 metadata 對不起來。**已修正為直接讀取 `SYNC_DEVICE_ID`**，
   spec 對應修正見 `30dd2eb` commit。這是 brainstorming 完成、寫計畫前的例行程式碼
   核對抓到的問題，不是事後 review 才發現——供未來類似情境參考：**brainstorming
   階段拍板的資料來源決策，動工前務必再核對一次現有 codebase 有沒有同名/同義的
   既有機制，不能只憑 spec 文字描述就開工**。
2. **選單排序**：8 項維持 SoT §19.1 原始順序（不是圖方便直接接在既有 5 項後面），
   代價是要為未實作的 App連線設定／Type-C連線 補 placeholder 項目。
3. **8 項裝不下 240px 螢幕**：加捲動機制，不壓縮既有版面、不砍項目。這個發現是
   brainstorming 過程中才算出來的（既有選單 40px 等間距 × 8 項 = 超出螢幕），
   不是預先規劃好的——過程記錄見 spec §1.2「規格缺口」。

### 3-B. 上機驗收清單（尚未執行，等 Task 5 完成後）

完整清單在計畫檔末尾，此處僅列摘要：選單捲動流暢度、placeholder 顯示與返回、裝置資訊
六欄正確性（含序號與 App 端案件同步紀錄的 device_id 交叉核對）、恢復預設對話框捲動後
觸發位置正確性、裝置名稱捲出視窗行為。

---

## 4. 已完成的 task

（尚無——全部 6 個 task 都在「計畫已寫、尚未開工」狀態）

---

## 5. 殘餘風險（延續自 Phase H whole-branch review，本次擴大範圍時需留意）

Phase H whole-branch review（`docs/superpowers/phase-h-handover.md §3-A10`）留下的
架構債 ⑨⑩⑪ 都跟本次要動的 `kSettingsAdjustableItems`／`settings_menu_item_t` 直接相關：

- **⑨ 選單反白框覆蓋不足**（17% 面積問題）：8 項擴充後這個既有視覺缺陷會出現在
  全部 8 列，本次計畫不修（spec §7 已明列不在範圍），維持既有 park
- **⑩⑪ 型別設計債**（`settings_menu_item_t` 缺 action-kind 欄位、子畫面狀態仍是
  平行 bool 不是 discriminated enum）：本次選擇繼續維持既有查表模式擴充，不藉此機會
  重構（brainstorming 時明確問過使用者，選了「不重構」，見 spec §2 決策 #7 替代方案 B
  未採用的理由）。**這次擴充後平行 bool 又多了一個（`settingsDeviceInfoMode`），
  下次若還要在這裡加第三個真正的 modal（不是 placeholder），這個判斷可能要重新評估**。

---

## 6. 對應文件

| 文件 | 用途 |
|---|---|
| `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md` | 設計 spec（決策記錄 + 架構） |
| `docs/superpowers/plans/2026-09-01-phase-g-device-info.md` | 實作計畫（6 task，TDD 步驟） |
| `docs/pm-dev-spec.md §四 Phase G` | 高層 Phase 描述（完成後待更新，見計畫 Task 6） |
| `docs/superpowers/phase-h-handover.md §3-A7` | 本次工作範圍的裁決背景（上一階段文件） |
