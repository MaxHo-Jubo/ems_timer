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
#define SETTINGS_VOLUME_MIN         1   // 不可靜音（V1 §19.4）
#define SETTINGS_VOLUME_MAX         5
#define SETTINGS_VOLUME_DEFAULT     3

// ===== 通氣音量常數 =====
#define SETTINGS_VENT_VOLUME_MIN     0   // 可靜音（V1 §19.5）
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

// ===== 設定鍵值 =====
#define SETTING_KEY_BRIGHTNESS    0x01
#define SETTING_KEY_SYSTEM_VOL    0x02
#define SETTING_KEY_VENT_VOL      0x03

/**
 * 設定狀態結構（記憶體緩衝，開機時從 NVS 讀入）
 */
typedef struct {
    uint8_t brightness;       // 螢幕亮度 1~5
    uint8_t system_volume;    // 系統音量 1~5
    uint8_t vent_volume;      // 通氣音量 0~5
    char device_name[DEVICE_NAME_MAX_LEN];  // 裝置名稱
} settings_state_t;

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
