# Phase G — 系統設定 + Type-C 管理工具

> **對應規範**：V1 §18.2-§18.4、§19 / pm-dev-spec §15 / §三
> **狀態**：⬜ 未開工（純韌體、無硬體/PM 阻塞）
> **前置**：Phase D 韌體已完成（commit `d5f64ab` + `aa39def`）

---

## 一、Phase G 範圍總覽

### 1.1 韌體：系統設定（V1 §19）

| 設定項目 | 範圍 | 持久化 | 預設 | 現況 |
|----------|------|--------|------|------|
| 螢幕亮度 | 1~5 | NVS | 3 | ⬜ 無實作 |
| 系統音量 | 1~5（不可靜音） | NVS | 3 | ⬜ 無實作 |
| 通氣音量 | 0~5（0=靜音） | NVS | 3 | 🟡 記憶體實作（`ventVolume`），缺 NVS |
| 裝置名稱 | App 寫入 | LittleFS | 「未命名」 | 🟡 有 `SYNC_DEVICE_NAME` 字串，缺 NVS + BLE 寫入 |
| 韌體版本 | read-only | 編譯時嵌入 | — | ✅ 已有 |

**恢復預設值**（V1 §19.6）：只清除亮度/系統音量/通氣音量，**不清** 裝置名稱 / 案件 / Training / 同步狀態。

### 1.2 Type-C 管理工具（V1 §18.3）

- 獨立 Electron 工具
- 連線 ESP32 USB CDC
- **列出案件**（OHCA + Training）
- **匯出**（CSV / JSON）
- **二次確認後清除**案件區
- **不可修改**既有案件內容
- 協定可重用 NUS payload 格式（透過 USB CDC 傳輸而非 BLE）

### 1.3 系統設定 UI 項目（V1 §19.1）

```text
系統設定

> 裝置名稱
  螢幕亮度
  系統音量
  通氣音量
  電池資訊          ← Phase H 範圍（ADC 腳已用盡）
  App 連線設定      ← Phase F 範圍（BLE 已實作）
  Type-C 連線       ← 只顯示狀態，不提供模式切換
  裝置資訊          ← 讀取 NVS + 編譯時版本
  韌體版本
```

> 📌 **Phase G 只做前 4 項 + 裝置資訊 + 韌體版本 + Type-C 連線狀態顯示**
> 電池資訊 / App 連線設定 / Type-C 模式切換 留待 Phase H / Phase F 整合。

---

## 二、Wave 拆分

### 二、零、Wave 完成狀態（新 session 接續用）

> **每次完成一個 Wave，請勾選下方 checkbox。新 session 開工時以此表判斷哪些 Wave 尚未完成。**

| Wave | 狀態 | 完成時間 | 備註 |
|------|------|----------|------|
| Wave 0: NVS 設定持久化層 | ✅ 完成 | 2026-07-15 | 12 案例全綠；refactor 檢查清單通過（Flash 68.8% / RAM 32.3% 在預估範圍、無 magic number、註解完整） |
| Wave 1: 系統設定 UI + 恢復預設 | ✅ 程式完成 | 2026-07-18 | review 三缺口皆修：① `settings_write()` 補上 `nvs_write_uint8()` 持久化；② `settingsCursor`/`settingsEditorMode`/`settingsRestoreConfirm` 已進 DisplaySnapshot（flags 擴為 uint32_t）＋ native regression test；③ 死碼 `confirmRestoreDefaults()` 移除，`test_g15` 改為「重繪不得覆寫已調整值」的真 regression。另修：`drawSettingsMenu` 不再每次重繪清掉 `s_brightness`；恢復預設對話框改為依 `restore_confirm` 顯示（原為無條件畫出）。**實機待測：亮度 setBacklight、系統音量 beep、通氣音量同步、NVS 重開機保值** |
| Wave 2: 裝置名稱管理 + BLE 寫入 | ✅ 程式完成 | 2026-07-18 | stub 已實作：`settings_set/get_device_name()` 走真實 LittleFS（`/config/device_name.txt`，含 mkdir 與部分寫入偵測）。BLE callback 改為只做淨化＋暫存，LittleFS 寫入搬到 main loop（避免阻塞 GATT task）。截斷邏輯抽成純函式 `device_name_sanitize()`（拒空／內嵌 NUL 截止／**UTF-8 邊界安全截斷**），`test_ble_nus` 重寫為測產品碼而非自建替身。**實機待測：App 端 BLE 寫入流程、LittleFS 讀寫、sync payload 欄位** |
| Wave 3: Type-C 管理工具（Electron） | ⬜ 未完成 | — | 可與 Wave 0~2 並行 |

