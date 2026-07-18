// EMS DoseSync Pro — Wave 2 Unit Test: 裝置名稱淨化（BLE 寫入路徑）
//
// 對應規格：docs/phase-g-system-settings-plan.md §2.2 / V1 §19.2
//
// 測試 framework：Unity
//
// 2026-07-18 重寫（POST-COMMIT-REVIEW #4）：
//   原版在測試檔內自建 mock_ble_device_name_write() 假 callback，連 ble_nus.h 都沒
//   include，測的是測試替身而非產品碼——BLE 寫名路徑實際上零覆蓋。
//   現改為測 device_name_sanitize()：BLE callback 的驗證/截斷邏輯已抽成這支純函式，
//   native 可直接驗證，ble_nus.cpp 的 onWrite 只負責呼叫它。

#include <unity.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "ems_settings.h"

static settings_state_t g_state;

void setUp() {
    mock_nvs_clear();
    mock_fs_clear();
    settings_init_mock(&g_state);
}

void tearDown() {}

// ============================================================
//  Group A: device_name_sanitize — 拒絕無效輸入
// ============================================================

/** A1: 長度 0 → 拒絕（BLE 寫入空 payload 不應清空裝置名稱） */
static void test_sanitize_rejects_empty_input() {
    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_FALSE_MESSAGE(device_name_sanitize("", 0, out, sizeof(out)),
        "A1: 空 payload 應被拒絕");
}

/** A2: nullptr 輸入 → 拒絕，不可解參考 */
static void test_sanitize_rejects_null_input() {
    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_FALSE_MESSAGE(device_name_sanitize(nullptr, 5, out, sizeof(out)),
        "A2: nullptr 應被拒絕");
}

/** A3: out_size 為 0 → 拒絕（否則 buf[buf_size-1] 會下溢成 SIZE_MAX 越界寫入） */
static void test_sanitize_rejects_zero_out_size() {
    char out[1];
    TEST_ASSERT_FALSE_MESSAGE(device_name_sanitize("ABC", 3, out, 0),
        "A3: out_size=0 應被拒絕");
}

// ============================================================
//  Group B: device_name_sanitize — 正常輸入原樣通過
// ============================================================

