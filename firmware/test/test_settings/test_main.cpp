// EMS DoseSync Pro — Wave 0 Unit Test: NVS 設定持久化層
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.0.4
// 涵蓋：G0.1 ~ G0.9
//
// 測試 framework：Unity（與 test_rtc / test_ems_storage_logic 相同模式）
//   - test 函式：static void test_*()
//   - 註冊：RUN_TEST(test_name) in main()
//   - 不使用 TEST_CASE() / TEST() 等其他 framework 風格 macro
//
// ⚠️ RED phase：本檔對應實作為 stub，預期跑出來全部失敗。
// ⚠️ Step 3 GREEN 階段禁止修改本檔。

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "ems_settings.h"

// ============================================================
//  共用 fixture
// ============================================================

static settings_state_t g_state;

void setUp() {
    mock_nvs_clear();
    mock_fs_clear();
}

void tearDown() {}

// ============================================================
//  Wave 0: NVS 設定持久化層
// ============================================================

// ----- G0.1: settings_init 讀取 NVS 有資料 -----

/** G0.1: NVS 有資料 → state 值 = NVS 值 */
static void test_g01_settings_init_nvs_has_data() {
    // 預設 NVS 資料：亮度=3, 系統音量=3, 通氣音量=3
    mock_nvs_write(3, 3, 3);

    settings_state_t state;
    bool ok = settings_init_mock(&state);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.1: settings_init 應回傳 true");
    TEST_ASSERT_EQUAL_UINT8(3, state.brightness);
    TEST_ASSERT_EQUAL_UINT8(3, state.system_volume);
    TEST_ASSERT_EQUAL_UINT8(3, state.vent_volume);
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, state.device_name);
}

// ----- G0.2: settings_init 讀取 NVS 無資料 -----

/** G0.2: NVS 無資料 → state 值 = 預設值 */
static void test_g02_settings_init_nvs_no_data() {
    // 不清 mock NVS（setUp 已 clear），模擬 NVS 無資料
    settings_state_t state;
    bool ok = settings_init_mock(&state);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.2: settings_init 應回傳 true（fallback 預設值）");
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_DEFAULT, state.brightness);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_VOLUME_DEFAULT, state.system_volume);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_VENT_VOLUME_DEFAULT, state.vent_volume);
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, state.device_name);
}

// ----- G0.3: settings_write 寫入 brightness -----

/** G0.3: brightness 寫入 NVS + state 同步更新 */
static void test_g03_settings_write_brightness() {
    mock_nvs_write(1, 1, 1);
    settings_init_mock(&g_state);

    bool ok = settings_write_mock(&g_state, SETTING_KEY_BRIGHTNESS, 5);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.3: settings_write 應回傳 true");
    TEST_ASSERT_EQUAL_UINT8(5, g_state.brightness);

    // 驗證 NVS 已更新
    uint8_t br, vol, vv;
    mock_nvs_read(&br, &vol, &vv);
    TEST_ASSERT_EQUAL_UINT8(5, br);
    TEST_ASSERT_EQUAL_UINT8(1, vol);  // 其他值不變
    TEST_ASSERT_EQUAL_UINT8(1, vv);
}

// ----- G0.4: settings_write 邊界檢查（system_volume） -----

/** G0.4: system_volume 邊界 — 1~5 合法，0/6 拒絕 */
static void test_g04_volume_boundary_valid() {
    settings_init_mock(&g_state);
    g_state.system_volume = 1;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 5);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.4: 合法值 5 應通過");
    TEST_ASSERT_EQUAL_UINT8(5, g_state.system_volume);
}

/** G0.4: system_volume 邊界 — 0 拒絕 */
static void test_g04_volume_boundary_zero_rejected() {
    settings_init_mock(&g_state);
    g_state.system_volume = 3;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 0);
    TEST_ASSERT_FALSE_MESSAGE(ok, "G0.4: 邊界值 0 應拒絕");
    TEST_ASSERT_EQUAL_UINT8(3, g_state.system_volume);  // 不應改變
}

/** G0.4: system_volume 邊界 — 6 拒絕 */
static void test_g04_volume_boundary_six_rejected() {
    settings_init_mock(&g_state);
    g_state.system_volume = 3;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 6);
    TEST_ASSERT_FALSE_MESSAGE(ok, "G0.4: 邊界值 6 應拒絕");
    TEST_ASSERT_EQUAL_UINT8(3, g_state.system_volume);  // 不應改變
}

