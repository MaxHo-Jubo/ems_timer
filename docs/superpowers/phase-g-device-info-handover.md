# Impl-Phase G 裝置資訊畫面 — 交接文件

- **最後更新**：2026-09-02（全分支整合 review 發現的 1 個 CRITICAL + 4 個 Important
  已全部修復並通過 repo Tier 3 codex 兩輪 re-review〔6/6 面向、0 CRITICAL〕，commit
  `62bf11f`，pending-review 閘門已解鎖——見下方「✅ 已解決」區塊。**下次接手可以直接
  跳到 §3-B 上機驗收**，程式碼工作全部完成）
- **狀態**：SDD（subagent-driven-development）流程完成 6 個 task + 1 輪全分支整合
  review fix round，全部經 repo Tier 2/3 codex gate 驗證通過。**分支目前是「可合併」
  狀態**（仍待上機驗收，見 §3-B），HEAD 在 `62bf11f`。

## ✅ 已解決：全分支整合 review 發現（2026-09-02，修復並通過兩輪 re-review）

commit `62bf11f`（原始修復 `340fca7`，兩輪 codex Tier 3 fix round amend 到此）。
native test 651 cases / 650 通過（唯一 ERRORED 仍是既有 `test_storage_hw`，與本次
無關）；ESP32 build SUCCESS，Flash 73.0% / RAM 33.4%；VLW 字型重生 byte-identical
（0 新字）。pending-review 閘門已用 `clear-pending-review.ts --aspects-done=6` 解鎖。

以下保留原始發現記錄（file:line 對應修復前的 `cba8ce9`），供之後想知道「這個 bug
當初長什麼樣」時查閱；每項後面補上**實際修法**（可能與原始建議不同，見下方說明）。

### CRITICAL：`drawDeviceInfo()` 與 `drawBatteryInfo()` 的文字渲染都是壞的

**根因**：`drawCenteredText()`（`ui_screens.cpp:54-58`）會永久修改共用繪圖狀態
（`setTextColor()` + `setTextDatum(top_center)`），不是純函式。`drawDeviceInfo()`
（`ui_screens.cpp:318-322`）先設定 `setTextColor(COLOR_TEXT_PRIMARY)` +
`setTextDatum(top_left)`，但緊接著呼叫 `drawCenteredText("裝置資訊", ...)` 畫標題，
這一行把兩個設定都覆蓋掉了——後面六列 `display.drawString()`（`:356,360,365,369,377,
378,383,389`）因此全部用**錯的顏色**（`COLOR_ACCENT_OK` 深綠，不是白色）跟**錯的對齊
方式**（置中在 x=24，不是左靠）畫出來。一個約 200px 寬的字串置中在 x=24 會從 x≈-76
畫到 x≈124，螢幕左半邊直接被裁掉。

**同一個 bug 也存在於 `drawBatteryInfo()`**（`ui_screens.cpp:258-259` vs
`:276,280,286`）——是 Phase H 既有的，從來沒人抓到過，因為這是純渲染邏輯，native test
測不到（`src/` 排除）、編譯也不會報錯，只有上機才看得出來或像這次靠仔細追蹤程式碼邏輯
抓到。

**驗證依據**：這個 repo 自己的 `drawHistoryList()`（`:479-480`）跟 `drawSyncScreen()`
（`:131`）都在畫完標題後**重新設定**顏色跟對齊方式才畫本文——這是既有的正確 pattern，
`drawDeviceInfo()`/`drawBatteryInfo()` 兩個是例外，沒跟上。

**建議修法**（review 建議的結構性修法，不是治標）：讓 `drawCenteredText()` 自己
save/restore datum 跟 color，一次性關掉這整類問題，而不是在兩個呼叫點各自補
`setTextColor`/`setTextDatum`。次選：在兩個函式的標題呼叫之後各補一行重新設定。

**為什麼這個現在才被抓到**：5 個 task 各自的 review（repo Tier 2/3 gate + SDD
task-reviewer）都沒抓到，因為每個 task 各自看到的 diff 都沒有理由去追蹤
`drawCenteredText()` 的副作用；只有全分支整合 review 專門去讀了完整的
`drawDeviceInfo()` 執行路徑才發現。這正是 SDD 流程最後一步「全分支 review」存在的
理由。

**✅ 實際修法**：採用建議的結構性修法——`drawCenteredText()` 進入時
`display.getTextStyle()` 存下呼叫端目前樣式，畫完後 `display.setTextStyle()`
還原，一次性關掉整類問題。收工前逐一 grep 全檔案所有 `drawCenteredText()` 呼叫點
與所有 `display.drawString()`/`print()` 呼叫點，確認沒有任何既有函式依賴
`drawCenteredText()` 遺留的樣式副作用（每個 draw 函式進入時都會自己重設所需樣式，
是本檔既有的一致慣例）。

### Important（4 個，不影響核心可用性，已一併處理）

1. **placeholder 返回提示文字寫錯**（`main.cpp:1193-1196` → `ui_screens.cpp:501` →
   `input_handler.cpp:840-844`）：`drawPlaceholder()` 固定顯示「返回　主功能表」，但
   App連線設定／Type-C連線這兩個新 placeholder 是任意鍵返回**設定選單**（spec §3.4
   明定），不是主功能表也不限定 BACK 鍵。同段的 Phase 標記寫「Phase G」也不對——
   Phase G 就是正在出貨這兩個 placeholder 的階段，寫法暗示功能還要等更後面的 phase。
   **✅ 實際修法**：`drawPlaceholder()` 加 `hint` 參數（無預設值，兩個呼叫點都改
   明確傳「返回　系統設定」）；`phase` 參數傳空字串 `""` 時略過大字級 Phase 標記
   （目前沒有明確排定的未來 phase 可標示，比起填一個猜測值更誠實），兩個呼叫點都
   改傳 `""`。fix round 2 補上 `phase != nullptr` 防護。
