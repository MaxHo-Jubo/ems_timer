// EMS DoseSync Pro — Wave 0: NVS 設定持久化層
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.0 / V1 §19
//
// 設計：
//   - ESP32 端：走 NVS (non-volatile storage) 持久化
//   - native test：mock_nvs.h 以 std::map<uint8_t, uint8_t> 模擬
//   - #ifdef ARDUINO 守護：native env 可 #include 但不連結 ESP32 API

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===== 亮度常數 =====
#define SETTINGS_BRIGHTNESS_MIN     1   // 螢幕亮度最低
#define SETTINGS_BRIGHTNESS_MAX     5   // 螢幕亮度最高
#define SETTINGS_BRIGHTNESS_DEFAULT 3   // 預設中等亮度

// ===== 系統音量常數 =====
// 2026-09-06 工程變更：原為 1~5 級且不可靜音（V1 §19.4）。蜂鳴器是主動式、
// 由 digitalWrite 開/關驅動，同一顆只有一種音量，1~5 在硬體上完全沒有差別，
// 級數是假的；改成開/關兩態，並放寬為可靜音（0）。此設定只 gate UI 確認音，
// 危急警報（ALARMING 連續發報 / EPI 到期）不受影響，見 input_handler.cpp
// uiConfirmBeep()。
#define SETTINGS_VOLUME_MIN         0   // 0 = 關（靜音）
#define SETTINGS_VOLUME_MAX         1   // 1 = 開
#define SETTINGS_VOLUME_DEFAULT     1   // 預設開

// ===== 通氣音量常數 =====
// 2026-09-06 工程變更：原為 0~5 級（V1 §19.5）。理由同系統音量——decideVentOutput()
// 本來就只用 `volume > 0` 當 gate，1~5 之間毫無行為差異，改成開/關兩態。
// 需與 ems_vent_metronome.h 的 VENT_VOLUME_* 一致（同一個值的兩份常數，
// input_handler.cpp 有 static_assert 鎖住）。
#define SETTINGS_VENT_VOLUME_MIN     0   // 0 = 關（靜音）
#define SETTINGS_VENT_VOLUME_MAX     1   // 1 = 開
#define SETTINGS_VENT_VOLUME_DEFAULT 1   // 預設開

// 兩個音量在 2026-09-06 之前的值域上界（系統音量 1~5、通氣音量 0~5）。只用於開機
// 時把舊裝置 NVS 裡的值遷移成 0/1，不是現行值域——現行值域是上面的 *_MIN/*_MAX。
#define SETTINGS_LEGACY_VOLUME_MAX  5

// 兩態設定的「關」值。判斷是否啟用一律走下方 settings_toggle_enabled()，不要在
// 呼叫端裸寫 `> 0`。
#define SETTINGS_TOGGLE_OFF  0

/**
 * 兩態（開/關）設定是否啟用（純函式）。
 *
 * 存在理由：這個判斷原本以 `value > 0` 散在五個呼叫點（UI 確認音 gate、設定編輯器
 * 兩處、通氣畫面兩處），既是裸寫的 Magic Number，也讓「什麼算開」沒有單一出處——
 * 日後若值域再變（例如加入第三態），漏改任一處就會讓畫面顯示與實際發聲行為分歧。
 *
 * 刻意寫成 `!= OFF` 而非 `== 1`：載入時雖已由 settings_normalize_toggle() 把值收進
 * 0/1，但 NVS 遷移寫回失敗時 runtime 仍可能短暫帶著舊值域的數字，此時「非關即開」
 * 才是對的解讀（舊值域 1~5 全都會發聲）。
 *
 * @param value 設定值（0 = 關，非 0 = 開）
 * @return      true = 開
 */
static inline bool settings_toggle_enabled(uint8_t value) {
    // STEP 01: 只有「關」這個值代表停用，其餘一律視為啟用
    return value != SETTINGS_TOGGLE_OFF;
}

/**
 * 兩態（開/關）設定值的舊值域遷移（純函式）。
 *
 * 2026-09-06 把系統音量與通氣音量從 1~5／0~5 收斂成 0=關 / 1=開。已經在使用者手上
 * 的裝置，NVS 裡存的是舊值域的數字（預設 3），開機直接載入會讓 runtime 帶著一個
 * 值域外的值跑：顯示端用 `> 0` 判斷會顯示「開」看似正常，但按一次「關」只會被
 * clamp 成 1（3 - 1 = 2 → clamp → 1），畫面停在「開」像是按鍵壞了，要按第二次才
 * 關得掉。所以載入時就要把值收進 0/1，不能留給下游各自解讀。
 *
 * 這不是 no-fallback-after-root-cause 所禁止的靜默 fallback——舊值域的每個合法值
 * 都有明確且唯一的對應（0 = 靜音 → 關；1~5 一律會發聲 → 開），呼叫端會把遷移後的
 * 值寫回 NVS，跑完一次之後就不再有值域外的值存在。
 *
 * @param raw          NVS 讀出的原始值（可能是舊值域，也可能是損壞的任意 byte）
 * @param default_val  raw 落在任何已知值域之外時採用的預設值
 * @return             0（關）或 1（開）
 */
static inline uint8_t settings_normalize_toggle(uint8_t raw, uint8_t default_val) {
    // STEP 01: 舊值域與新值域的 0 語意相同（靜音／關），直接沿用
    if (raw == 0) {
        return 0;
    }
    // STEP 02: 舊值域 1~5 全都是「會發聲」，一律遷移成 1（開）
    if (raw <= SETTINGS_LEGACY_VOLUME_MAX) {
        return 1;
    }
    // STEP 03: 兩個值域都容不下的值（NVS 損壞／被別的韌體寫過）→ 回預設，
    //   不猜測它原本想表達什麼
    return default_val;
}