### 二、零、一、POST-COMMIT-REVIEW 發現（2026-07-18，範圍 `70b11fb..5e0920b`）

> Tier 3 分級審查（5 agent 平行 + 主線逐項驗證）。以下 6 項均經 grep/讀碼證實，非推測。
> **共同根因：測試大量打在 mock 與測試檔自建替身上，與會編譯進硬體的路徑脫節** —— 483 個測試真的有跑、真的全綠，但綠燈不代表真機可用。

| # | 問題 | 位置 | 受影響的宣稱 | 修復 |
|---|------|------|-------------|------|
| 1 | `settings_write()` 不寫 NVS，只改 RAM，卻回傳 `true` | `ems_settings.cpp:283` | 2ca8039 接通持久化 | ✅ 補 `nvs_write_uint8()` |
| 2 | `settings_set/get_device_name()` 為 stub 恆回 `false` | `ems_settings.cpp:330,335` | 68ee81a / af089a7（G2.1/G2.2） | ✅ 實作 LittleFS |
| 3 | 設定 UI 三個 state 未進 DisplaySnapshot → 畫面不重繪 | `ems_display_snapshot.h` / `main.cpp` captureDisplaySnapshot | adb8a1d 畫面反饋 | ✅ 補欄位＋regression test |
| 4 | `test_ble_nus` 未 include `ble_nus.h`，測的是自建替身 | `test/test_ble_nus/test_main.cpp` | 483 綠 | ✅ 重寫為測 `device_name_sanitize()` |
| 5 | `test_g15` 測到死碼，`confirmRestoreDefaults()` 刪掉照樣綠 | `ui_settings.cpp:229` | a30c5ee（G1.5） | ✅ 死碼移除＋改真 regression |
| 6 | 裝置名稱**永遠**置灰：`g_case_mode` 值域只有 0/1，`is_device_name_locked()` 對兩者皆回 `true` | `ui_settings.cpp:265` | 63a3a05（G2.3） | ✅ 判準改為 `storage_has_unsynced_case()` |

**連帶修復的 Important 項**：`nvs_read_uint8()` 失敗零 log（現區分 `ESP_ERR_NVS_NOT_FOUND` 與真異常）、`settings_write` 回傳值被 7 處呼叫端忽略（現統一經 `adjustCurrentSetting()` 並記錄失敗）、亮度/音量的 (cursor,key,min,max,getter,setter) 散在 5 處重複（現收斂為 `kSettingsSlots` 一張表）、`SETTINGS_CURSOR_*` 未 export 導致 13 處裸數字、`sync_send.cpp` ternary 內用 comma operator 藏 side effect、UTF-8 名稱純 byte 截斷、BLE callback 未來接 LittleFS 會阻塞 GATT task、`app_globals.h` 兩個無呼叫點的 getter。

**驗證狀態**：native 501 個測試 500 綠（`test_storage_hw` 為 on-target 測試，native 環境本就 ERRORED）；ESP32-S3 韌體編譯通過（Flash 69.1% / RAM 33.4%）。**實機未測**。

**#6 的延伸發現（架構層）**：§2.2.5 原本描述的「案件進行中」情境在當前架構下**不可達**——進入設定選單的唯一路徑是主選單（`input_handler.cpp:165`），而回到主選單的唯一路徑 `enterMainMenu()` 只被 `exitOhcaCase()` 呼叫，後者會同時重置 `eventCount`/`ohcaState`/`g_case_mode`。能走到設定選單，就代表案件已結束並清空。§2.2.5 已依此改寫為可達判準（見下方）。

**教訓對照**：#3 是 `feedback_display_snapshot_field_sync` 記錄的同類 bug 第 4 次重演（前三次：historyCursor / summarySubmenuCursor / endCheckCursor），而 `ems_display_snapshot.h:26-33` 檔頭就寫著新增 state 的 4 步驟 checklist，本次未照做。

### 二、零、TDD 執行規範（red-green-refactor）

