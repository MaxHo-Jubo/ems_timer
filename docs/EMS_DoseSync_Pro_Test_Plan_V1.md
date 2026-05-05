# EMS DoseSync Pro Prototype V1 — 測試計劃

> 對齊 SoT：`EMS_DoseSync_Pro_Prototype_V1.md`
> 對齊工程規格：`pm-dev-spec.md`（v2.0，Phase A~H）
> 版本：v1.0（初版）
> 日期：2026-05-03
> 範圍：Impl-Phase A~H 全部範圍（單元測試 + 整合測試 + 實機驗收）

---

## 0. 文件定位與閱讀指引

本測試計劃為 V1 韌體**從零開工**的測試藍本（既有韌體於 Phase A 開工時 throwaway 重寫，舊測試 `tasks/unit-test-plan.md` 已不適用）。

| 對象 | 閱讀重點 |
|------|---------|
| **PM** | §1 測試金字塔 / §2 Phase 測試矩陣 / §11 涵蓋率目標 / §12 風險與排程 |
| **韌體工程師** | §3~§10 各 Phase 詳細測試案例 / 邊界情況 / §13 測試框架 |
| **驗收方** | §14 與 V1 SoT §22 對應表 |

---

## 1. 測試金字塔（四層架構）

```
┌──────────────────────────────────────┐
│  L4  PM 驗收測試（V1 §22 對齊）        │ ← 最終確認
├──────────────────────────────────────┤
│  L3  實機煙霧測試（手動 / 半自動）       │ ← 體感、計時精度、I/O
├──────────────────────────────────────┤
│  L2  整合 / 狀態機測試（native 模擬）    │ ← 跨模組行為
├──────────────────────────────────────┤
│  L1  單元測試（PlatformIO native）      │ ← 純函式、決策邏輯
└──────────────────────────────────────┘
```

**比例目標**：L1 70% / L2 20% / L3+L4 10%

**原則**：
1. **純邏輯一律抽純函式**（注入 `now_ms`、不 include `Arduino.h`），native 環境直接覆蓋
2. **狀態機用 transition table 驗**，不依賴實際 millis()
3. **時序敏感（debounce、長按、倒數精度）必上實機驗**，native 模擬只能保證邏輯正確
4. **每個 Phase 進場前先寫 characterization test 釘住「現有行為」**，重構後才能保證無 regression

---

## 2. Phase 測試矩陣（總覽）

| Phase | 範圍 | L1 單元 | L2 整合 | L3 實機 | L4 PM |
|-------|------|:------:|:------:|:------:|:----:|
| **A** OHCA 核心 + EPI 倒數 + 兩段確認 | §3 | ≥ 50 | ≥ 15 | ≥ 8 | §22.1 |
| **B** 補登 + Amio + 案件總覽 + Timeline | §4 | ≥ 35 | ≥ 12 | ≥ 6 | §22.2/22.3/22.4 |
| **C** 6 秒通氣節奏（獨立 + OHCA 切入） | §5 | ≥ 25 | ≥ 10 | ≥ 6 | §22.5/22.6 |
| **D** Training 模式 | §6 | ≥ 20 | ≥ 8 | ≥ 5 | §22.7 |
| **E** 持久化 + 歷史紀錄 | §7 | ≥ 18 | ≥ 6 | ≥ 4 | — |
| **F** App 配對碼同步 | §8 | ≥ 15 | ≥ 8 | ≥ 4 | §22.8 |
| **G** 系統設定 + Type-C 工具 | §9 | ≥ 15 | ≥ 5 | ≥ 4 | §22.x |
| **H** 電源管理 | §10 | ≥ 8 | ≥ 4 | ≥ 6 | §22.9 |
| **合計** | | **≥ 186** | **≥ 68** | **≥ 43** | 全 §22 |

---

## 3. Phase A — OHCA 核心 + EPI 倒數 + 兩段確認

對應 V1 §5–11、§23.1–23.2、pm-dev-spec §3–§5。

### 3.1 EPI 倒數引擎 `decideOhcaOutput()`

**輸入**：`(phase, since_last_epi_ms, now_ms)` → **輸出**：`ohca_output_t`

#### 3.1.1 Phase = WAIT_FIRST_EPI
- ✅ since=任意 → 全部輸出 false / 0 / NULL
- ✅ display_label = NULL（不顯示「請給藥」）
- ✅ display_remaining_ms = 0

#### 3.1.2 Phase = COUNTDOWN（0 ≤ since ≤ 180000）
- ✅ since=0 → display_remaining_ms = 240000，無蜂鳴
- ✅ since=120000 → display_remaining_ms = 120000
- ✅ since=180000 → display_remaining_ms = 60000，**仍未轉 WARNING**（≤ 而非 <）
- ⚠️ **邊界 since=179999** → 仍 COUNTDOWN
- ⚠️ **邊界 since=180001** → 應已轉 WARNING（外層狀態機決定，純函式回傳依 phase 而定）

#### 3.1.3 Phase = WARNING（180000 < since ≤ 240000）
- ✅ since=180001 → buzz_short=true（首次 15s 嗶）, led_yellow_slow=true
- ✅ since=195001 → buzz_short=true（第 2 次 15s 嗶）
- ✅ since=185000（兩次嗶之間）→ buzz_short=false, led_yellow_slow=true
- ✅ display_label = "請準備給藥"
- ✅ display_remaining_ms = 240000 - since
- ⚠️ **邊界 since=239999** → 還在 WARNING
- ⚠️ **邊界 since=240000** → 進入 ALARMING

#### 3.1.4 Phase = ALARMING（240000 < since ≤ 245000）
- ✅ buzz_alarm_continuous=true, led_red_fast=true, vibrate=true, screen_flash=true
- ✅ display_label = "請給藥"
- ✅ display_remaining_ms = 0
- ⚠️ **邊界 since=240001** → 剛進入 ALARMING
- ⚠️ **邊界 since=245000** → 還在 ALARMING（≤）
- ⚠️ **邊界 since=245001** → 轉 OVERTIME

#### 3.1.5 Phase = OVERTIME（since > 245000）
- ✅ display_remaining_ms = 自上次 EPI 起算累計（since 直接傳）
- ✅ display_label = NULL（畫面顯示累計時間）
- ✅ since=245001 → buzz_short=true（首次超時嗶）, led_red_slow=true
- ✅ since=260001 → buzz_short=true（每 15s 嗶）
- ✅ since=275001 → buzz_short=true
- ✅ since=250000（兩嗶之間）→ buzz_short=false
- ⚠️ **邊界 since=259999 vs 260001** → 後者觸發嗶
- ⚠️ **邊界 long-term**：since=600000（OVERTIME 6 分鐘）→ 仍每 15s 嗶不停

#### 3.1.6 millis() rollover（uint32 ~49.7 天）
- ✅ task_start_ms=4294967295（uint32 max）, now_ms=4 → since=5（rollover 算法正確）
- ✅ wrap-around 不導致 phase 誤判

### 3.2 兩段確認 `twoStepConfirm_press()`

#### 3.2.1 Happy path
- ✅ 第一次按（armed=false）→ armed=true, first_press_ms=now, 回 false
- ✅ 5s 內第二次按 → armed=false, 回 true
- ✅ 重複「按 → 5s 內按」可成立多次（每次都是新 cycle）

#### 3.2.2 Timeout
- ✅ 第一次按後 5001ms 第二次按 → 視為新 first press（armed 重置）, 回 false
- ⚠️ **邊界 5000ms 整**：定義為「≤ 5000ms 成立」→ 5000ms 仍成立, 5001ms 不成立
- ✅ 5s 內無第二次按 → armed 自動清除（外層 update tick 偵測）