// ===== NVS 欄位鍵名 =====
#define NVS_NAMESPACE       "ems_config"
#define NVS_BRIGHTNESS_KEY  "brt"
#define NVS_VOLUME_KEY      "vol"
#define NVS_VENT_VOL_KEY    "vvol"

// ===== 裝置名稱 =====
#define DEVICE_NAME_MAX_LEN   32
#define DEVICE_NAME_DEFAULT   "未命名"
#define DEVICE_NAME_DIR       "/config"
#define DEVICE_NAME_FILE      "/config/device_name.txt"

// ===== 設定鍵值 =====
#define SETTING_KEY_BRIGHTNESS    0x01
#define SETTING_KEY_SYSTEM_VOL    0x02
#define SETTING_KEY_VENT_VOL      0x03

/**
 * 設定狀態結構（記憶體緩衝，開機時從 NVS 讀入）
 */
typedef struct {
    uint8_t brightness;       // 螢幕亮度 1~5（2026-09-06 起已離開選單，背光不可控）
    uint8_t system_volume;    // 系統音量 0=關 / 1=開
    uint8_t vent_volume;      // 通氣音量 0=關 / 1=開
    char device_name[DEVICE_NAME_MAX_LEN];  // 裝置名稱
} settings_state_t;

// ============================================================
//  裝置名稱淨化（純邏輯，ARDUINO 與 native 共用）
// ============================================================

/**
 * 淨化外部傳入的裝置名稱（BLE payload → 可安全存放的 C 字串）。
 *
 * 處理三件事：
 *   1. 拒絕空輸入（空 payload 不應把裝置名稱清掉）
 *   2. 在第一個內嵌 NUL 處截止（BLE 送來的是 raw bytes，可能含 NUL）
 *   3. 超長時**切在 UTF-8 字元邊界**——預設名稱本身就是中文「未命名」，
 *      純 byte 截斷會產生半個字，顯示端出現亂碼
 *
 * 刻意收 (ptr, len) 而非只收 const char*：BLE payload 不保證 NUL 結尾，
 * 用 strlen 推斷長度會讀出界（對齊專案規則：byte buffer API 必須帶明確 length）。
 *
 * @param raw       原始位元組（允許不以 NUL 結尾；nullptr 時回 false）
 * @param raw_len   原始位元組長度
 * @param out       輸出緩衝（成功時必為 NUL 結尾）
 * @param out_size  輸出緩衝大小（為 0 時回 false，避免 out_size-1 下溢）
 * @return true 產生了長度 ≥ 1 的有效名稱
 */
bool device_name_sanitize(const char* raw, size_t raw_len, char* out, size_t out_size);

// ============================================================
//  ESP32 端實作（ARDUINO 環境）
// ============================================================

#ifdef ARDUINO

/**
 * 初始化：從 NVS 讀取設定到 state
 * @param state 輸出參數（呼叫端持有）
 * @return true 成功讀取（NVS 有資料或 fallback 預設值）
 */
bool settings_init(settings_state_t* state);

/**
 * 寫入單一設定值（寫 NVS + 更新記憶體）
 * @param state   記憶體緩衝
 * @param key     設定鍵（SETTING_KEY_*）
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

#endif  // ARDUINO

// ============================================================
//  Native test 支援：mock NVS 讀寫（無需 ESP32）
// ============================================================

/**
 * 初始化 mock NVS 資料（測試用，清除所有資料）
 */
void mock_nvs_clear(void);

/**
 * 寫入 mock NVS 資料（測試用）
 * @param brightness 亮度值
 * @param volume     系統音量值
 * @param vent_vol   通氣音量值
 */
void mock_nvs_write(uint8_t brightness, uint8_t volume, uint8_t vent_vol);

/**
 * 讀取 mock NVS 資料（測試用）
 * @param brightness 輸出亮度值
 * @param volume     輸出系統音量值
 * @param vent_vol   輸出通氣音量值
 * @return true 有資料
 */
bool mock_nvs_read(uint8_t* brightness, uint8_t* volume, uint8_t* vent_vol);

/**
 * 模擬 settings_init 的 native 版本（直接讀 mock NVS）
 * @param state 輸出參數
 * @return true 成功
 */
bool settings_init_mock(settings_state_t* state);

/**
 * 模擬 settings_write 的 native 版本（直接寫 mock NVS）
 * @param state 記憶體緩衝
 * @param key   設定鍵
 * @param value 新值
 * @return true 成功
 */
bool settings_write_mock(settings_state_t* state, uint8_t key, uint8_t value);

/**
 * 模擬 settings_reset_defaults 的 native 版本
 * @param state 記憶體緩衝
 * @return true 成功
 */
bool settings_reset_defaults_mock(settings_state_t* state);

/**
 * 模擬 settings_read 的 native 版本（僅讀 state）
 * @param state 記憶體緩衝
 * @param key   設定鍵
 * @return 當前值
 */
uint8_t settings_read_mock(const settings_state_t* state, uint8_t key);

/**
 * 模擬 device name 檔案系統（header-only, std::map）
 */
void mock_fs_clear(void);
bool mock_fs_write(const char* path, const char* content, size_t len);
bool mock_fs_read(const char* path, char* buf, size_t buf_size, size_t* out_len);

/**
 * 同步裝置名稱到 state（測試用，BLE callback 收到寫入後呼叫）
 * @param state  記憶體緩衝
 * @param name   新名稱
 * @return true 成功同步
 */
bool settings_sync_device_name(settings_state_t* state, const char* name);
