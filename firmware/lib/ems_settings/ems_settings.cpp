// EMS DoseSync Pro — Wave 0: NVS 設定持久化層（實作）
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.0
//
// 設計：
//   - ESP32 端：走 NVS + LittleFS（#ifdef ARDUINO）
//   - native 端：mock NVS（std::map）+ mock FS（header-only in .h）

#include "ems_settings.h"

#ifdef ARDUINO
#include <NVS.h>
#include <Arduino.h>
#endif

#include <map>
#include <string>
#include <cstring>

// ============================================================
//  Native test 支援：mock NVS 實作（無需 ESP32）
// ============================================================

/**
 * Mock NVS：以 static map 模擬 NVS 儲存空間
 *
 * layout:
 *   key 0x01 → brightness
 *   key 0x02 → system_volume
 *   key 0x03 → vent_volume
 */
static std::map<uint8_t, uint8_t> s_mock_nvs;

void mock_nvs_clear() {
    s_mock_nvs.clear();
}

void mock_nvs_write(uint8_t brightness, uint8_t volume, uint8_t vent_vol) {
    s_mock_nvs[SETTING_KEY_BRIGHTNESS] = brightness;
    s_mock_nvs[SETTING_KEY_SYSTEM_VOL] = volume;
    s_mock_nvs[SETTING_KEY_VENT_VOL] = vent_vol;
}

bool mock_nvs_read(uint8_t* brightness, uint8_t* volume, uint8_t* vent_vol) {
    auto it_b = s_mock_nvs.find(SETTING_KEY_BRIGHTNESS);
    auto it_v = s_mock_nvs.find(SETTING_KEY_SYSTEM_VOL);
    auto it_vv = s_mock_nvs.find(SETTING_KEY_VENT_VOL);

    bool has_data = (it_b != s_mock_nvs.end() &&
                     it_v != s_mock_nvs.end() &&
                     it_vv != s_mock_nvs.end());

    if (has_data) {
        *brightness = it_b->second;
        *volume = it_v->second;
        *vent_vol = it_vv->second;
    }
    return has_data;
}

/**
 * settings_init 的 native 版本
 * 邏輯：讀 mock NVS，有資料就用 NVS 值，無資料就 fallback 預設值
 */
bool settings_init_mock(settings_state_t* state) {
    uint8_t br, vol, vv;
    bool has_data = mock_nvs_read(&br, &vol, &vv);

    if (has_data) {
        state->brightness = br;
        state->system_volume = vol;
        state->vent_volume = vv;
    } else {
        state->brightness = SETTINGS_BRIGHTNESS_DEFAULT;
        state->system_volume = SETTINGS_VOLUME_DEFAULT;
        state->vent_volume = SETTINGS_VENT_VOLUME_DEFAULT;
    }

    strncpy(state->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX_LEN - 1);
    state->device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';

    return true;
}

/**
 * settings_write 的 native 版本
 * 邏輯：邊界檢查 → 寫入 state + mock NVS
 */
bool settings_write_mock(settings_state_t* state, uint8_t key, uint8_t value) {
    uint8_t min_val, max_val;

    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            min_val = SETTINGS_BRIGHTNESS_MIN;
            max_val = SETTINGS_BRIGHTNESS_MAX;
            break;
        case SETTING_KEY_SYSTEM_VOL:
            min_val = SETTINGS_VOLUME_MIN;
            max_val = SETTINGS_VOLUME_MAX;
            break;
        case SETTING_KEY_VENT_VOL:
            min_val = SETTINGS_VENT_VOLUME_MIN;
            max_val = SETTINGS_VENT_VOLUME_MAX;
            break;
        default:
            return false;
    }

    // 邊界檢查
    if (value < min_val || value > max_val) {
        return false;
    }

    // 寫入 state
    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            state->brightness = value;
            break;
        case SETTING_KEY_SYSTEM_VOL:
            state->system_volume = value;
            break;
        case SETTING_KEY_VENT_VOL:
            state->vent_volume = value;
            break;
    }

    // 寫入 mock NVS
    s_mock_nvs[key] = value;

    return true;
}

/**
 * settings_read 的 native 版本（僅讀 state）
 */
uint8_t settings_read_mock(const settings_state_t* state, uint8_t key) {
    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            return state->brightness;
        case SETTING_KEY_SYSTEM_VOL:
            return state->system_volume;
        case SETTING_KEY_VENT_VOL:
            return state->vent_volume;
        default:
            return 0;
    }
}

/**
 * settings_reset_defaults 的 native 版本
 * 邏輯：亮度/系統音量/通氣音量→預設，裝置名稱不變，寫入 NVS
 */
bool settings_reset_defaults_mock(settings_state_t* state) {
    state->brightness = SETTINGS_BRIGHTNESS_DEFAULT;
    state->system_volume = SETTINGS_VOLUME_DEFAULT;
    state->vent_volume = SETTINGS_VENT_VOLUME_DEFAULT;

    // 裝置名稱不變（不清除）

    // 寫入 NVS
    mock_nvs_write(state->brightness, state->system_volume, state->vent_volume);

    return true;
}

// ============================================================
//  Mock FS：裝置名稱檔案系統（header-only, static map）
// ============================================================