#### 3.2.3 跨型別獨立
- ✅ EPI 確認中 armed → 按 Amio 鍵 → 進入 Amio armed（兩個 instance 各自獨立）
- ✅ 兩種同時 armed 不互相影響

### 3.3 OHCA 子狀態機 transition

#### 3.3.1 正常流程
- ✅ MAIN_MENU → 短按主鍵 → OHCA_START_FLASH
- ✅ OHCA_START_FLASH（1s）→ OHCA_WAIT_FIRST_EPI（自動）
- ✅ OHCA_WAIT_FIRST_EPI → EPI 二段確認成立 → OHCA_COUNTDOWN
- ✅ OHCA_COUNTDOWN → since ≥ 180000 → OHCA_WARNING
- ✅ OHCA_WARNING → since ≥ 240000 → OHCA_ALARMING
- ✅ OHCA_ALARMING → since ≥ 245000 → OHCA_OVERTIME
- ✅ OHCA_ALARMING / OVERTIME → EPI 二段確認成立 → OHCA_COUNTDOWN（重啟）
- ✅ OHCA 進行中任一 phase（WAIT_FIRST_EPI / COUNTDOWN / WARNING / ALARMING / OVERTIME）→ 主鍵長按 ≥ 3s → OHCA_END_CHECK（SoT V1 §10.1：正式 OHCA 中皆可）
- ✅ OHCA_END_CHECK → 取消 → 還原至進入 END_CHECK 前的 source phase
- ✅ OHCA_END_CHECK → 確認結束 → OHCA_LOCKED → OHCA_SUMMARY

#### 3.3.2 EPI 到期警報期主鍵行為（V1 §6.7）
- ✅ ALARMING / OVERTIME 中**主鍵短按**：只消音不建立紀錄、不重啟倒數
- ✅ ALARMING / OVERTIME 中**EPI 鍵**：進入 EPI 二段確認流程
- ⚠️ **邊界**：ALARMING 中先按主鍵消音、再按 EPI 鍵 → EPI 確認流程仍可正常進入

#### 3.3.3 EPI 確認取消（V1 §6.8）
- ✅ EPI 確認 armed → 5s 內未第二次按 → 回到原 phase（ALARMING / OVERTIME / COUNTDOWN）
- ✅ 取消後不影響原倒數時序

#### 3.3.4 LOCKED 拒絕
- ✅ OHCA_LOCKED 後：EPI / 電擊 / Amio / 補登 / 結束鍵 → 全部 noop
- ⚠️ **邊界**：LOCKED 期間長按 Power → 仍允許關機

#### 3.3.5 啟動倒數白名單（V1 §23.2）
- ❌ 電擊不重啟倒數
- ❌ Amiodarone 不重啟倒數
- ❌ 接手前 EPI 不重啟倒數
- ❌ 純補登 EPI 不重啟倒數
- ✅ 僅本機 EPI 二段確認成立**唯一**啟動 / 重啟倒數路徑

### 3.4 Phase A L3 實機煙霧測試（操作流程）

#### A.S1 — 開機到倒數啟動
- **前置**：電量 ≥ 50%，未連線 App
- **操作**：(1) 長按 Power ≥ 2s 開機 → (2) 主功能表短按主鍵進 OHCA → (3) 觀察「案件開始 OHCA」1s 提示 → (4) 短按 EPI 鍵（首次）→ (5) 5s 內再短按 EPI 鍵（第二次）
- **預期**：步驟 4 顯示 EPI 確認對話框；步驟 5 後 OLED 切倒數畫面 `04:00`
- **通過**：倒數從 04:00 開始，1s 內進入 03:59

#### A.S2 — 倒數精度 ±50ms
- **前置**：A.S1 已啟動倒數
- **操作**：(1) 啟動碼錶（按下瞬間記錄當下 OLED 倒數值，例如 03:50）→ (2) 等待 OLED 顯示 `01:00` → (3) 立即停碼錶
- **預期**：碼錶讀數約 02:50（從 03:50 遞減到 01:00 經過 170s）
- **通過**：誤差 ≤ ±50ms（即 169.95~170.05s）

#### A.S3 — 1 分鐘預警
- **前置**：A.S1 倒數中
- **操作**：(1) 等待 OLED 顯示 `01:00` → (2) 連續觀察 60s
- **預期**：每 15s 短嗶 1 聲（共 4 次）；LED 黃慢閃；OLED 顯示「請準備給藥」
- **通過**：60s 內聽到 4 次短嗶，LED 持續黃慢閃

#### A.S4 — EPI 警報
- **前置**：A.S3 預警中
- **操作**：等待倒數歸零（不操作任何按鍵）
- **預期**：OLED 顯示「請給藥」、蜂鳴連續發報、LED 紅快閃、震動馬達持續、畫面閃紅
- **通過**：連續 5s 高優先警報行為齊全（蜂鳴 + 紅閃 + 震動 + 畫面閃）

#### A.S5 — 進入 OVERTIME
- **前置**：A.S4 警報已 5s
- **操作**：警報滿 5s 後不操作
- **預期**：OLED 切換為累計時間顯示，從 `+0:05` 開始遞增；蜂鳴停止連發
- **通過**：OLED 顯示 `+0:05` 並 1s 內遞增至 `+0:06`

#### A.S6 — OVERTIME 每 15s 嗶
- **前置**：A.S5 OVERTIME 中
- **操作**：連續觀察 60s
- **預期**：每 15s 1 聲短嗶（共 4 次）；LED 紅慢閃
- **通過**：60s 內聽到 4 次短嗶

#### A.S7 — 第二次 EPI 重啟倒數
- **前置**：A.S6 OVERTIME 中
- **操作**：(1) 短按 EPI 鍵（首次）→ (2) 5s 內再短按 EPI 鍵（第二次）
- **預期**：步驟 1 顯示 EPI 確認對話框，警報維持；步驟 2 後 OLED 重置 `04:00` 倒數
- **通過**：OLED 重置 04:00 並開始遞減；累計時間清零

#### A.S8 — 結束鎖定
- **前置**：任意 phase
- **操作**：(1) 主鍵**長按 3s** → (2) 結束選單選「完成並結束」→ (3) 短按主鍵確認 → (4) 鎖定後嘗試短按 EPI / 電擊 / Amio 鍵
- **預期**：步驟 3 後進入 OHCA_SUMMARY；步驟 4 任何按鍵 noop
- **通過**：總覽畫面顯示完整資料；按鍵無響應、無新事件寫入

---

## 4. Phase B — 補登 + Amiodarone + 案件總覽 + Timeline

對應 V1 §8、§9、§10、§11、§23.3、pm-dev-spec §6、§9。

### 4.1 補登次數選擇

| 補登類型 | 範圍 | 邊界測試 |
|---------|------|---------|
| 接手前 EPI | 1~5 次 | 嘗試 0 / 6 → 拒絕 |
| 接手前電擊 | 1~5 次 | 嘗試 0 / 6 → 拒絕 |
| 純補登 EPI | 1~3 次 | 嘗試 0 / 4 → 拒絕 |
| 純補登電擊 | 1~3 次 | 嘗試 0 / 4 → 拒絕 |
| Amiodarone | 1 次（每次成立寫一筆） | 連續確認多次 → 多筆紀錄 |

### 4.2 補登事件規則

#### 4.2.1 actual_time_null=true
- ✅ 接手前 EPI：actual_time_null=true, count=N（1~5）, timestamp_ms=recorded_at
- ✅ 純補登：同上
- ✅ Timeline 顯示「-」（不顯示時間）