> **本節為 Phase G 強制規範**。違反順序 = 實作錯誤。

每個 Wave 的實作必須遵循以下循環：

```
red   → 寫入測試案例（失敗的測試），確認測試環境正確
green → 寫入最小實作讓測試通過（不重構、不優化）
refac → 在測試保護下重構、優化、檢查影響
```

**執行要點**：

1. **測試先行**：`ems_settings.cpp` 的實作必須在 `test_settings.cpp` 的測試通過之後才寫入。
   - 先寫入測試 → 看到 red（編譯失敗或測試失敗）→ 再寫入實作 → 看到 green。
   - 禁止先寫實作再補測試。
   - 禁止跳過 red 階段直接 green。

2. **一個測試一個循環**：每個測試案例（G0.1 ~ G0.9）必須獨立完成 red-green-refactor。
   - 不允許一次寫入所有測試再一次寫入所有實作。
   - 不允許為了讓多個測試通過而寫入超出範圍的實作。

3. **refactor 階段檢查清單**：
   - [ ] 測試通過（green）
   - [ ] Flash 影響在預估範圍內（§5.1）
   - [ ] RAM 影響在預估範圍內（§5.1）
   - [ ] 沒有引入新的 magic number
   - [ ] 變數/函式有註解

4. **Wave 間依賴**：Wave N+1 必須等 Wave N 的 refactor 階段完成後才開始。
   - 不允許 Wave 1 在 Wave 0 的測試還未全部 green 時開始。
   - 但 Wave 3（Type-C 工具）可與 Wave 0~2 並行。

5. **commit 規範**：
   - 每次 red→green 完成一個測試案例，應 commit 一次。
   - commit message 格式：`[PHASE-G] feat: 通過測試 G0.1（settings_init NVS 有資料）`
   - refactor 完成後再 commit 一次：`[PHASE-G] refactor: 優化 NVS 讀寫邏輯`

---

### Wave 0: NVS 設定持久化層（`ems_settings` lib）

**目標**：建立設定持久化基礎設施，所有設定項目的讀寫統一由此 lib 提供。

**TDD 執行順序**（red-green-refactor 循環）：

| 步驟 | 測試案例 | 預期 red | 預期 green | 實作內容 |
|------|----------|----------|------------|----------|
| 1 | G0.1 | `settings_init` 不存在 → 編譯失敗 | NVS 有資料，state 值 = NVS 值 | 最小 `settings_init()` 讀 NVS |
| 2 | G0.2 | `settings_init` 無資料 → state 未設 | NVS 無資料，state 值 = 預設值 | 加 fallback 預設值邏輯 |
| 3 | G0.3 | `settings_write` 不存在 → 編譯失敗 | brightness 寫入 NVS + state 同步 | 最小 `settings_write()` |
| 4 | G0.4 | `settings_write` 無邊界檢查 → 邊界錯誤 | 1~5 合法，0/6 拒絕 | 加邊界檢查 |
| 5 | G0.5 | `settings_write` vent_volume 無 0 值 → 錯誤 | 0~5 合法 | 修正 vent_volume 範圍 |
| 6 | G0.6 | `settings_reset_defaults` 未實作 → 錯誤 | 亮度/系統音量/通氣音量→預設，名稱不變 | 實作 reset，確認名稱不觸及 |
| 7 | G0.7 | `settings_set_device_name` 不存在 → 編譯失敗 | LittleFS 寫入成功 | 最小 LittleFS 寫入 |
| 8 | G0.8 | `settings_get_device_name` 未實作 → 錯誤 | LittleFS 讀取成功（含 UTF-8） | 最小 LittleFS 讀取 |
| 9 | G0.9 | `settings_get_device_name` 檔不存在 → 錯誤 | 返回預設值「未命名」 | 加檔不存在 fallback |

**範圍**：

#### 2.0.1 新建 `firmware/lib/ems_settings/`

```
firmware/lib/ems_settings/
├── ems_settings.h        # API + 常數 + settings_state_t
├── ems_settings.cpp      # NVS read/write 實作
└── test/
    └── test_settings.cpp # mock NVS 單元測試
```

#### 2.0.2 常數定義（`ems_settings.h`）

