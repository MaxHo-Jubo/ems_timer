// EMS DoseSync Pro — Impl-Phase F: case_sync JSON serializer unit test
//
// 對應 lib：firmware/lib/case_sync_serializer/
// 對齊：docs/pm-dev-spec.md §14.1 schema
// 執行：pio test -e native -f test_case_sync_serializer
#include <unity.h>
#include <cstring>
#include <ArduinoJson.h>
#include "case_sync_serializer.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// 輔助：產 minimal meta
static CaseSyncMeta make_meta() {
    CaseSyncMeta m;
    m.case_id       = "11111111-2222-3333-4444-555555555555";
    m.mode          = "ohca";
    m.device_name   = "安康91";
    m.device_id     = "DSP-0001";
    m.fw_version    = "v1.0.0";
    m.started_at_ms = 1745740800000ULL;
    m.ended_at_ms   = 1745741700000ULL;
    return m;
}

// 輔助：parse 回 JSON 驗欄位
static void parse_json(const char* buf, size_t len, JsonDocument& doc) {
    DeserializationError err = deserializeJson(doc, buf, len);
    TEST_ASSERT_FALSE(err);
}

// ============================================================
//  Empty case：無 events + 空 summary
// ============================================================

static void test_serialize_empty_case_basic_schema() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);
    summary.case_start_ms = meta.started_at_ms;
    summary.case_end_ms   = meta.ended_at_ms;

    char buf[2048];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta,
                                            /*events=*/nullptr, 0, summary);
    TEST_ASSERT_GREATER_THAN(0, n);

    JsonDocument doc;
    parse_json(buf, n, doc);

    TEST_ASSERT_EQUAL_STRING("case_sync", doc["type"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("11111111-2222-3333-4444-555555555555",
                              doc["case_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ohca", doc["mode"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("安康91", doc["device_name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("DSP-0001", doc["device_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("v1.0.0", doc["fw_version"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT64(1745740800000ULL, doc["started_at_ms"].as<uint64_t>());
    TEST_ASSERT_EQUAL_UINT64(1745741700000ULL, doc["ended_at_ms"].as<uint64_t>());

    TEST_ASSERT_TRUE(doc["events"].is<JsonArray>());
    TEST_ASSERT_EQUAL_size_t(0, doc["events"].as<JsonArray>().size());

    TEST_ASSERT_TRUE(doc["summary"].is<JsonObject>());
}

// ============================================================
//  Events array 序列化
// ============================================================

static void test_serialize_events_array() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    ems_event_t events[2];
    events[0] = { 1, EVT_EPI_LOCAL,        1745740900000ULL, 100000, 1, false };
    events[1] = { 2, EVT_SHOCK_PRE_HANDOVER, 1745741000000ULL,      0, 2, true };

    char buf[2048];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta, events, 2, summary);
    TEST_ASSERT_GREATER_THAN(0, n);

    JsonDocument doc;
    parse_json(buf, n, doc);

    JsonArray arr = doc["events"].as<JsonArray>();
    TEST_ASSERT_EQUAL_size_t(2, arr.size());

    JsonObject e0 = arr[0];
    TEST_ASSERT_EQUAL_UINT8(EVT_EPI_LOCAL, e0["type"].as<uint8_t>());
    TEST_ASSERT_EQUAL_UINT64(1745740900000ULL, e0["timestamp_ms"].as<uint64_t>());
    TEST_ASSERT_EQUAL_UINT32(100000, e0["elapsed_ms"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT8(1, e0["count"].as<uint8_t>());
    TEST_ASSERT_FALSE(e0["actual_time_null"].as<bool>());

    JsonObject e1 = arr[1];
    TEST_ASSERT_EQUAL_UINT8(EVT_SHOCK_PRE_HANDOVER, e1["type"].as<uint8_t>());
    TEST_ASSERT_EQUAL_UINT8(2, e1["count"].as<uint8_t>());
    TEST_ASSERT_TRUE(e1["actual_time_null"].as<bool>());
}

// ============================================================
//  Summary object 對齊
// ============================================================

static void test_serialize_summary_fields() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);
    summary.epi_local           = 3;
    summary.epi_pre_handover    = 2;
    summary.epi_pure_supp       = 1;
    summary.epi_total           = 6;
    summary.shock_local         = 2;
    summary.shock_pre_handover  = 1;
    summary.shock_pure_supp     = 0;
    summary.shock_total         = 3;
    summary.amio_total          = 1;
    summary.first_epi_local_ms  = 1745740900000ULL;
    summary.last_epi_local_ms   = 1745741000000ULL;
    summary.last_shock_local_ms = 1745741100000ULL;
    summary.last_amio_ms        = 1745741200000ULL;
    summary.case_start_ms       = meta.started_at_ms;
    summary.case_end_ms         = meta.ended_at_ms;

    char buf[2048];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta, nullptr, 0, summary);
    TEST_ASSERT_GREATER_THAN(0, n);

    JsonDocument doc;
    parse_json(buf, n, doc);

    JsonObject s = doc["summary"].as<JsonObject>();
    TEST_ASSERT_EQUAL_UINT16(3, s["epi_local"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(6, s["epi_total"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(3, s["shock_total"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(1, s["amio_total"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT64(1745740900000ULL, s["first_epi_local_ms"].as<uint64_t>());
    TEST_ASSERT_EQUAL_UINT64(1745741200000ULL, s["last_amio_ms"].as<uint64_t>());
}

// ============================================================
//  防呆：buf 不足 / nullptr 字串
// ============================================================

static void test_serialize_buffer_too_small_returns_zero() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    char tiny[16];
    size_t n = case_sync_serialize_to_json(tiny, sizeof(tiny), meta, nullptr, 0, summary);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

static void test_serialize_null_string_outputs_empty() {
    CaseSyncMeta meta = make_meta();
    meta.device_name = nullptr;
    meta.fw_version  = nullptr;

    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    char buf[2048];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta, nullptr, 0, summary);
    TEST_ASSERT_GREATER_THAN(0, n);

    JsonDocument doc;
    parse_json(buf, n, doc);
    TEST_ASSERT_EQUAL_STRING("", doc["device_name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("", doc["fw_version"].as<const char*>());
}

// ============================================================
//  Training mode
// ============================================================

// ============================================================
//  Edge / stress：50 events 滿載 + 1-byte 邊界
// ============================================================

/** 50 events 滿載（V1 §4.4 case 容量上限）→ 預期 buffer ~10KB 內可容納 */
static void test_serialize_50_events_within_10kb() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    constexpr size_t N = 50;
    ems_event_t events[N];
    for (size_t i = 0; i < N; ++i) {
        events[i] = { static_cast<uint32_t>(i + 1), EVT_EPI_LOCAL,
                      1745740900000ULL + i * 1000, static_cast<uint32_t>(i * 1000),
                      1, false };
    }

    char buf[10240];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta, events, N, summary);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_LESS_THAN(sizeof(buf), n);

    JsonDocument doc;
    parse_json(buf, n, doc);
    TEST_ASSERT_EQUAL_size_t(N, doc["events"].as<JsonArray>().size());
}

/** buf 剛好 1 byte 不足（caller required-1）→ 必須回 0，不寫部分 JSON */
static void test_serialize_exactly_one_byte_short_returns_zero() {
    CaseSyncMeta meta = make_meta();
    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    // 先量真實需求
    char ref[2048];
    size_t required = case_sync_serialize_to_json(ref, sizeof(ref), meta, nullptr, 0, summary);
    TEST_ASSERT_GREATER_THAN(0, required);

    // 給 required（剛好不夠 '\0'）→ 應回 0
    char tight[2048];
    size_t n = case_sync_serialize_to_json(tight, required, meta, nullptr, 0, summary);
    TEST_ASSERT_EQUAL_size_t(0, n);

    // 給 required + 1（剛好夠）→ 應回 required
    n = case_sync_serialize_to_json(tight, required + 1, meta, nullptr, 0, summary);
    TEST_ASSERT_EQUAL_size_t(required, n);
}

static void test_serialize_training_mode() {
    CaseSyncMeta meta = make_meta();
    meta.mode = "training";

    ohca_case_summary_t summary;
    caseSummary_init(&summary);

    char buf[2048];
    size_t n = case_sync_serialize_to_json(buf, sizeof(buf), meta, nullptr, 0, summary);
    TEST_ASSERT_GREATER_THAN(0, n);

    JsonDocument doc;
    parse_json(buf, n, doc);
    TEST_ASSERT_EQUAL_STRING("training", doc["mode"].as<const char*>());
}

// ============================================================
//  Test runner
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_empty_case_basic_schema);
    RUN_TEST(test_serialize_events_array);
    RUN_TEST(test_serialize_summary_fields);
    RUN_TEST(test_serialize_buffer_too_small_returns_zero);
    RUN_TEST(test_serialize_null_string_outputs_empty);
    RUN_TEST(test_serialize_50_events_within_10kb);
    RUN_TEST(test_serialize_exactly_one_byte_short_returns_zero);
    RUN_TEST(test_serialize_training_mode);
    return UNITY_END();
}
