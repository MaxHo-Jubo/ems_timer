// Integration test: RTC backend × time_sync × case start/end epoch — Wave 3
// 執行：pio test -e native -f test_rtc_integration
//
// 對齊 plan：docs/ds3231-integration-plan.md §4.1（boot seed）/ §5.1（write-back）/ §8 Wave 3
//
// 涵蓋（以 MockRtcBackend 模擬 DS3231）：
//   1. MockBackend 基本契約（advance / set_present / set_epoch round-trip）
//   2. Boot seed 三分支：
//        (a) backend 在線 + RTC 已有有效時間 → seed g_ts_state → current_epoch_ms 回 RTC 時間
//        (b) backend 在線 + RTC 未設過（now_epoch_ms=0）→ 不 seed → current_epoch_ms 回 0
//        (c) backend 不在線（Null）→ 不 seed → current_epoch_ms 回 0
//   3. BLE write-back：time_sync_handle Applied 後，呼叫 backend->set_epoch_ms 寫回 RTC

#include <unity.h>
#include <cstring>

#include "ems_rtc.h"
#include "null_backend.h"
#include "ems_time_sync.h"
#include "mock_rtc_backend.h"

using namespace ems;

// 共用 fixture
static TimeSyncState g_state;
static MockRtcBackend g_mock;
static char g_ack[256];
static size_t g_ack_len;

void setUp() {
    time_sync_init(&g_state);
    g_mock = MockRtcBackend{};  // reset to defaults (present=true, now=0)
    std::memset(g_ack, 0, sizeof(g_ack));
    g_ack_len = 0;
}

void tearDown() {}

// ============================================================
// 模擬 plan §4.1 main.cpp setup() boot 偵測序：
//   - backend 在線 + epoch > floor → seed g_ts_state
//   - 否則不 seed
// 回傳是否實際 seed（test 斷言用）
// ============================================================
static bool simulate_boot_detect_and_seed(RtcBackend* backend,
                                          TimeSyncState* ts_state,
                                          uint64_t now_millis,
                                          uint64_t floor_ms) {
    if (!backend->is_present()) {
        return false;
    }
    const uint64_t rtc_epoch = backend->now_epoch_ms();
    if (rtc_epoch <= floor_ms) {
        return false;
    }
    time_sync_seed_from_rtc(ts_state, rtc_epoch, now_millis);
    return true;
}

// ============================================================
// §1 MockBackend 基本契約
// ============================================================

static void test_mock_backend_basic_contract() {
    MockRtcBackend mock;
    TEST_ASSERT_TRUE(mock.is_present());      // 預設在線
    TEST_ASSERT_EQUAL_UINT64(0, mock.now_epoch_ms());  // 預設未設

    TEST_ASSERT_TRUE(mock.set_epoch_ms(1713715200000ULL));
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, mock.now_epoch_ms());

    mock.advance_ms(5000);
    TEST_ASSERT_EQUAL_UINT64(1713715205000ULL, mock.now_epoch_ms());

    mock.set_present(false);
    TEST_ASSERT_FALSE(mock.is_present());
    TEST_ASSERT_FALSE(mock.set_epoch_ms(1713720000000ULL));  // absent 回 false
}

// ============================================================
// §2 Boot seed 三分支
// ============================================================

static void test_boot_present_and_set_seeds_software_clock() {
    g_mock.set_present(true);
    g_mock.set_epoch_ms(1713715200000ULL);  // 2024-04-21 UTC

    const bool seeded = simulate_boot_detect_and_seed(
        &g_mock, &g_state, /*now_millis=*/2000, TIME_SYNC_MIN_EPOCH_MS);

    TEST_ASSERT_TRUE(seeded);
    TEST_ASSERT_TRUE(g_state.synced);
    // current_epoch_ms 在同一 millis=2000 應等於 RTC 讀值
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             time_sync_current_epoch_ms(&g_state, 2000));
    // 模擬 case 開始於 millis=12000（boot 後 10 秒）
    TEST_ASSERT_EQUAL_UINT64(1713715210000ULL,
                             time_sync_current_epoch_ms(&g_state, 12000));
}

static void test_boot_present_but_unset_does_not_seed() {
    g_mock.set_present(true);
    // now_epoch_ms 預設 0（模擬 DS3231 在線但未設過時間）

    const bool seeded = simulate_boot_detect_and_seed(
        &g_mock, &g_state, /*now_millis=*/2000, TIME_SYNC_MIN_EPOCH_MS);

    TEST_ASSERT_FALSE(seeded);
    TEST_ASSERT_FALSE(g_state.synced);
    // current_epoch_ms 回 0（spec §4.1 未對時 fallback）
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 12000));
}

static void test_boot_not_present_does_not_seed() {
    NullRtcBackend null_backend;

    const bool seeded = simulate_boot_detect_and_seed(
        &null_backend, &g_state, /*now_millis=*/2000, TIME_SYNC_MIN_EPOCH_MS);

    TEST_ASSERT_FALSE(seeded);
    TEST_ASSERT_FALSE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 12000));
}

// ============================================================
// §3 BLE write-back：time_sync_handle Applied → set_epoch_ms
//    對齊 plan §5.1
// ============================================================

// 模擬 main.cpp time_sync handler 內 Applied 後寫回 RTC 的邏輯
static bool simulate_ble_apply_with_write_back(RtcBackend* backend,
                                               TimeSyncState* ts_state,
                                               const char* json,
                                               uint64_t now_millis) {
    const bool rtc_present = backend->is_present();
    char ack[256];
    size_t ack_len = 0;
    TimeSyncResult r = time_sync_handle(
        ts_state, reinterpret_cast<const uint8_t*>(json), std::strlen(json),
        now_millis, rtc_present, ack, sizeof(ack), &ack_len);

    if (r != TimeSyncResult::Applied || !backend->is_present()) {
        return false;
    }
    const uint64_t app_epoch = time_sync_current_epoch_ms(ts_state, now_millis);
    return backend->set_epoch_ms(app_epoch);
}

static void test_ble_apply_writes_back_to_present_rtc() {
    g_mock.set_present(true);
    g_mock.set_epoch_ms(0);  // DS3231 未設過

    const char* json =
        R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";

    const bool wrote = simulate_ble_apply_with_write_back(
        &g_mock, &g_state, json, /*now_millis=*/1000);

    TEST_ASSERT_TRUE(wrote);
    TEST_ASSERT_TRUE(g_state.synced);
    // 寫回值 = current_epoch_ms at now_millis=1000 = 1713715200000
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, g_mock.now_epoch_ms());
}

static void test_ble_apply_write_back_skipped_when_backend_absent() {
    NullRtcBackend null_backend;
    const char* json =
        R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";

    const bool wrote = simulate_ble_apply_with_write_back(
        &null_backend, &g_state, json, /*now_millis=*/1000);

    TEST_ASSERT_FALSE(wrote);
    // 但 g_state 仍應 Applied（BLE 對時生效，只是 RTC 寫回 noop）
    TEST_ASSERT_TRUE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             time_sync_current_epoch_ms(&g_state, 1000));
}

// ============================================================
// main
// ============================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_mock_backend_basic_contract);

    RUN_TEST(test_boot_present_and_set_seeds_software_clock);
    RUN_TEST(test_boot_present_but_unset_does_not_seed);
    RUN_TEST(test_boot_not_present_does_not_seed);

    RUN_TEST(test_ble_apply_writes_back_to_present_rtc);
    RUN_TEST(test_ble_apply_write_back_skipped_when_backend_absent);

    return UNITY_END();
}