2. **`SYNC_FW_VERSION` 過期**（`app_globals.h:233`，值 `"v0.6-phaseF"`）：本分支是
   這個常數第一次會被使用者直接在畫面上看到（`ui_screens.cpp:367`），之前只出現在 BLE
   同步 metadata。字串**格式**重新設計已由 spec §7 明列不在範圍（不重提），但**值本身
   過期**是沒人問過的獨立問題——顯示這個版本號的畫面就是本分支的成果，卻報告一個
   Phase F 的版本。**✅ 實際修法**：改為 `"v0.7-phaseG"`，一行修正。獨立回歸測試
   （斷言裝置資訊畫面/BLE metadata 輸出這個值）review 兩輪都有提，兩輪都裁決不加
   ——`app_globals.h`／`ui_screens.cpp`／`sync_send.cpp` 全部在 `src/`，native
   build 整檔排除，加測試需要改 native env 的 include path 或把常數搬出 `src/`，
   超出本輪「修 review 發現」的範圍，記錄為殘餘風險。
3. **UTF-8 截斷迴圈是純邏輯卻放在 `src/`，無測試覆蓋**（`ui_screens.cpp:344-355`）：
   計畫 Global Constraints 明定「純邏輯一律先抽到 `lib/` 再由 `src/` 呼叫，native test
   測 `lib/` 那份」——這正是 `clampScrollOffset()`/`advanceSettingsCursorAndScroll()`
   遵循的原則，但這段 UTF-8 continuation-byte 判斷的巢狀迴圈沒有照做，只用一個沒進
   repo 的獨立 g++ harness 驗證過。Review 追蹤過邏輯本身是對的，缺的只是回歸保護。
   **✅ 實際修法**：抽出 `size_t utf8PrevCharBoundary(const char* s, size_t idx)`
   到新 lib `lib/ems_utf8/ems_utf8.h`，補 6 個 native test case（ASCII／3-byte
   CJK／index 0 邊界／混合字串／`idx==0` 與 `s==nullptr` 違反契約時 `abort()` 的
   `fork()` 死亡測試——後兩個是 fix round 1 codex 抓到的 CRITICAL：契約只寫在文件
   裡沒有真的擋，補上才算完整）。順手把 `ems_settings.cpp` 的
   `device_name_sanitize()` 內同一個 continuation-byte 判斷式也改呼叫這裡抽出的
   `utf8IsContinuationByte()`，消除兩處重複邏輯（fix round 1 codex 額外抓到）。
4. **`drawSettingsMenu()` 尾端三個參數仍留預設值**（`ui_settings.h:142-145`）：Task 2
   那次「`bool`→`uint16_t` 隱式轉換、參數全部錯位、恢復預設對話框永遠不顯示」的
   CRITICAL，根因機制就是尾端預設值讓呼叫端漏傳參數也能編譯過。現在每個呼叫點都驗證
   正確（1 個正式呼叫點 + 16 個測試呼叫點），但地雷本身沒拆——未來這個參數列表只要再
   插入一個新參數，同樣的錯位風險會重新出現。**✅ 實際修法**：拿掉全部 4 個尾端預設值
   （`cursor`／`scroll_offset`／`device_name_locked`／`restore_confirm`），17 個
   呼叫點（1 正式 + 16 測試）全部改明確傳值。

**已核閱但不採納**（兩輪 codex re-review 都重複提出，記錄理由供之後想重新評估時參考）：
- `ui_screens.cpp` 文字渲染邏輯（`drawCenteredText`／`drawPlaceholder`）與
  `SYNC_FW_VERSION` 的 native 回歸測試——`src/` 整檔被 native build 排除是既有架構
  限制（`platformio.ini` 的 `build_src_filter = -<*>` 與 `-I$PROJECT_LIB_DIR`），
  同 `drawBatteryInfo()`/`drawDeviceInfo()` 從 Task 13/Task 5 起就有的既有慣例，
  不是本次新增的缺口。要修須新增可測的顯示抽象層或搬動 include path，是獨立的架構
  任務。
- `utf8PrevCharBoundary()` 改用 `string_view`／`drawPlaceholder()` 改用
  `enum class ReturnTarget` 的型別重設計——內部 helper 且呼叫點單純（分別 1 個與
  2 個），過度工程化，同 `clampScrollOffset()` 既有 Ruling（`ui_scroll.h`，Task 1）
  的判斷邏輯。

原始全分支整合 review 完整報告（含 Minor 項目與 6 條 Strengths）當時記錄在 SDD
ledger（`.superpowers/sdd/2026-09-01-phase-g-device-info/progress.md`）；fix round
兩輪 codex Tier 3 review 的完整結果在
`~/.claude/state/codex-review/-Users-maxhero-Documents-MaxHero-Projects-ems_timer-.worktrees-phase-g-device-info/{340fca7,249472e}/`
（本機 state 目錄，不隨 repo 走）。**SDD workspace（`.superpowers/sdd/`）已於
fix round 完成、review 閘門解鎖後依流程刪除**——git-ignored、非最終記錄，最終記錄
是 commit `62bf11f` 的完整訊息（已含所有發現與實際修法對照）與本文件。
- **branch**：`feat/phase-g-device-info`（git worktree，路徑
  `.worktrees/phase-g-device-info`，從本機 `main`（`a634ba9`）分支——**不是**
  `origin/main`，本機 main 領先 origin/main 99+ commit 未推送）

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

**Task 1 完成**（2026-09-01）：`clampScrollOffset()` 共用純函式 + 10 個 native test，
最終 commit `1f924b4`（3 輪 amend）。經 repo 自己的 Tier 3 codex review（6 面向）+
SDD task-reviewer + SDD scoped re-reviewer 三層驗證，共抓到 1 CRITICAL（整數下溢/溢位）
+ 14 IMPORTANT + re-review 額外抓到 3 個殘留 magic number，三輪 fix 後全部收斂，細節見 §4。

**Task 2 完成**（2026-09-01）：設定選單擴充至 SoT §19.1 完整 8 項並支援捲動，最終 commit
`c980927`（1 輪 amend）。經 repo 自己的 Tier 3 codex review 抓到 5 CRITICAL（4 個同源於
`main.cpp:1177` 舊呼叫點參數錯位——team-lead 覆蓋原「留給 Task 4」的裁決，當場修正）+
12 IMPORTANT，全部處理後 SDD task-reviewer 給 Approved（0 Critical/Important，1 Minor
延後）。細節見 §4。**過程中發現一個流程缺口**：implementer 第一次 commit 後，repo 的
Tier 3 codex review 閘門從未被實際執行過（marker 卡在磁碟上、沒有背景 process、implementer
沒有理會 hook 的 systemMessage 指派）——是 team-lead 後來想結束回合被 Stop hook 攔下才發現，
手動補跑才解決，已存 feedback memory（`feedback_sdd_dispatch_must_mention_commit_gate.md`）
供未來 dispatch brief 參考。

