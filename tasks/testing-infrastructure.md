# EMS Timer 測試基礎建設 Todo

> 起始日期：2026-05-12
> 動機：Phase E 持久化遇到 bootloader 無限重啟 / flash script 燒錯檔 / UI dirty flag 缺欄位等 bug，這些都不是時序問題、應在燒板前就抓到。本檔追蹤 unit test（L1）以外要補的測試層。
> 對齊：`docs/EMS_DoseSync_Pro_Test_Plan_V1.md` §1 測試金字塔

## 執行順序（使用者指定）

`#1 → #6 → #2 → #4 → #3 → #5`

- `#1` Build-time binary verify
- `#6` 記憶體 / Flash 預算 watchdog
- `#2` L2 整合測試（native）
- `#4` Static analysis
- `#3` On-target Unity test
- `#5` Boot smoke test（依賴 CI；做之前先確認替代方案）

每完成一項先停下讓使用者確認。

---

## 重啟 session 怎麼接

**進度（2026-05-12）**：#1 / #6 / #2 / #4 / #3 **已完成且已 commit**（3 個 commit）。**下次 session 直接做 #5**（見下方「#5」段落，動工前先確認 CI 替代方案）。

下次 session 開始時：

1. **檢查現況**（一行確認所有完成項仍綠）：
   ```bash
   cd /Users/maxhero/Documents/MaxHero/Projects/ems_timer/firmware
   pio test -e native              # 應該 246 cases 全綠
   pio check -e check              # 應該 No defects found
   pio run -e esp32-s3-devkitc-1   # 應該 Flash 27.1% / RAM 16.4% + [verify] OK x3
   ```

2. **下一步：#5 Boot smoke test**（見下方章節）— **動工前先確認 CI 替代方案**（見 #5 段落「前置條件」）

---

## ⚠️ #3 On-target test 跑法 SOP（每次要重跑 #3 都按這步驟）

> GOOUUU ESP32-S3 板沒有 CP2102，native USB 沒 RTS/DTR，esptool autoreset 不可靠。
> **每次跑 #3 都是 4 步流程，沒辦法用 `pio test -e esp32-s3-test` 一行打發**。
> 接 CP2102 模組或換板之前，這 SOP 一直有效。

**標準 4 步**：

```bash
# Step 1：板子進 download mode
#   按住 BOOT 鍵不放 → 短按 RESET 鍵 → 放開 BOOT 鍵
pio device list   # Description 應為 "USB JTAG/serial debug unit"

# Step 2：燒板但不跑 test（避開 pio test 自動 monitor 連不上的問題）
cd /Users/maxhero/Documents/MaxHero/Projects/ems_timer/firmware
pio test -e esp32-s3-test --without-testing
#   看到 "Hash of data verified." + "Leaving..." 代表燒錄成功

# Step 3：板子離開 download mode 跑新韌體
#   純按 RESET 鍵（不按 BOOT）
#   等 5 秒讓 USB CDC 重新列舉

# Step 4：找當下 port name 並開 monitor
ls /dev/cu.usbmodem*                              # 可能 cu.usbmodem101 / cu.usbmodem1101 / 其他
pio device monitor --port <實際 port> -b 115200
#   預期 6 個 PASS + "6 Tests 0 Failures 0 Ignored  OK"
```

**Hard reset 救板對照表**（卡住、port 消失、Errno 6 等狀況）：

| 症狀 | 解法 |
|---|---|
| `read failed: [Errno 6] Device not configured` | 板子卡某 state。**拔 USB cable → 等 10 秒 → 重插**，再 Step 1 |
| 燒完按 RESET 後 `ls /dev/cu.usbmodem*` 看不到 port | 等 5 秒（USB 列舉延遲），仍沒就拔插 USB；或重做 Step 1 重燒 |
| 監聽 `cu.usbmodem101` 但 port not found | port name 漂了，**用 `pio device list` 看當下真實 name**（常見變 `cu.usbmodem1101`、`cu.usbmodem<MAC>`） |
| 按 RESET 沒反應 / 板子無 LED 亮 | USB cable 可能只供電不傳資料，換一條傳檔用 cable |
| 燒錄階段 `Failed to connect: No serial data received` | 板子不在 download mode。重做 Step 1 的 BOOT+RESET 操作 |
| `Hard resetting via RTS pin...` 印完板子停 download mode | 預期行為（native USB 沒 RTS）。手動按 RESET 帶板子離開 download |

