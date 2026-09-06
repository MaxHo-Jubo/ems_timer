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
#include <LittleFS.h>  // 裝置名稱持久化（/config/device_name.txt）
#endif

#include <map>
#include <string>
#include <cstring>

#include "ems_utf8.h"  // utf8IsContinuationByte()（Impl-Phase G 抽出共用，見該檔案註解）

// ============================================================
//  裝置名稱淨化（純邏輯，ARDUINO 與 native 共用）
// ============================================================

bool device_name_sanitize(const char* raw, size_t raw_len, char* out, size_t out_size) {
    // STEP 01: 參數防護。out_size 為 0 時後續的 out_size-1 會下溢成 SIZE_MAX
    if (raw == nullptr || out == nullptr || out_size == 0 || raw_len == 0) {
        return false;
    }

    // STEP 02: 在第一個內嵌 NUL 處截止——BLE 送來的是 raw bytes，NUL 之後的內容
    //          不屬於這個名稱，直接採用會把垃圾位元組帶進顯示層
    size_t effective_len = raw_len;
    for (size_t i = 0; i < raw_len; i++) {
        if (raw[i] == '\0') {
            effective_len = i;
            break;
        }
    }
    if (effective_len == 0) {
        return false;
    }

    // STEP 03: 限制在輸出緩衝可容納的長度內（保留 1 byte 給 NUL）
    size_t copy_len = effective_len;
    if (copy_len > out_size - 1) {
        copy_len = out_size - 1;

        // STEP 03.01: 若切點落在 UTF-8 字元中間，往回退到字元起始邊界。
        //   raw[copy_len] 是第一個被丟棄的 byte；它若是 continuation byte，
        //   代表前一個字元被切成兩半，必須整個字元一起丟掉。
        while (copy_len > 0 && utf8IsContinuationByte((unsigned char)raw[copy_len])) {
            copy_len--;
        }
        if (copy_len == 0) {
            // 單一字元就超過整個緩衝（緩衝過小），無法產生任何完整字元
            return false;
        }
    }

    // STEP 04: 複製並補 NUL
    memcpy(out, raw, copy_len);
    out[copy_len] = '\0';
    return true;
}

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
 * 邏輯：讀 mock NVS，有資料就用 NVS 值，無資料就 fallback 預設值。
 *       兩個音量與 ARDUINO 版一樣要跑舊值域遷移（見 settings_normalize_toggle），
 *       否則 native test 涵蓋不到「舊裝置升級」這條路徑。
 */