**Task 3 完成**（2026-09-02）：`input_handler.cpp` 新游標分派——`GlobalState` 新增
`GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER`/`TYPEC_PLACEHOLDER`、`settingsDeviceInfoMode`
全域、`BTN_PRIMARY` 對 cursor 5~7 的分派、`historyScrollOffset` 改用共用
`clampScrollOffset()`。最終 commit `928692c`（1 輪 amend）。細節見 §4。

**Task 4 完成**（2026-09-02）：DisplaySnapshot 接線——`SNAP_FLAG_SETTINGS_DEVICE_INFO`
+ `settingsDeviceInfo` 欄位五步驟走完、`updateDisplay()` 新增裝置資訊子畫面與兩個
placeholder 的實際畫面渲染分派（這正是 Task 3 完成後 repo Tier 2 gate 抓到的 2 個
CRITICAL 缺口）。最終 commit `d5b33e6`（1 輪 amend）。細節見 §4。

**Task 5 完成**（2026-09-02）：`drawDeviceInfo()` 畫面本體——名稱／型號／序號／韌體／
電池／充電狀態六列，抽出與 `drawBatteryInfo()` 共用的 `chargeStateText()` helper，
UTF-8 安全的裝置名稱寬度截斷。字型重生 0 缺字。最終 commit `43b40a8`（1 輪 amend）。
細節見 §4。**這個 commit 完成後韌體才第一次真正編譯成功**（Task 4 之前的每個中間
commit 都因 `drawDeviceInfo()` 前向引用而編不過，這是計畫本身設計好的序列狀態，不是
迴歸）。

**尚未開工**：無。6 個 task + 全分支整合 review fix round 全部完成，全套 native
test 651 cases / 650 通過（唯一 ERRORED 仍是既有的 `test_storage_hw`，與本計畫無關），
韌體 Flash 73.0% / RAM 33.4%。

---

## 2. 如何驗證現況 / 交接給下一步（上機驗收）

所有 6 個 task + 全分支整合 review 的 fix round 都已完成，**沒有剩餘的程式碼工作**
——下一步是 §3-B 的上機驗收清單，需要實體硬體，不是繼續開發。

```bash
cat docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md   # 設計決策記錄
cat docs/superpowers/plans/2026-09-01-phase-g-device-info.md          # 實作計畫 + 每個 task 執行時發現的計畫缺陷修正

# 驗證最終狀態（2026-09-02，fix round 完成時）
cd firmware && pio test -e native                            # 應為 651 cases / 650 通過，唯一 ERRORED = test_storage_hw（既有、與本計畫無關）
cd firmware && pio run -e esp32-s3-devkitc-1                  # 應為 SUCCESS，Flash 73.0% / RAM 33.4%
git log --oneline -15                                         # 確認在 feat/phase-g-device-info worktree、HEAD 是 62bf11f（全分支 review fix round）
```

**SDD ledger 已刪除**（fix round 完成、review 閘門解鎖後依流程清理，git-ignored、
非最終記錄）。完整過程記錄改查：commit `62bf11f` 的完整訊息（發現＋實際修法對照）、
以及本文件上方「✅ 已解決」區塊。想知道「某個 finding 為什麼沒修」查該區塊最後的
「已核閱但不採納」段落。

> 📌 **過程中兩次抓到計畫文字本身的缺陷，執行時當場修正並重新 commit 計畫檔**（不是
> implementer 的錯，是寫計畫當下沒發現）：
> 1. Task 3 原 Step 6 要求進入設定選單時只重置 `settingsScrollOffset`，但
>    `settingsCursor` 從無重置邏輯——兩者拆開會重新製造游標移出可見視窗的 bug，已裁決
>    跳過該步驟（commit `de97f59`）。
> 2. Task 5 原 Step 3 把 `ui_settings.cpp`（`lib/`，native + Arduino 雙軌編譯）的
>    `#ifdef ARDUINO` 分支模式鏡像進 `ui_screens.cpp`（`src/`，native build 整檔排除），
>    `#else` 分支在這裡永遠不可達、是死碼，已修正為直接呼叫（commit `55b63b5`）。
>
> 兩者都是 `dont-blindly-mirror` 原則的案例：把既有結構鏡像到新情境前，沒有先確認
> 新情境的實際流程是否真的對等。

> ⚠️ 本次工作在 `feat/phase-g-device-info` git worktree（`git worktree add
> .worktrees/phase-g-device-info -b feat/phase-g-device-info`，明確從本機 HEAD 分支，
> 沒用 harness 內建的 `EnterWorktree` 工具——那個工具預設 `baseRef=fresh` 會從
> `origin/main` 分支，而 origin/main 落後本機 main 99+ commit，會漏掉 Phase G/H 全部
> 既有工作）。續跑時 `cd .worktrees/phase-g-device-info` 或用 `EnterWorktree` 的
> `path` 參數切進去，不要在 `main` 上直接改。

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

### 3-B. 上機驗收清單（Task 5 已完成，現在可以執行——**這是本計畫唯一剩下的待辦**）

完整清單在計畫檔末尾（`docs/superpowers/plans/2026-09-01-phase-g-device-info.md` 「上機
驗收清單」段落），此處列摘要：

| 驗收項 | 步驟 | 預期結果 |
|---|---|---|
| 選單捲動流暢度 | 進系統設定，連續按 DOWN 8 次繞完整圈 | 畫面平滑捲動，無殘影/閃爍，游標高亮位置正確跟隨 |
| App連線設定／Type-C連線 placeholder | 游標停在第 6/7 項按主鍵 | 顯示「尚未實作」，任意鍵按下後回到系統設定選單，底部提示文字正確顯示「返回　系統設定」（不是舊的「返回　主功能表」） |
| 裝置資訊六欄正確性＋文字渲染（**含全分支 review CRITICAL 修復驗證**） | 進裝置資訊畫面 | 六列文字**從螢幕左邊界正常起排、白色（非深綠）、未被裁切**（這正是修復前的 bug 症狀：置中在 x=24 導致左半邊被裁掉）；名稱與裝置本身設定一致、型號固定顯示 EMS DoseSync Pro、序號與 App 端案件同步紀錄的 device_id 一致、韌體版本字串顯示「v0.7-phaseG」、電池%與充電狀態跟電池資訊畫面顯示一致 |
| 電池資訊畫面文字渲染（**同一個 CRITICAL 的既有 Phase H bug，本次一併修復**） | 系統設定 → 電池資訊 | 電量／電壓／充電狀態三列**從螢幕左邊界正常起排、白色、未被裁切**——這是 Phase H 就存在、從未被抓到的既有 bug，跟裝置資訊畫面同一個根因（`drawCenteredText()` 副作用），已隨本次修復一併解決 |
| 恢復預設對話框捲動後觸發 | 捲到選單中段（如第 6 項）長按主鍵 | 對話框正確顯示，視窗最後一格項目正確跳過繪製不與對話框重疊 |
| 裝置名稱捲出視窗行為 | 捲到選單底部（裝置資訊項）再捲回頂部 | 裝置名稱正確恢復顯示，鎖定/置灰狀態正確（若當下有未同步案件） |
| 裝置名稱超長截斷（新增，Task 5 fix round） | App 端把裝置名稱改成接近 31 bytes 上限（含中文） | 裝置資訊畫面「名稱」列不溢出螢幕，超長時尾端顯示「…」，不切斷 UTF-8 字元造成亂碼 |

