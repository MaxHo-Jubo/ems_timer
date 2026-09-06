# tasks/todo — 系統設定選單修正（2026-09-06 起，跨 session）

> 狀態（2026-09-06 更新）：**T1～T5 已完成並 commit，韌體已燒錄上機**。
> T6 上機驗收部分通過（選單結構／字型／編輯器開關字樣），**聲音 gate 與持久化仍待驗**；
> T7（Test Plan V1 同步）未開工。各項細節見下方各段。
>
> 早期狀態備忘（2026-09-06 上午，已不再適用，保留供追溯）：當時只完成 #1，且判斷
> 「這片板子只能電池供電開機、USB 只能燒錄，主韌體改動要換新板才驗得到」——實際上
> 當天稍後直接以 USB 燒錄並上機驗收成功，該限制沒有成立。硬體診斷全貌見 memory
> `project_usb_power_conflict_incident_2026_09_05`。

## 使用者拍板決策（2026-09-06）— 需記進 SoT §19（見 T5）

1. **螢幕亮度 → 從設定選單移除**
   根因：背光（BL）焊死在 3.3V 常亮，沒接任何可控 GPIO（原要用的 GPIO 1 被 TFT DC 佔走），
   `setBrightness()` 只存 `s_brightness`、沒有任何 PWM 驅動硬體 → 韌體完全改不動亮度。
   決策：直接從選單移除，不留誤導。底層 NVS/getter/setter 可保留（無害），只移除選單面向。

2. **系統音量 → 只補死接線（維持現狀級數，不做真正大小聲）**
   根因：`getSystemVolume()` 目前**沒有任何蜂鳴程式碼在讀**（死設定）；蜂鳴器是
   `digitalWrite` 開/關驅動（主動式，單一固定音量），改 duty 也改不了大小聲。
   決策：把系統音量接進 UI 提示音路徑當 gate；**危急警報（`triggerBeep(255,...)` / EPI /
   ALARMING）絕不受 gate**（醫療安全）。注意 spec §19.4 目前寫「系統音量不可靜音」
   （`SETTINGS_VOLUME_MIN=1`），若不放寬 MIN，接線後實際仍恆發聲——這點要在 SoT 一併釐清
   （見 T5：是否放寬 §19.4 允許 0=靜音，否則此接線只是「非死碼、前向相容」而無可聞差異）。

3. **通氣音量 → 改成開/關，不要 1~5（本 session 新增決策）**
   現況：`decideVentOutput()` 本來就是 `volume > 0` 當 gate（功能上已是開/關），只是
   UI 用 1~5 級編輯器 + `VENT_VOLUME_MAX=5` 呈現。
   決策：把呈現與值域改成開/關（0/1）。

## 進度

- [x] **#1 裝置名稱列「未命名」壓在標籤上** — `lib/ui_settings/ui_settings.cpp`
  `SETTINGS_VALUE_X_OFFSET` 70→120（標籤「裝置名稱」4 中文字 ×~26px≈104 + 一格）。
  已補推導註解；`pio test -e native -f test_settings_ui` **31/31 綠**。
  ⚠️ 偏移仍是估算（無 textWidth、vlw 變寬字），實機截圖再微調。

- [x] **T2 移除「螢幕亮度」選單項**（2026-09-06）
  改動：`ui_settings.h`（刪 `SETTINGS_CURSOR_BRIGHTNESS`、其後 renumber 至 0~6、
  `SETTINGS_MENU_COUNT` 8→7、get/setBrightness doc 補「為何保留」）、`ui_settings.cpp`
  （`kSettingsMenuItems[]` 刪列、全檔 8→7 註解校正）、`input_handler.cpp`（`kSettingsSlots[]`
  刪列、BTN_PRIMARY 可調範圍下界改 `SETTINGS_CURSOR_SYSTEM_VOL`）、`main.cpp`（編輯器
  分派刪亮度分支、`settingsCursor` 初值改具名常數 `SETTINGS_CURSOR_SYSTEM_VOL`）、
  `app_globals.h` / `ems_display_snapshot.h` 註解範圍 0~7→0~6。
  測試：`test_settings_ui` 由 31 改為 30 cases（刪 `test_g13_brightness_in_range`，
  改名 3 個 cursor 測試，新增「螢幕亮度不得出現」「不得再有 8/8 頁碼」兩條反向斷言）。
  驗證：`pio test -e native -f test_settings_ui` **30/30 綠**；全套 native **657/658**
  （`test_storage_hw` ERRORED 為既有編譯錯誤，與本次無關）；
  `pio run -e esp32-s3-devkitc-1` **SUCCESS**（Flash 72.7% / RAM 33.4%）。
  未重生 .vlw：本次只移除中文字串、未新增，字集無需更新。