```cpp
// ===== 亮度常數 =====
#define SETTINGS_BRIGHTNESS_MIN     1
#define SETTINGS_BRIGHTNESS_MAX     5
#define SETTINGS_BRIGHTNESS_DEFAULT 3

// ===== 系統音量常數 =====
#define SETTINGS_VOLUME_MIN         1   // 不可靜音
#define SETTINGS_VOLUME_MAX         5
#define SETTINGS_VOLUME_DEFAULT     3

// ===== 通氣音量常數 =====
#define SETTINGS_VENT_VOLUME_MIN     0   // 可靜音
#define SETTINGS_VENT_VOLUME_MAX     5
#define SETTINGS_VENT_VOLUME_DEFAULT 3

// ===== NVS 欄位鍵名 =====
#define NVS_NAMESPACE       "ems_config"
#define NVS_BRIGHTNESS_KEY  "brt"
#define NVS_VOLUME_KEY      "vol"
#define NVS_VENT_VOL_KEY    "vvol"

// ===== 裝置名稱 =====
#define DEVICE_NAME_MAX_LEN   32
#define DEVICE_NAME_DEFAULT   "未命名"
#define DEVICE_NAME_FILE      "/config/device_name.txt"
```

#### 2.0.3 API 設計

```cpp
/**
 * 設定狀態結構（記憶體緩衝，開機時從 NVS 讀入）
 */
typedef struct {
    uint8_t brightness;       // 螢幕亮度 1~5
    uint8_t system_volume;    // 系統音量 1~5
    uint8_t vent_volume;      // 通氣音量 0~5
    char device_name[DEVICE_NAME_MAX_LEN];  // 裝置名稱
} settings_state_t;

/**
 * 初始化：從 NVS 讀取設定到 state
 * @param state 輸出參數（呼叫端持有）
 * @return true 成功讀取（NVS 有資料或 fallback 預設值）
 */
bool settings_init(settings_state_t* state);

/**
 * 寫入單一設定值（寫 NVS + 更新記憶體）
 * @param state   記憶體緩衝
 * @param key     設定鍵（brightness/system_volume/vent_volume）
 * @param value   新值
 * @return true 成功寫入 NVS
 */
bool settings_write(settings_state_t* state, uint8_t key, uint8_t value);

/**
 * 讀取單一設定值（僅記憶體緩衝）
 * @param state 記憶體緩衝
 * @param key   設定鍵
 * @return 當前值
 */
uint8_t settings_read(const settings_state_t* state, uint8_t key);

/**
 * 恢復預設值（只清亮度/系統音量/通氣音量）
 * @param state 記憶體緩衝
 * @return true 成功寫入 NVS
 *
 * 不清除：裝置名稱 / 案件 / Training / 同步狀態（V1 §19.6）
 */
bool settings_reset_defaults(settings_state_t* state);

/**
 * 寫入裝置名稱（LittleFS /config/device_name.txt）
 * @param name 新名稱（由 App 寫入，長度 ≤ DEVICE_NAME_MAX_LEN-1）
 * @return true 成功寫入
 */
bool settings_set_device_name(const char* name);

/**
 * 讀取裝置名稱（LittleFS /config/device_name.txt）
 * @param buf   輸出緩衝
 * @param buf_size 緩衝大小
 * @return true 成功讀取（使用預設值若不存在）
 */
bool settings_get_device_name(char* buf, size_t buf_size);
```

#### 2.0.4 單元測試（`test/test_settings.cpp`）

| 編號 | 測試內容 | 驗證點 |
|------|----------|--------|
| G0.1 | `settings_init` 讀取 NVS 有資料 | state 值 = NVS 值 |
| G0.2 | `settings_init` 讀取 NVS 無資料 | state 值 = 預設值 |
| G0.3 | `settings_write` 寫入 brightness | NVS + state 同步更新 |
| G0.4 | `settings_write` 寫入 system_volume | 邊界：1~5 合法，0/6 拒絕 |
| G0.5 | `settings_write` 寫入 vent_volume | 邊界：0~5 合法 |
| G0.6 | `settings_reset_defaults` | 亮度/系統音量/通氣音量→預設，裝置名稱不變 |
| G0.7 | `settings_set_device_name` | LittleFS 寫入成功 |
| G0.8 | `settings_get_device_name` | LittleFS 讀取成功（含 UTF-8 多字節） |
| G0.9 | `settings_get_device_name` 檔不存在 | 返回預設值「未命名」 |