**為什麼這板要這麼麻煩**（背景）：
- `feedback_goouuu_esp32s3_no_uart_bridge.md` — 兩個 USB port 都接 chip native USB-JTAG，沒有 CP2102 USB-to-UART bridge
- `feedback_macos_usb_port_name_drift.md` — macOS port name 在 USB topology 變動時會漂
- `feedback_unity_64bit_on_32bit_mcu.md` — Unity 64-bit assert 要 `-DUNITY_SUPPORT_64`
- 長期解法：未來採購 CP2102 模組焊上（淘寶／蝦皮「CP2102 USB to TTL」+ 0.1μF cap 接 IO0/EN）或換有 CP2102 內建的 DevKitC-1 板

---

## #1 Build-time binary verify ✅ 已完成（2026-05-12）

**改動**：`firmware/scripts/post_build_merge.py`

新增功能：
- `parse_app_partition_size()` 讀 `partitions.csv` 取 ota_0 容量
- `verify_merged_bin()` 三項驗證：
  1. `merged.bin` 存在且非空
  2. `merged.bin` ≤ 16MB flash 容量
  3. `app.bin` ≤ ota_0 partition（超 90% 警告 ⚠️）
- 把 4 個 silent `return` 改成 `sys.exit(1)` + 明確錯誤訊息

**驗證**：
- 實機 build pass，印出 `App: 533,024 / 1,966,080 bytes (27.1%)`
- Python inline 4 個 fail case 全綠（partition parse / 缺 ota_0 / app 超標 / merged 超 16MB）

**抓得到的 Phase E bug**：
- ✅ flash_size 沒設導致 partition 超界（build 階段 fail）
- ✅ app 膨脹超過 1.9MB OTA partition
- ✅ post_build silent skip 改為明確 exit 1

---

## #6 記憶體 / Flash 預算 watchdog ✅ 已完成（2026-05-12）

**改動**：`firmware/scripts/post_build_merge.py`

新增功能：
- `flash_size_to_bytes()` — 解析 `"16MB"` / `"4MB"` 字串
- `parse_partitions()` — 取代舊 `parse_app_partition_size()`，回傳整張 partition table
- `find_app_partition_size()` — 用解析後的 entries 找 ota_0
- `verify_partition_layout()` — 三項檢查：
  1. **Critical**: `max(offset + size) > flash_size` → fail（Phase E bootloader bug 根因）
  2. **Critical**: 兩個 partition 範圍重疊 → fail
  3. **Warning**: 相鄰 partition 之間有 gap（不 fail，只 warn）
- `merge_bin()`：從 `env.BoardConfig().get("upload.flash_size")` 動態取 flash_size，不再硬寫 16MB 常數
- merge_bin 命令的 `--flash_size` 參數也改用動態 flash_size_str（避免主 env / tft-smoke-test 不一致）

**驗證**：
- 實機 build pass，新增訊息：
  ```
  [verify] OK: partition layout — max boundary 0x1000000 (100.0% of flash 16,777,216 bytes)
  ```
- Python inline 6 個測試全綠：
  1. ✓ `flash_size_to_bytes` 解析五種輸入
  2. ✓ `parse_partitions` 正確讀 5 個 entries
  3. ✓ 正常 16MB layout 通過
  4. ✓ partition 超界（partitions.csv 到 16MB 但 flash_size = 8MB）→ `SystemExit(1)` ← **Phase E bootloader bug 會被擋**
  5. ✓ partition 重疊 → `SystemExit(1)`
  6. ✓ partition gap 只 warn 不 fail

**抓得到的 Phase E bug**：
- ✅ partitions.csv 改到 16MB 但 `board_upload.flash_size` 還是 8MB → build 階段直接 fail，附「請檢查 platformio.ini」訊息
- ✅ partition table 拼錯導致 offset 重疊 → fail
- ✅ partition gap 提示（不 fail，給開發者看是否刻意留白）

---

## #2 L2 整合測試（native）✅ 已完成（2026-05-12）