全套 7 項目前皆**未執行**——本計畫全程無硬體，所有「已完成」結論建立在 native test 加
韌體編譯（靜態推理）之上，跟 Phase H 收尾時的狀態一樣。**其中「裝置資訊六欄正確性＋
文字渲染」與「電池資訊畫面文字渲染」兩項是本次全分支整合 review CRITICAL 的直接
驗收項，是這輪上機驗收裡優先度最高的兩項**——這個 bug 純渲染邏輯、native test 測不到、
編譯也不會報錯，唯一驗證管道就是上機肉眼看。

---

## 4. 已完成的 task

### Task 1 完成（2026-09-01）——捲動 clamp 共用純函式

**現況**：`firmware/lib/ui_scroll/ui_scroll.h`（`clampScrollOffset()`）+
`firmware/test/test_ui_scroll/test_main.cpp`（10 個 native test），commit `e5bb45d`
（2 輪 amend，最終版）。native test 634 cases / 633 通過（唯一 ERRORED 仍是既有的
`test_storage_hw`，與本次無關），韌體編譯 SUCCESS。

**Repo 自己的 Tier 3 codex review 抓到的問題**（跟 SDD 流程平行的獨立閘門，非本計畫
文件定義的 task-review）：

- **1 CRITICAL**：`clampScrollOffset()` 對 `visible_rows == 0` 會整數下溢（`uint8_t`
  的 `visible_rows - 1`），靜默回傳看似合法但錯誤的 offset；`offset + visible_rows`
  轉型 `uint16_t` 在 cursor/offset 接近 `UINT16_MAX` 時會回繞。fix round 1 修正：加
  `visible_rows == 0` guard（回傳 offset 不變）+ 改用 `cursor - offset >= visible_rows`
  避免加法溢位（不用模板/`static_assert`，維持函式內部直接處理，見下方 Ruling）。
- **14 IMPORTANT**：多數是我寫 Task 1 plan brief 時內嵌的範例程式碼本身沒做到位
  （doc comment 用完成式語氣宣稱「兩處呼叫點統一引用」但這個 commit 還沒有任何呼叫點、
  8 個測試函式缺 STEP/JSDoc、測試斷言用裸數字）——**Ruling: plan-mandated 缺陷**，
  已授權 implementer 偏離 plan brief 字面程式碼補齊 Global Constraints。

**三輪 fix 摘要**：
- **Round 1**（對 codex 的 1 CRITICAL + 14 IMPORTANT）：CRITICAL 結構性修正（確認正確）；
  doc comment 改未來式；STEP 01.01/02.01/03.01 補齊巢狀編號；測試檔全數補 JSDoc+STEP+
  部分具名常數；新增 2 個邊界測試（`visible_rows=0`、`UINT16_MAX` 附近溢位）。
- **Round 2**（controller 自行核對 diff 抓到的殘留）：`(visible_rows - 1)` 的 `1` 抽成
  `LAST_VISIBLE_INDEX_OFFSET`；doc comment 修正為準確描述目前的 guard 行為（不再宣稱
  「計算錯誤」）；兩處裸數字 `3` 抽成 `EXPECTED_OFFSET_AFTER_SCROLL_DOWN`。
- **Round 3**（SDD scoped re-reviewer `phase-g-task1-rereview` 額外抓到、round 2 未覆蓋
  的殘留）：`test_main.cpp:140` 的 `8`、`:155` 的 `0`、`:175` 的 `65531` 三處裸數字，
  分別抽成 `FIXTURE_VISIBLE_ROWS_FULL_LIST`／`INVALID_VISIBLE_ROWS_ZERO`／
  `EXPECTED_OFFSET_NEAR_MAX`。

**SDD 流程自身的三層驗證意見**（延遲約 15-20 分鐘才以 idle_notification 批次送達，
過程中一度誤判為「未回報」，見下方異常說明）：
- `phase-g-task1-review`（對 round 0／原始 commit `016c9e0` 的 task-review）：Approved，
  0 Critical/Important，獨立手算重新驗證全部 8 個原始測試案例數學正確。**唯一分歧**：
  它把 `visible_rows==0` 未防護標記為 Minor「不需要修」，跟 codex 的 CRITICAL 判定不同調——
  已按 codex（repo 強制閘門、機械判定）為準處理，未採用 SDD reviewer 這條「不用修」的
  建議。
- `phase-g-task1-rereview`（對 fix round 1／commit `6e0c144` 的 scoped re-review）：確認
  CRITICAL 結構性修正無誤；額外抓到 round 2 dispatch 時我自己漏掉的 3 個殘留裸數字
  （即上方 round 3 處理的三項），並確認 round 2 已把「明確指示錯誤」但實際靜默 fallback
  的矛盾註解修正掉。

**Rulings（供之後驗證/翻案參考）**：

1. `visible_rows == 0` 時該不該用更重的機制（模板 + `static_assert`）杜絕，還是函式內
   guard 靜默回傳 offset？**Ruling：函式內 guard，不用模板**——理由：這是一個內部
   helper，目前唯二呼叫端（Task 3 待接線的 `settingsScrollOffset`／`historyScrollOffset`）
   都傳編譯期常數（`SETTINGS_VISIBLE_ROWS=5`／`HISTORY_VISIBLE_ROWS=5`），非公開 API，
   模板化是過度工程化。**代價**：若未來有人以非常數 `visible_rows=0` 呼叫，會靜默拿到
   不動的 offset 而非明確錯誤訊號——風險評估為低（純 UI 捲動內部邏輯，非安全關鍵路徑，
   跟 EPI 劑量這類判斷不同量級）。
