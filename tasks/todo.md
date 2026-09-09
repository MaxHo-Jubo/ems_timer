# tasks/todo — 系統設定選單修正（2026-09-06 起，跨 session）

> 狀態（2026-09-06 更新）：**T1～T5 已完成並 commit，韌體已燒錄上機**。
> T6 上機驗收部分通過（選單結構／字型／編輯器開關字樣），**聲音 gate 與持久化仍待驗**；
> T7（Test Plan V1 同步）未開工。**T9（MAX17043 鎖 bus 判別，2026-09-06 新增）：換模組前必做的
> 通斷／地線偏移量測，i2c-scan 已改成印錯誤碼與 reset reason（已編譯通過、已 commit；板上目前是主韌體）。
> 判別結果：該顆 MAX17043 故障需換，換裝方案 MAX17048 見 T9 表格與 T10。**
> 各項細節見下方各段。
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

### T9 — MAX17043 鎖 bus 判別（**換模組前必做**，2026-09-06 新增，需上機）

**為什麼要做**：09-05 的六條隔離測試只證明「問題在 MAX17043 那一側」，分不出「晶片壞」
跟「晶片的地線參考跟 ESP32 不是同一個」——兩者對六條證據的預測完全相同，而 MAX17043
的 VIL 只有 0.5V，地線差 0.5V 就會把 ESP32 的 0V 判成不是 low。完整分析與出處見 memory
`project_max17043_lockup_reanalysis_2026_09_06`。另外查到模組板載 2.2kΩ 上拉接的是
電池電壓（你 09-05 在焊盤量到 SDA 對電池負極 3.9V 已證實），會把 GPIO 41/42 閒置電位
拉到 ESP32-S3 的 VIH 上限 3.6V 之上；08-22 能跑是超規狀態下的能跑。

**工具**：`pio run -e i2c-scan -t upload`。2026-09-06 已改成每輪印 endTransmission
錯誤碼統計（NACK／逾時／其他）、掃描耗時、bus 判讀，開機 banner 印 `Reset reason`。
全部 NACK 且整輪不到 0.1s = bus 正常只是沒裝置；出現 `[TIMEOUT]` = bus 被拉住。
收 log 用 `scripts/serial_monitor.py`（open 不會 reset 板子）。

**判別順序（約 15 分鐘，一步都不要跳）**

- [ ] **1. 斷電量通斷三條**（電表 Ω 檔，電池、USB 全拔）
  - [x] 模組 GND 排針 ↔ ESP32 GND 腳：期望 < 1Ω。**2026-09-06 實測：表棒零點 0.3Ω；模組端已焊死，
        GND 線插在靠近 ESP32 GND 腳的麵包板孔讀 4Ω，插在遠處靠另一條跳線共地的孔讀 20Ω。**
        → 麵包板地線網路每個接點都是幾 Ω 等級，不是單一壞線。主電流 200mA 走 4Ω 就是 0.8V 偏移，
        超過 MAX17043 的 VIL 0.5V。地線假說的實證。
        **接著把 MAX17043 GND 與 TP4056 OUT− 都改插到緊鄰 ESP32 GND 腳的孔：兩條都讀 0.4Ω（扣零點 ≈ 0.1Ω）；
        插遠一點數字就往上爬** → 問題是麵包板電源軌／鏈式跳線的接點，不是線也不是模組。
        現在地線已可靠，可以進第 2 步通電量偏移，然後跑新版 i2c-scan
  - [ ] 模組 JST「−」↔ 模組 GND 排針：期望 < 1Ω。開路 = 板上地線燒斷（保護 FET 被繞過時大電流走這條）
  - [x] 模組 VCC 排針 ↔ JST「+」：**2026-09-06 晚間已確認 V1.1 佈局**——使用者拿模組的 VCC/GND
        排針直接供電給 ESP32，板子開機。V1.2 的 VCC 只接到 2.2kΩ 上拉，供不出這種電流，開不了機。
        `§10.7`「VCC 不接」對這片成立。（V1.2 型的判別法留著給重買時用：Ω 檔量 VCC↔JST+ 為開路）
        ⚠️ 這招只能當一次性判別，不要再拿模組當供電路徑：電流全走模組上的細走線與 JST 接點；
        而且若當時是餵進 ESP32 的 3V3 腳而非 5V/VIN，等於把 4V 打在晶片 VDD 上（上限 3.6V），下不為例