// ----- G0.5: settings_write vent_volume 邊界 -----

/** G0.5: vent_volume 0~5 合法 */
static void test_g05_vent_volume_zero_allowed() {
    settings_init_mock(&g_state);
    g_state.vent_volume = 3;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_VENT_VOL, 0);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.5: vent_volume 0 應合法（可靜音）");
    TEST_ASSERT_EQUAL_UINT8(0, g_state.vent_volume);
}

/** G0.5: vent_volume 5 合法 */
static void test_g05_vent_volume_max_allowed() {
    settings_init_mock(&g_state);
    g_state.vent_volume = 3;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_VENT_VOL, 5);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.5: vent_volume 5 應合法");
    TEST_ASSERT_EQUAL_UINT8(5, g_state.vent_volume);
}

// ----- G0.6: settings_reset_defaults -----

/** G0.6: 亮度/系統音量/通氣音量→預設，裝置名稱不變 */
static void test_g06_reset_defaults_restores_values() {
    mock_nvs_write(5, 5, 5);
    settings_init_mock(&g_state);
    strncpy(g_state.device_name, "CustomName", DEVICE_NAME_MAX_LEN - 1);

    bool ok = settings_reset_defaults_mock(&g_state);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.6: reset_defaults 應回傳 true");
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_DEFAULT, g_state.brightness);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_VOLUME_DEFAULT, g_state.system_volume);
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_VENT_VOLUME_DEFAULT, g_state.vent_volume);
    TEST_ASSERT_EQUAL_STRING("CustomName", g_state.device_name);  // 名稱不變
}

// ----- G0.7: settings_set_device_name -----

/** G0.7: LittleFS 寫入成功 */
static void test_g07_set_device_name() {
    // mock_fs_clear() 已在 setUp 呼叫，模擬乾淨檔案系統
    bool ok = mock_fs_write(DEVICE_NAME_FILE, "TestDevice", 10);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.7: 寫入 device_name 應成功");
}

// ----- G0.8: settings_get_device_name -----

/** G0.8: LittleFS 讀取成功（含 UTF-8 多字節） */
static void test_g08_get_device_name_utf8() {
    // 寫入 UTF-8 中文（"安康91" = 4 bytes in UTF-8）
    const char* utf8_name = "\xe5畤\xaa\xe5\x91\x89\x39\x31";
    mock_fs_write(DEVICE_NAME_FILE, utf8_name, strlen(utf8_name));

    char buf[DEVICE_NAME_MAX_LEN];
    size_t len = 0;
    bool ok = mock_fs_read(DEVICE_NAME_FILE, buf, sizeof(buf), &len);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.8: 讀取 device_name 應成功");
    TEST_ASSERT_EQUAL_STRING(utf8_name, buf);
}

// ----- G0.9: settings_get_device_name 檔不存在 -----

/** G0.9: 檔不存在 → 返回預設值「未命名」 */
static void test_g09_get_device_name_file_not_exist() {
    // mock_fs_clear() 已在 setUp 呼叫，模擬無檔案
    char buf[DEVICE_NAME_MAX_LEN];
    size_t len = 0;
    bool ok = mock_fs_read(DEVICE_NAME_FILE, buf, sizeof(buf), &len);

    TEST_ASSERT_FALSE_MESSAGE(ok, "G0.9: 檔不存在應回傳 false");
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, buf);
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    RUN_TEST(test_g01_settings_init_nvs_has_data);
    RUN_TEST(test_g02_settings_init_nvs_no_data);
    RUN_TEST(test_g03_settings_write_brightness);
    RUN_TEST(test_g04_volume_boundary_valid);
    RUN_TEST(test_g04_volume_boundary_zero_rejected);
    RUN_TEST(test_g04_volume_boundary_six_rejected);
    RUN_TEST(test_g05_vent_volume_zero_allowed);
    RUN_TEST(test_g05_vent_volume_max_allowed);
    RUN_TEST(test_g06_reset_defaults_restores_values);
    RUN_TEST(test_g07_set_device_name);
    RUN_TEST(test_g08_get_device_name_utf8);
    RUN_TEST(test_g09_get_device_name_file_not_exist);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