2. Plan brief 內嵌的範例程式碼（doc comment 用詞、測試檔缺 STEP/JSDoc/具名常數）算不算
   implementer 的缺陷？**Ruling：不算，是 plan 文字本身的缺陷**——Global Constraints
   是 binding authority，已授權 implementer 偏離 brief 字面程式碼補齊。**教訓**：日後
   writing-plans 階段把完整程式碼寫進 task brief 時，那段範例程式碼也要先過一次
   Global Constraints checklist，不能假設「brief 裡的程式碼一定合規」。

**已知異常（已解除，記錄供參考）**：SDD 流程自己的 task-reviewer 與 fix round 1 的
scoped re-reviewer 兩個 subagent 完成工作後（`ListAgents` 顯示 idle）長時間沒有
task-notification 送達，補發訊息詢問也沒有立即回應，一度判斷為「未回報」並改用其他
驗證管道收工 Task 1。約 15-20 分鐘後兩者的報告才以 `idle_notification` 批次送達（連同
補發訊息的回覆一起），內容完整且發現了 round 2 遺漏的 3 個殘留（已在 round 3 處理）。
**結論：不是遺失，是嚴重延遲**——已用 `SendFeedback` 記錄這個延遲異常。教訓：往後遇到
subagent 顯示 idle 但報告遲遲不到，除了補發訊息詢問，也該考慮再多等一段時間再改用
備援驗證手段，避免像這次一樣兩條路徑分別跑（雖然最後互補、沒有造成錯誤結論，但多花了
一輪不必要的 controller 自行核對）。

### Task 2 完成（2026-09-01）——設定選單擴充至 SoT §19.1 完整 8 項並支援捲動

**現況**：`firmware/lib/ui_settings/ui_settings.h`／`.cpp`（8 項 `kSettingsMenuItems[]`、
捲動視窗迴圈、`scroll_offset` 參數）+ `firmware/test/test_settings_ui/test_main.cpp`
（21 個 test case），commit `c980927`（1 輪 amend，最終版）。native test 637 cases /
636 通過（唯一 ERRORED 仍是既有的 `test_storage_hw`，與本次無關），韌體編譯 SUCCESS，
Flash 72.7%（較 Task 1 前 71.4% 微增）。

**Repo 自己的 Tier 3 codex review 抓到的問題**（跟 SDD 流程平行的獨立閘門）：

- **5 CRITICAL，其中 4 個同源**：`firmware/src/main.cpp:1177` 仍用舊 4-arg 呼叫
  `drawSettingsMenu(settingsDisp, settingsCursor, g_device_name_locked,
  settingsRestoreConfirm)`。新簽名插入 `scroll_offset` 於第 3 位，這個舊呼叫編譯通過
  （bool→uint8_t 隱式轉換 + 尾端預設值）但靜默錯位：`g_device_name_locked`→
  `scroll_offset`、`settingsRestoreConfirm`→`device_name_locked`、`restore_confirm`
  永遠吃預設 `false`。實機上恢復預設對話框永遠不顯示、名稱鎖定狀態錯、選單不會捲動。
  第 5 個 CRITICAL（silent-failure）是獨立問題：`drawSettingsMenu()` 的捲動迴圈信任
  呼叫端已執行過 `clampScrollOffset()`，越界 `scroll_offset` 會靜默畫出空白選單。
- **12 IMPORTANT**：多為 STEP 編號未跟著 Step 4 的迴圈重寫更新（STEP 04→03 殘留）、
  多處註解過時（Y 座標常數說明、`drawPlaceholder()` 已接線的錯誤暗示、JSDoc 宣稱呼叫端
  已算好 scroll_offset）、`SETTINGS_ITEM2/3/5_Y` 死碼未清、捲動測試斷言力度不足（只查
  文字有無畫出，未驗證 Y 座標）。

**Ruling（推翻我自己先前的裁決）**：main.cpp 那 4 個 CRITICAL 的根因（呼叫點錯位）**team-lead
裁定必須當場修，不留給 Task 4**——這推翻了本文件先前版本、以及 Task 1 pre-flight scan 對
「2→4」依賴關係下的「Clean」判斷。理由：(a) 本專案 GLOBAL-MUTATION 規則要求修改共用函式簽名
時同一改動內搜尋並更新全部呼叫點，不能拖到兩個 task 之後；(b) 一個編譯過但實機行為全錯的
commit 留在分支歷史裡是真實風險，這個 worktree 隨時可能被燒錄到硬體測試；(c) 修法不需要
提前接 Task 3 的範圍（`settingsScrollOffset` 全域與按鍵驅動的更新邏輯仍歸 Task 3）——只需
把兩個既有值重排到新位置，`scroll_offset` 傳字面值 `0`（跟目前尚不能捲動的行為完全等價，
零迴歸，零 scope creep）。第 5 個 CRITICAL（呼叫端信任 clamp 過的 offset）**裁定接受、不修**，
跟 Task 1 `clampScrollOffset()` 的 `visible_rows==0` guard 是同一種已核准的 trade-off——
內部 helper、目前所有呼叫端不是編譯期常數就是走 `clampScrollOffset()`，迴圈邊界
（`i < SETTINGS_MENU_ITEM_COUNT`）本身已避免真正的越界記憶體存取。12 個 IMPORTANT 全部
授權修正（非 plan-mandated，是 implementer 自我審查真的漏掉的缺口）。

**Fix round 1 摘要**：main.cpp 依裁定的字面順序修正；`input_handler.cpp`／`app_globals.h`／
`ems_display_snapshot.h` 三處游標範圍註解更新為誠實描述現況（0~7，但 5~7 尚未接線）；
`ui_settings.cpp` STEP 04→03 全部改回正確編號並補齊巢狀 STEP（03.02/03.03/03.03.01/
03.03.02/03.04/03.04.01）；`SETTINGS_ITEM2/3/5_Y` 死碼移除（grep 全庫確認無殘留引用）；
`drawPlaceholder()`／JSDoc 誇大現況的措辭改為誠實的「未來規劃」；6 個測試函式補 STEP 註解；
`test_scroll_offset_three_shows_last_five_items` 強化為逐項斷言 5 個可見列的 Y 座標
（30/70/110/150/190）與高亮 fill_rect 的 Y。修正後 focused 21/21、full suite 636/637、
ESP32 build SUCCESS（Flash 大小不變，純參數順序 + 註解修正）。

