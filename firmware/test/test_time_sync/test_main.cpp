// Unit test: ems_time_sync
// 執行：pio test -e native -f test_time_sync
//
// 對齊 spec：docs/ble-time-sync-protocol.md §2 / §4
// 涵蓋：
//   - init() 初始狀態
//   - current_epoch_ms() 未對時態 / 對時後遞增
//   - handle() valid input → Applied + ACK 內容
//   - handle() rtc_present 旗標 → ACK source 切換
//   - handle() epoch 越界 → Rejected + ACK applied:false
//   - handle() malformed JSON / 錯 type / 缺欄位 → ParseError + 不寫 ACK
//   - handle() req_id 鏡射 / 省略
//   - handle() tz_offset_min 寫入 state

#include <unity.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

#include "ems_time_sync.h"

using namespace ems;

// 共用 fixture：每個 test 跑前重置
static TimeSyncState g_state;
static char g_ack[256];
static size_t g_ack_len;

void setUp() {
    time_sync_init(&g_state);
    std::memset(g_ack, 0, sizeof(g_ack));
    g_ack_len = 0;
}

void tearDown() {}

// Helper：包裝 c-string 呼叫 handle()
static TimeSyncResult call_handle(const char* json,
                                  uint64_t now_millis,
                                  bool rtc_present = false) {
    return time_sync_handle(
        &g_state,
        reinterpret_cast<const uint8_t*>(json),
        std::strlen(json),
        now_millis,
        rtc_present,
        g_ack,
        sizeof(g_ack),
        &g_ack_len);
}

// Helper：解析 ACK JSON 方便個別欄位斷言
static void parse_ack(JsonDocument& doc) {
    DeserializationError err = deserializeJson(doc, g_ack);
    TEST_ASSERT_FALSE_MESSAGE(err, "ACK should be valid JSON");
}

// ============================================================
// §1 init / current_epoch_ms
// ============================================================

static void test_init_clears_state() {
    g_state.synced = true;
    g_state.epoch_ms_offset = 12345;
    g_state.tz_offset_min = 480;

    time_sync_init(&g_state);

    TEST_ASSERT_FALSE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(0, g_state.epoch_ms_offset);
    TEST_ASSERT_EQUAL_INT16(0, g_state.tz_offset_min);
}

static void test_current_epoch_ms_returns_0_when_not_synced() {
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 5000));
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 0));
}

static void test_current_epoch_ms_after_sync_increases_with_millis() {
    // Apply via handle() then read current_epoch_ms at various millis
    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";
    TEST_ASSERT_EQUAL(TimeSyncResult::Applied, call_handle(json, 1000));

    // Apply at millis=1000 with epoch=1713715200000 → offset = 1713715199000
    // At millis=5000 → epoch = 1713715199000 + 5000 = 1713715204000
    TEST_ASSERT_EQUAL_UINT64(1713715204000ULL,
                             time_sync_current_epoch_ms(&g_state, 5000));
    // At millis=1000 (just after apply) → epoch = 1713715200000
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             time_sync_current_epoch_ms(&g_state, 1000));
}

// ============================================================
// §2 handle() valid input → Applied
// ============================================================

static void test_handle_valid_returns_applied_and_sets_state() {
    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480,"req_id":"ts-001"})";
    TimeSyncResult r = call_handle(json, 1000);

    TEST_ASSERT_EQUAL(TimeSyncResult::Applied, r);
    TEST_ASSERT_TRUE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(1713715199000ULL, g_state.epoch_ms_offset);
    TEST_ASSERT_EQUAL_INT16(480, g_state.tz_offset_min);
}

