// EMS DoseSync Pro — Wave 2 Unit Test: 裝置名稱管理 + BLE 寫入
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.2 / V1 §19.2
// 涵蓋：G2.1 ~ G2.3
//
// 測試 framework：Unity
//
// ⚠️ RED phase：本檔對應實作為 stub，預期跑出來全部失敗。
// ⚠️ Step 3 GREEN 階段禁止修改本檔。

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "ems_settings.h"

// ============================================================
//  BLE mock：模擬 BLE characteristic write callback
//  實作 G2.1：BLE write callback → settings_set_device_name → state 同步
// ============================================================

/**
 * BLE mock：模擬收到 device name 寫入時觸發的 callback
 *
 * G2.1 預期：
 *   - 寫入前：state.device_name = "未命名"
 *   - BLE 收到寫入 "NewDevice"
 *   - callback 內呼叫 settings_set_device_name("NewDevice")
 *   - state.device_name 應同步為 "NewDevice"
 */
static char g_ble_mock_device_name[DEVICE_NAME_MAX_LEN] = {};
static bool g_ble_mock_name_set = false;

/**
 * Mock BLE write callback：模擬 App 透過 BLE 寫入裝置名稱
 *
 * @param name 寫入的裝置名稱
 */
void mock_ble_device_name_write(const char* name) {
    strncpy(g_ble_mock_device_name, name, DEVICE_NAME_MAX_LEN - 1);
    g_ble_mock_device_name[DEVICE_NAME_MAX_LEN - 1] = '\0';
    g_ble_mock_name_set = true;
}

/**
 * Mock BLE：取得目前寫入的裝置名稱（測試用）
 */
const char* mock_ble_get_written_name() {
    return g_ble_mock_device_name;
}

/**
 * Mock BLE：是否已觸發寫入 callback
 */
bool mock_ble_is_name_set() {
    return g_ble_mock_name_set;
}

/**
 * Mock BLE：重設寫入狀態
 */
void mock_ble_reset() {
    g_ble_mock_device_name[0] = '\0';
    g_ble_mock_name_set = false;
}

// ============================================================
//  共用 fixture
// ============================================================

static settings_state_t g_state;

void setUp() {
    mock_nvs_clear();
    mock_fs_clear();
    mock_ble_reset();

    settings_state_t default_state;
    settings_init_mock(&default_state);
    memcpy(&g_state, &default_state, sizeof(settings_state_t));
}

void tearDown() {}

// ============================================================
//  Wave 2: 裝置名稱管理 + BLE 寫入
// ============================================================

// ----- G2.1: BLE write callback → settings_state 同步更新 -----

/** G2.1: BLE 收到 device name 寫入 → state.device_name 同步更新 */
static void test_g21_ble_write_updates_state() {
    // 寫入前：確認 state 為預設值
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, g_state.device_name);

    // 模擬 BLE 收到寫入 "NewDevice"
    mock_ble_device_name_write("NewDevice");

    // 驗證 mock 已觸發
    TEST_ASSERT_TRUE_MESSAGE(mock_ble_is_name_set(),
        "G2.1: BLE mock 應已觸發寫入 callback");
    TEST_ASSERT_EQUAL_STRING("NewDevice", mock_ble_get_written_name());

    // BLE callback 應呼叫 settings_sync_device_name 同步 state
    settings_sync_device_name(&g_state, mock_ble_get_written_name());

    // 驗證 state 已同步
    TEST_ASSERT_EQUAL_STRING("NewDevice", g_state.device_name);
}

// ----- G2.2: BLE write 中文名稱（UTF-8） -----

/** G2.2: BLE 寫入 UTF-8 中文 → state.device_name 正確 */
static void test_g22_ble_write_utf8_name() {
    const char* utf8_name = "安康91";  // UTF-8: 4 bytes
    mock_ble_device_name_write(utf8_name);

    settings_sync_device_name(&g_state, mock_ble_get_written_name());

    TEST_ASSERT_EQUAL_STRING(utf8_name, g_state.device_name);
}

// ----- G2.3: BLE write 超出 max len 截斷 -----

/** G2.3: BLE 寫入超過 DEVICE_NAME_MAX_LEN → 截斷不爆 */
static void test_g23_ble_write_truncate_long_name() {
    char long_name[DEVICE_NAME_MAX_LEN + 10];
    memset(long_name, 'A', DEVICE_NAME_MAX_LEN + 9);
    long_name[DEVICE_NAME_MAX_LEN + 9] = '\0';

    mock_ble_device_name_write(long_name);

    settings_sync_device_name(&g_state, mock_ble_get_written_name());

    // 應截斷至 DEVICE_NAME_MAX_LEN - 1
    size_t actual_len = strlen(g_state.device_name);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(DEVICE_NAME_MAX_LEN - 1), (uint8_t)actual_len,
        "G2.3: 截斷長度應為 DEVICE_NAME_MAX_LEN - 1");
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    RUN_TEST(test_g21_ble_write_updates_state);
    RUN_TEST(test_g22_ble_write_utf8_name);
    RUN_TEST(test_g23_ble_write_truncate_long_name);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