**測試環境**：PlatformIO native，NVS mock 為 `std::map<uint8_t, uint8_t>`，LittleFS mock 為 `/tmp` 檔案系統。

#### 2.0.5 依賴與影響

- **依賴**：無（獨立 lib）
- **影響**：Wave 1/2 依賴此 lib 的讀寫 API
- **Flash 影響**：約 +1.5 KB（NVS API + 設定邏輯）
- **RAM 影響**：約 +64 bytes（`settings_state_t`）

---

### Wave 1: 系統設定 UI + 恢復預設

**目標**：替換 `GLOBAL_SETTINGS_PLACEHOLDER` 為真實設定畫面，使用者可調整亮度/音量。

**範圍**：

#### 2.1.1 替換 `GLOBAL_SETTINGS_PLACEHOLDER` 處理

**檔案**：`firmware/src/main.cpp`

```cpp
// 舊：
else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
    drawPlaceholder("系統設定", "G 階段");
}

// 新：
else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
    drawSettingsMenu();
}
```

#### 2.1.2 新建 `firmware/src/ui_settings.cpp`

```cpp
/**
 * 設定主選單畫面
 * 項目：裝置名稱 / 螢幕亮度 / 系統音量 / 通氣音量
 * 底部：恢復預設值（紅色警告列）
 */
void drawSettingsMenu();

/**
 * 設定子畫面（亮度/音量調整）
 * @param title   設定名稱（「螢幕亮度」等）
 * @param value   當前值
 * @param max     最大值
 * @param min     最小值
 */
void drawSettingEditor(const char* title, uint8_t value, uint8_t min, uint8_t max);
```

#### 2.1.3 按鍵處理（`firmware/src/input_handler.cpp`）

```cpp
// 在 GLOBAL_SETTINGS_PLACEHOLDER 分支加入：
if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
    // 主鍵：進入子項目
    // 上/下：游標移動
    // 左/右：數值調整
    // 返回：回主功能表
    // 長按主鍵：恢復預設值（彈出確認對話框）
}
```

#### 2.1.4 恢復預設值確認對話框

```text
恢復預設設定？
主鍵確認
返回取消
```

- 確認 → 呼叫 `settings_reset_defaults()` + 更新 UI
- 取消 → 關閉對話框

#### 2.1.5 設定值應用（brightness / volume）

**亮度**：
- 呼叫 `display.setBacklight()` 或 PWM 設定 —— 🟡 **實機待測**（ui_settings 已存 s_brightness，未接 display.setBacklight()）
- 即時生效（不需重啟）

**系統音量**：
- 影響：OHCA 警報音 / EPI 預警音 / 成功音 / 錯誤音 / 案件完成音 —— 🟡 **實機待測**（ui_settings 已存 s_system_volume，未接 beep 函式音量計算）
- 修改 `beepPulsesRemaining` 等 beep 函式的音量計算

**通氣音量**：
- 沿用現有 `ventVolume` 變數 —— 🟡 **實機待測**（ui_settings 已存 s_vent_volume，未同步到 ventVolume global）
- 從 `settings_state` 讀取同步

#### 2.1.6 單元測試（新增於 `test/test_settings_ui.cpp`）

**TDD 執行順序**（red-green-refactor 循環）：

| 步驟 | 測試案例 | 預期 red | 預期 green | 實作內容 |
|------|----------|----------|------------|----------|
| 1 | G1.1 | `drawSettingsMenu` 不存在 → 編譯失敗 | 4 項目文字正確 | 最小 `drawSettingsMenu()` |
| 2 | G1.2 | 游標未實作 → 游標位置錯誤 | 游標位置正確 | 加游標邏輯 |
| 3 | G1.3 | 數值調整未實作 → 超出範圍 | 數值在 min~max | 加左右鍵調整 |
| 4 | G1.4 | 長按未實作 → 無確認對話框 | 彈出確認對話框 | 加長按判斷 + 對話框 |
| 5 | G1.5 | 確認未實作 → 設定值不變 | 亮度/系統音量/通氣音量→預設 | 加確認處理 |
| 6 | G1.6 | 取消未實作 → 設定值已改 | 設定值不變 | 加取消處理 |

---

### Wave 2: 裝置名稱管理 + BLE 寫入