#### 4.2.2 補登不啟動 EPI 倒數（V1 §23.2）
- ✅ 待本機 EPI 階段補登接手前 EPI ×5 → 倒數**不啟動**
- ✅ COUNTDOWN 中補登接手前 EPI → 倒數不重啟
- ⚠️ **邊界**：補登後再做本機 EPI 二段確認 → 此時才啟動倒數

#### 4.2.3 補登不可變（V1 §9.7）
- ✅ 補登成立後嘗試刪除 / 修改 / 撤銷 → 全部拒絕
- ✅ 即使案件未鎖定也拒絕

#### 4.2.4 OHCA_LOCKED 後拒絕新增補登
- ✅ 鎖定後長按 EPI 鍵 / 電擊鍵 → 不進入補登選單

### 4.3 Amiodarone（V1 §8）

#### 4.3.1 兩段確認獨立 instance
- ✅ Amio 與 EPI 共用 `two_step_confirm_t` 模組，但獨立 state
- ✅ EPI armed 時按 Amio → 兩者各自 armed
- ✅ Amio 確認成立 → 不重啟 EPI 倒數
- ✅ Amio 在 ALARMING 中確認 → Amio 紀錄成立、ALARMING 持續（EPI 警報未消）

#### 4.3.2 Amio 入口
- ✅ EPI 鍵長按 ≥ 1000ms → 藥物選單 → Amio
- ✅ Amio 不出現於補登入口

### 4.4 案件總覽 `ohca_case_summary_t`（V1 §11）

#### 4.4.1 計數欄位
- ✅ epi_total = epi_local + epi_pre_handover + epi_pure_supp
- ✅ shock_total = shock_local + shock_pre_handover + shock_pure_supp
- ✅ amio_total = Σ Amio 紀錄筆數

#### 4.4.2 時間欄位
- ✅ first_epi_local_ms = 第一次本機 EPI timestamp
- ✅ last_epi_local_ms = 最後一次本機 EPI timestamp
- ✅ last_shock_local_ms = 最後一次本機電擊（**不顯示**「第一次本機電擊」）
- ✅ last_amio_ms = 最後一次 Amio
- ⚠️ **邊界**：本機 EPI = 0 → first_epi_local_ms = 0, last_epi_local_ms = 0
- ⚠️ **邊界**：僅補登 EPI、無本機 EPI → first/last = 0，total > 0

#### 4.4.3 Timeline 排序與顯示
- ✅ 本機事件依 timestamp 排序
- ✅ 補登事件依 recorded_at 排序，時間欄顯示「-」
- ✅ 同 timestamp 多事件 → 依 event_id 流水號排序
- ✅ 補登 count > 1 顯示為「補登 EPI ×3」

### 4.5 結束前檢查（V1 §10）

#### 4.5.1 三選項
- ✅ OHCA 進行中任一 phase（WAIT_FIRST_EPI / COUNTDOWN / WARNING / ALARMING / OVERTIME）→ 主鍵長按 3s → OHCA_END_CHECK 顯示三選項（SoT V1 §10.1）
- ✅ 「完成並結束」→ OHCA_LOCKED
- ✅ 「前往補登」→ 進入補登入口（回原 phase 後可長按 EPI/電擊鍵）
- ✅ 「返回案件」→ 回原 phase（依進入 END_CHECK 前的 source state 還原）

#### 4.5.2 時序保留
- ✅ 結束前檢查期間 EPI 倒數**繼續累計**（不暫停）
- ⚠️ **邊界**：在 END_CHECK 顯示期間 EPI 到期 → 是否切回 ALARMING？（建議：保留 END_CHECK，但 ALARMING 警報音/燈仍觸發）

### 4.6 Phase B L3 實機煙霧測試（操作流程）

#### B.S1 — 補登不啟動倒數
- **前置**：OHCA 進入 WAIT_FIRST_EPI（尚未做本機 EPI）
- **操作**：(1) **長按 EPI 鍵 ≥ 1s** 進入藥物選單 → (2) 選「接手前 EPI」→ (3) 用上下鍵選次數 `3` → (4) 短按主鍵確認 → (5) 5s 內再短按主鍵第二次確認
- **預期**：步驟 5 後寫入 3 筆接手前 EPI 補登；OLED **仍顯示「待本機 EPI」**
- **通過**：倒數**未啟動**；總覽 epi_pre_handover=3、epi_local=0

#### B.S2 — 補登不可變
- **前置**：B.S1 已寫入 3 筆補登
- **操作**：(1) 進入案件總覽 / Timeline → (2) 嘗試對該補登事件按刪除 / 修改任何鍵
- **預期**：UI 不提供刪除選項；若工程後門觸發 → 韌體層拒絕並提示
- **通過**：補登筆數仍為 3，無法被修改 / 撤銷 / 刪除

#### B.S3 — Amio 不重啟倒數
- **前置**：A.S1 已啟動 OHCA_COUNTDOWN（剩約 02:30）
- **操作**：(1) **長按 EPI 鍵 ≥ 1s** 進入藥物選單 → (2) 選「Amiodarone」→ (3) 短按主鍵首次確認 → (4) 5s 內再短按主鍵第二次確認 → (5) 觀察倒數秒數
- **預期**：步驟 4 寫入 Amio 紀錄，蜂鳴成功音 1 聲；步驟 5 倒數秒數**連續遞減**未跳動
- **通過**：倒數時間軸無重啟（不變回 04:00）；amio_total +1

#### B.S4 — 結束前檢查 → 補登 → 再結束
- **前置**：OHCA_OVERTIME 中（任意進行中 phase 皆可，本案以 OVERTIME 為示範）
- **操作**：(1) 主鍵**長按 3s** → 結束選單 → (2) 選「前往補登」→ 回到原 phase（OVERTIME） → (3) 長按電擊鍵 → 補登「純補登電擊 ×1」→ 二次確認 → (4) 主鍵長按 3s → (5) 選「完成並結束」→ 短按主鍵確認
- **預期**：步驟 5 後 LOCKED；步驟 2「前往補登」/取消會還原至步驟 1 進入 END_CHECK 前的 source phase
- **通過**：總覽 shock_pure_supp=1，案件鎖定後不可新增

#### B.S5 — 總覽數值對應
- **前置**：B.S4 完成案件鎖定（含 1 次本機 EPI、3 次接手前 EPI、1 次純補登電擊、1 次 Amio）
- **操作**：(1) 進入 OHCA_SUMMARY → (2) 比對總覽欄位 → (3) 切到 Timeline → (4) 計數每類事件
- **預期**：epi_total=4 (local 1 + pre_handover 3)、shock_total=1、amio_total=1、first_epi_local_ms 為步驟 1 EPI 時間
- **通過**：總覽欄位與 Timeline 計數完全一致

#### B.S6 — Timeline 補登時間「-」
- **前置**：B.S5 同案件
- **操作**：進入 Timeline 視覺檢查
- **預期**：本機 EPI / Amio 顯示精確時間；接手前 EPI ×3 與純補登電擊 ×1 時間欄顯示「-」
- **通過**：所有補登事件時間欄為「-」，actual_time_null=true 標記正確

---

## 5. Phase C — 6 秒通氣節奏（獨立 + OHCA 切入）

對應 V1 §13、§14、pm-dev-spec §7。

### 5.1 獨立模式（V1 §13）