bool settings_init_mock(settings_state_t* state) {
    uint8_t br, vol, vv;
    bool has_data = mock_nvs_read(&br, &vol, &vv);

    if (has_data) {
        state->brightness = br;
        // 舊值域（1~5）遷移成 0/1，並把結果寫回 mock NVS，語意對齊 ARDUINO 版的
        // read_toggle_setting_with_migration()——遷移只發生一次
        state->system_volume = settings_normalize_toggle(vol, SETTINGS_VOLUME_DEFAULT);
        state->vent_volume = settings_normalize_toggle(vv, SETTINGS_VENT_VOLUME_DEFAULT);
        if (state->system_volume != vol) {
            s_mock_nvs[SETTING_KEY_SYSTEM_VOL] = state->system_volume;
        }
        if (state->vent_volume != vv) {
            s_mock_nvs[SETTING_KEY_VENT_VOL] = state->vent_volume;
        }
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

    // 先寫 mock NVS 再更新 state，順序對齊 ARDUINO 版的交易式流程（見 settings_write()
    // STEP 03）。mock 寫入不會失敗，這裡對齊的是語意而非錯誤路徑——兩版流程分歧的話，
    // native test 驗到的就不是正式路徑的行為。
    s_mock_nvs[key] = value;

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

/**
 * 同步裝置名稱到 state（BLE callback 收到寫入後呼叫）
 * 邏輯：安全複製（截斷至 DEVICE_NAME_MAX_LEN-1），更新 state.device_name
 * @param state  記憶體緩衝
 * @param name   新名稱（由 BLE / App 寫入）
 * @return true 成功同步
 */
bool settings_sync_device_name(settings_state_t* state, const char* name) {
    strncpy(state->device_name, name, DEVICE_NAME_MAX_LEN - 1);
    state->device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
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
    // STEP 01: 開 NVS namespace——失敗屬異常（非首次開機情境），必須留痕
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.printf("[SETTINGS] ERROR nvs_open on read \"%s\": %d\n", key, err);
        return default_val;
    }

    // STEP 02: 讀值。ESP_ERR_NVS_NOT_FOUND = 首次開機尚未寫過，屬預期行為不報 ERROR；
    //          其餘 error code（partition 損毀、handle 失效等）必須印出來，
    //          否則「設定每次開機都跑掉」的 field report 將無從判斷是韌體 bug 還是硬體故障。
    uint8_t val = default_val;
    esp_err_t get_err = nvs_get_u8(handle, key, &val);
    if (get_err == ESP_ERR_NVS_NOT_FOUND) {
        val = default_val;
    } else if (get_err != ESP_OK) {
        Serial.printf("[SETTINGS] ERROR nvs_get_u8 \"%s\": %d (fallback=%u)\n",
                      key, get_err, default_val);
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

/**
 * 讀一個兩態音量設定並就地完成舊值域遷移。
 *
 * 抽成 helper 而非在 settings_init() 內寫兩遍：兩個音量的處理完全相同，複製一份
 * 等於要求日後改遷移規則的人記得兩處都改（EXTRACT-SHARED-HELPER）。
 *
 * @param nvs_key      NVS 欄位鍵名
 * @param default_val  NVS 無資料或值損壞時採用的預設值
 * @return             已收進 0/1 的值
 */
static uint8_t read_toggle_setting_with_migration(const char* nvs_key, uint8_t default_val) {
    // STEP 01: 讀原始值（可能是 2026-09-06 前的 1~5 舊值域）
    uint8_t raw = nvs_read_uint8(nvs_key, default_val);

    // STEP 02: 收進 0/1
    uint8_t normalized = settings_normalize_toggle(raw, default_val);

    // STEP 03: 值有變動才寫回，讓遷移只發生一次；寫回失敗不阻斷開機（runtime 值
    //   已經是對的，下次開機會再遷移一次），但要留痕，不能靜默吞掉
    if (normalized != raw) {
        Serial.printf("[SETTINGS] migrate \"%s\": %u -> %u（2026-09-06 值域改為 0/1）\n",
                      nvs_key, (unsigned)raw, (unsigned)normalized);
        if (!nvs_write_uint8(nvs_key, normalized)) {
            Serial.printf("[SETTINGS] ERROR 遷移寫回失敗 key=\"%s\"，下次開機會再試\n", nvs_key);
        }
    }
    return normalized;
}

bool settings_init(settings_state_t* state) {
    state->brightness = nvs_read_uint8(NVS_BRIGHTNESS_KEY, SETTINGS_BRIGHTNESS_DEFAULT);
    // 兩個音量 2026-09-06 起值域為 0/1，舊裝置 NVS 存的是 1~5，載入時就要遷移——
    // 留著舊值會讓「按一次關不掉」（3 → clamp → 1，畫面仍是「開」）
    state->system_volume = read_toggle_setting_with_migration(NVS_VOLUME_KEY, SETTINGS_VOLUME_DEFAULT);
    state->vent_volume = read_toggle_setting_with_migration(NVS_VENT_VOL_KEY, SETTINGS_VENT_VOLUME_DEFAULT);
    // 裝置名稱暫由 LittleFS 管理（Phase 2）
    strncpy(state->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX_LEN - 1);
    state->device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
    return true;
}

bool settings_write(settings_state_t* state, uint8_t key, uint8_t value) {
    // STEP 01: 依 key 取出值域與對應的 NVS 欄位鍵名（此步不動 state）
    uint8_t min_val;
    uint8_t max_val;
    const char* nvs_key = nullptr;

    switch (key) {
        case SETTING_KEY_BRIGHTNESS:
            min_val = SETTINGS_BRIGHTNESS_MIN;
            max_val = SETTINGS_BRIGHTNESS_MAX;
            nvs_key = NVS_BRIGHTNESS_KEY;
            break;
        case SETTING_KEY_SYSTEM_VOL:
            // 2026-09-06 起 0 = 關（原 V1 §19.4 的「不可靜音」已放寬，見該節工程變更註記）
            min_val = SETTINGS_VOLUME_MIN;
            max_val = SETTINGS_VOLUME_MAX;
            nvs_key = NVS_VOLUME_KEY;
            break;
        case SETTING_KEY_VENT_VOL:
            min_val = SETTINGS_VENT_VOLUME_MIN;
            max_val = SETTINGS_VENT_VOLUME_MAX;
            nvs_key = NVS_VENT_VOL_KEY;
            break;
        default:
            return false;
    }

    // STEP 02: 值域檢查——超出範圍直接拒絕，state 與 NVS 都不動
    if (value < min_val || value > max_val) {
        return false;
    }

    // STEP 03: 先持久化。順序刻意是「NVS 成功才改 state」而非反過來——寫入失敗時
    //   若 state 已經改了，呼叫端會拿到一個「這次看起來生效、重開機卻不見」的值，
    //   而失敗只有一行 Serial log，使用者無從察覺（2026-09-06 codex Tier 3 review
    //   的 CRITICAL）。通氣音量尤其嚴重：它的 state 直接決定實際會不會發出節奏音。
    if (!nvs_write_uint8(nvs_key, value)) {
        return false;
    }

    // STEP 04: 持久化成功後才更新記憶體狀態，兩者保證一致
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
        default:
            // STEP 01 的 switch 已擋掉所有未知 key，這裡不可達；留著讓
            //   -Wswitch 在未來新增 key 時提醒兩處都要改
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
    // STEP 01: 參數防護。空名稱不得覆寫既有設定
    if (name == nullptr || name[0] == '\0') {
        Serial.println("[SETTINGS] ERROR set_device_name 收到空名稱");
        return false;
    }

    // STEP 02: 確保父目錄存在——LittleFS 開啟含路徑的新檔時不會自動建目錄
    if (!LittleFS.exists(DEVICE_NAME_DIR)) {
        LittleFS.mkdir(DEVICE_NAME_DIR);
    }

    // STEP 03: 覆寫模式開檔
    File f = LittleFS.open(DEVICE_NAME_FILE, "w");
    if (!f) {
        Serial.printf("[SETTINGS] ERROR 無法開啟 %s 寫入\n", DEVICE_NAME_FILE);
        return false;
    }

    // STEP 04: 寫入並確認完整落盤（部分寫入視為失敗，避免留下半截名稱）
    const size_t len     = strlen(name);
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(name), len);
    f.close();

    if (written != len) {
        Serial.printf("[SETTINGS] ERROR 裝置名稱寫入不完整 %u/%u bytes\n",
                      (unsigned)written, (unsigned)len);
        return false;
    }

    Serial.printf("[SETTINGS] OK 裝置名稱已寫入 '%s'\n", name);
    return true;
}

bool settings_get_device_name(char* buf, size_t buf_size) {
    // STEP 01: 參數防護。buf_size 為 0 時 buf_size-1 會下溢成 SIZE_MAX
    if (buf == nullptr || buf_size == 0) {
        return false;
    }

    // STEP 02: 檔案不存在 = 尚未設定過，屬正常狀態（非錯誤）→ 回預設名稱
    File f = LittleFS.open(DEVICE_NAME_FILE, "r");
    if (!f) {
        strncpy(buf, DEVICE_NAME_DEFAULT, buf_size - 1);
        buf[buf_size - 1] = '\0';
        return true;
    }

    // STEP 03: 讀取內容。檔案為空同樣視為未設定，回預設值
    const size_t n = f.readBytes(buf, buf_size - 1);
    f.close();
    buf[n] = '\0';

    if (n == 0) {
        strncpy(buf, DEVICE_NAME_DEFAULT, buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
    return true;
}

#endif  // ARDUINO