- [x] **T3 + T4 兩個音量改開/關並真正接線**（2026-09-06，合併執行）

  **使用者當日追加拍板（覆寫原決策 2「維持現狀級數」）**：系統音量也改成開/關（0/1）。
  理由：主動式蜂鳴器只有 `digitalWrite` 響/不響，1~5 級在硬體上毫無差別，級數是假的。

  **執行中發現、經使用者拍板一併修掉的既有 bug**：設定選單的「通氣音量」寫的是
  `ui_settings.cpp` 的 `s_vent_volume`，而通氣節奏實際用的是 `app_globals.h` 的
  `ventVolume` 全域，兩者從未同步 → **在設定選單裡調通氣音量對節奏完全沒有作用**。
  修法採「消除重複，只留一份」：刪掉 lib 的副本，`kSettingsSlots` 的 vent 列改用
  `input_handler.cpp` 內直接讀寫 `ventVolume` 的 slot getter/setter，開機由 NVS 灌入。

  改動：
  - `lib/ems_settings/ems_settings.h`：`SETTINGS_VOLUME_MIN` 1→0、`MAX` 5→1、`DEFAULT` 3→1；
    `SETTINGS_VENT_VOLUME_MAX` 5→1、`DEFAULT` 3→1。原設計以工程變更註記保留。
  - `lib/ems_vent/ems_vent_metronome.h`：`VENT_VOLUME_MAX` 5→1、`DEFAULT` 3→1。
  - `src/input_handler.cpp`：新增 `uiConfirmBeep()`（`getSystemVolume() > 0` 才響）取代
    4 個裸寫的 `triggerBeep(1,80,0)`（Amiodarone／補登／EPI／電擊「已紀錄」）；新增
    `getVentVolumeSetting()`/`setVentVolumeSetting()` 直接讀寫 `ventVolume`；新增 3 條
    `static_assert` 鎖住 `VENT_VOLUME_*` 與 `SETTINGS_VENT_VOLUME_*` 兩份常數一致。
  - `lib/ui_settings/`：移除 `s_vent_volume`/`getVentVolume()`/`setVentVolume()`；數值版
    `drawSettingEditor()` 移除（已無呼叫點），改為 `drawToggleEditor(disp, title, enabled)`；
    新增 `settingsToggleLabel(bool)` 與「開」「關」字串（.h 內，與通氣畫面共用）。
  - `src/main.cpp`：開機 `ventVolume = g_settings_state.vent_volume`；編輯器改
    `drawToggleEditor`。
  - `src/ui_screens.cpp`：通氣畫面「音量 N/5」→「音量 開/關」、提示文字改「上/下 開關」。
  - `scripts/regen_vlw.sh`：`SRC_FILES` 補 `lib/ui_settings/ui_settings.h`（新的「開」「關」
    定義在 .h，原 allowlist 只列 .cpp → 會靜默漏字）。

  **危急警報未受影響**：`triggerBeep(255,...)`（ALARMING）、EPI 到期警示、通氣節奏音
  都不走 `uiConfirmBeep()`。

  測試：`test_settings`（+3 案，邊界改 0/1）、`test_vent_metronome`（clamp 邊界改 0/1、
  測資 vol=3→1）、`test_settings_ui`（g15/g16 去掉 vent 斷言、g18 改測 toggle editor 兩個方向）。

  驗證：全套 `pio test -e native` **658/659**（`test_storage_hw` 為既有編譯錯誤）；
  `pio run -e esp32-s3-devkitc-1` **SUCCESS**（Flash 72.8% / RAM 33.4%）；
  `bash scripts/regen_vlw.sh` 重生字型 345→**346 glyphs**，並以解析 .vlw binary 的
  方式驗證「開／關／上下鍵切換／通氣音量可由上下開關」全部在字集內、既有選單字未被
  union 掉，header `len=145803` 與 .vlw 位元組數一致。

- [x] **T5 — 決策記進文件（保留原設計、疊加工程變更）**（2026-09-06）
  - `docs/EMS_DoseSync_Pro_Prototype_V1.md`（SoT V1）§19.1／§19.3／§19.4／§19.5／§19.6
    各加一個「⚙️ 工程變更 2026-09-06」區塊，原 PM 規格文字一字未刪。§19.4 另附一張
    「哪些音效受開關影響」的對照表，並點名原設計「系統音量控制 OHCA 警報音／EPI 預警音」
    在韌體上不成立。
  - `docs/pm-dev-spec.md` §15 設定表下方加同型工程變更表，另在 §通氣節奏、Phase 清單
    共 3 處加指回 §15 的一行註記。