#### 5.1.1 1~6 循環邏輯
- ✅ 進入後立即播放秒 1 → 1s 後秒 2 → ... → 秒 6 → 1s 後秒 1（循環）
- ✅ 秒 1：加強提示音 + LED 紅閃 + 畫面反紅
- ✅ 秒 2~6：短提示音、LED 不閃、畫面數字顯示
- ⚠️ **邊界**：循環不停止直到使用者結束（無自動停止條件）

#### 5.1.2 通氣音量獨立 NVS（V1 §13.9 通氣音量 / §13.10 靜音規則）
- ✅ 通氣音量 NVS key 與系統音量分開
- ✅ 通氣音量 = 0：蜂鳴關閉，**畫面反紅 + LED 紅閃保留**
- ✅ 通氣音量 = 5：最大音量
- ✅ 系統音量 = 3、通氣音量 = 0 → 確認音 / 警報音照常

#### 5.1.3 結束（V1 §13.14 結束獨立 6 秒通氣節奏）
- ✅ 主鍵 / 返回鍵長按 → 結束確認 → 回主功能表

### 5.2 OHCA 中切入（V1 §14）

#### 5.2.1 進入路徑
- ✅ OHCA_COUNTDOWN / WARNING / OVERTIME → 返回鍵 → 快速功能選單 → 6 秒通氣
- ✅ 進入後 EPI 倒數**背景持續**
- ✅ 畫面顯示 `EPI MM:SS` 小提示

#### 5.2.2 EPI 高優先打斷（V1 §14.8 EPI 到期優先權 / pm-dev-spec §7.2）
- ✅ vent overlay 中 OHCA 進入 ALARMING → vent metronome **立即靜音**（≤ 50ms）
- ✅ UI 強制切到 OHCA_ALARM_SCREEN
- ✅ 必須完成 EPI 二段確認才能離開警報畫面
- ✅ EPI 確認成立 → 彈出「返回通氣節奏？」
  - ✅ 是 → 重新進入 vent overlay
  - ✅ 否 → 回 OHCA 主畫面（ COUNTDOWN ）
- ⚠️ **邊界**：ALARMING 中按返回鍵 → **不允許**離開警報畫面

#### 5.2.3 OHCA 內 6 秒通氣恆常顯示（V1 §14.3 OHCA 內 6 秒通氣顯示方式）
- ✅ 開啟後常駐 OHCA 主畫面下方
- ✅ EPI / 電擊 / Amio / 補登確認後，6 秒區塊**仍維持顯示**
- ✅ EPI ALARMING 時 6 秒區塊**不蓋過**「請給藥」警報
- ✅ 案件結束（LOCKED）→ 6 秒自動停止

### 5.3 Phase C L1 純函式測試

```c
typedef enum { VENT_BEAT_1, VENT_BEAT_2, ..., VENT_BEAT_6 } vent_beat_t;
typedef struct {
  bool         buzz_short;
  bool         buzz_emphasis;
  bool         led_red_flash;
  bool         screen_invert_red;
  uint8_t      display_number;  // 1~6
} vent_output_t;

vent_output_t decideVentOutput(uint32_t since_start_ms, uint8_t volume);
```

- ✅ since=0 → BEAT_1 (emphasis + flash + invert)
- ✅ since=1000 → BEAT_2 (short)
- ✅ since=5000 → BEAT_6
- ✅ since=6000 → BEAT_1 again（循環）
- ✅ since=12000 → BEAT_1（第 3 個循環）
- ✅ volume=0 + BEAT_1 → emphasis=false, flash=true, invert=true
- ✅ volume=0 + BEAT_2 → short=false, flash=false
- ⚠️ **邊界 since=999** → 仍 BEAT_1
- ⚠️ **邊界 since=1000** → 切 BEAT_2

### 5.4 Phase C L3 實機煙霧測試（操作流程）

#### C.S1 — 獨立模式 60s 觀察
- **前置**：主功能表
- **操作**：(1) 上下鍵選「6 秒通氣節奏」→ (2) 短按主鍵進入 → (3) 連續觀察 60s
- **預期**：每秒切換顯示數字 1→2→...→6→1；秒 1 加強提示音 + LED 紅閃 + 畫面反紅；秒 2~6 短提示音
- **通過**：60s 內看到 10 個完整循環，秒 1 行為與秒 2~6 明顯區分

#### C.S2 — 通氣音量 = 0 靜音
- **前置**：在 6 秒通氣畫面或先於設定中將通氣音量設為 0
- **操作**：(1) 進入系統設定 → 通氣音量調至 `0` → (2) 返回主功能表 → (3) 進入 6 秒通氣節奏 → (4) 觀察 12s（2 個循環）
- **預期**：蜂鳴**完全靜音**；秒 1 畫面反紅 + LED 紅閃**仍保留**；秒 2~6 數字顯示但無聲
- **通過**：聽不到任何蜂鳴；視覺提示（反紅 / 紅閃）正常

#### C.S3 — OHCA 切入 vent，倒數背景持續
- **前置**：A.S1 已啟動倒數，剩約 03:00
- **操作**：(1) 在 OHCA 主畫面短按返回鍵 → (2) 快速功能選「6 秒通氣」→ (3) 觀察 vent overlay 中右上角 `EPI MM:SS` 小提示 → (4) 60s 後按返回離開 vent
- **預期**：步驟 3 小提示秒數連續遞減；步驟 4 回 OHCA 主畫面，倒數秒數約 02:00（少 60s）
- **通過**：倒數時間軸不中斷、不暫停

#### C.S4 — EPI 高優先打斷 ≤ 50ms
- **前置**：OHCA_COUNTDOWN 剩 00:01；已切入 vent overlay
- **操作**：(1) 啟動高速攝影（120fps+）對準 OLED 與蜂鳴器 → (2) 等待倒數歸零觸發 ALARMING → (3) 攝影分析從 ALARMING 觸發到 vent 蜂鳴停止的 frame 數
- **預期**：vent 蜂鳴在 ALARMING 觸發後 ≤ 6 frames（120fps × 50ms）內停止；UI 強制切到 OHCA_ALARM_SCREEN
- **通過**：靜音延遲 ≤ 50ms

#### C.S5 — 完成 EPI 後詢問返回 vent
- **前置**：C.S4 在 ALARM_SCREEN
- **操作**：(1) 短按 EPI 鍵首次 → (2) 5s 內再短按 EPI 鍵第二次 → (3) 觀察詢問對話框
- **預期**：步驟 2 後 OLED 跳出「返回通氣節奏？」對話框；選「是」→ 回到 vent overlay；選「否」→ 回 OHCA 主畫面
- **通過**：兩條路徑都能正確切換，倒數重啟為 04:00

#### C.S6 — 6 秒恆常顯示不蓋警報
- **前置**：OHCA_COUNTDOWN，已開啟 6 秒恆常顯示（從快速功能選單）
- **操作**：(1) 確認主畫面下方有 6 秒節奏小區塊 → (2) 等待倒數歸零進 ALARMING → (3) 觀察畫面布局
- **預期**：步驟 1 主畫面下方常駐「秒 1~6」迴圈顯示；步驟 3 ALARMING 觸發後「請給藥」覆蓋上半部，6 秒區塊保留下方但不阻礙警報訊息
- **通過**：「請給藥」清晰可見、6 秒節奏未消失但不蓋過警報

---

## 6. Phase D — Training 模式

對應 V1 §15、§23.7、pm-dev-spec §8。

### 6.1 Training 與 OHCA 共用核心狀態機

`case_mode = Training` 時：
- ✅ epi_interval_seconds 可選 30 / 60 / 240
- ✅ history_limit = 20
- ✅ allow_device_delete = true
- ✅ show_training_label = true（全程浮水印）

