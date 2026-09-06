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
// 狀態：GREEN 完成（Wave 0）。TDD 的「禁止修改」凍結期已結束，
// 後續異動走一般 code review 流程即可。

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
    // NVS 測資：亮度=3, 系統音量=1（開）, 通氣音量=0（關）。兩個音量刻意給不同值，
    // 這樣讀取時若把兩個欄位接錯（都讀同一個 NVS key）會被斷言抓到；音量自
    // 2026-09-06 起值域為 0/1，不再用 3 這種已超出值域的測資。
    mock_nvs_write(3, 1, 0);

    settings_state_t state;
    bool ok = settings_init_mock(&state);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.1: settings_init 應回傳 true");
    TEST_ASSERT_EQUAL_UINT8(3, state.brightness);
    TEST_ASSERT_EQUAL_UINT8(1, state.system_volume);
    TEST_ASSERT_EQUAL_UINT8(0, state.vent_volume);
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, state.device_name);
}

// ----- G0.2: settings_init 讀取 NVS 無資料 -----

/**
 * G0.2: NVS 無資料 → state 值 = 預設值。
 *
 * 兩個音量的期望值寫死字面值 1 而非 `SETTINGS_*_DEFAULT`：拿常數比對「由同一個常數
 * 算出來的結果」是恆真式，預設值若被誤改成 0（開機後靜音）expected 與 actual 會一起
 * 變，測試照樣全綠（memory feedback_test_assert_not_same_symbol）。
 */
static void test_g02_settings_init_nvs_no_data() {
    // 不清 mock NVS（setUp 已 clear），模擬 NVS 無資料
    settings_state_t state;
    bool ok = settings_init_mock(&state);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.2: settings_init 應回傳 true（fallback 預設值）");
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_DEFAULT, state.brightness);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, state.system_volume,
        "G0.2: 系統音量預設應為 1（開）——預設靜音會讓使用者收不到操作回饋");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, state.vent_volume,
        "G0.2: 通氣音量預設應為 1（開）");
    TEST_ASSERT_EQUAL_STRING(DEVICE_NAME_DEFAULT, state.device_name);
}

// ----- G0.2b: 舊值域（2026-09-06 前的 1~5）遷移 -----

/**
 * G0.2b: settings_normalize_toggle() 的三條路徑。
 *
 * 這是升級既有裝置的唯一防線：NVS 裡存的是舊值域的 1~5，不遷移的話 runtime 會帶著
 * 值域外的值跑，「按一次關不掉」（3 → clamp → 1，畫面仍顯示「開」）。
 */
static void test_g02b_normalize_toggle_legacy_values() {
    // STEP 01: 0 在新舊值域語意相同（靜音／關）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings_normalize_toggle(0, 1),
        "G0.2b: 舊值 0（靜音）應維持 0（關）");

    // STEP 02: 舊值域 1~5 全都會發聲 → 一律遷移成 1（開）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings_normalize_toggle(1, 1), "G0.2b: 舊值 1 → 1");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings_normalize_toggle(3, 1),
        "G0.2b: 舊值 3（前一版的預設值，實機上最常見）→ 1（開）");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, settings_normalize_toggle(5, 1), "G0.2b: 舊值 5 → 1");

    // STEP 03: 兩個值域都容不下的值 → 回傳呼叫端給的預設，不猜測原意。
    //   預設值故意傳 0 而非 1，證明回傳的真的是參數而不是寫死的 1
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings_normalize_toggle(6, 0),
        "G0.2b: 值域外的 6 應回傳呼叫端給的預設值（此處為 0）");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, settings_normalize_toggle(255, 0),
        "G0.2b: 損壞值 255 應回傳呼叫端給的預設值");
}

/**
 * G0.2c: 開機載入舊值域的 NVS → state 收進 0/1，且遷移結果寫回 NVS（只發生一次）。
 *
 * 對照 test_g01（NVS 已是新值域）：那條路徑不觸發遷移，這條才是升級既有裝置的實況。
 */