## 剩餘工作

### T6 — 上機驗收（**進行中**，2026-09-06 已燒錄新韌體到板上）

韌體已燒進板子（`pio run -e esp32-s3-devkitc-1 -t upload`，port `/dev/cu.usbmodem1101`）。
板上版本 = **commit `da6f6cf`**，也就是**含 codex review 三個 CRITICAL 修正之後**的版本
（開機 log 佐證：`[FONT] vlw loaded: 145803 bytes`、`[SETTINGS] NVS loaded: brt=1 vol=1 vvol=1`）。
**下次接手不必重燒**，除非又改了程式碼。

**收 serial log 的工具**：`firmware/scripts/serial_monitor.py`
```bash
cd firmware && python3 scripts/serial_monitor.py /tmp/ems_serial.log
```
要拿開機 log 就請人按板上 RST 鍵：腳本靠「USB CDC 重新列舉」偵測並自動重連，畫面會出現
`=== detached ===` 接著 `=== attached ===`。**若按了 RST 遲遲沒出現那兩行就直接拔插 USB**
——重連依賴 host 真的看到 detach，不保證每次都成立，不要空等。

**不要改成自己 toggle RTS 去 reset**：這片是 native USB-JTAG、沒有 UART bridge，那樣做
不保證有效，在有 bridge 的板子上則會把正在被操作的板子重開。另注意 pyserial 光傳
`dsrdtr=False` 並不等於不碰控制線（反而保證 open 時拉高 DTR/RTS），腳本已改成先建
未開啟物件、設 `dtr/rts = False` 再 open，理由寫在腳本檔頭。

> ⚠️ **log 能佐證什麼、不能佐證什麼**：`[REDRAW]` 只印 `globalState / ohcaState /
> mainMenuCursor / countdownSec`，**不印 `settingsCursor` 與 `settingsEditorMode`**，
> 所以 log 只能證明「人在系統設定畫面（gs=5）」，分不出在選單還是編輯器，畫面上顯示
> 什麼字更是完全看不到。**畫面內容與聲音只能靠人回報**。若要讓下次驗收有機器可讀的
> 證據，得在 `main.cpp` 的 REDRAW printf 補印 settingsCursor／editorValue（需重燒）。

#### ✅ 已驗證通過（2026-09-06，使用者回報「都正確」）
log 佐證：31.6~35.7s 期間 7 次 `gs=5` 重繪；重開機後 `[SETTINGS] NVS loaded: brt=1 vol=1 vvol=1`。
- [x] 設定選單只剩 **7 項**，「螢幕亮度」已不在選單上
- [x] 右上角頁碼顯示 `N/7`（不是 `N/8`）
- [x] 上下捲動到最後一項「裝置資訊」正常，無卡住／跳空
- [x] 裝置名稱列「未命名」不再壓到標籤（#1 的 `SETTINGS_VALUE_X_OFFSET` 70→120 生效）
- [x] 系統音量編輯器顯示「開」與提示「上下鍵切換」，**沒有 ▯**（字型子集正確）
- [x] 上/下鍵切換「開」↔「關」畫面即時更新

#### ⏳ 待驗：聲音 gate（使用者 2026-09-06 決定延後，步驟照抄即可）
確認音只有 80ms，建議在安靜環境驗。

**2-1 確認音該被關掉**
- [ ] 系統設定 → 系統音量切「關」→ 返回主選單
- [ ] 進 OHCA → 按**電擊鍵兩次**（兩段確認）→ 預期**無嗶聲**，但螢幕照跳「電擊已紀錄」
- [ ] 按 **EPI 鍵兩次** → 預期同樣無嗶聲，畫面「EPI 已紀錄」照常

**2-2 確認音該回來**
- [ ] 系統音量切回「開」→ 再按一次電擊兩段確認 → 預期**嗶一聲**

**2-3 醫療警報不受 gate（最關鍵的一條，醫療安全）**
- [ ] 系統音量設「關」
- [ ] 主選單 → **Training 模式 → 選 30 秒週期**（不要用 OHCA，EPI 倒數要等 4 分鐘）
- [ ] 等倒數到 0 → 預期**警報照響**。走的是 `triggerBeep(255,...)`，刻意沒經過
      `uiConfirmBeep()` 的 gate；若這裡被靜音了就是 CRITICAL，必須立刻修