**改動**：
- **新增 lib** `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`（header-only）
  - `struct DisplaySnapshot`（從 main.cpp 1682-1694 搬過來）
  - `struct DisplaySnapshotInputs`（新增，純輸入結構，無 Arduino 依賴）
  - `enum DisplaySnapshotFlag`（12 個具名 bit mask 取代原 raw `0x80` 等）
  - `captureSnapshot(inputs)`：純函式做欄位 + bit 映射
  - `snapshotsEqual(a, b)`：memcmp 包裝
- **refactor** `src/main.cpp`：
  - 1682-1741 原 struct + capture function → 改用 `DisplaySnapshotInputs` 構造 + 呼叫 `captureSnapshot(in)`
  - 時間相關衍生值（countdownSec / ventBeat / alarmingFlashOn）保留在 main.cpp 計算，lib 不依賴 `millis()` / `EPI_CYCLE_MS`
  - 第 1812 行 raw `0x80` → `SNAP_FLAG_ALARMING_FLASH`
- **新測試** `firmware/test/test_display_snapshot/test_display_snapshot.cpp`（27 cases，5 groups）：
  - **Group 1**：baseline — 全相同 input → snapshots 相等
  - **Group 2**：11 個 1:1 欄位獨立改變 → 觸發 memcmp 差異
    - 含 Phase E regression：`historyCursor` / `historyScrollOffset` 各一個 case
  - **Group 3**：12 個 bool flag → 唯一 bit mask 對應
  - **Group 4**：全 flag 同開 → OR 結果正確；12 個 bit 不撞號

**驗證**：
- `pio test -e native` 全綠：**246 cases 全部通過**（原 219 + 新增 27）
- `pio run -e esp32-s3-devkitc-1` 通過：Flash 27.1%（+100 bytes inline 衍生值計算）/ RAM 16.4%（無變化）
- 新 partition layout / app 預算驗證仍正常輸出

**抓得到的 bug 類別**：
- ✅ Phase E history UI 漏 historyCursor 那類「DisplaySnapshot 漏欄位」regression
- ✅ 新增 bool flag 但忘了分配 bit
- ✅ 兩個 flag 撞同一 bit mask（Group 4 uniqueness 檢查）
- ✅ raw hex mask 在 main.cpp 與 lib 各處漂移（改用 enum 後鎖死）

---

## #4 Static analysis ✅ 已完成（2026-05-12）

**改動**：
- `firmware/platformio.ini` 新增 `[env:check]`：
  - `check_tool = cppcheck`
  - `check_src_filters = +<src/> +<lib/ems_*/>`（只掃自家程式碼，跳過 framework / 第三方）
  - `check_severity = medium, high`
  - `check_flags = cppcheck: --enable=warning,performance,portability --suppress=missingInclude --inline-suppr --std=c++17`
- 跑法：`pio check -e check`（首次跑會自動 install `tool-cppcheck@1.21100.241030`）

**Baseline scan（fix 前）— `--enable=all` 完整檢查 3 個 real findings**：

| Severity | 檔案 | 問題 | 處理 |
|---|---|---|---|
| Style | `lib/ems_vent/ems_vent_metronome.cpp:29` | `out.display_number = 1` 立即被覆寫，dead code | ✅ 移除冗餘賦值 |
| Style | `src/main.cpp:123` | `FrameSprite(LovyanGFX*)` 單參數 ctor 未加 `explicit`，可能 implicit conversion | ✅ 加 `explicit` |
| Style | `src/main.cpp:1807` | local `now` shadow 1736 的 `DisplaySnapshot now`，可讀性差 | ✅ rename 為 `nowMs` |

**False positives（不修）**：

| 類別 | 數量 | 不修理由 |
|---|---|---|
| `unusedStructMember` | 58 | cppcheck 無法跨 translation unit 追蹤；`ohca_output_t` / `case_meta_t` 等成員實際在 main.cpp 被讀 |
| `badBitmaskCheck` | 10 | `c["key"] \| (uint64_t)0` 是 **ArduinoJson 7 提供預設值的官方寫法**，不是 bit mask |
| `cstyleCast` | 2 | `(const uint8_t*)json` 給 CRC32 byte read，C-style cast 簡潔且安全 |