- [ ] **2. 通電、用會鎖死的接法（SDA+SCL 都接），量電壓**（電表 DCV 檔，黑棒固定在 ESP32 GND）
  - [x] ESP32 GND ↔ 電池負極：期望 < 50mV。**2026-09-06 實測 −28mV 左右（地線改插緊鄰 ESP32 GND 腳之後；
        探針手持會跳動），TP4056 B− 差不多** → 負載下地線合格、保護 FET 有導通。
        注意這是「地線修好之後」的數字；09-05 鎖死時的接法沒量過，所以 09-05 當時的偏移只能推定不能證明
  - [x] SDA、SCL 對 ESP32 GND 的閒置電位：**2026-09-06 晚間實測，全部接上、電池剛充飽，GPIO 41/42
        對地接近 4V**。「上拉接電池電壓、超 ESP32-S3 3.6V 上限」成立，待記進 `§10.7`
  - [ ] 順手一併：拔掉 TP4056 與電池、只插 USB，量 ESP32 5V 腳對地——約 4.7V = VBUS 經二極體、
        ≥ 5.0V = 直通（09-05 量到 5.3V 是直通的線索）。直通代表 USB 與 TP4056 同接時兩個 5V 硬併聯，
        TP4056 只准校在 5.00V 以下（見 09-05 事件 memory 的 IN-OUT 焊點待辦）
  - [ ] 把上面的數字連同電池電壓一起抄下來，每一筆寫明黑棒接哪裡（09-05 兩次量測互相矛盾就是沒寫）
- [ ] **3. 跑 i2c-scan 抓證據**：記 banner 的 `Reset reason`、每輪的「耗時／NACK／逾時」與 `[TIMEOUT]` 位址；
      若板子反覆重開，`Reset reason` 是 BROWNOUT / TASK_WDT / INT_WDT / PANIC 各指向不同根因
  - **2026-09-06 首跑（地線修好後、USB+TP4056+電池、MAX17043 兩線接著）**：`[FOUND] 0x57`、`0x68` 都在，
    但出現 `[TIMEOUT] code=5`（位址清單待補）。bus 沒有整條死，DS3231 正常，逾時是局部的。
  - **靜態電位（黑棒 ESP32 GND，掃描中）**：模組 SDA 焊盤 3.9V、SCL 焊盤 **2.6V**；ESP32 端 GPIO42 3.9V、
    GPIO41 2.6V；兩線在 ESP32 端對調後電壓跟著對調、仍逾時 → 2.6V 是模組 SCL 節點自己的性質，
    不是 ESP32、不是接反。SCL 比 SDA 低 1.3V = 有東西從 SCL 抽走約 0.6mA（1.3V / 2.2kΩ）。
    候選：(a) 焊線時 SCL 與相鄰的 GND 焊盤之間有助焊劑／錫渣形成約 4.4kΩ 的弱短路；(b) 晶片 SCL 輸入腳漏電（損壞）。
    判別：斷電拔電池拔線，Ω 20k 檔量模組 SCL↔GND 正反兩個方向，對照 SDA↔GND——兩方向都 ~4k = 橋接（清洗重焊可救）；
    SDA 那組開路、SCL 那組單向或雙向漏 = 晶片。09-05 記憶裡「SDA 2.6V」多半是把兩條線標反了，2.6V 從 09-05 起就一直在 SCL 上
  - **2026-09-06 fuel-gauge-check 定案**：地線修好、電池重插、兩線接對（對調過也一樣）之後，每次讀 VCELL 都是
    `i2cWriteReadNonStop returned Error 263`（ESP_ERR_TIMEOUT），每筆卡 1 秒。09-06 早上模組沒接時是 `Error -1`
    （ESP_FAIL = NACK）。**263 = 晶片有 ACK 自己的位址、之後抓住 SDA 不放**；同 bus 的 DS3231 全程正常。
    → 晶片 I2C 前端活著、後面壞了，**判定這顆 MAX17043 要換**。Ω 檔 SDA/SCL 對 GND 兩腳讀數同型
    （一向 0、反向 9k／15.4k），沒抓到腳位差異；「SCL 2.6V」是逾時期間 ESP32 壓低 SCL 的電表平均值，是結果不是原因。
    根因**不明**：焊杜邦線在 08-22 驗收前就存在，排除。08-22 到 09-05 之間的候選只剩拔插時的 ESD、
    並聯線瞬間反接（使用者回憶 09-05 一度「量到電壓偏低」）、或未知。晶片有 ACK 但後續卡住，跟反接把 VDD
    打死的「完全不回應」不同型，所以連反接都只是候選。決策不依賴根因：263 的證據已足夠。