/** B1: ASCII 名稱原樣通過 */
static void test_sanitize_passes_ascii() {
    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_TRUE(device_name_sanitize("Ambulance01", 11, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Ambulance01", out);
}

/** B2: UTF-8 中文名稱原樣通過（「安康91」= 3+3+1+1 = 8 bytes） */
static void test_sanitize_passes_utf8_chinese() {
    const char* name = "安康91";
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, (uint32_t)strlen(name),
        "B2: 前提檢查——安康91 的 UTF-8 長度應為 8 bytes（中文字各 3 bytes）");

    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_TRUE(device_name_sanitize(name, strlen(name), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(name, out);
}

/** B3: 不以 NUL 結尾的 raw buffer（BLE payload 常態）→ 依 len 正確取用 */
static void test_sanitize_handles_non_terminated_buffer() {
    // 刻意不放 '\0'，模擬 BLE 收到的 raw bytes
    const char raw[3] = { 'A', 'B', 'C' };
    char out[DEVICE_NAME_MAX_LEN];

    TEST_ASSERT_TRUE(device_name_sanitize(raw, sizeof(raw), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ABC", out);
}

// ============================================================
//  Group C: device_name_sanitize — 截斷（UTF-8 邊界安全）
// ============================================================

/** C1: 超長 ASCII → 截斷至 out_size - 1 */
static void test_sanitize_truncates_long_ascii() {
    char long_name[DEVICE_NAME_MAX_LEN + 10];
    memset(long_name, 'A', sizeof(long_name));

    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_TRUE(device_name_sanitize(long_name, sizeof(long_name), out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(DEVICE_NAME_MAX_LEN - 1, (uint32_t)strlen(out),
        "C1: 應截斷至 DEVICE_NAME_MAX_LEN - 1");
}

/**
 * C2: 超長中文 → 必須切在 UTF-8 字元邊界，不得產生半個字。
 *
 * 這是原測試完全沒覆蓋的缺口：原本只用 ASCII 'A' 測截斷，
 * 而預設裝置名稱本身就是中文「未命名」，超長中文是現實輸入。
 */
static void test_sanitize_truncates_utf8_on_char_boundary() {
    // 「中」= E4 B8 AD（3 bytes）。11 個「中」= 33 bytes > 31 可用空間，
    // 31 不是 3 的倍數 → 必須退到 30（10 個字），不能切出半個字。
    char long_utf8[64] = {};
    for (int i = 0; i < 11; i++) {
        memcpy(long_utf8 + i * 3, "中", 3);
    }

    char out[DEVICE_NAME_MAX_LEN];  // 32 → 可用 31 bytes
    TEST_ASSERT_TRUE(device_name_sanitize(long_utf8, 33, out, sizeof(out)));

    const size_t out_len = strlen(out);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(30, (uint32_t)out_len,
        "C2: 31 不是 3 的倍數，應退到 30 bytes（10 個完整中文字）");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)(out_len % 3),
        "C2: 截斷後長度必須是完整字元的倍數");

    // 逐 byte 確認沒有殘留的 continuation byte 開頭
    for (size_t i = 0; i < out_len; i += 3) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xE4, (uint8_t)out[i],
            "C2: 每個字元應以合法的 UTF-8 起始 byte 開頭");
    }
}

/** C3: 內嵌 NUL byte → 截到第一個 NUL，不得把後面的位元組帶進來 */
static void test_sanitize_stops_at_embedded_nul() {
    const char raw[7] = { 'A', 'B', '\0', 'X', 'Y', 'Z', 'W' };
    char out[DEVICE_NAME_MAX_LEN];

    TEST_ASSERT_TRUE(device_name_sanitize(raw, sizeof(raw), out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("AB", out,
        "C3: 內嵌 NUL 之後的位元組不應被採用");
}

/** C4: 全部是 NUL → 有效長度 0 → 拒絕 */
static void test_sanitize_rejects_all_nul_payload() {
    const char raw[4] = { '\0', '\0', '\0', '\0' };
    char out[DEVICE_NAME_MAX_LEN];

    TEST_ASSERT_FALSE_MESSAGE(device_name_sanitize(raw, sizeof(raw), out, sizeof(out)),
        "C4: 全 NUL payload 有效長度為 0，應拒絕");
}

// ============================================================
//  Group D: 淨化結果同步進 settings_state
// ============================================================

/** D1: sanitize → settings_sync_device_name → state 更新 */
static void test_sanitized_name_syncs_into_state() {
    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_TRUE(device_name_sanitize("NewDevice", 9, out, sizeof(out)));

    TEST_ASSERT_TRUE(settings_sync_device_name(&g_state, out));
    TEST_ASSERT_EQUAL_STRING("NewDevice", g_state.device_name);
}

/** D2: 中文名稱走完整路徑後 state 內容正確 */
static void test_sanitized_utf8_name_syncs_into_state() {
    const char* name = "安康91";
    char out[DEVICE_NAME_MAX_LEN];
    TEST_ASSERT_TRUE(device_name_sanitize(name, strlen(name), out, sizeof(out)));

    TEST_ASSERT_TRUE(settings_sync_device_name(&g_state, out));
    TEST_ASSERT_EQUAL_STRING(name, g_state.device_name);
}

// ============================================================
//  Main
// ============================================================

void run_all_tests() {
    // Group A: 無效輸入
    RUN_TEST(test_sanitize_rejects_empty_input);
    RUN_TEST(test_sanitize_rejects_null_input);
    RUN_TEST(test_sanitize_rejects_zero_out_size);

    // Group B: 正常輸入
    RUN_TEST(test_sanitize_passes_ascii);
    RUN_TEST(test_sanitize_passes_utf8_chinese);
    RUN_TEST(test_sanitize_handles_non_terminated_buffer);

    // Group C: 截斷
    RUN_TEST(test_sanitize_truncates_long_ascii);
    RUN_TEST(test_sanitize_truncates_utf8_on_char_boundary);
    RUN_TEST(test_sanitize_stops_at_embedded_nul);
    RUN_TEST(test_sanitize_rejects_all_nul_payload);

    // Group D: 同步進 state
    RUN_TEST(test_sanitized_name_syncs_into_state);
    RUN_TEST(test_sanitized_utf8_name_syncs_into_state);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    run_all_tests();
    return UNITY_END();
}