static std::map<std::string, std::string> s_mock_fs;

void mock_fs_clear() {
    s_mock_fs.clear();
}

bool mock_fs_write(const char* path, const char* content, size_t len) {
    s_mock_fs[std::string(path)] = std::string(content, len);
    return true;
}

bool mock_fs_read(const char* path, char* buf, size_t buf_size, size_t* out_len) {
    auto it = s_mock_fs.find(std::string(path));
    if (it == s_mock_fs.end()) {
        // 檔不存在 → 寫入預設值
        strncpy(buf, DEVICE_NAME_DEFAULT, buf_size - 1);
        buf[buf_size - 1] = '\0';
        if (out_len) {
            *out_len = strlen(buf);
        }
        return false;
    }

    size_t content_len = it->second.size();
    if (content_len >= buf_size) {
        return false;
    }

    memcpy(buf, it->second.c_str(), content_len + 1);  // +1 for null terminator
    if (out_len) {
        *out_len = content_len;
    }
    return true;
}

// ============================================================
//  ESP32 端實作：NVS 讀寫（#ifdef ARDUINO）
// ============================================================

#ifdef ARDUINO

/**
 * 從 NVS 讀取設定值
 * @param key NVS key 名稱
 * @param default_val 無資料時的預設值
 * @return 讀取值
 */
static uint8_t nvs_read_uint8(const char* key, uint8_t default_val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return default_val;

    uint8_t val = default_val;
    if (nvs_get_u8(handle, key, &val) != ESP_OK) {
        val = default_val;
    }
    nvs_close(handle);
    return val;
}

/**
 * 寫入設定值到 NVS
 * @param key NVS key 名稱
 * @param val 新值
 * @return true 成功
 */
static bool nvs_write_uint8(const char* key, uint8_t val) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.printf("[SETTINGS] ERROR nvs_open \"%s\": %d\n", key, err);
        return false;
    }

    err = nvs_set_u8(handle, key, val);
    if (err != ESP_OK) {
        Serial.printf("[SETTINGS] ERROR nvs_set_u8 \"%s\": %d\n", key, err);
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        Serial.printf("[SETTINGS] ERROR nvs_commit \"%s\": %d\n", key, err);
        return false;
    }
    return true;
}

bool settings_init(settings_state_t* state) {
    state->brightness = nvs_read_uint8(NVS_BRIGHTNESS_KEY, SETTINGS_BRIGHTNESS_DEFAULT);
    state->system_volume = nvs_read_uint8(NVS_VOLUME_KEY, SETTINGS_VOLUME_DEFAULT);
    state->vent_volume = nvs_read_uint8(NVS_VENT_VOL_KEY, SETTINGS_VENT_VOLUME_DEFAULT);
    // 裝置名稱暫由 LittleFS 管理（Phase 2）
    strncpy(state->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX_LEN - 1);
    state->device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
    return true;
}

bool settings_write(settings_state_t* state, uint8_t key, uint8_t value) {
    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            if (value < SETTINGS_BRIGHTNESS_MIN || value > SETTINGS_BRIGHTNESS_MAX) return false;
            state->brightness = value;
            break;
        case SETTING_KEY_SYSTEM_VOL:
            if (value < SETTINGS_VOLUME_MIN || value > SETTINGS_VOLUME_MAX) return false;
            state->system_volume = value;
            break;
        case SETTING_KEY_VENT_VOL:
            if (value < SETTINGS_VENT_VOLUME_MIN || value > SETTINGS_VENT_VOLUME_MAX) return false;
            state->vent_volume = value;
            break;
        default:
            return false;
    }
    return true;
}

uint8_t settings_read(const settings_state_t* state, uint8_t key) {
    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            return state->brightness;
        case SETTING_KEY_SYSTEM_VOL:
            return state->system_volume;
        case SETTING_KEY_VENT_VOL:
            return state->vent_volume;
        default:
            return 0;
    }
}

bool settings_reset_defaults(settings_state_t* state) {
    state->brightness = SETTINGS_BRIGHTNESS_DEFAULT;
    state->system_volume = SETTINGS_VOLUME_DEFAULT;
    state->vent_volume = SETTINGS_VENT_VOLUME_DEFAULT;
    bool ok1 = nvs_write_uint8(NVS_BRIGHTNESS_KEY, SETTINGS_BRIGHTNESS_DEFAULT);
    bool ok2 = nvs_write_uint8(NVS_VOLUME_KEY, SETTINGS_VOLUME_DEFAULT);
    bool ok3 = nvs_write_uint8(NVS_VENT_VOL_KEY, SETTINGS_VENT_VOLUME_DEFAULT);
    if (!ok1 || !ok2 || !ok3) {
        Serial.println("[SETTINGS] ERROR reset_defaults NVS write failed");
        return false;
    }
    return true;
}

bool settings_set_device_name(const char* name) {
    // Phase 2: LittleFS /config/device_name.txt
    return false;
}

bool settings_get_device_name(char* buf, size_t buf_size) {
    // Phase 2: LittleFS /config/device_name.txt
    strncpy(buf, DEVICE_NAME_DEFAULT, buf_size - 1);
    buf[buf_size - 1] = '\0';
    return false;
}

#endif  // ARDUINO