static void test_g02c_settings_init_migrates_legacy_nvs() {
    // STEP 01: 模擬升級前的裝置——亮度 3（值域未變）、兩個音量都是舊預設值 3
    mock_nvs_write(3, 3, 3);

    settings_state_t state;
    bool ok = settings_init_mock(&state);

    // STEP 02: 兩個音量都應被收進「開」，亮度不受影響（它的值域沒變）
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, state.brightness,
        "G0.2c: 亮度值域未變（1~5），不該被遷移邏輯動到");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, state.system_volume, "G0.2c: 舊系統音量 3 → 1（開）");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, state.vent_volume, "G0.2c: 舊通氣音量 3 → 1（開）");

    // STEP 03: 遷移結果要寫回 NVS，否則每次開機都得重算，且外部工具讀到的仍是舊值
    uint8_t br, vol, vv;
    TEST_ASSERT_TRUE(mock_nvs_read(&br, &vol, &vv));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, vol, "G0.2c: 遷移後的系統音量應寫回 NVS");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, vv, "G0.2c: 遷移後的通氣音量應寫回 NVS");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, br, "G0.2c: 亮度不該被改寫");
}

/** G0.2d: settings_toggle_enabled() — 0 為關，其餘（含未遷移的舊值）皆為開 */
static void test_g02d_toggle_enabled() {
    // STEP 01: 只有 0 是關
    TEST_ASSERT_FALSE_MESSAGE(settings_toggle_enabled(0), "G0.2d: 0 應為關");

    // STEP 02: 1 是正常的「開」
    TEST_ASSERT_TRUE_MESSAGE(settings_toggle_enabled(1), "G0.2d: 1 應為開");

    // STEP 03: 遷移寫回失敗時 runtime 可能仍帶著舊值域的數字，此時「非關即開」
    //   才是對的解讀（舊值域 1~5 全都會發聲）
    TEST_ASSERT_TRUE_MESSAGE(settings_toggle_enabled(3),
        "G0.2d: 未遷移的舊值 3 應仍判為開，不可因為不等於 1 就當成關");
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
//
// 2026-09-06 起系統音量為開/關兩態（0/1），原本的「1~5 合法、0 拒絕」不再成立：
// 0 從「不可靜音」改為合法的「關」，上界從 5 收到 1。斷言全部寫死字面值而非
// 引用 SETTINGS_VOLUME_MIN/MAX，否則常數被改錯時測試會跟著錯、照樣全綠。

/** G0.4: system_volume 邊界 — 1（開）合法 */
static void test_g04_volume_boundary_valid() {
    settings_init_mock(&g_state);
    g_state.system_volume = 0;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 1);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.4: 合法值 1（開）應通過");
    TEST_ASSERT_EQUAL_UINT8(1, g_state.system_volume);
}

/** G0.4: system_volume 邊界 — 0（關）合法（2026-09-06 起放寬，原為拒絕） */
static void test_g04_volume_boundary_zero_allowed() {
    settings_init_mock(&g_state);
    g_state.system_volume = 1;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 0);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.4: 邊界值 0（關）應合法——蜂鳴器只有開/關兩態");
    TEST_ASSERT_EQUAL_UINT8(0, g_state.system_volume);
}

/** G0.4: system_volume 邊界 — 2 拒絕（超出開/關兩態的上界） */
static void test_g04_volume_boundary_two_rejected() {
    settings_init_mock(&g_state);
    g_state.system_volume = 1;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_SYSTEM_VOL, 2);
    TEST_ASSERT_FALSE_MESSAGE(ok, "G0.4: 邊界值 2 應拒絕（值域只有 0/1）");
    TEST_ASSERT_EQUAL_UINT8(1, g_state.system_volume);  // 不應改變
}

// ----- G0.5: settings_write vent_volume 邊界 -----

/** G0.5: vent_volume 0（關）合法 */
static void test_g05_vent_volume_zero_allowed() {
    settings_init_mock(&g_state);
    g_state.vent_volume = 1;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_VENT_VOL, 0);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.5: vent_volume 0 應合法（可靜音）");
    TEST_ASSERT_EQUAL_UINT8(0, g_state.vent_volume);
}