- [x] **4. 依結果分流**——走「三條通斷正常、偏移 < 50mV、仍逾時 → 換」這條
  - 第 1 步任一條開路，或第 2 步偏移 ≥ 0.3V → 修接線後重掃，**不換模組**
  - 三條通斷正常、偏移 < 50mV、但 i2c-scan 仍 `[TIMEOUT]` → 這顆晶片才算真的壞，換
  - 換上新模組前：先對新模組做第 1 步第三條驗板型；**不管新舊，先把「上拉接電池電壓」處理掉**
    再接 ESP32，不然新模組一樣在超規下跑。這片是 V1.1 佈局、VCC 就是電池節點，**沒有「改接
    3.3V」這個選項**（那是 V1.2 才有的獨立 VCC 腳）。可行的三條路：
    (a) 拆掉 SDA/SCL 那兩顆 SMD 上拉（先用 Ω 檔確認 SDA↔VCC、SCL↔VCC 各約 2.2kΩ 找對顆；
        兩端焊盤堆錫、烙鐵同時加熱推開即可，或用刀片割斷電阻到 VCC 那端的走線）；
    (b) **不動模組**：SDA/SCL 各經一個雙向電平轉換模組（BSS138 四通道那種）再進 ESP32，
        HV 接電池正極（模組 VCC/JST+）、LV 接 ESP32 3V3、GND 共地——現階段最省事；
    (c) 重買時挑 V1.2 佈局（VCC 獨立腳，Ω 檔量 VCC↔JST+ 為開路），VCC 接 ESP32 3V3

  **備用模組（2026-09-06 量過，也是 V1.1）＋ 電平轉換模組接線表**（四通道雙向那種，BSS138 或 TXS0104E）：

  | 轉換模組腳 | 接到 | 說明 |
  |---|---|---|
  | HV（或 VCCB / VB） | MAX17043 的 **VCC 排針** | 就是電池節點 3.0~4.2V，高壓側參考 |
  | LV（或 VCCA / VA） | ESP32 **3V3** | 低壓側參考 |
  | GND（兩側板上相通） | ESP32 GND 腳旁的孔 | MAX17043 GND 也接同一點，星狀地 |
  | HV1 / B1 | MAX17043 **SDA** | |
  | LV1 / A1 | ESP32 **GPIO 42** | DS3231 照舊直接掛 GPIO 42 |
  | HV2 / B2 | MAX17043 **SCL** | |
  | LV2 / A2 | ESP32 **GPIO 41** | DS3231 照舊直接掛 GPIO 41 |
  | 3、4 通道 | 不接 | |

  接線順序照 `§10.7`：先只接電池量模組 VCC 對 GND 為 3.0~4.2V 正值 → 接轉換模組 HV/LV/GND → 最後接 SDA/SCL。
  ⚠️ 已知限制：模組沒插電池時，它的 2.2kΩ 上拉會變成拉到 0V 的下拉，把 SDA/SCL 拖低——**這是現況就有的問題**
  （V1.1 模組掛著但沒電池，DS3231 也會讀不到）。BSS138 型轉換模組會把這個低準位傳到 ESP32 側，沒有改善；
  **TXS0104E 型有 VCC isolation（任一側供電為 0 時全部腳位高阻）**，HV 接電池的話電池拔掉時 ESP32 側 bus 自動隔離，
  DS3231 不受影響——買得到就優先 TXS0104E。

  **換裝方案（2026-09-06 選定）：MAX17048 模組，Adafruit 5580 設計的複刻（背面料號 MRS182A）。**
  上拉 10k 接 VIN 不接電池、晶片 VDD 可跳線選 Vin/Bat、兩個 JST PH 並聯內建 Y 分接、I2C 位址同 0x36。
  **不需要電平轉換模組**，也解掉「模組沒電池就拖死 bus」的問題。接線：

  | 模組腳 | 接到 | 說明 |
  |---|---|---|
  | VIN | ESP32 **3V3** | 上拉與晶片 VDD 的電源。**不可接 5V**：晶片 VDD 上限 4.5V |
  | GND | ESP32 GND 腳旁的孔 | 星狀地，不走電源軌 |
  | SCL | GPIO **41** | DS3231 照舊同腳 |
  | SDA | GPIO **42** | DS3231 照舊同腳 |
  | INT（=ALRT）、QStart | 不接 | |
  | JST PH 其一 | 電池 | |
  | JST PH 另一 | PH 對 PH 線 → TP4056 **B+/B−** | 取代 §10.7 順位 1 的轉接器鏈 |
  | 背面 Vin–VDD–Bat 跳線 | 橋在 **Vin** 側 | ESP32 有電晶片就有電；電池拔掉上拉仍在 3.3V。到手先用 Ω 檔看出廠橋哪邊 |
  | 背面 LED 跳線 | 省電時割開 | 「on」LED 吃 VIN |

  到手驗收（Ω 檔）：VIN ↔ 背面 Bat 焊盤 = 「1」（上拉不在電池上）；SDA ↔ VIN 轉 20k 檔 ≈ 10k。
  接線順序仍照 §10.7：先接 VIN/GND 量 VIN 3.3V → 插電池 → 最後接 SDA/SCL → i2c-scan 看 0x36 → fuel-gauge-check 看讀數。