**SDD task-reviewer（`phase-g-task2-review`）**：Approved，0 Critical/Important，1 Minor
（`STEP 03.03`／`03.04` 的 `if (selected) fill_rect(...)` 重複樣式——brief 原文就是這樣寫、
不算本 task 缺陷，已知的未來清理項見 §5「⑨」）。獨立 grep 驗證 `drawSettingsMenu()` 全庫
只剩這一個非測試呼叫點、零殘留舊符號引用；獨立核對新增中文字（連/線/設/定/裝/置/資/訊）
確實已存在既有畫面字串中，佐證「VLW byte-identical、0 缺字」的說法不只是信implementer報告。

**過程中發現的流程缺口（已記錄 feedback memory）**：implementer 第一次 commit（`a750a5c`）
觸發 repo 的 Tier 3 codex-review 閘門（PostToolUse hook 寫 marker + 印 systemMessage 軟指派
`Skill(commit-review)`），但 implementer 沒有執行這個 skill——它接著在自我審查時想疊第二個
commit，撞上 `commit-gate-guard` 的 PreToolUse deny，正確地改用 `git commit --amend` 繞過，
但從未真正跑過那輪 review。這個 marker 就這樣卡在磁碟上，直到 team-lead 自己想結束回合時
被 `stop-review-guard`（Stop hook）攔下才發現（`~/.claude/state/pending-review/` 有一個
未清的 marker，`ps`／codex-review state 目錄都查無任何背景 process 在跑）。team-lead 手動
用 `commit-review` skill 補跑 `compute-tier.ts` + `codex-review.ts --tier=3`，找出上述 5
CRITICAL + 12 IMPORTANT，裁決、派 fix round、驗證、`clear-pending-review.ts` 解鎖，才讓
自己的回合能正常結束。**教訓**：dispatch 會在啟用此 gate 的 repo 內 commit 的 subagent 時，
brief 必須明講這個機制存在並要求「收到 systemMessage 指派就要執行」，否則 marker 會安靜
卡住直到某個 session 的 Stop 事件命中它——已存為
`feedback_sdd_dispatch_must_mention_commit_gate.md`，Task 3 起的 dispatch prompt 應該
把這段提醒加進去。

### ✅ 已解決（2026-09-02）：amend round 從未跑過完整 codex Tier 3

使用者事後追問「Task 1 跟 Task 2 是否都完整跑過 commit-review」時查出的落差（見下方
「原始記錄」），2026-09-02 本次 session 已對 Task 1／Task 2 的最終 commit 補跑完整 6
面向 review，兩者皆真的抓到新問題，已修復並 amend：

- **Task 1**（原最終 commit `e5bb45d`，補跑後 amend 為 `3c46a31`）：codex 兩面向
  （silent-failure、types）+ agent 四面向（rules/code-review/comments/tests，codex 服務端
  持續性 capacity 錯誤、經使用者同意改派 agent 引擎）皆已收齊。找到 1 個 IMPORTANT：
  `assert()` 在 `-DNDEBUG` 建置下會被完全編譯掉，屆時 `visible_rows==0` 會算出比原始
  bug 更糟的垃圾值（`cursor+1`）——已改用不受 `NDEBUG` 影響的 `abort()`，並用獨立編譯
  （含 `-DNDEBUG` 變體）交叉驗證修復生效。另修 8 個既有的 IMPORTANT 措辭問題與 2 個
  MINOR（`waitpid()` EINTR 處理、死亡測試哨兵值比對）。
- **Task 2**（原最終 commit `c980927`，補跑後 amend 為 `1b93baa`）：codex 服務端同樣
  capacity 錯誤，6 個面向全改派 agent 執行。找到 1 個 IMPORTANT：游標與捲動視窗的成對
  更新邏輯（`input_handler.cpp` BTN_UP/BTN_DOWN）完全沒有測試涵蓋（`src/` 被 native
  build 排除），唯一防線是註解警告——已抽成 `advanceSettingsCursorAndScroll()` 純函式
  移到 `ui_settings.h`，讓呼叫端拿不到「只更新其中一個」的機會，並在 `test_settings_ui`
  補 5 個涵蓋視窗內移動／下捲觸發／連續下捲／wrap 回頂端／wrap 到底端的測試。另修
  `uint16_t`/`uint8_t` 隱性窄化（3 個獨立審查者都各自抓到同一處）與誤導性的 commit
  歸因註解。**未修**：`settingsCursor`/`settingsScrollOffset` 這對不變量仍只靠慣例維持
  （建議 Task 3 抽成共用 `ScrollCursorState` 型別，跟 `historyCursor`/`historyScrollOffset`
  一起統一）；恢復預設對話框跟捲動後的游標高亮格重疊（c980927 既有小 bug，觸發面從
  1/8 擴大到 4/8，純視覺）——兩者記錄為殘餘風險，見下方 §5。

驗證：native test 643 cases 全綠（含新增 6 個 test），ESP32 build SUCCESS Flash 72.8%。

**方法論教訓**：codex CLI（ChatGPT 帳號）本次遭遇持續性 "model at capacity" 錯誤，6 次
重試橫跨 20+ 分鐘均失敗，且該帳號不支援 `--model=` 換用其他模型繞過（`gpt-5` 被拒絕：
"not supported when using Codex with a ChatGPT account"）。經使用者明確指示，改派對應的
Claude subagent（pr-reviewer lite + pr-review-toolkit 五個 agent）執行卡住的面向，兩輪
都做到 6/6 收齊、無降級——證明 `engine=agent` 是 codex 服務端不可用時的可行備援路徑，
即使 marker 上記錄的 `engine` 欄位仍是 `codex`（`clear-pending-review.ts` 只檢查
`--aspects-done=N` 是否達標，不檢查實際引擎）。

<details>
<summary>原始記錄（2026-09-01，補跑前）</summary>

使用者事後追問「Task 1 跟 Task 2 是否都完整跑過 commit-review」時查出的落差，記錄於此、
本次不補跑，留給下次接手或下次 whole-branch review 時決定是否要補。