**驗證**：
- `pio check -e check` 通過：`No defects found`（medium+ severity）
- 修復後 `pio test -e native` 246 cases 全綠
- 韌體 build 通過，Flash/RAM 無大幅變動（27.1% / 16.4%）

**抓得到的 bug 類別**：
- ✅ dead code / redundant assignment
- ✅ implicit constructor conversion
- ✅ variable shadowing
- 🟡 cross-TU usage 偵測不到（cppcheck 限制）→ 之後若加 clang-tidy 才能補

**本機跑法**：
```bash
cd firmware
pio check -e check          # PlatformIO 包裝（推薦，乾淨整合）
# 或直接呼叫 cppcheck 看 style+info 完整 baseline：
~/.platformio/packages/tool-cppcheck/cppcheck \
    --enable=all --language=c++ --std=c++17 --inline-suppr \
    --suppress=missingInclude --suppress=missingIncludeSystem --suppress=unusedFunction \
    -I lib/ems_ohca -I lib/ems_logic -I lib/ems_storage \
    -I lib/ems_supp -I lib/ems_vent -I lib/ems_display_snapshot \
    src/main.cpp lib/ems_*/*.cpp lib/ems_*/*.h
```

---

## #3 On-target Unity test ✅ 已完成（2026-05-12）

**實機驗證結果**：`pio device monitor --port /dev/cu.usbmodem1101 -b 115200` 顯示
```
6 Tests 0 Failures 0 Ignored
OK
```
6 個 testcase 全綠：partition info → mount/format → save → remount → persists → delete。

**踩雷紀錄（後人勿重蹈）**：
- GOOUUU ESP32-S3 沒 CP2102，native USB 沒 RTS/DTR，`ESP.restart()` 跨 phase 不可行 → 改 `LittleFS.end()` + remount。
- ESP32-S3 32-bit，必加 `-DUNITY_SUPPORT_64` 否則 `TEST_ASSERT_EQUAL_UINT64` 一律報「Unity 64-bit Support Disabled」FAILED。
- 燒錄後 esptool「Hard resetting via RTS pin」對 native USB 無效，要**手動按 RESET** 讓板子離開 download mode。
- macOS USB cycle 後 port name 可能從 `/dev/cu.usbmodem101` 變 `cu.usbmodem1101`（LOCATION 編號變），`ls cu.usbmodem*` 才能抓到所有變體，別只記固定名稱。
- 跑法是兩步：`pio test --without-testing` 燒板 → 手動按 RESET → `pio device monitor --port <實際 port> -b 115200` 看 Unity output。



**改動**：
- **platformio.ini**：新增 `[env:esp32-s3-test]`
  - `framework = arduino` + `test_framework = unity`
  - `test_filter = test_storage_hw`（限定僅跑 on-target 測試，避免誤觸 native test）
  - `build_src_filter = -<*>`（不編 src/main.cpp，測試自帶 setup/loop）
  - `board_build.partitions = partitions.csv` + `board_upload.flash_size = 16MB`（同主韌體 layout）
  - `build_flags` 加 `-DUNITY_SUPPORT_64`（ESP32-S3 32-bit，Unity auto-detect 不開 64-bit assert，沒這旗會跑 `TEST_ASSERT_EQUAL_UINT64` 印「Unity 64-bit Support Disabled」FAILED）
  - 不掛 `post:scripts/post_build_merge.py`（測試 binary 不需要 merged.bin）
- **新測試** `firmware/test/test_storage_hw/test_storage_hw.cpp`（200 行）：
  - **同 session unmount/remount 模擬 reboot**：
    - `LittleFS.end()` + `memset(backend, 0)` + `delay(50)` + `emsStorage_fs_mount()` 重新 mount
    - 為何不用 `ESP.restart()`：見下方「設計決策」
  - **6 個 testcase**（順序執行）：
    1. `test_partition_info_matches_spec` — `esp_partition_find_first` 對 `partitions.csv` SoT（offset 0x3D0000 / size 0xC30000 / subtype 0x82）
    2. `test_littlefs_mount_and_ensure_dirs` — `mount + ensure_dirs + format + init`，backend 函式表完整 + list 為空
    3. `test_save_event_to_littlefs` — `storage_save_case` + `storage_list` 確認 metadata
    4. `test_remount_after_simulated_reboot` — `LittleFS.end()` 後重新 mount + init 仍 OK
    5. `test_event_persists_across_remount` — 6 欄位逐一 assert（event_id / type / timestamp_ms / elapsed_ms / count / actual_time_null）
    6. `test_delete_after_verify` — `storage_delete` + list 回 0