**目標**：裝置名稱 NVS/LittleFS 持久化 + BLE 寫入支援。

**範圍**：

#### 2.2.1 裝置名稱讀寫（Wave 0 lib 已提供）

- 使用 `ems_settings` lib 的 `settings_set_device_name()` / `settings_get_device_name()`

#### 2.2.2 BLE Device Info Service 寫入

**檔案**：`firmware/lib/ble_nus/`（新增 characteristic）

```cpp
// 新增 BLE characteristic：device_name_write
// 屬性：writeWithoutResponse / write
// 回調：收到寫入 → settings_set_device_name() → 寫入成功回 ack
```

#### 2.2.3 裝置名稱顯示（設定畫面）🟡 UI 已接線，顯示內容待 §2.2.1 落地

- 在設定主選單顯示「裝置名稱：[current_name]」
- 進入子項目 → 顯示「請連接 App 設定裝置名稱」（裝置端不負責中文輸入）

#### 2.2.4 案件同步時帶入裝置名稱

**檔案**：`firmware/src/sync_send.cpp`

```cpp
// 現：js_meta.device_name = SYNC_DEVICE_NAME;
// 新：char name[DEVICE_NAME_MAX_LEN];
//     settings_get_device_name(name, sizeof(name));
//     js_meta.device_name = name;
```

#### 2.2.5 有未同步案件時不可修改裝置名稱 ✅ 完成（2026-07-18）

> **2026-07-18 改寫**：原文為「案件進行中不可修改」，但該情境在當前架構下不可達
> （進設定選單必經 `exitOhcaCase()`，案件已重置——詳見 §二、零、一 #6）。
> 依「加防禦碼前先驗證情境可達」原則，改用下列**可達且有真實理由**的判準。

**判準**：storage 內存在任一 `!case_meta_is_synced(m)` 的案件 → 設定選單「裝置名稱」項目置灰。

**理由**：`sync_send.cpp` 是在**同步當下**才讀 `device_name` 寫進 payload。若此時已改名，
先前錄製的未同步案件會帶著新名字送出，與錄製當下的裝置不符，造成紀錄歸屬錯亂。

**實作**（已完成）：
- 純函式 `storage_has_unsynced_case(const case_meta_t*, uint16_t)` 置於 `ems_storage_logic.h`，
  複用既有的 `case_meta_is_synced()` 語意；native 測試見 test_ems_storage_logic Group I（5 案例）
- `drawSettingsMenu()` 改收 `bool device_name_locked`（呼叫端算好再傳），
  對齊 DisplaySnapshot「衍生值由呼叫端先算，lib 不依賴 runtime 狀態」的既有原則
- `(ems::CaseMode)2` 裸 cast 與 `case_mode` 參數已移除
- `refreshDeviceNameLock()`（input_handler.cpp）在**進入設定選單時**掃描一次即可——
  lock 狀態只由「儲存新案件」或「同步完成」改變，兩者都不可能在設定選單內發生
- 測試同時覆蓋 lock=true 與 lock=false 兩個方向（原測試只驗 true，故 #6 未被抓到），
  並實際斷言繪製顏色（DIM vs WHITE）而非只斷言「有畫東西」

#### 2.2.6 單元測試

**TDD 執行順序**（red-green-refactor 循環）：

| 步驟 | 測試案例 | 預期 red | 預期 green | 實作內容 |
|------|----------|----------|------------|----------|
| 1 | G2.1 | BLE write callback 未實作 → 寫入失敗 | settings_state 同步更新 | 加 BLE characteristic + callback |
| 2 | G2.2 | 同步未讀取 device_name → payload 錯誤 | payload 正確 | 改 `settings_get_device_name()` + static buf + fallback log（實機待測） |
| 3 | G2.3 | 案件中未置灰 → 可修改裝置名稱 | 裝置名稱項目置灰 | 加案件狀態判斷 + 置灰 |

---

### Wave 3: Type-C 管理工具（Electron）

**目標**：獨立 Electron 工具，USB CDC 列案件 / 匯出 / 清除。

**範圍**：

#### 2.3.1 Electron 專案設定

```
tools/ems-cdc-tool/
├── package.json
├── main.js              # Electron 主進程
├── index.html           # UI
├── renderer.js          # 前端邏輯
└── assets/
```

#### 2.3.2 USB CDC 連線