### 6.2 倒數時間參數化測試

| 倒數 | 預警時機 | ALARMING 起算 |
|------|---------|--------------|
| 30s | V1 §15.7：30s 倒數**不**提供 1 分鐘預警 | 30s 到 → ALARMING |
| 1m  | V1 §15.7：1m 倒數**不**提供 1 分鐘預警 | 60s 到 → ALARMING |
| 4m  | 標準 1 分鐘預警（同 OHCA） | 240s 到 → ALARMING |

#### 6.2.1 邊界
- ⚠️ **30s 倒數預警邏輯**：since=15000 不觸發任何預警
- ⚠️ **1m 倒數預警邏輯**：since=30000 不觸發任何預警
- ✅ ALARMING 連續 5s 後 OVERTIME 邏輯與 OHCA 一致

### 6.3 重置功能（僅 Training 提供）
- ✅ Training 提供「重置」按鍵 → 倒數歸零、清除事件、保留 case 不結束
- ✅ OHCA 不提供重置

### 6.4 結束保存規則（V1 §15.12）
- ✅ 結束 → 選擇保存 / 不保存
- ✅ 保存 → 寫入 Training 區（FIFO 20 筆）
- ✅ 不保存 → 不占用儲存
- ✅ Training 案件**不混入** OHCA 列表（V1 §22.7）

### 6.5 Training 補登（V1 §15.10）
- ✅ Training 中補登邏輯與 OHCA 一致（同 ems_supp 模組）
- ✅ Training 補登 immutability 同 OHCA

### 6.6 Training 裝置端刪除（V1 §15.16）
- ✅ allow_device_delete=true → 裝置端可刪除單筆 Training
- ✅ OHCA 列表不顯示刪除選項（allow_device_delete=false）

### 6.7 Phase D L3 實機煙霧測試（操作流程）

#### D.S1 — Training 30s 浮水印
- **前置**：主功能表
- **操作**：(1) 進「訓練模式」→ (2) 選 30s → (3) 啟動倒數 → (4) 全程觀察
- **預期**：OLED 全程顯示半透明斜紋「訓練模式」浮水印
- **通過**：浮水印於 IDLE / COUNTDOWN / ALARMING / OVERTIME 全部畫面均存在

#### D.S2 — 1m 倒數無預警
- **前置**：D.S1 流程但選 1m
- **操作**：(1) 啟動 1m 倒數 → (2) 等待 OLED 顯示 `00:30`（since=30000）→ (3) 從 30s 觀察至 0s
- **預期**：倒數至 00:30 至 00:00 期間**無短嗶 / 無黃慢閃**；僅 0:00 觸發 ALARMING
- **通過**：30s 期間蜂鳴器靜默、LED 不閃；ALARMING 在 60s 觸發

#### D.S3 — 4m 倒數標準預警
- **前置**：選 4m
- **操作**：等待倒數至 01:00 → 觀察 60s
- **預期**：行為與 A.S3 完全一致（每 15s 短嗶 + 黃慢閃）
- **通過**：60s 內聽到 4 次短嗶

#### D.S4 — 結束保存
- **前置**：D.S1 倒數中
- **操作**：(1) 主鍵長按 3s → 結束選單 → (2) 選「結束訓練」→ (3) 出現「保存 / 不保存」對話框 → (4) 選「保存」
- **預期**：步驟 4 後寫入 Training 區一筆
- **通過**：Training 列表筆數 +1

#### D.S5 — Training / OHCA 列表分離
- **前置**：歷史紀錄至少各有 1 筆 OHCA + 1 筆 Training
- **操作**：(1) 進「歷史紀錄」→ (2) 切換 OHCA 與 Training 列表 → (3) 比對筆數
- **預期**：兩列表內容完全不混；OHCA 不出現 Training 浮水印 case，反之亦然
- **通過**：兩列表互不影響，數量分別正確

#### D.S6 — Training 裝置端刪除
- **前置**：Training 列表至少 1 筆
- **操作**：(1) 進入該筆 Training → (2) 選「刪除」→ (3) 二次確認 → (4) 返回列表
- **預期**：步驟 4 後該筆消失，列表筆數 -1
- **通過**：刪除成功；OHCA 列表無「刪除」選項（allow_device_delete=false）

---

## 7. Phase E — 持久化 + 歷史紀錄

對應 V1 §12、§18.1–§18.2、pm-dev-spec §10。

### 7.1 LittleFS partition 規劃驗證

| 區域 | 上限 | 覆蓋策略 | 測試 |
|------|------|---------|------|
| `/cases/ohca/` | 50 | FIFO | 寫滿 50 → 第 51 筆 → 最舊一筆刪除 |
| `/cases/training/` | 20 | FIFO | 寫滿 20 → 第 21 筆 → 最舊一筆刪除 |
| `/config/system.json` | 1 | 覆寫 | 多次寫入仍只有 1 個 |
| `/config/device_name.txt` | 1 | 覆寫 | App 多次更新仍只有 1 個 |
| `/sync_state.json` | 1 | 索引維護 | 同步成功後更新對應 case_id |

### 7.2 重啟資料保留

- ✅ 已鎖定 case → 重啟後可從歷史列表讀取
- ✅ 系統設定（亮度 / 音量）→ 重啟後保留
- ⚠️ **進行中 case 拔電**：spec 未明確要求 resume，但**不應丟事件**已寫入 flash 的部分（建議：每筆事件成立後立刻 append 到 `/cases/ohca/<case_id>.dat`，重啟後可從未鎖定 case 讀取）
- ⚠️ **邊界**：重啟瞬間正在寫入 → 必須有 fsync / atomic rename 保證檔案不毀損

### 7.3 從歷史紀錄重新進入

- ✅ 歷史列表顯示 OHCA / Training 分組（V1 §12.1）
- ✅ 點擊已鎖定 case → 案件總覽（read-only）
- ✅ 顯示 50 / 20 上限 footer 提示

### 7.4 寫入失敗處理

- ⚠️ **邊界**：LittleFS 滿（partition 損毀 / 寫入失敗）→ 回傳錯誤 + UI 提示「儲存失敗」
- ⚠️ **邊界**：寫入中拔電 → 重啟後檢查最後一筆 case 完整性，若不完整則丟棄該筆

### 7.5 Phase E L1 / L2 測試

#### L1（host filesystem mock）
- ✅ FIFO 演算法：50 筆 → 加 1 筆 → 刪最舊（依檔名 timestamp 排序）
- ✅ 同 case_id 寫入 → 覆寫不新增
- ✅ atomic write：write to tmp → fsync → rename

#### L2（native + LittleFS host adapter）
- ✅ 寫滿 50 → 51 → FIFO 刪最舊
- ✅ 重開 mount → 讀回所有 case
- ✅ 損毀檔案模擬 → fsck 流程跳過

### 7.6 Phase E L3 實機煙霧測試（操作流程）