**事實**（查 `~/.claude/state/pending-review/unlock-audit.log` 逐筆核對，非憑印象）：兩個
task 全程只有各自的**第一次** commit 真正跑過完整 6 面向 codex Tier 3 review 並解鎖——
`016c9e0`（Task 1）與 `a750a5c`（Task 2），各自 `expected=6 done=6 result=OK`。之後**所有**
修 bug 的 amend（Task 1 的 `6e0c144`／`e5bb45d`／`1f924b4` 三輪；Task 2 implementer 自我審查
的 `815c8d8`、以及 team-lead 裁決 fix round 1 的 `c980927`）都沒有再跑過這套機制的第二輪。

**根因**（讀 `~/.claude/scripts/post-commit-review.ts` 原始碼確認）：
```ts
const skipMarker = /\[skip-review\]/i.test(command) || /--amend/.test(command);
```
commit 指令只要含 `--amend`，hook 就不寫新的 pending-review marker——仍會印 systemMessage
建議跑 `/commit-review`，但沒有 PreToolUse/Stop 兩層機械閘門背書，純軟性建議，跟
`feedback_sdd_dispatch_must_mention_commit_gate.md` 記錄的那個「implementer 沒理會
systemMessage」是同一種訊息、同一種沒人接住的落差——差別是這次連 team-lead 自己（我）
在裁決並派 fix round 時也沒注意到 `git commit --amend` 這個動作本身會讓新一輪的修正完全
繞過機械閘門。

**這些 amend 實際上靠什麼把關**（不是機械閘門，是兩條獨立的替代驗證路徑）：
1. Controller（我）自行逐行讀 diff——兩個 task 的每一輪 amend 我都有做，細節見上方各輪
   fix round 摘要。
2. SDD 流程自己的 task-reviewer／re-reviewer subagent（`phase-g-task1-review`／
   `phase-g-task1-rereview`／`phase-g-task2-review`）——對 amend 後的最終 diff 給出
   Approved，是跟 codex Tier 3 平行、獨立存在的第二套機制，不是同一套機制的重跑。

**使用者裁示**：不現在補跑，先記錄到本文件，下次接手時再決定要不要對 Task 1／Task 2 的
最終 commit（`1f924b4`/`77c5bba` 與 `c980927`/`f9982b6`）補一次完整 6 面向 codex Tier 3
（`bun ~/.claude/scripts/codex-review.ts --tier=3 --target=<commit>`）。**風險評估**：兩層
替代驗證都已跑過且都是 Approved，殘餘風險偏低，但 codex Tier 3 過去確實抓到過 SDD
reviewer 沒抓到的 CRITICAL（Task 1 的整數下溢／溢位、Task 2 的 main.cpp 呼叫點錯位皆是
codex 先抓到，SDD reviewer 對前者只標 Minor）——若要徹底排除疑慮，仍以補跑一次最省事。

</details>

### Task 3 完成（2026-09-02）——`input_handler.cpp` 新游標分派

**現況**：`firmware/src/input_handler.cpp`／`app_globals.h`／`main.cpp`，最終 commit
`928692c`（1 輪 amend）。native test 643 cases / 642 通過（本 task 只動 `src/`，native
build 排除，數字理論上不變，這步是確認沒有連帶弄壞），ESP32 build SUCCESS Flash 72.8%。

**Pre-flight scan 發現**：計畫原 Step 1/2/4 的「捲動」半部分已被同日稍早的 Task 2 補跑
CRITICAL 修復提前做完（見上方「已解決」段落），計畫文字已瘦身避免重工（commit
`de97f59`）。Step 6（進入選單重置 `settingsScrollOffset`）是計畫本身的缺陷——裁決跳過，
理由見 §2 上方提示框。

**Review**：repo Tier 2 codex gate 抓到 2 CRITICAL（`updateDisplay()` 尚未渲染新游標對應
的畫面——這是刻意留給 Task 4 的缺口，不是本 task 的錯）+ 1 IMPORTANT（`STEP 03.5` 應
整段重排為 `STEP 04`，1 輪 fix 修正）；SDD task-reviewer Approved，同一個 STEP finding
獨立確認（標記為 plan-mandated，即計畫文字本身的缺陷）。implementer 自行找到並修正一個
brief 未提到的缺口：`onLongPress()` 的恢復預設對話框守衛漏了排除 `settingsDeviceInfoMode`
（同電池資訊既有 pattern）。

### Task 4 完成（2026-09-02）——DisplaySnapshot 接線 + `updateDisplay()` 渲染分派

**現況**：`firmware/lib/ems_display_snapshot/ems_display_snapshot.h`／`src/main.cpp`／
`test/test_display_snapshot/test_display_snapshot.cpp`，最終 commit `d5b33e6`（1 輪
amend）。native test 645 cases / 644 通過（`test_display_snapshot` 57→59，+2），ESP32
build SUCCESS Flash 72.8%——**這是 Task 3 完成後 repo review 抓到的 2 個 CRITICAL 的下半場**：
新增 `settingsDeviceInfoMode` 分派與兩個 placeholder 全域狀態的實際畫面渲染。

**Review**：repo Tier 3 codex gate 1 CRITICAL（`drawDeviceInfo()` 未定義，韌體無法獨立
編譯——計畫本身的前向引用，Task 5 完成後自動解決，不是缺陷）+ 10 IMPORTANT（2 個真的要修：
STEP 編號、測試檔 flag 總數標題殘留舊數字；其餘 8 個逐一驗證後確認吻合這個檔案既有的
大量前例——`if` 無大括號的 flag 映射寫法、`DisplaySnapshotInputs` 循序賦值、Group 3
測試零文件慣例，這些都是整個 DisplaySnapshot pattern 本來就有的既有寫法，不是本次新增的
問題）。implementer 自行補了一個 brief 沒明講的加固：既有的 flag 總數斷言測試同步更新，
避免未來新增/移除 flag 忘記同步。SDD task-reviewer Approved，獨立確認同一個「if 無大括號」
finding 也是既有慣例。

### Task 5 完成（2026-09-02）——`drawDeviceInfo()` 畫面本體