- 偵測 ESP32 USB CDC 裝置
- 連線協議：重現 NUS payload 格式（JSON over CDC）

#### 2.3.3 列出案件

- 請求裝置端回傳所有 OHCA + Training 案件
- 顯示列表（seq / 類型 / 事件數 / 時間）

#### 2.3.4 匯出

- 選取案件 → 匯出 CSV / JSON
- CSV 欄位：event_id, type, timestamp_ms, elapsed_ms, count, actual_time_null

#### 2.3.5 清除案件

- 選取「清除全部」→ 二次確認對話框
- 提示備援 → 確認 → 發送清除命令
- 裝置端執行 `storage_clear_all()`（僅 OHCA + Training，不清除系統設定）

#### 2.3.6 不可修改既有案件

- 工具只提供讀取 / 匯出 / 清除
- 不提供編輯 / 寫入案件內容的 API

#### 2.3.7 單元測試（E2E）

**TDD 執行順序**（red-green-refactor 循環）：

| 步驟 | 測試案例 | 預期 red | 預期 green | 實作內容 |
|------|----------|----------|------------|----------|
| 1 | G3.1 | Electron 未連 CDC → 無法偵測 | 偵測到 ESP32 CDC | 最小 USB CDC 連線邏輯 |
| 2 | G3.2 | 列案件未實作 → 列表空白 | 列表正確 | 加案件請求 + 顯示 |
| 3 | G3.3 | 匯出 CSV 未實作 → 欄位錯誤 | 欄位正確 | 加 CSV 匯出邏輯 |
| 4 | G3.4 | 匯出 JSON 未實作 → payload 錯誤 | payload 正確 | 加 JSON 匯出邏輯 |
| 5 | G3.5 | 清除未確認 → 無二次確認 | 二次確認 → 清除成功 | 加確認對話框 + 清除命令 |
| 6 | G3.6 | 有編輯 API → 可修改案件 | 無編輯 API | 確認無編輯 API（驗證性測試） |

---

## 三、依賴關係與執行順序

```
Wave 0 (NVS 層)
    │
    ├──→ Wave 1 (系統設定 UI)
    │        │
    │        └──→ Wave 2 (裝置名稱 + BLE 寫入)
    │
    └──→ Wave 3 (Type-C Electron 工具)  ← 可並行
```

### 3.1 建議執行順序

| 順序 | Wave | 理由 |
|------|------|------|
| 1 | Wave 0 | 基礎設施，所有設定項目的讀寫統一由此 lib 提供 |
| 2 | Wave 1 | 替換 placeholder，完成設定 UI 核心功能 |
| 3 | Wave 2 | 裝置名稱管理（依賴 Wave 0 的 NVS API） |
| 4 | Wave 3 | Type-C 工具（可與 Wave 0~2 並行） |

### 3.2 並行建議

- **Wave 0~2**：韌體部分，建議依序執行（Wave 0 是基礎）
- **Wave 3**：Type-C 工具可獨立進行，不需等韌體完成（協議已定義：NUS payload 格式）

---

## 四、驗收標準

### 4.1 韌體驗收（Wave 0~2）

| 編號 | 驗收內容 | 來源 |
|------|----------|------|
| V1 | 恢復預設不影響案件 / Training / 裝置名稱 | V1 §19.6 |
| V2 | 亮度 1~5 即時生效，開機沿用 | V1 §19.3 |
| V3 | 系統音量 1~5 即時生效，開機沿用 | V1 §19.4 |
| V4 | 通氣音量 0~5 即時生效，開機沿用 | V1 §19.5 |
| V5 | 裝置名稱由 App 寫入，開機沿用 | V1 §19.2 |
| V6 | 案件中不可修改裝置名稱 | V1 §19.2 |
| V7 | 系統音量不可靜音（最低 1） | V1 §19.4 |
| V8 | 通氣音量可靜音（0） | V1 §19.5 |

### 4.2 Type-C 工具驗收（Wave 3）

| 編號 | 驗收內容 | 來源 |
|------|----------|------|
| C1 | USB CDC 連線 ESP32 | V1 §18.3 |
| C2 | 列出案件（OHCA + Training） | V1 §18.3 |
| C3 | 匯出 CSV / JSON | V1 §18.3 |
| C4 | 二次確認後清除 | V1 §18.3 |
| C5 | 不可修改既有案件 | V1 §18.3 |