static void test_handle_applied_ack_contains_expected_fields() {
    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480,"req_id":"ts-001"})";
    call_handle(json, 1000, /*rtc_present=*/false);

    JsonDocument doc;
    parse_ack(doc);

    TEST_ASSERT_EQUAL_STRING("time_sync_ack", doc["type"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ts-001", doc["req_id"].as<const char*>());
    TEST_ASSERT_TRUE(doc["applied"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("ble", doc["source"].as<const char*>());
    TEST_ASSERT_FALSE(doc["rtc_present"].as<bool>());
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             doc["device_now_ms"].as<uint64_t>());
}

static void test_handle_applied_with_rtc_present_source_is_ble_plus_rtc() {
    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";
    call_handle(json, 1000, /*rtc_present=*/true);

    JsonDocument doc;
    parse_ack(doc);
    TEST_ASSERT_EQUAL_STRING("ble+rtc", doc["source"].as<const char*>());
    TEST_ASSERT_TRUE(doc["rtc_present"].as<bool>());
}

static void test_handle_omits_req_id_in_ack_when_request_omits() {
    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";
    call_handle(json, 1000);

    JsonDocument doc;
    parse_ack(doc);
    // spec §2.2: req_id 鏡射 request；若 request 沒帶則本欄位省略
    TEST_ASSERT_FALSE(doc["req_id"].is<const char*>());
}

// ============================================================
// §3 handle() epoch out-of-range → Rejected
// ============================================================

static void test_handle_epoch_too_small_rejected() {
    // 2023-12-31 (< 2024-01-01) → 應拒絕
    const char* json = R"({"type":"time_sync","epoch_ms":1703980800000,"tz_offset_min":480})";
    TimeSyncResult r = call_handle(json, 1000);

    TEST_ASSERT_EQUAL(TimeSyncResult::Rejected, r);
    TEST_ASSERT_FALSE(g_state.synced);  // state 不應被更新
}

static void test_handle_epoch_zero_rejected() {
    const char* json = R"({"type":"time_sync","epoch_ms":0,"tz_offset_min":480})";
    TimeSyncResult r = call_handle(json, 1000);
    TEST_ASSERT_EQUAL(TimeSyncResult::Rejected, r);
}

static void test_handle_epoch_too_large_rejected() {
    // > 2100-01-01 → 拒絕
    const char* json = R"({"type":"time_sync","epoch_ms":5000000000000,"tz_offset_min":480})";
    TimeSyncResult r = call_handle(json, 1000);
    TEST_ASSERT_EQUAL(TimeSyncResult::Rejected, r);
    TEST_ASSERT_FALSE(g_state.synced);
}

static void test_handle_rejected_ack_marks_applied_false() {
    const char* json = R"({"type":"time_sync","epoch_ms":0,"tz_offset_min":480,"req_id":"bad-1"})";
    call_handle(json, 1000);

    JsonDocument doc;
    parse_ack(doc);
    TEST_ASSERT_FALSE(doc["applied"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("rejected", doc["source"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("bad-1", doc["req_id"].as<const char*>());
}

// ============================================================
// §4 handle() ParseError → no ACK
// ============================================================

static void test_handle_malformed_json_returns_parse_error() {
    const char* json = "this is not json {";
    TimeSyncResult r = call_handle(json, 1000);

    TEST_ASSERT_EQUAL(TimeSyncResult::ParseError, r);
    // spec §2.3: ParseError 時不寫 ACK
    TEST_ASSERT_EQUAL_UINT(0, g_ack_len);
    TEST_ASSERT_EQUAL_UINT8(0, g_ack[0]);  // buffer 未被寫入
    TEST_ASSERT_FALSE(g_state.synced);
}

static void test_handle_wrong_type_field_returns_parse_error() {
    const char* json = R"({"type":"not_time_sync","epoch_ms":1713715200000})";
    TimeSyncResult r = call_handle(json, 1000);
    TEST_ASSERT_EQUAL(TimeSyncResult::ParseError, r);
    TEST_ASSERT_EQUAL_UINT(0, g_ack_len);
}

static void test_handle_missing_epoch_field_returns_parse_error() {
    const char* json = R"({"type":"time_sync","tz_offset_min":480})";
    TimeSyncResult r = call_handle(json, 1000);
    TEST_ASSERT_EQUAL(TimeSyncResult::ParseError, r);
    TEST_ASSERT_EQUAL_UINT(0, g_ack_len);
}

static void test_handle_missing_type_field_returns_parse_error() {
    const char* json = R"({"epoch_ms":1713715200000,"tz_offset_min":480})";
    TimeSyncResult r = call_handle(json, 1000);
    TEST_ASSERT_EQUAL(TimeSyncResult::ParseError, r);
}

// ============================================================
// §5 handle() 邊界值
// ============================================================

static void test_handle_epoch_at_min_boundary_accepted() {
    // 恰等於 MIN（2024-01-01）→ 允許（spec §2.3 用 < 拒絕）
    char json[128];
    std::snprintf(json, sizeof(json),
                  R"({"type":"time_sync","epoch_ms":%llu,"tz_offset_min":480})",
                  (unsigned long long)TIME_SYNC_MIN_EPOCH_MS);
    TEST_ASSERT_EQUAL(TimeSyncResult::Applied, call_handle(json, 1000));
}

static void test_handle_epoch_at_max_boundary_accepted() {
    // 恰等於 MAX（2100-01-01）→ 允許（spec §2.3 用 > 拒絕）
    char json[128];
    std::snprintf(json, sizeof(json),
                  R"({"type":"time_sync","epoch_ms":%llu,"tz_offset_min":480})",
                  (unsigned long long)TIME_SYNC_MAX_EPOCH_MS);
    TEST_ASSERT_EQUAL(TimeSyncResult::Applied, call_handle(json, 1000));
}

// ============================================================
// §6 handle() 連續 apply 行為（重對時）
// ============================================================

// ============================================================
// §7 ACK buffer 不足（regression guard for measureJson 預判分支）
// ============================================================

/**
 * 整合層應依 ack_len_out > 0 判斷是否 notify。
 * 此 test 防護未來若有人刪掉 write_ack 內 measureJson 預判邏輯導致
 * BLE 傳送截斷 JSON（spec §2.2 ACK schema 不可半截）。
 */
static void test_handle_ack_buffer_too_small_writes_empty_and_zero_len() {
    char tiny[8];  // 遠小於 ACK ~150 bytes
    std::memset(tiny, 0xFF, sizeof(tiny));  // 先填非 0，驗證 impl 確實清空
    size_t out_len = 999;

    const char* json = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";
    TimeSyncResult r = time_sync_handle(
        &g_state,
        reinterpret_cast<const uint8_t*>(json),
        std::strlen(json),
        /*now_millis=*/1000,
        /*rtc_present=*/false,
        tiny, sizeof(tiny), &out_len);

    // Applied 仍回（state 已正確更新；ACK 缺失是整合層用 ack_len 判斷的訊號）
    TEST_ASSERT_EQUAL(TimeSyncResult::Applied, r);
    TEST_ASSERT_TRUE(g_state.synced);

    // 但 ACK 應為「未寫入」狀態
    TEST_ASSERT_EQUAL_UINT(0, out_len);
    TEST_ASSERT_EQUAL_UINT8(0, tiny[0]);  // 首 byte 已被清零
}

static void test_handle_second_apply_overwrites_offset() {
    const char* json1 = R"({"type":"time_sync","epoch_ms":1713715200000,"tz_offset_min":480})";
    call_handle(json1, 1000);
    uint64_t first_offset = g_state.epoch_ms_offset;

    const char* json2 = R"({"type":"time_sync","epoch_ms":1713800000000,"tz_offset_min":480})";
    call_handle(json2, 2000);

    TEST_ASSERT_NOT_EQUAL_UINT64(first_offset, g_state.epoch_ms_offset);
    TEST_ASSERT_EQUAL_UINT64(1713799998000ULL, g_state.epoch_ms_offset);
}

// ============================================================
// Runner
// ============================================================

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_init_clears_state);
    RUN_TEST(test_current_epoch_ms_returns_0_when_not_synced);
    RUN_TEST(test_current_epoch_ms_after_sync_increases_with_millis);

    RUN_TEST(test_handle_valid_returns_applied_and_sets_state);
    RUN_TEST(test_handle_applied_ack_contains_expected_fields);
    RUN_TEST(test_handle_applied_with_rtc_present_source_is_ble_plus_rtc);
    RUN_TEST(test_handle_omits_req_id_in_ack_when_request_omits);

    RUN_TEST(test_handle_epoch_too_small_rejected);
    RUN_TEST(test_handle_epoch_zero_rejected);
    RUN_TEST(test_handle_epoch_too_large_rejected);
    RUN_TEST(test_handle_rejected_ack_marks_applied_false);

    RUN_TEST(test_handle_malformed_json_returns_parse_error);
    RUN_TEST(test_handle_wrong_type_field_returns_parse_error);
    RUN_TEST(test_handle_missing_epoch_field_returns_parse_error);
    RUN_TEST(test_handle_missing_type_field_returns_parse_error);

    RUN_TEST(test_handle_epoch_at_min_boundary_accepted);
    RUN_TEST(test_handle_epoch_at_max_boundary_accepted);

    RUN_TEST(test_handle_ack_buffer_too_small_writes_empty_and_zero_len);

    RUN_TEST(test_handle_second_apply_overwrites_offset);

    return UNITY_END();
}