### T10 — 換裝 MAX17048 的韌體調整（**待模組到貨**，2026-09-06 新增）

- [ ] `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h`：VCELL 換算從 MAX17043 的 12-bit × 1.25mV 改為
      MAX17048 的 16-bit × 78.125µV（`VCELL_SHIFT_BITS` / `VCELL_LSB_MV` 兩個常數，§10.8 已標「換型號必改」）；
      對應 native test 的 expected 值同步改（依 feedback：expected 側寫死字面值，不拿常數比自己）
- [ ] SOC 暫存器格式相同（高位元組整數 %、低位元組 1/256 %），不用動；VERSION 讀值不同但 probe 只看讀取成功，不用動
- [ ] `src_fuel_gauge_check/main.cpp` 檔頭與判讀文字改稱 MAX17048；`src_i2c_scan` 對照表 0x36 說明改 MAX17048
- [ ] 文件：`power-module-purchase.md §10.7/§10.8`、`gpio-allocation.md §5.4` 速查表與位址表、`hardware-procurement-v2.md #13`
      改為 MAX17048 與新接線；把 T9 的 V1.1 上拉接電池、地線、263 vs −1 三個教訓寫進 §10.7 的「踩坑」
- [ ] 上機：i2c-scan 看 `[FOUND] 0x36` 且逾時 0 → fuel-gauge-check 讀數對電表 < 50mV → 燒回主韌體看 `[FUEL] MAX17048 detected`
      與每 10 秒的 `[FUEL] xx% xxxxmV` 行
- [ ] **5. 收尾文件**：`power-module-purchase.md §10.7` 補「上拉接電池電壓、V1.1/V1.2 板型判別」，
      `gpio-allocation.md §5.4` 同步；memory 的 09-05 事件檔把「模組故障」結論改成實測結果
- [ ] **6. 後續（commit review 建議，非本輪範圍）**：主韌體 `[BOOT]` 之後補印 `esp_reset_reason()`，
      並把 i2c-scan 的 `resetReasonName()` 抽成 `firmware/include/` 共用 header 給主韌體與五個 `src_*` 工具用——
      會重開的是主韌體，現在只有 smoke 工具印得出 BROWNOUT／PANIC

### T11 — 廠商焊接交付包：廠商文件 + 產測韌體（2026-09-09 新增）

背景：要請廠商把 V1 模組焊成一台；決策「電量計不焊、只留 5-pin I2C 排針（SDA/SCL/3V3/GND/BAT+）」，
驗收以產測韌體 PASS 為準（對方只拿 merged .bin，不拿原始碼）。