#### ⏳ 待驗：通氣音量（驗的是本次修掉的那個 bug）
- [ ] **設定選單**把通氣音量設「關」→ 返回 → 進 6 秒通氣節奏 → 預期**確實靜音**
      （改動前這個入口對節奏完全沒作用，是本次合併 `ventVolume` 才修好的，重點驗這條）
- [ ] 靜音時畫面反紅／LED 紅閃／秒數 1~6 **仍要正常顯示**（SoT §13.10 靜音規則）
- [ ] 通氣畫面內按上/下切換 → 底部「音量 開／關」即時更新
- [ ] 通氣畫面內切成「關」→ 退出 → 回設定選單看通氣音量 → 預期也是「關」（兩個入口同一個值）

#### ⏳ 待驗：持久化與恢復預設
- [ ] 兩個音量都設「關」→ 關機再開機 → 預期仍是「關」
      （可用 serial log 的 `[SETTINGS] NVS loaded: brt=? vol=? vvol=?` 直接佐證，
      這行是機器可讀的，不必靠肉眼）
- [ ] **通氣畫面內**按上/下切換 → 關機再開機 → 預期也被記住
      （2026-09-06 修正前這個入口不寫 NVS，重開機會丟失）
- [ ] 長按主鍵觸發恢復預設 → 確認 → 預期兩個音量都回「開」（`vol=1 vvol=1`）

#### ⚠️ 實機驗不到的一條（只有 native test 覆蓋）
舊 NVS 值遷移（存舊值 3 → 開機遷成 1 並寫回，log 會出現 `[SETTINGS] migrate "vol": 3 -> 1`）
在這片板子上重現不了——它的 NVS 早已被寫成新值域的 0/1，遷移不會觸發（2026-09-06 重燒後
的開機 log 確認沒有 migrate 行，這是正確行為）。要在實機驗得把 NVS 灌回舊值，沒有簡便管道。
這條目前由 `test_g02b_normalize_toggle_legacy_values` / `test_g02c_settings_init_migrates_legacy_nvs`
覆蓋。**它影響的是別人手上還沒升級過的裝置，不是這片**——若之後有第二片沒燒過新韌體的
板子，那片的首次開機就是驗這條的唯一機會，記得先接 serial 收 log。

### T8 — codex Tier 3 review 提出、本次刻意未做的兩類建議

**(a) 兩態設定改用型別表達（types 面向）**
`system_volume` / `vent_volume` 語意只有 0/1，型別仍是 `uint8_t`（可裝 0~255）。
建議改成 `enum class ToggleState : uint8_t { Off, On }`，只在 NVS 序列化邊界用原始整數。
未做的原因：會擴散到 `settings_state_t`、`DisplaySnapshot`、`kSettingsSlots` 的函式指標
簽名與 NVS 讀寫，屬獨立重構。**本次已用另兩道防線覆蓋實質風險**：載入時
`settings_normalize_toggle()` 把值收進 0/1，判斷一律走 `settings_toggle_enabled()`
（`!= OFF`，未遷移的舊值也判為開）。

**(b) 兩條測試缺口（tests 面向）— 受既有架構限制**
- 系統音量 gate（`uiConfirmBeep()` 是否真的擋住四個確認音）
- 通氣音量接線（設定選單改值 → `ventVolume` → 節奏靜音的整條路徑）

兩者都在 `src/`，而 `platformio.ini` 的 `build_src_filter = -<*>` 讓 native build 完全
排除 `src/`，現況測不到。要補得先把邏輯抽進 `lib/`（例如把「哪些聲音受 gate」做成純
函式），或加 on-target 測試。**在此之前，這兩條只能靠 T6 的上機驗收把關**——這也是
T6「系統音量設關但警報仍要響」那條被列為最關鍵的原因。

### T7 — Test Plan V1 尚未同步（**本次刻意未做**）
`docs/EMS_DoseSync_Pro_Test_Plan_V1.md` 與同名 `.html`（PM 版，手工排版、非生成物）
仍寫著舊值域，直接照做會失敗的步驟至少有：
- §5.1.3「通氣音量 = 5：最大音量」
- C.S2 / G.S1「系統音量調至 5」「通氣音量調至 0」的操作步驟
- 附錄設定值域表（螢幕亮度 1~5 / 系統音量 1~5 不可靜音 / 通氣音量 0~5）
- G.S1「系統音量 NVS 持久化」的驗收方式（調至 5 → 試聽）
未做的原因：這是重寫上機驗收步驟，且 .md 與 .html 兩份都要改；本 session 無硬體，
改完也無法確認步驟可行。建議與 T6 一起做，或用 `/pm-html-report` 重出 HTML。

## Review
（完工後補：改了哪些檔、測試結果、commit）