#### E.S1 — FIFO 50 筆覆蓋
- **前置**：清空 /cases/ohca/（透過 Type-C 工具或燒錄空 partition）
- **操作**：(1) 連續完成 50 筆短 OHCA（可用 Training 或快速結束）→ (2) 進歷史列表確認 50 筆 → (3) 完成第 51 筆 → (4) 進歷史列表 → (5) 完成第 52 筆 → (6) 再進列表
- **預期**：步驟 4 列表 50 筆，最舊一筆 (case#1) 已被 case#51 取代；步驟 6 case#2 也被 case#52 取代
- **通過**：列表始終 ≤ 50 筆，FIFO 順序正確

#### E.S2 — 拔電後重啟資料保留
- **前置**：A.S1 啟動 OHCA 並至少完成 1 次 EPI 確認 + 1 次電擊
- **操作**：(1) OHCA 進行中（不結束、不鎖定）直接拔 USB-C 並等待裝置斷電 → (2) 重新插上 USB-C / 開機 → (3) 進歷史列表查看
- **預期**：步驟 3 該未鎖定 case 仍存在（雖未鎖定但已寫入事件保留）；事件清單包含步驟 1 的 EPI 與電擊
- **通過**：事件不丟失；檔案完整可讀（無毀損）

#### E.S3 — 系統設定持久化
- **前置**：恢復預設設定（亮度 3 / 系統音量 3 / 通氣音量 3）
- **操作**：(1) 設定亮度 5、系統音量 1、通氣音量 0 → (2) 長按 Power 關機 → (3) 重新開機 → (4) 進系統設定查看
- **預期**：步驟 4 三個設定值與步驟 1 一致
- **通過**：NVS 寫入正確、重啟後保留

---

## 8. Phase F — App 配對碼同步

對應 V1 §16、§17、pm-dev-spec §14。

### 8.1 配對碼產生與 TTL

#### 8.1.1 4 位數隨機
- ✅ 產生範圍 0000~9999
- ✅ 每次重新產生為新值（避免重複可預測）
- ⚠️ **邊界**：產生 0001 / 0010 → 顯示 4 位完整（前導 0）

#### 8.1.2 120s TTL（V1 §16.4）
- ✅ 產生時刻 t0，t0 + 119s 內可成功配對
- ✅ t0 + 121s 失效
- ⚠️ **邊界**：t0 + 120000ms 整 → 定義為「< 120000ms 有效」 → 失效
- ✅ 配對成功後配對碼立即作廢
- ✅ 失敗後重新產生新配對碼

### 8.2 同步流程

#### 8.2.1 NUS + JSON Payload（pm-dev-spec §14.1）
- ✅ payload schema 完整：type / case_id / mode / device_name / device_id / fw_version / started_at_ms / ended_at_ms / events[] / summary
- ✅ events[] 中 actual_time_null=true 的事件必須帶 count

#### 8.2.2 Case ID 去重（V1 §16.8 / §23.4）
- ✅ App 端依 case_id（UUID v4）去重
- ✅ 同 case_id 重複同步 → 覆蓋而非新增
- ✅ 重新同步時整筆重傳，不做片段續傳

#### 8.2.3 同步狀態標記
- ✅ 同步成功 → 裝置端 sync_state.json 紀錄 synced_to_app=true, synced_at_ms
- ✅ 已同步 case 仍允許再次同步（覆蓋 App 端）

### 8.3 同步中斷與失敗

| 情境 | 預期行為 |
|------|---------|
| 配對碼錯誤 | 顯示「配對失敗」訊息，重試 |
| 配對碼逾時 | 顯示「配對碼已失效」，請使用者重新產生 |
| 傳輸中斷（BLE 斷線） | 整筆重傳，App 端 case_id 去重 |
| App 端解析失敗 | 顯示錯誤類別（V1 §16.9） |

### 8.4 Phase F L1 / L2 測試

#### L1
- ✅ pair code 產生 1000 次 → 平均分布、無連續重複
- ✅ Case ID 去重邏輯（純函式）

#### L2
- ✅ Mock BLE transport：完整 payload roundtrip
- ✅ 中斷模擬：傳輸 50% 後切斷 → 重連後整筆重傳
- ✅ 同 case_id 兩次同步 → App 端只有 1 筆（覆蓋）

### 8.5 Phase F L3 實機煙霧測試（操作流程）

#### F.S1 — 配對成功（119s 內）
- **前置**：裝置已鎖定 1 筆 OHCA 案件；App 已安裝、未連線
- **操作**：(1) 裝置進案件總覽 → 選「同步至 App」→ OLED 顯示 4 位配對碼（如 `4827`）→ 紀錄 t0 → (2) App 點「掃描裝置」→ 選到該裝置 → (3) 100s 後輸入配對碼 → (4) 確認連線
- **預期**：步驟 4 後 App 顯示「同步成功」、案件出現在 App 列表
- **通過**：120s 內配對成功；裝置 sync_state.json 標記 synced_to_app=true

#### F.S2 — 配對碼逾時失效
- **前置**：F.S1 流程
- **操作**：(1) 裝置產生配對碼 → 紀錄 t0 → (2) 等待 121s → (3) App 輸入配對碼
- **預期**：步驟 3 拒絕，App 顯示「配對碼已失效」；裝置端要求重新產生
- **通過**：120s 後配對碼確實失效，無法配對

#### F.S3 — 中斷重傳整筆
- **前置**：裝置鎖定 1 筆大型案件（≥ 20 events）；F.S1 已配對成功
- **操作**：(1) 開始同步 → 在傳輸進度約 50% 時 → (2) 強制關閉 App / 將手機飛航 → (3) 等待 5s → (4) 重啟 App / 關閉飛航 → 重新連線 → (5) 重新觸發同步
- **預期**：步驟 5 整筆重傳（非從 50% 接續）；App 端依 case_id 覆蓋而非新增
- **通過**：App 列表只有 1 筆該 case_id；events 完整 = 裝置端事件數

#### F.S4 — 同 case 重複同步去重
- **前置**：F.S3 已成功同步該 case
- **操作**：(1) 在裝置端對同一 case 再次點「同步至 App」→ (2) 完成配對與同步流程 → (3) 檢查 App 列表
- **預期**：App 列表仍只有 1 筆（case_id 相同 → 覆蓋更新而非新增）
- **通過**：列表筆數 = 同步前筆數（去重生效）

#### F.S5 — App 端刪除不影響裝置
- **前置**：F.S4 同步完成，App 與裝置都有該 case
- **操作**：(1) 在 App 內刪除該 case → 二次確認 → (2) 進裝置端歷史列表查看
- **預期**：步驟 2 裝置端該 case 仍存在；裝置 sync_state 維持 synced_to_app=true
- **通過**：裝置端列表筆數不減；可從裝置端再同步至 App 重新出現

---

## 9. Phase G — 系統設定 + Type-C 工具

對應 V1 §18.3、§18.4、§19、pm-dev-spec §15。

### 9.1 NVS 持久化設定

| 設定 | 範圍 | 預設 | 邊界測試 |
|------|------|------|---------|
| 螢幕亮度 | 1~5 | 3 | 嘗試 0 / 6 → 拒絕；最低 1 |
| 系統音量 | 1~5 | 3 | 嘗試 0 → 拒絕（不可靜音） |
| 通氣音量 | 0~5 | 3 | 嘗試 6 → 拒絕；0 = 靜音允許 |

### 9.2 恢復預設值（V1 §19.6）

#### 9.2.1 清除範圍
- ✅ 螢幕亮度 → 3
- ✅ 系統音量 → 3
- ✅ 通氣音量 → 3

#### 9.2.2 不清除
- ✅ 裝置名稱保留
- ✅ /cases/ohca/ 案件保留
- ✅ /cases/training/ 案件保留
- ✅ /sync_state.json 同步狀態保留
- ⚠️ **邊界**：恢復預設後重啟 → NVS 重讀仍為預設值

### 9.3 裝置名稱（V1 §19.2）
- ✅ 由 App 透過 BLE 寫入 → /config/device_name.txt
- ✅ 預設「未命名」
- ⚠️ **邊界**：UTF-8 多位元組字元 → 正確存取
- ⚠️ **邊界**：長度上限（pm-dev-spec 未明確；建議 32 bytes）→ 超過截斷或拒絕

### 9.4 Type-C 管理工具（V1 §18.3）

#### 9.4.1 USB CDC 連線
- ✅ 插上 USB-C → 電腦端工具偵測到 ESP32 USB CDC
- ✅ 連線後不影響裝置端 OHCA 進行中

#### 9.4.2 列案件 / 匯出 / 清除
- ✅ 列出 OHCA 50 + Training 20 上限
- ✅ 匯出 CSV：欄位完整（event_id / type / timestamp / elapsed / count）
- ✅ 匯出 JSON：raw payload
- ✅ 二次確認後清除 → 全部案件刪除（不影響系統設定）

#### 9.4.3 不可修改既有案件
- ✅ 工具僅讀取與刪除，**不提供**修改 API

### 9.5 Phase G L3 實機煙霧測試（操作流程）

#### G.S1 — 系統音量 NVS 持久化
- **前置**：開機後預設音量 3
- **操作**：(1) 進系統設定 → 系統音量調至 5 → (2) 短按 EPI 鍵試聽確認音 → (3) 長按 Power 關機 → (4) 重新開機 → (5) 進系統設定查看
- **預期**：步驟 2 確認音明顯較大聲；步驟 5 顯示音量值仍為 5
- **通過**：NVS 正確保留；體感音量分級明確

#### G.S2 — 通氣音量 0 靜音
- **前置**：G.S1 開機完成
- **操作**：(1) 進系統設定 → 通氣音量調至 0 → (2) 進「6 秒通氣節奏」→ (3) 觀察 12s
- **預期**：通氣模式蜂鳴**全靜音**；秒 1 反紅 + LED 紅閃保留
- **通過**：聽不到任何蜂鳴；視覺提示正常

#### G.S3 — 系統音量不可 = 0
- **前置**：開機完成
- **操作**：(1) 進系統設定 → 嘗試將系統音量調至 0
- **預期**：UI 滑桿最低停在 1，無法達 0；若工程後門強塞 0 → 韌體拒絕並回 1
- **通過**：系統音量範圍實際為 1~5

#### G.S4 — 恢復預設不清案件
- **前置**：歷史列表 ≥ 5 筆 OHCA、設定值非預設
- **操作**：(1) 進系統設定 → 「恢復預設值」→ 二次確認 → (2) 進系統設定查看 → (3) 進歷史列表 → (4) 進系統設定 → 「裝置名稱」
- **預期**：步驟 2 三個音量 / 亮度回 3；步驟 3 案件數量不變；步驟 4 裝置名稱保留
- **通過**：清除範圍精確（僅清音量 / 亮度），其餘保留

#### G.S5 — App 寫入裝置名稱
- **前置**：F.S1 已配對成功
- **操作**：(1) App 端編輯裝置名稱為「安康91」→ 寫入 → (2) 裝置端進系統設定 → 裝置名稱 → (3) 長按 Power 關機 → 重啟 → (4) 進系統設定查看
- **預期**：步驟 2 顯示「安康91」；步驟 4 仍顯示「安康91」（重啟保留）
- **通過**：UTF-8 中文正確存取；重啟保留

#### G.S6 — Type-C 工具列案件並清除
- **前置**：歷史列表有 ≥ 3 筆 OHCA
- **操作**：(1) USB-C 連電腦 → 開 Type-C 工具 → (2) 點「列案件」→ (3) 選一筆匯出 CSV → 開 CSV 檢查欄位 → (4) 點「清除全部」→ 二次確認 → (5) 裝置端進歷史列表
- **預期**：步驟 2 列出全部案件；步驟 3 CSV 欄位完整（event_id / type / timestamp / elapsed / count）；步驟 5 列表為空
- **通過**：流程完整；清除後系統設定 / 裝置名稱保留

---

## 10. Phase H — 電源管理

對應 V1 §20、§22.9、pm-dev-spec §16。

### 10.1 螢幕常亮

#### 10.1.1 強制常亮場景
- ✅ OHCA_COUNTDOWN / WARNING / ALARMING / OVERTIME → 螢幕**禁止** sleep
- ✅ 6 秒通氣（獨立 + OHCA 切入）→ 螢幕禁止 sleep
- ✅ Training 進行中 → 螢幕禁止 sleep

#### 10.1.2 允許 sleep 場景
- ✅ MAIN_MENU 閒置 N 分鐘（spec 是否定義？）→ 進入低亮度或 sleep
- ⚠️ **邊界**：spec 未明確時，建議 MAIN_MENU 閒置 5 分鐘進入 sleep

### 10.2 邊充邊用

- ✅ 案件中插上 USB-C → 充電 + 使用
- ✅ 案件中拔下 USB-C → 改吃電池、案件不中斷
- ⚠️ **邊界**：充電過程中電池升溫 → TP4056 自動降流（硬體保護）
- ⚠️ **邊界**：USB-C 5V/500mA host port 上限 → 韌體層需保證峰值 ≤ 500mA（pm-dev-spec §20.4 規範）

### 10.3 低電量提醒（V1 §20.3）

- ✅ 電量 ≤ X%（spec 是否定義門檻？）→ 電量圖示閃爍
- ✅ 不發聲（避免干擾警報）
- ✅ 不強制關機

### 10.4 插拔 USB-C 不中斷案件（V1 §22.9）

- ✅ OHCA_COUNTDOWN 中拔下 → 倒數連續
- ✅ OHCA_ALARMING 中插上 → 警報持續
- ✅ 6 秒通氣中插拔 → 節奏連續
- ⚠️ **邊界**：插拔瞬間 boot loop？→ 必須驗證電源切換不觸發 reset

### 10.5 Phase H L3 實機煙霧測試（操作流程）

#### H.S1 — OHCA 螢幕常亮 30 分鐘
- **前置**：A.S1 啟動倒數，倒數結束後反覆做 EPI 二段確認重啟倒數
- **操作**：(1) 持續放置 30 分鐘不操作（倒數 / 警報 / OVERTIME 反覆）→ (2) 全程觀察 OLED
- **預期**：30 分鐘內螢幕**從未** sleep / dim / 黑屏
- **通過**：OLED 持續顯示 OHCA 畫面

#### H.S2 — OHCA 中插拔 USB-C ×3
- **前置**：A.S1 啟動倒數（剩約 03:00）；接 USB-C 供電
- **操作**：(1) 拔下 USB-C（改吃電池）→ 觀察 5s → (2) 插回 USB-C → 觀察 5s → (3) 重複步驟 1~2 兩次（共拔插 3 次）→ (4) 比對倒數時間
- **預期**：每次插拔過程中倒數**連續遞減**、無 reset；蜂鳴 / 警報行為不中斷
- **通過**：步驟 4 倒數時間 = 起始時間 - (3 次拔插總耗時)；裝置未重啟

#### H.S3 — 低電量提醒
- **前置**：電池電量 ≤ 低電量門檻（如 ≤ 20%）；案件進行中
- **操作**：(1) 觀察 OLED 角落電量圖示 → (2) 等待 60s 確認行為
- **預期**：電量圖示閃爍；**蜂鳴不發聲**；案件不被強制結束
- **通過**：圖示視覺提示 + 無干擾警報音；案件可繼續

#### H.S4 — 邊充邊用 1 小時
- **前置**：電量 ≤ 50%，接 USB-C
- **操作**：(1) 啟動 OHCA 倒數 + 6 秒通氣恆常顯示 → (2) 持續 1 小時 → (3) 用紅外線測溫槍量測電池表面溫度
- **預期**：1 小時後溫度 < 45°C（TP4056 自動降流保護）
- **通過**：未過熱、充電仍進行

#### H.S5 — 螢幕常亮續航 8h
- **前置**：1000mAh 電池滿電（充至 4.2V）；不接 USB-C
- **操作**：(1) 啟動 OHCA 倒數 + 反覆 EPI 確認 → (2) 連續執行至自動關機
- **預期**：續航 ≥ 8 小時
- **通過**：連續使用達 8 小時門檻

#### H.S6 — 連續開關機 50 次
- **前置**：電量 ≥ 80%
- **操作**：(1) 長按 Power 開機 → 等待 5s → (2) 長按 Power 關機 → 等待 3s → (3) 重複步驟 1~2 共 50 個循環 → (4) 開機後檢查歷史列表
- **預期**：50 次後仍可正常開機；歷史列表完整可讀
- **通過**：無 brick、NVS 與 LittleFS 無毀損

---

## 11. 涵蓋率目標與品質門

### 11.1 涵蓋率目標

| 類別 | 行涵蓋率 | 分支涵蓋率 | 備註 |
|------|---------|-----------|------|
| L1 純函式（Phase A 倒數引擎、兩段確認、補登模型、總覽聚合） | **100%** | **95%** | 不允許未測 path |
| L1 純函式（其他 Phase） | ≥ 90% | ≥ 85% | |
| L2 狀態機（OHCA / Training）| **100% transition coverage** | — | 每個 transition 至少 1 case |
| L3 實機煙霧 | 全表案例通過 | — | 每 phase 5~8 case |
| L4 PM 驗收 | V1 §22 全部 case 通過 | — | 上線必經 |

### 11.2 品質門（每個 Phase 完工標準）

1. ✅ L1 全綠 + 涵蓋率達標
2. ✅ L2 狀態機 transition 全綠
3. ✅ L3 煙霧測試全部 PASS（簽收清單）
4. ✅ 對應的 V1 §22 章節驗收 PASS
5. ✅ 無已知 P0 / P1 bug

---

## 12. 風險與排程

### 12.1 高風險區

| 區域 | 風險 | 降低做法 |
|------|------|---------|
| EPI 倒數精度 | ±50ms 在 RTOS 下不易保證 | 用獨立 FreeRTOS task + 高優先級；L3 實機用碼錶與 OLED 對拍 |
| EPI 高優先打斷通氣 | ≤ 50ms 靜音延遲 | vent task 低優先；L3 高速攝影 |
| 補登資料不可變 | UI 層誤觸 | 韌體層拒絕 API；L1 釘住「補登成立後嘗試修改」一定 fail |
| LittleFS 寫入中拔電 | 檔案毀損 | atomic rename + fsync；L3 拔電壓力測試 50 次 |
| BLE 中斷重傳 | 狀態不一致 | 整筆重傳 + Case ID 去重；L2 mock 中斷模擬 |
| 系統音量不可靜音 | UI 滑桿越界 | 韌體層拒絕 0；L1 釘住 |
| millis() rollover | 49.7 天後計時錯誤 | 全部時間運算用 uint32 減法（rollover-safe）；L1 釘住 wrap-around case |

### 12.2 排程估算

| Phase | L1 設計 + 寫測 | L2 整合 | L3 實機 | 小計 |
|-------|---------------|---------|--------|------|
| A | 8h | 4h | 4h | **16h** |
| B | 6h | 3h | 3h | 12h |
| C | 5h | 3h | 3h | 11h |
| D | 4h | 2h | 2h | 8h |
| E | 5h | 4h | 3h | 12h |
| F | 5h | 4h | 3h | 12h |
| G | 4h | 2h | 2h | 8h |
| H | 3h | 2h | 4h | 9h |
| 基礎設施（native env、Unity、CI）| 4h | — | — | 4h |
| **合計** | | | | **~92h** |

---

## 13. 測試框架與基礎設施

### 13.1 PlatformIO native 環境

```ini
; firmware/platformio.ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -I test/mocks
```

### 13.2 目錄結構

```
firmware/
├── platformio.ini
├── src/main.cpp                          ; #ifndef UNIT_TEST 包 setup/loop
├── lib/
│   ├── ems_ohca/                         ; Phase A 新建
│   │   ├── ems_ohca_countdown.{h,cpp}
│   │   ├── ems_ohca_state.{h,cpp}
│   │   └── ems_two_step_confirm.{h,cpp}
│   ├── ems_supp/                         ; Phase B
│   ├── ems_vent/                         ; Phase C
│   ├── ems_training/                     ; Phase D
│   ├── ems_persist/                      ; Phase E
│   ├── ems_pairing/                      ; Phase F
│   └── ble_nus/                          ; Phase F 過渡保留
└── test/
    ├── mocks/
    │   └── Arduino.h                     ; stub millis / digitalRead / Serial
    ├── test_ohca_countdown/              ; Phase A
    ├── test_two_step_confirm/            ; Phase A
    ├── test_ohca_state/                  ; Phase A 狀態機
    ├── test_supp_model/                  ; Phase B
    ├── test_case_summary/                ; Phase B
    ├── test_vent_decide/                 ; Phase C
    ├── test_training_param/              ; Phase D
    ├── test_persist_fifo/                ; Phase E
    ├── test_pair_code/                   ; Phase F
    └── test_settings_nvs/                ; Phase G
```

### 13.3 純函式設計原則

- ✅ 所有 decision 函式輸入 `now_ms` 參數，**不**直接呼叫 `millis()`
- ✅ 回傳 struct（如 `ohca_output_t`），**不**做 side effect
- ✅ Side effect（蜂鳴、LED、寫 flash）由外層 caller 執行
- ✅ 不 include `Arduino.h`（用 `<stdint.h>` + `<stdbool.h>`）

### 13.4 Mock 策略

| 依賴 | Mock 方式 |
|------|----------|
| millis() | 純函式不需要；ButtonFsm 等用 stub Arduino.h |
| LittleFS | host filesystem adapter（檔案 I/O 用 stdio） |
| NimBLE / NUS | fake transport：write / notify 寫入 in-memory queue |
| GPIO / I2C / I2S | 純邏輯不需要；硬體層測 L3 實機 |

### 13.5 CI / CD

- ⚠️ 目前 GitLab webotopia 無 runner，CI 不跑單元測試（README 已註）
- 建議：開發者本機 `pio test -e native` 必跑綠燈才 commit
- 未來：補 GitHub Actions runner（備份 mirror 上）跑 `pio test`

---

## 14. 與 V1 SoT §22 對應表

| V1 §22 章節 | 對應 Phase | 本計劃章節 |
|-------------|----------|----------|
| 22.1 OHCA 基本流程測試 | A | §3.4（A.S1~S8） |
| 22.2 電擊測試 | B | §4.6（B.S1~S6 部分） |
| 22.3 Amiodarone 測試 | B | §4.6（B.S3） |
| 22.4 補登測試 | B | §4.6（B.S1, B.S2, B.S4） |
| 22.5 6秒通氣節奏測試 | C | §5.4（C.S1~S2） |
| 22.6 OHCA 中 6秒通氣恆常顯示 | C | §5.4（C.S6） |
| 22.7 Training 測試 | D | §6.7（D.S1~S6） |
| 22.8 App 同步測試 | F | §8.5（F.S1~S5） |
| 22.9 電源測試 | H | §10.5（H.S1~S6） |

---

## 15. 變更紀錄

- **2026-05-03 v1.0**：初版。涵蓋 Phase A~H 全部範圍（單元 + 整合 + 實機 + PM 驗收）。