- [x] `docs/vendor-assembly-brief.html`：零件表（圖 6 去掉 WS2812、電量計改「留排針」）、電源／接地表、
      訊號接線表（TFT／DS3231／按鍵／蜂鳴器）、電量計決策、必讀注意事項、產測判讀表、交付檢核表。
      自足 HTML；2026-09-09 依使用者指示加進 Cloudflare 部署清單（不掛連結，直接 URL 給廠商）
- [x] `firmware/lib/ems_factory_test/`：純邏輯（按鍵遮罩、RTC 走時分類、PASS／FAIL 判定）+
      `test/test_factory_test_logic/` native test（RED → GREEN）
- [x] `firmware/src_factory_test/main.cpp` + `platformio.ini [env:factory-test]`：開機彩條 → I2C 掃 0x68／0x57／0x36 →
      RTC 走時 → 8 鍵逐顆打勾 + 蜂鳴 → TFT 顯示 RESULT；TFT 只用 ASCII（避開 vlw 字集重生的坑）
- [x] `pio test -e native` 全綠、`pio run -e factory-test` 編譯過、產出 `firmware-merged-factory-test.bin`
- [x] `docs/README.md` 登錄廠商文件與產測 env；`release-template/HOW_TO_BUILD_RELEASE.md` 補產測包打包步驟
- [x] commit 後 codex Tier 3 review（6/6 面向）：3 個 CRITICAL 全部以設計修正——
      (1) 按鍵改「依序驗收」，接反 → `FAIL: BUTTON ORDER`、同輪兩顆 → `FAIL: BUTTON SHORT`；
      (2) 拿掉 RTClib，直接用 Wire 讀寫 DS3231 暫存器，每次讀寫驗回傳、讀值驗 BCD／範圍，失敗 → `FAIL: RTC I/O ERROR`；
      (3) RTC 失憶不再靜默覆蓋：seed 後要求斷電再上電，OSF 再亮 → `FAIL: RTC BATTERY`（NVS 記已 seed、
      RTC_NOINIT magic 分辨 RST 與真斷電）。所有 FAIL 黏性到 RST。native test 22 → 41 條。
      補跑 code-review／silent-failure 兩面向又抓到 3 個 CRITICAL（`rtc_present` 與 `rtc_tick` 每輪重算，
      虛焊短暫恢復可翻回 PASS）→ 加 `rtc_missing_seen` 黏性、`ft_apply_rtc_tick` 讓 Stuck 黏性、
      失敗優先序改由 `FtFailKind` 單一定義、NVS begin/remove/putBool 回傳有查 → `FAIL: NVS ERROR`；test 44 條。
      第三～五輪再修：OSF 優先於軟重置、斷電偵測改雙證據（RTC 離線秒數 ≥ 5 且 RTC_NOINIT magic 消失）、
      ESP-IDF nvs API 分辨讀取失敗與 key 不存在、epoch 換算改 Hinnant days_from_civil、走時倒退／跳 >10 秒 → Invalid、
      日期依月份天數與閏年驗證；test 49 條。五輪 codex 共 14 個 CRITICAL 全修完
- [ ] 上機：燒 factory-test 到現有原型，8 鍵／RTC／蜂鳴／彩條走一遍，確認判讀表與實機一致（需硬體）。
      特別要驗：(a) 斷電偵測用兩個證據：「RTC epoch − NVS 每 2 秒記的最後時間 − 開機秒數 ≥ 5 秒」且
      「RTC_NOINIT magic 不在」（第三、四輪 codex 要求 fail-closed）：按一下 RST 應維持 WAITING POWER CYCLE、
      真斷電 10 秒後轉 BACKUP OK、按住 RST 10 秒應仍維持 WAITING（若實測變 OK，代表 EN 重置清掉 RTC memory，
      退化成只剩證據 1，記下來但不擋交付）；
      (b) 故意把兩顆按鍵接反，應出 `FAIL: BUTTON ORDER exp N got M`；(c) Font4 的 `WAITING POWER CYCLE` 沒被切邊；
      (d) 開機按住主按鍵能清 NVS `rtc_seeded`（開發板換 RTC 模組用）
- [ ] MAX17048 到貨後：插上排針驗 `GAUGE 0x36: PRESENT`，再把換算常數接進 T10

## Review
（完工後補：改了哪些檔、測試結果、commit）