---

## 五、風險與注意事項

### 5.1 NVS 容量

- ESP32 NVS partition 預設 0x10000（64 KB）
- 4 個 uint8_t 設定值 ≈ 4 bytes，無容量壓力

### 5.2 裝置名稱 UTF-8

- 裝置名稱可能含中文（「安康91」）
- LittleFS 寫入需確保 UTF-8 編碼正確
- NVS string key 需注意最大長度

### 5.3 通氣音量與 ventVolume 同步

- 現有 `ventVolume` 變數在 `main.cpp` 全域
- Wave 0 需確保 `settings_state.vent_volume` 與 `ventVolume` 同步
- 建議：`ventVolume` 改為從 `settings_state` 讀取，不再獨立維護

### 5.4 Type-C 工具與 BLE 協議一致性

- Type-C 工具重用 NUS payload 格式
- 確保 USB CDC 傳輸的 JSON 結構與 BLE NUS 一致
- 方便後續 App / Type-C 工具共用解析碼

---

## 六、預估工作量

| Wave | 新增檔案 | 預估時間 | 測試 |
|------|----------|----------|------|
| Wave 0 | 3（lib）+ 1（test） | 1~2 天 | 9 case |
| Wave 1 | 2（ui + handler） | 1~2 天 | 6 case |
| Wave 2 | 1（BLE callback） | 0.5~1 天 | 3 case |
| Wave 3 | 5（electron） | 2~3 天 | 6 E2E |

> 💡 **總計**：約 5~8 天（韌體 3~5 天 + Type-C 工具 2~3 天）

---

## 七、後續整合（Phase H 相關）

### 7.1 電池資訊（V1 §19.7）

- 顯示：名稱 / 型號 / 序號 / 韌體 / 電池 / 充電狀態
- **電池資訊**需 Phase H 實作（ADC 腳已用盡，見 `power-module-purchase.md §10.6`）
- Phase G 可先顯示「電池：待 Phase H」

### 7.2 Type-C 連線狀態（V1 §18.4）

- 設定內 Type-C 連線頁只顯示狀態
- 不提供手動模式切換
- Phase G 可實作「已連線 / 未連線」狀態顯示

### 7.3 裝置資訊（V1 §19.7）

- 名稱：從 `settings_get_device_name()` 讀取
- 型號：`EMS DoseSync Pro`（字串常數）
- 序號：`DSP-0001`（字串常數，未來可改為 NVS 寫入）
- 韌體：編譯時嵌入 `FW_VERSION`
- 電池：Phase H 範圍
- 充電狀態：Phase H 範圍

---

## 八、Git 分支建議

```bash
# 建立 Phase G 分支
git checkout -b feat/phase-g-system-settings

# Wave 0
git checkout -b feat/phase-g-wave0-nvs
# ... commit ...
git checkout feat/phase-g-system-settings
git merge feat/phase-g-wave0-nvs

# Wave 1
git checkout -b feat/phase-g-wave1-ui
# ... commit ...
git checkout feat/phase-g-system-settings
git merge feat/phase-g-wave1-ui

# Wave 2
git checkout -b feat/phase-g-wave2-device-name
# ... commit ...
git checkout feat/phase-g-system-settings
git merge feat/phase-g-wave2-device-name

# Wave 3（獨立，可並行）
git checkout -b feat/phase-g-wave3-cdc-tool
# ... commit ...
git checkout feat/phase-g-system-settings
git merge feat/phase-g-wave3-cdc-tool
```

---

## 九、參考文件

| 文件 | 用途 |
|------|------|
| `docs/EMS_DoseSync_Pro_Prototype_V1.md §19` | 系統設定 PM 規格 |
| `docs/EMS_DoseSync_Pro_Prototype_V1.md §18.2-§18.4` | Type-C 管理 PM 規格 |
| `docs/pm-dev-spec.md §15` | 系統設定工程規格 |
| `docs/pm-dev-spec.md §三` | Type-C 工具工程規格 |
| `docs/gpio-allocation.md` | GPIO 分配（確認亮度/音量腳位） |
| `docs/power-module-purchase.md §10.6` | 電池顯示能力現況 |
| `tasks/todo.md` | 開發進度追蹤 |