/** G0.5: vent_volume 1（開）合法 */
static void test_g05_vent_volume_max_allowed() {
    settings_init_mock(&g_state);
    g_state.vent_volume = 0;

    bool ok = settings_write_mock(&g_state, SETTING_KEY_VENT_VOL, 1);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.5: vent_volume 1（開）應合法");
    TEST_ASSERT_EQUAL_UINT8(1, g_state.vent_volume);
}

/** G0.5: vent_volume 2 拒絕（超出開/關兩態的上界） */
static void test_g05_vent_volume_two_rejected() {
    // STEP 01: 起始為合法的「開」，用來驗證被拒絕時原值不被動到
    settings_init_mock(&g_state);
    g_state.vent_volume = 1;

    // STEP 02: 寫入值域外的 2；ok 保存這次非法寫入是否被接受
    bool ok = settings_write_mock(&g_state, SETTING_KEY_VENT_VOL, 2);

    // STEP 03: 應被拒絕，且 state 維持原值
    TEST_ASSERT_FALSE_MESSAGE(ok, "G0.5: vent_volume 2 應拒絕（值域只有 0/1）");
    TEST_ASSERT_EQUAL_UINT8(1, g_state.vent_volume);  // 不應改變
}

// ----- G0.6: settings_reset_defaults -----

/** G0.6: 亮度/系統音量/通氣音量→預設，裝置名稱不變 */
static void test_g06_reset_defaults_restores_values() {
    // 起始狀態刻意設成「兩個音量都關」而非舊值 5——5 會被 settings_init_mock() 的
    // 遷移直接變成 1（= 預設值），那樣即使 reset 什麼都沒做，斷言也會通過
    mock_nvs_write(5, 0, 0);
    settings_init_mock(&g_state);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, g_state.system_volume, "G0.6: 前置條件——重設前應為關");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, g_state.vent_volume, "G0.6: 前置條件——重設前應為關");
    strncpy(g_state.device_name, "CustomName", DEVICE_NAME_MAX_LEN - 1);

    bool ok = settings_reset_defaults_mock(&g_state);
    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.6: reset_defaults 應回傳 true");
    TEST_ASSERT_EQUAL_UINT8(SETTINGS_BRIGHTNESS_DEFAULT, g_state.brightness);
    // 期望值寫死字面值 1，不引用 SETTINGS_*_DEFAULT——理由同 G0.2 的說明（恆真式）
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, g_state.system_volume, "G0.6: 系統音量應回到 1（開）");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, g_state.vent_volume, "G0.6: 通氣音量應回到 1（開）");
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
    // 「安康91」= 安(3) + 康(3) + 9(1) + 1(1) = 8 bytes
    //   原字面值為編輯器貼壞的位元組序列（混入「畤」字，非合法 UTF-8），
    //   且註解誤植為 4 bytes；因 mock_fs 只做 raw byte round-trip，
    //   任何位元組序列都會通過，故此測試原本並未驗證到任何 UTF-8 行為。
    const char* utf8_name = "安康91";
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, (uint32_t)strlen(utf8_name),
        "G0.8: 前提檢查——字面值必須是合法的 8-byte UTF-8");

    mock_fs_write(DEVICE_NAME_FILE, utf8_name, strlen(utf8_name));

    char buf[DEVICE_NAME_MAX_LEN];
    size_t len = 0;
    bool ok = mock_fs_read(DEVICE_NAME_FILE, buf, sizeof(buf), &len);

    TEST_ASSERT_TRUE_MESSAGE(ok, "G0.8: 讀取 device_name 應成功");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, (uint32_t)len, "G0.8: 讀回長度應為 8 bytes");
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
    RUN_TEST(test_g02b_normalize_toggle_legacy_values);
    RUN_TEST(test_g02c_settings_init_migrates_legacy_nvs);
    RUN_TEST(test_g02d_toggle_enabled);
    RUN_TEST(test_g03_settings_write_brightness);
    RUN_TEST(test_g04_volume_boundary_valid);
    RUN_TEST(test_g04_volume_boundary_zero_allowed);
    RUN_TEST(test_g04_volume_boundary_two_rejected);
    RUN_TEST(test_g05_vent_volume_zero_allowed);
    RUN_TEST(test_g05_vent_volume_max_allowed);
    RUN_TEST(test_g05_vent_volume_two_rejected);
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