**現況**：`firmware/src/ui_screens.cpp`／`app_globals.h`／字型產物
（`ems_zh_24_vlw.h`、`ems_zh_24.vlw`），最終 commit `43b40a8`（1 輪 amend）。native test
645/644（本 task 不新增 native test，跟既有 `drawBatteryInfo()` 等所有 `ui_screens.cpp`
畫面函式同一慣例——`src/` 整檔排除 native build）。VLW 字型重生 0 缺字（新增「名稱」／
「型號」／「序號」／「韌體」四字）。ESP32 build SUCCESS Flash 73.0%——**韌體第一次真正
編譯成功**（Task 4 完成當下的 commit 因 `drawDeviceInfo()` 前向引用編不過，是計畫設計
好的序列狀態）。

**Pre-flight scan 發現**：計畫 Step 3 原始程式碼把 `ui_settings.cpp`（`lib/`，native +
Arduino 雙軌編譯）慣用的 `#ifdef ARDUINO` / `mock_fs_read` 分支模式鏡像進
`ui_screens.cpp`（`src/`，native build 整檔排除）——`#else` 分支在這個檔案永遠不可達，
是死碼。已修正為直接呼叫（commit `55b63b5`），`dont-blindly-mirror` 原則案例。

**Review**：repo Tier 3 codex gate 1 CRITICAL + 7 IMPORTANT。CRITICAL（`settings_get_
device_name()` 把 LittleFS 掛載/讀取失敗靜默偽裝成「未命名」的合法預設值）**驗證後
確認是 Task 2 就已存在的既有缺陷**（`ui_settings.cpp:152` 的 `drawSettingsMenu()` 也是
同一種不檢查回傳值的呼叫方式）——裁決不在本 task 修，記錄為殘餘風險（見 §5）。7 個
IMPORTANT 中 4 個真的要修：STEP 巢狀編號（`STEP 07.01`／`08.01~03`）、Y 座標 magic
number 改具名常數、與 `drawBatteryInfo()` 重複的充電狀態文字判斷抽成共用
`chargeStateText()` helper、裝置名稱超長時用 `display.textWidth()` 做 UTF-8 安全截斷
（1 輪 fix，re-reviewer 逐字元手算驗證截斷迴圈正確性）；其餘 3 個判定為既有慣例（無
native test）或低價值（函式註解未明講「無參數」）不修。implementer 額外用獨立 g++ 測試
harness 驗證截斷邏輯的 5 個案例（含長中文名稱），才提交 commit。

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

**⑫ `settingsCursor`/`settingsScrollOffset` 成對更新只靠慣例（2026-09-02 補跑 review 發現）**：
兩個獨立 `extern` 全域變數的「必須成對更新」不變量沒有型別層級保證，目前只有
`input_handler.cpp` 的兩個 UP/DOWN 分支會寫入（已改用 `advanceSettingsCursorAndScroll()`
讓這兩個呼叫點不會出錯）。**更新（2026-09-02，Task 3 完成後）**：Task 3 新增的
`BTN_PRIMARY` 對 cursor 5~7 分派（進 `settingsEditorMode`/`settingsBatteryInfoMode`/
`settingsDeviceInfoMode`/`globalState` 等旗標）不寫 `settingsCursor` 本身，所以這個
不變量目前仍只有那兩個 UP/DOWN 分支在維護，沒有被 Task 3 擴大成新的潛在寫入面。跟既有
的 `historyCursor`/`historyScrollOffset`（同樣兩個獨立全域、同樣的不變量問題）仍是同一類
待統一問題，**未來若要新增第三個會寫 `settingsCursor` 的分支**，建議屆時一併抽成共用的
`ScrollCursorState{cursor, offset}` 型別，兩處一次收斂——本計畫全部 6 個 task 都沒有
新增這樣的分支，故未觸發，繼續 park。

**⑬ 恢復預設對話框覆蓋捲動後的游標高亮格（既有 bug，本次擴大觸發面）**：
`drawSettingsMenu()` 的 `restore_confirm && is_last_visible_slot` 邏輯會跳過視窗內最後
一格讓位給對話框文字；捲動接上真實 offset 後，只要游標落在 4/5/6/7（對應 offset
0/1/2/3）都會撞上這個判斷，游標所在那一列的高亮與文字同時消失（c980927 就有這個
bug，當時只影響 cursor=4 這一種情況，是 1/8；本次接上真實捲動後擴大為 4/8）。純視覺
困擾，不影響按鍵分派邏輯，本次未修（超出「最小必要修復」範圍）。若要修，方向是讓
對話框改用非選單格的獨立 Y 座標。

**⑭ `settings_get_device_name()` 把儲存層故障靜默偽裝成「未命名」（2026-09-02 Task 5
補跑 review 發現，既有缺陷非本次新增）**：`lib/ems_settings/ems_settings.cpp` 的
`settings_get_device_name()` 在 LittleFS 未掛載或開檔失敗時，回傳值與「檔案確實不存在
（合法的尚未設定狀態）」相同——都是回傳一個預設「未命名」字串，呼叫端無從分辨。系統
允許 LittleFS mount 失敗後繼續開機，所以使用者會看到看似正常的名稱，完全不知道持久化
層已故障。目前有 2 個呼叫點都是這樣呼叫（不檢查回傳值）：`ui_settings.cpp:152`
（`drawSettingsMenu()`，Task 2 既有）與 `ui_screens.cpp`（`drawDeviceInfo()`，本次
Task 5 新增，是照抄既有呼叫慣例，不是新引入的問題）。治本修法：`settings_get_device_
name()` 的契約要能區分「檔案不存在」與「檔案系統未掛載／讀取失敗」兩種失敗，前者才
回預設值，後者要讓兩個呼叫端都能顯示明確的錯誤/不可用狀態——這是一個獨立的小 task，
會動到共用函式契約 + 兩個呼叫端，本次計畫範圍內未處理。

---

## 6. 對應文件

| 文件 | 用途 |
|---|---|
| `docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md` | 設計 spec（決策記錄 + 架構） |
| `docs/superpowers/plans/2026-09-01-phase-g-device-info.md` | 實作計畫（6 task，TDD 步驟） |
| `docs/pm-dev-spec.md §四 Phase G` | 高層 Phase 描述（已於 Task 6 標記完成，2026-09-02） |
| `docs/superpowers/phase-h-handover.md §3-A7` | 本次工作範圍的裁決背景（上一階段文件） |
| `.worktrees/phase-g-device-info/.superpowers/sdd/2026-09-01-phase-g-device-info/progress.md` | SDD ledger——每個 task 的 pre-flight scan、review 發現、fix round、ruling 完整記錄（worktree 內 git-ignored，換機器要重建） |