- 使用真 `EmsStorageFs` backend（非 InMemoryBackend），抓 native test 看不到的硬體面 bug。

**設計決策：為何不用 `ESP.restart()` 跨 session**
- GOOUUU ESP32-S3 兩個 Type-C port 都接 chip native USB（VID 303A:1001 USB-JTAG），**沒有 CP2102 USB-to-UART 晶片**，因此**沒有 RTS/DTR 線**。
- esptool 燒完印的 "Hard resetting via RTS pin" 對 native USB **完全無效**；板子燒完停在 download mode（`boot:0x20 DOWNLOAD(USB/UART0)`）。
- 即便手動按 RESET 進 application mode，`ESP.restart()` 期間 USB CDC 重列舉，pio test 的 monitor 會斷成 `read failed: [Errno 6] Device not configured`。
- 第一次嘗試的 NVS state machine + `ESP.restart()` 方案在 native USB port 上不可運作（已 commit log 記錄踩雷過程）。
- 退而求其次：用 `LittleFS.end()` + remount 模擬。資料真寫進 flash partition、unmount 後 inode table 真清掉、remount 時重新從 flash 讀 — 抓得到 LittleFS 持久化 / CRC / index.json 漏欄位等核心 bug。
- **蓋不到**：RTC 狀態保留 / NVS 殘留交互 / bootloader-level boot loop。這層留 Phase F 接 BLE 後手測。

**驗證**：
- ✅ `pio test -e esp32-s3-test --without-uploading --without-testing` 編譯通過（3.63s）
- ✅ **實機驗證通過**：燒到 ESP32-S3 後跑 monitor，6 個 testcase 全 PASS（OK）

**抓得到的 bug 類別**：
- ✅ `partitions.csv` 改 offset/size 但沒同步 SoT（partition info assert）
- ✅ LittleFS `formatOnFail=true` 誤觸發把資料清掉（跨 remount persist 失敗）
- ✅ `events.bin` CRC mismatch / `index.json` 漏寫欄位（跨 remount 逐欄位 assert）
- ✅ `next_seq` 沒 persist 導致 remount 後 case id 重置
- ✅ `storage_delete` 沒同步移除 index entry / 殘留 binary 檔
- 🔴 Phase F 開工必補：BLE 配對 + 真重啟 RTC 行為，single-session 蓋不到

**本機跑法**：
```bash
cd firmware

# 只編譯不上板（CI 友善，快速驗證 env 設定）
pio test -e esp32-s3-test --without-uploading --without-testing

# 實機：插上 ESP32-S3 後一次跑完
pio test -e esp32-s3-test

# 板子卡住救板（native USB 沒 RTS/DTR，autoreset 不可靠）：
#   1. 按住 BOOT 鍵不放
#   2. 短按 RESET 鍵
#   3. 放開 BOOT 鍵
#   4. pio device list 看到 "USB JTAG/serial debug unit" = download mode 已進
#   5. 重燒：pio test -e esp32-s3-test
#   6. 燒完純按 RESET（不按 BOOT），板子跑新韌體
```

---

## #5 Boot smoke test ⏸ 待辦（做之前先確認 CI 替代方案）

**目標**：自動讀 serial output 抓 `Guru Meditation` / `rst:` reset cause / boot loop。

**前置條件**：
- 確認專案是否有 CI（GitHub Actions / GitLab CI / local script）
- 若無 CI，評估替代方案：
  - local `pre-push` hook（push 前自動燒板 + 監聽 5 秒 serial）
  - PlatformIO `test_speed` 環境 + 手動跑
  - 不做，依賴 #3 on-target Unity test 涵蓋

**範圍預估（CI 存在時）**：
- pio run + pio upload + serial monitor 5 秒
- grep `rst:` 確認 reset cause
- 抓到 panic 字串就 fail
