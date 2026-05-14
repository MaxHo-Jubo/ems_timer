// EMS DoseSync Pro — Phase E Unit Test: 持久化邏輯
//
// 對應規格：
//   - docs/pm-dev-spec.md §四 Phase E（持久化 / 歷史紀錄）
//   - plan: ~/.claude/plans/ticklish-exploring-dragonfly.md (Step 1 RED phase)
//   - lib/ems_storage/ems_storage_logic.h
//
// 涵蓋（Group A~G，共 40 cases）：
//   A — events.bin 序列化（8；A6/A7/A8 review-pr B 補洞）
//   B — CRC32 演算法（3）
//   C — index.json schema（6；C5/C6 review-pr B 補洞）
//   D — FIFO 邏輯（7；D7 pin C-1 已知 bug 當前行為）
//   E — Recovery / robustness（7；E6/E7 review-pr B 補洞）
//   F — Load / list path（3）
//   G — delete / format / init guards（5；review-pr B 補 public API 漏洞）
//
// 測試 framework：Unity（與 test_case_summary.cpp 相同模式）
//   - test 函式：static void test_*()
//   - 註冊：RUN_TEST(test_name) in main()
//   - 不使用 TEST_CASE() / TEST() 等其他 framework 風格 macro
//
// ⚠️ RED phase：本檔對應實作為 stub，預期跑出來全部失敗。
// ⚠️ Step 3 GREEN 階段禁止修改本檔。
#include <unity.h>
#include <string.h>
#include <stdint.h>

#include "ems_supp_model.h"
#include "ems_case_summary.h"
#include "ems_storage_logic.h"
#include "../helpers/in_mem_backend.h"

using namespace ems;
using namespace test_helpers;

// ============================================================
//  共用 fixture
// ============================================================

/** 全域 backend；setUp 重綁 */
static IStorageBackend g_be;

void setUp() {
    in_mem_reset();
    wire_in_mem_backend(&g_be);
}

void tearDown() {}

// ============================================================
//  共用 helper：建立並儲存「N 筆 EPI 事件」的測試 case
// ============================================================

/**
 * 建立 N 筆 EVT_EPI_LOCAL 事件後呼叫 storage_save_case。
 *
 * @param be          backend
 * @param type        案件類型
 * @param start_ms    case_start_ms（同時做為事件起點 epoch）
 * @param event_n     事件筆數（會 clamp 到 EMS_STORAGE_MAX_EVENTS）
 */
static void save_dummy_case(IStorageBackend*    be,
                            storage_case_type_t type,
                            uint64_t            start_ms,
                            uint16_t            event_n) {
    ems_event_t evs[EMS_STORAGE_MAX_EVENTS];
    uint16_t n = (event_n > EMS_STORAGE_MAX_EVENTS) ? EMS_STORAGE_MAX_EVENTS : event_n;
    for (uint16_t i = 0; i < n; ++i) {
        buildLocalEvent(&evs[i], i + 1, EVT_EPI_LOCAL,
                        start_ms + i * 1000ULL, i * 1000);
    }
    bool ok = storage_save_case(be, type, evs, n, start_ms,
                                start_ms + n * 1000ULL);
    TEST_ASSERT_TRUE_MESSAGE(ok, "save_dummy_case: storage_save_case 失敗");
}

// ============================================================
//  Group A — events.bin 序列化（5 cases）
// ============================================================

/** A1: 0 events，header + CRC 仍可解析 */
static void A1_serialize_empty_events() {
    uint8_t buf[64];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(nullptr, 0, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT32(EMS_STORAGE_BIN_HEADER_SIZE + EMS_STORAGE_BIN_FOOTER_SIZE,
                             out_len);

    ems_event_t out[1];
    uint16_t out_count = 99;
    TEST_ASSERT_TRUE(storage_bin_deserialize(buf, out_len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** A2: 寫 1 筆 EPI_LOCAL → 讀回完全相等（含 u64 timestamp） */
static void A2_serialize_one_event_round_trip() {
    ems_event_t in[1];
    buildLocalEvent(&in[0], /*event_id*/ 42, EVT_EPI_LOCAL,
                    /*ts*/ 0x123456789ABCULL, /*elapsed*/ 240000);

    uint8_t buf[EMS_STORAGE_BIN_HEADER_SIZE
                + EMS_STORAGE_WIRE_EVENT_SIZE
                + EMS_STORAGE_BIN_FOOTER_SIZE];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, 1, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(buf), out_len);

    ems_event_t out[1];
    memset(&out, 0, sizeof(out));
    uint16_t out_count = 0;
    TEST_ASSERT_TRUE(storage_bin_deserialize(buf, out_len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(1, out_count);
    TEST_ASSERT_EQUAL_UINT32(42, out[0].event_id);
    TEST_ASSERT_EQUAL_INT(EVT_EPI_LOCAL, out[0].type);
    TEST_ASSERT_EQUAL_UINT64(0x123456789ABCULL, out[0].timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(240000, out[0].elapsed_ms);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].count);
    TEST_ASSERT_FALSE(out[0].actual_time_null);
}

/** A3: 滿載 50 筆混合 type → byte-equal round-trip */
static void A3_serialize_50_events_round_trip() {
    ems_event_t in[EMS_STORAGE_MAX_EVENTS];
    for (int i = 0; i < EMS_STORAGE_MAX_EVENTS; ++i) {
        switch (i % 5) {
            case 0:
                buildLocalEvent(&in[i], i + 1, EVT_EPI_LOCAL,
                                1000ULL * (i + 1), i * 100);
                break;
            case 1:
                buildLocalEvent(&in[i], i + 1, EVT_SHOCK_LOCAL,
                                1500ULL * (i + 1), i * 100);
                break;
            case 2:
                buildLocalEvent(&in[i], i + 1, EVT_AMIODARONE,
                                2000ULL * (i + 1), i * 100);
                break;
            case 3:
                buildSuppEvent(&in[i], i + 1, SUPP_TYPE_EPI_PRE_HANDOVER,
                               /*count*/ 3, 3000ULL * (i + 1), i * 100);
                break;
            default:
                buildSuppEvent(&in[i], i + 1, SUPP_TYPE_SHOCK_PURE,
                               /*count*/ 1, 4000ULL * (i + 1), i * 100);
                break;
        }
    }

    uint8_t buf[EMS_STORAGE_BIN_HEADER_SIZE
                + EMS_STORAGE_MAX_EVENTS * EMS_STORAGE_WIRE_EVENT_SIZE
                + EMS_STORAGE_BIN_FOOTER_SIZE];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, EMS_STORAGE_MAX_EVENTS,
                                           buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(buf), out_len);

    ems_event_t out[EMS_STORAGE_MAX_EVENTS];
    memset(out, 0, sizeof(out));
    uint16_t out_count = 0;
    TEST_ASSERT_TRUE(storage_bin_deserialize(buf, out_len, out,
                                             EMS_STORAGE_MAX_EVENTS, &out_count));
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_MAX_EVENTS, out_count);

    for (int i = 0; i < EMS_STORAGE_MAX_EVENTS; ++i) {
        TEST_ASSERT_EQUAL_UINT32(in[i].event_id, out[i].event_id);
        TEST_ASSERT_EQUAL_INT(in[i].type, out[i].type);
        TEST_ASSERT_EQUAL_UINT64(in[i].timestamp_ms, out[i].timestamp_ms);
        TEST_ASSERT_EQUAL_UINT32(in[i].elapsed_ms, out[i].elapsed_ms);
        TEST_ASSERT_EQUAL_UINT8(in[i].count, out[i].count);
        TEST_ASSERT_EQUAL(in[i].actual_time_null, out[i].actual_time_null);
    }

    // 第二次 serialize 應產生 byte-identical buffer（驗證 deterministic）
    //   未來若 struct padding / reserved byte 改動讓 wire format 漂移會抓到
    uint8_t buf2[sizeof(buf)];
    size_t out_len2 = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, EMS_STORAGE_MAX_EVENTS,
                                           buf2, sizeof(buf2), &out_len2));
    TEST_ASSERT_EQUAL_UINT32(out_len, out_len2);
    TEST_ASSERT_EQUAL_INT(0, memcmp(buf, buf2, out_len));
}

/** A4: 翻轉 payload 一個 bit → CRC 檢出，out_count 設 0 + 不污染 output buffer */
static void A4_crc_mismatch_detected() {
    ems_event_t in[1];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);

    uint8_t buf[64];
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, 1, buf, sizeof(buf), &len));

    // 翻轉 event payload 區的某個 bit（offset 落在 event 0 內）
    buf[EMS_STORAGE_BIN_HEADER_SIZE + 2] ^= 0x01;

    ems_event_t out[1];
    memset(out, 0xAA, sizeof(out));       // sentinel：每 byte 0xAA
    uint16_t out_count = 999;
    TEST_ASSERT_FALSE(storage_bin_deserialize(buf, len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);

    // 顯式驗證 deserialize 失敗時不污染 output buffer
    //   event_id 為 u32，4 bytes 全 0xAA → 0xAAAAAAAA
    //   timestamp_ms 為 u64，8 bytes 全 0xAA → 0xAAAAAAAAAAAAAAAA
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAAu,          out[0].event_id);
    TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAAAAAAULL, out[0].timestamp_ms);
}

/** A5: 破壞 magic → deserialize 拒絕，out_count 設 0 */
static void A5_magic_mismatch_detected() {
    ems_event_t in[1];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);

    uint8_t buf[64];
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, 1, buf, sizeof(buf), &len));

    buf[0] ^= 0xFF;  // 破壞 magic byte 0

    ems_event_t out[1];
    uint16_t out_count = 999;
    TEST_ASSERT_FALSE(storage_bin_deserialize(buf, len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** A6: 翻轉 CRC footer byte → CRC 檢出（A4 只測 payload，本 case 測 footer 自身） */
static void A6_crc_corruption_at_footer() {
    ems_event_t in[1];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);

    uint8_t buf[64];
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, 1, buf, sizeof(buf), &len));

    buf[len - 1] ^= 0x55;  // 翻轉 CRC footer 最後一個 byte

    ems_event_t out[1];
    uint16_t out_count = 999;
    TEST_ASSERT_FALSE(storage_bin_deserialize(buf, len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** A7: header version byte 改為未來版本 (2) → 拒絕（避免舊韌體讀新 schema 誤解資料） */
static void A7_version_byte_mismatch_rejected() {
    ems_event_t in[1];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);

    uint8_t buf[64];
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(in, 1, buf, sizeof(buf), &len));

    buf[4] = 0x02;  // version byte 改為未來版本 v2（offset 4，小端 LSB）

    ems_event_t out[1];
    uint16_t out_count = 999;
    TEST_ASSERT_FALSE(storage_bin_deserialize(buf, len, out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** A8: buffer 小於 HEADER+FOOTER (12 bytes) → 直接拒絕，不嘗試解析 */
static void A8_deserialize_truncated_buffer() {
    uint8_t buf[5] = {0};  // < 8 (HEADER) + 4 (FOOTER)

    ems_event_t out[1];
    uint16_t out_count = 999;
    TEST_ASSERT_FALSE(storage_bin_deserialize(buf, sizeof(buf), out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

// ============================================================
//  Group B — CRC32 演算法（3 cases）
// ============================================================

/** B1: IEEE 802.3 test vector "123456789" → 0xCBF43926 */
static void B1_crc32_known_vector() {
    const uint8_t input[] = "123456789";
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, storage_crc32(input, 9));
}

/** B2: 空 buffer → 0 */
static void B2_crc32_empty() {
    TEST_ASSERT_EQUAL_UINT32(0u, storage_crc32(nullptr, 0));
}

/** B3: 切兩段算 == 一次算（streaming 預留） */
static void B3_crc32_incremental() {
    const uint8_t a[] = "12345";
    const uint8_t b[] = "6789";
    const uint8_t whole[] = "123456789";

    uint32_t one_shot = storage_crc32(whole, 9);
    uint32_t inc = storage_crc32_update(0, a, 5);
    inc = storage_crc32_update(inc, b, 4);

    TEST_ASSERT_EQUAL_UINT32(one_shot, inc);
}

// ============================================================
//  Group C — index.json schema（4 cases）
// ============================================================

/** C1: 空 cases + next_seq=1 → 可被自己 parse 回來 */
static void C1_index_build_empty() {
    char json[512];
    memset(json, 0, sizeof(json));
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_index_serialize(nullptr, 0, /*next_seq*/ 1,
                                             json, sizeof(json), &len));
    TEST_ASSERT_TRUE(len > 0);

    case_meta_t out[8];
    uint16_t out_count = 99;
    uint32_t out_next = 0;
    TEST_ASSERT_TRUE(storage_index_parse(json, len, out, 8,
                                         &out_count, &out_next));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
    TEST_ASSERT_EQUAL_UINT32(1, out_next);
}

/** C2: 1 筆 metadata → 序列化 → parse → 所有欄位 byte-equal */
static void C2_index_with_one_case() {
    case_meta_t in[1];
    memset(&in[0], 0, sizeof(in[0]));
    strncpy(in[0].id, "0000000003", sizeof(in[0].id) - 1);
    in[0].type               = EMS_CASE_TYPE_OHCA;
    in[0].start_ms           = 1730000000000ULL;
    in[0].end_ms             = 1730001500000ULL;
    in[0].epi_local          = 3;
    in[0].epi_pre_handover   = 0;
    in[0].epi_pure_supp      = 0;
    in[0].shock_local        = 4;
    in[0].shock_pre_handover = 0;
    in[0].shock_pure_supp    = 1;
    in[0].amio               = 0;
    in[0].event_count        = 10;
    in[0].bin_v              = EMS_STORAGE_BIN_VERSION;

    char json[1024];
    memset(json, 0, sizeof(json));
    size_t len = 0;
    TEST_ASSERT_TRUE(storage_index_serialize(in, 1, /*next_seq*/ 4,
                                             json, sizeof(json), &len));

    case_meta_t out[1];
    memset(&out[0], 0xAA, sizeof(out[0]));
    uint16_t out_count = 0;
    uint32_t out_next = 0;
    TEST_ASSERT_TRUE(storage_index_parse(json, len, out, 1,
                                         &out_count, &out_next));
    TEST_ASSERT_EQUAL_UINT16(1, out_count);
    TEST_ASSERT_EQUAL_UINT32(4, out_next);
    TEST_ASSERT_EQUAL_STRING("0000000003", out[0].id);
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_OHCA, out[0].type);
    TEST_ASSERT_EQUAL_UINT64(1730000000000ULL, out[0].start_ms);
    TEST_ASSERT_EQUAL_UINT64(1730001500000ULL, out[0].end_ms);
    TEST_ASSERT_EQUAL_UINT16(3, out[0].epi_local);
    TEST_ASSERT_EQUAL_UINT16(4, out[0].shock_local);
    TEST_ASSERT_EQUAL_UINT16(1, out[0].shock_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(10, out[0].event_count);
    TEST_ASSERT_EQUAL_UINT8(EMS_STORAGE_BIN_VERSION, out[0].bin_v);
}

/** C3: 手寫 JSON 含未知欄位 → parser 忽略不報錯（彈性 spec 核心） */
static void C3_index_unknown_field_tolerated() {
    const char* json =
        "{"
            "\"v\":1,"
            "\"next_seq\":4,"
            "\"future_field\":42,"
            "\"cases\":["
                "{"
                    "\"id\":\"0000000003\","
                    "\"type\":\"ohca\","
                    "\"start_ms\":1730000000000,"
                    "\"end_ms\":1730001500000,"
                    "\"epi_local\":3,"
                    "\"epi_pre_handover\":0,"
                    "\"epi_pure_supp\":0,"
                    "\"shock_local\":4,"
                    "\"shock_pre_handover\":0,"
                    "\"shock_pure_supp\":1,"
                    "\"amio\":0,"
                    "\"event_count\":10,"
                    "\"bin_v\":1,"
                    "\"future_per_case_field\":\"abc\""
                "}"
            "]"
        "}";

    case_meta_t out[1];
    memset(&out[0], 0, sizeof(out[0]));
    uint16_t out_count = 0;
    uint32_t out_next = 0;
    TEST_ASSERT_TRUE(storage_index_parse(json, strlen(json), out, 1,
                                         &out_count, &out_next));
    TEST_ASSERT_EQUAL_UINT16(1, out_count);
    TEST_ASSERT_EQUAL_UINT32(4, out_next);
    TEST_ASSERT_EQUAL_STRING("0000000003", out[0].id);
    TEST_ASSERT_EQUAL_UINT16(3, out[0].epi_local);
}

/** C4: 非合法 JSON → parse 拒絕、out_count 設 0、不 crash */
static void C4_index_corrupt_json_detected() {
    const char* json = "this is not json {{";

    case_meta_t out[1];
    uint16_t out_count = 99;
    uint32_t out_next = 99;
    TEST_ASSERT_FALSE(storage_index_parse(json, strlen(json), out, 1,
                                          &out_count, &out_next));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** C5: serialize 緩衝不足 → return false（STEP 03 `n >= json_max` guard） */
static void C5_index_serialize_buffer_overflow() {
    case_meta_t in[1];
    memset(&in[0], 0, sizeof(in[0]));
    strncpy(in[0].id, "0000000001", sizeof(in[0].id) - 1);
    in[0].type        = EMS_CASE_TYPE_OHCA;
    in[0].event_count = 1;
    in[0].bin_v       = EMS_STORAGE_BIN_VERSION;

    // 故意給小到不夠的 buffer
    char small[16];
    size_t len = 0;
    TEST_ASSERT_FALSE(storage_index_serialize(in, 1, /*next_seq*/ 2,
                                              small, sizeof(small), &len));
}

/** C6: case_type_to_str / from_str 雙向 mapping + 未知字串 silent fallback OHCA
 *  Pin 住已知缺陷（review-pr B C-13）的當前行為，避免 silent regression。
 *  Spec 取捨：「能讀部分資料」優於「整個 index 拒絕」；修復方案見 Batch 2 討論。 */
static void C6_case_type_mapping_and_unknown_fallback() {
    // 已知 mapping 正確
    TEST_ASSERT_EQUAL_STRING("ohca",     case_type_to_str(EMS_CASE_TYPE_OHCA));
    TEST_ASSERT_EQUAL_STRING("training", case_type_to_str(EMS_CASE_TYPE_TRAINING));
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_OHCA,     case_type_from_str("ohca"));
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_TRAINING, case_type_from_str("training"));

    // 未知字串 / nullptr / 空字串 → 全部 silent fallback OHCA（已知缺陷契約）
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_OHCA, case_type_from_str("unknown"));
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_OHCA, case_type_from_str(""));
    TEST_ASSERT_EQUAL_INT(EMS_CASE_TYPE_OHCA, case_type_from_str(nullptr));
}

// ============================================================
//  Group D — FIFO 邏輯（6 cases，核心）
// ============================================================

/** D1: 存 49 筆（未滿 cap） → list 回傳 49、無刪除 */
static void D1_save_under_cap() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 49; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(49, n);
}

/** D2: 存到剛好滿 cap（50 筆） → 全部留下，無刪除 */
static void D2_save_exactly_at_cap() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 50; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);
}

/** D3: 存第 51 筆 → 最舊（seq=1）file 與 entry 都消失、其餘 50 完整 */
static void D3_save_overflow_one() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 51; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);

    // seq=1 應被擠掉
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/ohca/0000000001.bin"));
    // seq=51 應存在
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/cases/ohca/0000000051.bin"));
}

/** D4: 一次 burst 寫 55 筆 → 最終剩 50（seq 6..55） */
static void D4_save_overflow_burst() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 55; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);

    // 最舊 5 筆（seq 1~5）file 都被刪
    for (uint32_t s = 1; s <= 5; ++s) {
        char id[EMS_STORAGE_ID_LEN];
        char path[EMS_STORAGE_PATH_MAX];
        storage_format_id(s, id, sizeof(id));
        storage_build_event_path(path, sizeof(path), EMS_CASE_TYPE_OHCA, id);
        TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, path));
    }
    // 最新 50 筆（seq 6~55）都在
    for (uint32_t s = 6; s <= 55; ++s) {
        char id[EMS_STORAGE_ID_LEN];
        char path[EMS_STORAGE_PATH_MAX];
        storage_format_id(s, id, sizeof(id));
        storage_build_event_path(path, sizeof(path), EMS_CASE_TYPE_OHCA, id);
        TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, path));
    }
}

/** D5: OHCA 50 + Training 20 互不擠掉，兩邊獨立 cap */
static void D5_ohca_training_independent_caps() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 50; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    for (int i = 0; i < 20; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_TRAINING,
                        2000000ULL + i * 1000ULL, 1);
    }
    case_meta_t list_o[EMS_STORAGE_OHCA_CAP];
    case_meta_t list_t[EMS_STORAGE_TRAINING_CAP];
    uint16_t no = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                               list_o, EMS_STORAGE_OHCA_CAP);
    uint16_t nt = storage_list(&g_be, EMS_CASE_TYPE_TRAINING,
                               list_t, EMS_STORAGE_TRAINING_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, no);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_TRAINING_CAP, nt);
}

/** D6: pop oldest 不會把 next_seq 倒退；新 case 永遠拿新 seq */
static void D6_seq_monotonic_after_delete() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 51; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);
    // newest-first：list[0] = seq 51
    TEST_ASSERT_EQUAL_STRING("0000000051", list[0].id);
    // 最舊（list[49]）為 seq 2（seq 1 被擠掉）
    TEST_ASSERT_EQUAL_STRING("0000000002", list[EMS_STORAGE_OHCA_CAP - 1].id);

    // 再存 1 筆 → seq 應為 52（不倒退）
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 999999ULL, 1);
    n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                     list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);
    TEST_ASSERT_EQUAL_STRING("0000000052", list[0].id);
    // 此時 seq 2 也應被擠掉，最舊為 seq 3
    TEST_ASSERT_EQUAL_STRING("0000000003", list[EMS_STORAGE_OHCA_CAP - 1].id);

    // 模擬重啟 → next_seq 應從 index.json 還原（持久化），新存的 seq = 53
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 8888ULL, 1);
    n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                     list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, n);
    TEST_ASSERT_EQUAL_STRING("0000000053", list[0].id);
}

/** D7: case_count == kTotalCapacity (70) 時存新 OHCA → 同類別 FIFO 擠掉最舊 OHCA
 *
 *  **C-1 修復後行為**（review-pr B Batch 2，commit pending）：
 *  - storage_save_case 入口 pre-evict 同類別最舊一筆，騰位給新案件
 *  - Training 完全不受影響（cap_for_type 對稱性）
 *  - seq 仍單調遞增（next_seq=71，最舊 OHCA seq=1 被刪 → 最舊變 seq=2） */
static void D7_save_at_global_capacity_evicts_oldest_same_type() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < EMS_STORAGE_OHCA_CAP; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);  // seq 1~50
    }
    for (int i = 0; i < EMS_STORAGE_TRAINING_CAP; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_TRAINING,
                        2000000ULL + i * 1000ULL, 1);  // seq 51~70
    }

    // 此時 case_count = OHCA_CAP + TRAINING_CAP = kTotalCapacity
    ems_event_t evs[1];
    buildLocalEvent(&evs[0], 1, EVT_EPI_LOCAL, 999999ULL, 0);
    bool result = storage_save_case(&g_be, EMS_CASE_TYPE_OHCA, evs, 1,
                                    999999ULL, 999999ULL);
    TEST_ASSERT_TRUE_MESSAGE(result, "C-1 修復後跨類別滿 + 新 OHCA 應 FIFO 成功");

    // OHCA 仍 50 筆；最舊 seq=1 被擠掉、新 seq=71 存在
    case_meta_t list_o[EMS_STORAGE_OHCA_CAP];
    uint16_t no = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                               list_o, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_OHCA_CAP, no);
    TEST_ASSERT_FALSE_MESSAGE(g_be.exists(g_be.ctx, "/cases/ohca/0000000001.bin"),
                              "最舊 OHCA seq=1 應被擠掉");
    TEST_ASSERT_TRUE_MESSAGE(g_be.exists(g_be.ctx, "/cases/ohca/0000000071.bin"),
                             "新 OHCA seq=71 應存在");

    // Training 完全不動（仍 20 筆，seq 51~70 完整）
    case_meta_t list_t[EMS_STORAGE_TRAINING_CAP];
    uint16_t nt = storage_list(&g_be, EMS_CASE_TYPE_TRAINING,
                               list_t, EMS_STORAGE_TRAINING_CAP);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(EMS_STORAGE_TRAINING_CAP, nt,
                                     "Training 不該因 OHCA FIFO 受影響");
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/cases/training/0000000051.bin"));
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/cases/training/0000000070.bin"));
}

/** D8: D7 對稱情境 — 跨類別滿時存新 Training → 擠掉最舊 Training，OHCA 不動
 *
 *  cover D7 沒測到的方向（D3 + D5 + D7 三角形補完最後一邊）。 */
static void D8_save_at_global_capacity_evicts_oldest_training() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < EMS_STORAGE_OHCA_CAP; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);  // seq 1~50
    }
    for (int i = 0; i < EMS_STORAGE_TRAINING_CAP; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_TRAINING,
                        2000000ULL + i * 1000ULL, 1);  // seq 51~70
    }

    ems_event_t evs[1];
    buildLocalEvent(&evs[0], 1, EVT_EPI_LOCAL, 888888ULL, 0);
    bool result = storage_save_case(&g_be, EMS_CASE_TYPE_TRAINING, evs, 1,
                                    888888ULL, 888888ULL);
    TEST_ASSERT_TRUE_MESSAGE(result, "C-1 修復後跨類別滿 + 新 Training 應 FIFO 成功");

    // Training 仍 20 筆；最舊 seq=51 被擠、新 seq=71 存在
    case_meta_t list_t[EMS_STORAGE_TRAINING_CAP];
    uint16_t nt = storage_list(&g_be, EMS_CASE_TYPE_TRAINING,
                               list_t, EMS_STORAGE_TRAINING_CAP);
    TEST_ASSERT_EQUAL_UINT16(EMS_STORAGE_TRAINING_CAP, nt);
    TEST_ASSERT_FALSE_MESSAGE(g_be.exists(g_be.ctx, "/cases/training/0000000051.bin"),
                              "最舊 Training seq=51 應被擠掉");
    TEST_ASSERT_TRUE_MESSAGE(g_be.exists(g_be.ctx, "/cases/training/0000000071.bin"),
                             "新 Training seq=71 應存在");

    // OHCA 完全不動
    case_meta_t list_o[EMS_STORAGE_OHCA_CAP];
    uint16_t no = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                               list_o, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(EMS_STORAGE_OHCA_CAP, no,
                                     "OHCA 不該因 Training FIFO 受影響");
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/cases/ohca/0000000001.bin"));
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/cases/ohca/0000000050.bin"));
}

// ============================================================
//  Group E — Recovery / robustness（4 cases）
// ============================================================

/** E1: 刪 index.json 後重 init → 從 events.bin 列表 rebuild 全部 case */
static void E1_missing_index_rebuild_from_files() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 5; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    // 故意刪 index.json
    g_be.delete_file(g_be.ctx, "/cases/index.json");

    // 重 init → 應自動 rebuild
    TEST_ASSERT_TRUE(storage_init(&g_be));
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(5, n);
}

/** E2: 直接寫一個沒對應 entry 的 events.bin → init 清掉孤兒檔
 *  path 用 storage_build_event_path() 構造，避免硬編碼字串脫離實作命名規則 */
static void E2_orphan_event_file_purged() {
    TEST_ASSERT_TRUE(storage_init(&g_be));

    // 構造孤兒檔路徑（用 helper 對齊實作命名規則）
    char orphan_path[EMS_STORAGE_PATH_MAX];
    storage_build_event_path(orphan_path, sizeof(orphan_path),
                             EMS_CASE_TYPE_OHCA, "0000000099");

    // 直接寫一個合法 events.bin 但跳過 storage_save_case
    ems_event_t ev[1];
    buildLocalEvent(&ev[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);
    uint8_t bin_buf[EMS_STORAGE_BIN_HEADER_SIZE
                    + EMS_STORAGE_WIRE_EVENT_SIZE
                    + EMS_STORAGE_BIN_FOOTER_SIZE];
    size_t blen = 0;
    TEST_ASSERT_TRUE(storage_bin_serialize(ev, 1, bin_buf, sizeof(bin_buf), &blen));
    g_be.write_file(g_be.ctx, orphan_path, bin_buf, blen);

    // init 應清掉孤兒（或將其納入 index — 二擇一；本 test 採「清掉」契約）
    TEST_ASSERT_TRUE(storage_init(&g_be));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, orphan_path));
}

/** E3: 直接刪一個 events.bin 但 index 保留 entry → init 移除 orphan entry */
static void E3_orphan_index_entry_dropped() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 2000ULL, 1);

    // 直接刪 events.bin 留下 index entry
    g_be.delete_file(g_be.ctx, "/cases/ohca/0000000001.bin");

    // init 應移除 orphan entry
    TEST_ASSERT_TRUE(storage_init(&g_be));
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000002", list[0].id);
}

/** E4: index.json.tmp 殘留（power-loss 模擬） → init 清掉 .tmp、保留好的 index */
static void E4_tmp_file_recovery() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);

    // 模擬寫 index.json.tmp 後 power-loss（.tmp 與正式 index 並存）
    uint8_t garbage[] = "garbage";
    g_be.write_file(g_be.ctx, "/cases/index.json.tmp",
                    garbage, sizeof(garbage));

    // init 應清掉 .tmp、保留好的 index
    TEST_ASSERT_TRUE(storage_init(&g_be));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/index.json.tmp"));

    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
}

/** E5: 對齊 pm-dev-spec §四 Phase E 範圍「從歷史紀錄重新進入案件總覽」
 *  存一筆含 EPI+SHOCK+AMIO 混合事件 OHCA → 重 init → list → load_events
 *    → 餵給既有 caseSummary_build() → 統計欄位與存檔前 byte-equal */
static void E5_summary_reconstruction_after_persist() {
    TEST_ASSERT_TRUE(storage_init(&g_be));

    // 建立含 EPI+SHOCK+AMIO+補登 EPI 混合事件
    ems_event_t in[5];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL,                       100000ULL, 0);
    buildLocalEvent(&in[1], 2, EVT_SHOCK_LOCAL,                     150000ULL, 50000);
    buildLocalEvent(&in[2], 3, EVT_EPI_LOCAL,                       340000ULL, 240000);
    buildLocalEvent(&in[3], 4, EVT_AMIODARONE,                      400000ULL, 300000);
    buildSuppEvent (&in[4], 5, SUPP_TYPE_EPI_PRE_HANDOVER, /*c*/ 3, 500000ULL, 0);

    // 算存檔前 summary
    ohca_case_summary_t before;
    caseSummary_build(&before, in, 5, /*start*/ 100000ULL, /*end*/ 500000ULL);

    // 存檔
    TEST_ASSERT_TRUE(storage_save_case(&g_be, EMS_CASE_TYPE_OHCA,
                                       in, 5, 100000ULL, 500000ULL));

    // 模擬重啟（fs 內容保留，但重 init）
    TEST_ASSERT_TRUE(storage_init(&g_be));

    // 列出歷史，找到唯一一筆
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);

    // 載回事件陣列
    ems_event_t out[5];
    memset(out, 0, sizeof(out));
    uint16_t out_count = 0;
    TEST_ASSERT_TRUE(storage_load_events(&g_be, EMS_CASE_TYPE_OHCA,
                                         list[0].id, out, 5, &out_count));
    TEST_ASSERT_EQUAL_UINT16(5, out_count);

    // 重新聚合：路徑 = 載回 events + 載回 metadata 的 case_start/end
    ohca_case_summary_t after;
    caseSummary_build(&after, out, out_count,
                      list[0].start_ms, list[0].end_ms);

    // 統計欄位 byte-equal
    TEST_ASSERT_EQUAL_UINT16(before.epi_local,           after.epi_local);
    TEST_ASSERT_EQUAL_UINT16(before.epi_pre_handover,    after.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(before.epi_pure_supp,       after.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(before.epi_total,           after.epi_total);
    TEST_ASSERT_EQUAL_UINT16(before.shock_local,         after.shock_local);
    TEST_ASSERT_EQUAL_UINT16(before.shock_total,         after.shock_total);
    TEST_ASSERT_EQUAL_UINT16(before.amio_total,          after.amio_total);
    TEST_ASSERT_EQUAL_UINT64(before.first_epi_local_ms,  after.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(before.last_epi_local_ms,   after.last_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(before.last_shock_local_ms, after.last_shock_local_ms);
    TEST_ASSERT_EQUAL_UINT64(before.last_amio_ms,        after.last_amio_ms);
    TEST_ASSERT_EQUAL_UINT64(before.case_start_ms,       after.case_start_ms);
    TEST_ASSERT_EQUAL_UINT64(before.case_end_ms,         after.case_end_ms);

    // case_meta_t（save 時聚合，與 caseSummary_build 為兩條獨立路徑）cross-check
    //   若未來改 storage_save_case 內欄位填寫但漏改 caseSummary_build（或反之），本 assert 抓得到
    TEST_ASSERT_EQUAL_UINT16(before.epi_local,        list[0].epi_local);
    TEST_ASSERT_EQUAL_UINT16(before.epi_pre_handover, list[0].epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(before.epi_pure_supp,    list[0].epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(before.shock_local,      list[0].shock_local);
    TEST_ASSERT_EQUAL_UINT16(before.amio_total,       list[0].amio);
}

/** E6: index.json.tmp 含「有效但未 commit」payload + main index 為舊版 → init 丟棄 .tmp
 *
 *  Atomic write 契約：rename = commit barrier；若 rename 沒跑，.tmp 即便內容合法
 *  也代表「partial write」，必須丟棄不能誤認為已完成的寫入。E4 只測 .tmp = garbage
 *  + main 有效；本 case 測 .tmp 有效但 main 舊（典型 power-loss 在 rename 前）。 */
static void E6_tmp_valid_payload_purged_on_init() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);

    // 構造一個內容合法但代表「新 commit 還沒完成」的 .tmp
    case_meta_t fake[1];
    memset(&fake[0], 0, sizeof(fake[0]));
    strncpy(fake[0].id, "0000000099", sizeof(fake[0].id) - 1);
    fake[0].type        = EMS_CASE_TYPE_OHCA;
    fake[0].epi_local   = 1;
    fake[0].event_count = 1;
    fake[0].bin_v       = EMS_STORAGE_BIN_VERSION;

    char fake_json[512];
    size_t fake_len = 0;
    TEST_ASSERT_TRUE(storage_index_serialize(fake, 1, /*next_seq*/ 100,
                                             fake_json, sizeof(fake_json), &fake_len));
    g_be.write_file(g_be.ctx, "/cases/index.json.tmp",
                    (const uint8_t*)fake_json, fake_len);

    // init 必須丟掉 .tmp，main 保持原狀（seq 1，不是來自 .tmp 的 seq 99 + next 100）
    TEST_ASSERT_TRUE(storage_init(&g_be));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/index.json.tmp"));

    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA, list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000001", list[0].id);

    // 再存一筆 → 拿 seq 2（不是 100，證明 .tmp 內 next_seq=100 沒被吸收）
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 3000ULL, 1);
    n = storage_list(&g_be, EMS_CASE_TYPE_OHCA, list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(2, n);
    TEST_ASSERT_EQUAL_STRING("0000000002", list[0].id);
}

/** E7: rebuild 路徑遇非 .bin 結尾 / 非數字 id 檔 → name_is_bin 拒絕，靜默跳過 */
static void E7_non_bin_files_ignored_during_rebuild() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);  // 合法 seq 1

    // 在 OHCA 目錄塞兩個非合法 .bin 的檔
    uint8_t garbage[] = "noise";
    g_be.write_file(g_be.ctx, "/cases/ohca/notes.txt",
                    garbage, sizeof(garbage) - 1);
    g_be.write_file(g_be.ctx, "/cases/ohca/abc.bin",  // 非數字 id
                    garbage, sizeof(garbage) - 1);

    // 刪 index → 強迫走 rebuild 路徑（STEP 05.01）
    g_be.delete_file(g_be.ctx, "/cases/index.json");
    TEST_ASSERT_TRUE(storage_init(&g_be));

    // rebuild 後只剩合法 seq 1，垃圾檔被忽略
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA, list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000001", list[0].id);
}

// ============================================================
//  Group F — Load / list path（3 cases）
// ============================================================

/** F1: save 一個多 type 混合的 case → load by id 還原 byte-equal
 *  解耦 storage_format_id 內部格式：透過 storage_list 取得實際 id，不 hardcode */
static void F1_load_events_by_id() {
    TEST_ASSERT_TRUE(storage_init(&g_be));

    ems_event_t in[3];
    buildLocalEvent(&in[0], 1, EVT_EPI_LOCAL,    10000ULL, 0);
    buildLocalEvent(&in[1], 2, EVT_SHOCK_LOCAL,  20000ULL, 10000);
    buildLocalEvent(&in[2], 3, EVT_AMIODARONE,   30000ULL, 20000);

    TEST_ASSERT_TRUE(storage_save_case(&g_be, EMS_CASE_TYPE_OHCA,
                                       in, 3, 10000ULL, 30000ULL));

    // 從 list 取實際 id（解耦 zero-padded 10-digit 格式）
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n_list = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                                   list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n_list);

    ems_event_t out[3];
    memset(out, 0, sizeof(out));
    uint16_t out_count = 0;
    TEST_ASSERT_TRUE(storage_load_events(&g_be, EMS_CASE_TYPE_OHCA,
                                         list[0].id, out, 3, &out_count));
    TEST_ASSERT_EQUAL_UINT16(3, out_count);

    TEST_ASSERT_EQUAL_UINT32(1, out[0].event_id);
    TEST_ASSERT_EQUAL_INT(EVT_EPI_LOCAL, out[0].type);
    TEST_ASSERT_EQUAL_UINT64(10000ULL, out[0].timestamp_ms);

    TEST_ASSERT_EQUAL_UINT32(2, out[1].event_id);
    TEST_ASSERT_EQUAL_INT(EVT_SHOCK_LOCAL, out[1].type);
    TEST_ASSERT_EQUAL_UINT64(20000ULL, out[1].timestamp_ms);

    TEST_ASSERT_EQUAL_UINT32(3, out[2].event_id);
    TEST_ASSERT_EQUAL_INT(EVT_AMIODARONE, out[2].type);
    TEST_ASSERT_EQUAL_UINT64(30000ULL, out[2].timestamp_ms);
}

/** F2: load 不存在的 id → 回 false、out_count 設 0 */
static void F2_load_nonexistent_id() {
    TEST_ASSERT_TRUE(storage_init(&g_be));

    ems_event_t out[1];
    uint16_t out_count = 99;
    TEST_ASSERT_FALSE(storage_load_events(&g_be, EMS_CASE_TYPE_OHCA,
                                          "0000009999", out, 1, &out_count));
    TEST_ASSERT_EQUAL_UINT16(0, out_count);
}

/** F3: 存 5 筆 → list 回 newest-first 順序（seq 5,4,3,2,1） */
static void F3_list_returns_newest_first() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    for (int i = 0; i < 5; ++i) {
        save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,
                        1000ULL + i * 1000ULL, 1);
    }
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(5, n);
    TEST_ASSERT_EQUAL_STRING("0000000005", list[0].id);
    TEST_ASSERT_EQUAL_STRING("0000000004", list[1].id);
    TEST_ASSERT_EQUAL_STRING("0000000003", list[2].id);
    TEST_ASSERT_EQUAL_STRING("0000000002", list[3].id);
    TEST_ASSERT_EQUAL_STRING("0000000001", list[4].id);
}

// ============================================================
//  Group G — delete / format / init guards（5 cases，補 review-pr B 漏洞）
// ============================================================

/** G1: delete 存在的 case → bin 檔被刪 + 從 list 移除 + index 持久化 */
static void G1_delete_existing_case() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);  // seq 1
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 2000ULL, 1);  // seq 2

    TEST_ASSERT_TRUE(storage_delete(&g_be, EMS_CASE_TYPE_OHCA, "0000000001"));

    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/ohca/0000000001.bin"));
    TEST_ASSERT_TRUE (g_be.exists(g_be.ctx, "/cases/ohca/0000000002.bin"));

    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000002", list[0].id);

    // 模擬重啟，刪除應持久化（不靠 in-RAM state）
    TEST_ASSERT_TRUE(storage_init(&g_be));
    n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                     list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000002", list[0].id);
}

/** G2: delete 不存在的 id → 回 false，state 不動 */
static void G2_delete_nonexistent_id() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);

    TEST_ASSERT_FALSE(storage_delete(&g_be, EMS_CASE_TYPE_OHCA, "0000009999"));

    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
}

/** G3: delete 後再 save → next_seq 不倒退（單調遞增契約） */
static void G3_delete_then_save_seq_monotonic() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 1000ULL, 1);  // seq 1
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 2000ULL, 1);  // seq 2

    TEST_ASSERT_TRUE(storage_delete(&g_be, EMS_CASE_TYPE_OHCA, "0000000002"));

    // 新存應拿 seq 3，不能 reuse 已刪的 seq 2
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 3000ULL, 1);
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(2, n);
    TEST_ASSERT_EQUAL_STRING("0000000003", list[0].id);
    TEST_ASSERT_EQUAL_STRING("0000000001", list[1].id);
}

/** G4: format → 全部檔案 + index 全清 + state 重置 + 下次新 case 從 seq 1
 *  保護 simplify cleanup 抽出的 purge_dir lambda 不 silent regression */
static void G4_format_clears_all() {
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA,     1000ULL, 1);
    save_dummy_case(&g_be, EMS_CASE_TYPE_TRAINING, 2000ULL, 1);

    TEST_ASSERT_TRUE(storage_format(&g_be));

    // 檔案 + index 全清
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/ohca/0000000001.bin"));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/training/0000000002.bin"));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/index.json"));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/cases/index.json.tmp"));

    // in-RAM state 重置
    case_meta_t list[EMS_STORAGE_OHCA_CAP];
    uint16_t n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                              list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(0, n);
    n = storage_list(&g_be, EMS_CASE_TYPE_TRAINING,
                     list, EMS_STORAGE_TRAINING_CAP);
    TEST_ASSERT_EQUAL_UINT16(0, n);

    // 重新 init 仍乾淨，新 case 從 seq 1 起算
    TEST_ASSERT_TRUE(storage_init(&g_be));
    save_dummy_case(&g_be, EMS_CASE_TYPE_OHCA, 3000ULL, 1);
    n = storage_list(&g_be, EMS_CASE_TYPE_OHCA,
                     list, EMS_STORAGE_OHCA_CAP);
    TEST_ASSERT_EQUAL_UINT16(1, n);
    TEST_ASSERT_EQUAL_STRING("0000000001", list[0].id);
}

/** G5: storage_init null guard — nullptr 與全 0 fn ptr 都應 return false */
static void G5_init_null_backend_fails() {
    TEST_ASSERT_FALSE(storage_init(nullptr));

    IStorageBackend empty;
    memset(&empty, 0, sizeof(empty));
    TEST_ASSERT_FALSE(storage_init(&empty));  // read_file == nullptr
}

// ============================================================
//  main
// ============================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    // Group A — Binary serialization
    RUN_TEST(A1_serialize_empty_events);
    RUN_TEST(A2_serialize_one_event_round_trip);
    RUN_TEST(A3_serialize_50_events_round_trip);
    RUN_TEST(A4_crc_mismatch_detected);
    RUN_TEST(A5_magic_mismatch_detected);
    RUN_TEST(A6_crc_corruption_at_footer);
    RUN_TEST(A7_version_byte_mismatch_rejected);
    RUN_TEST(A8_deserialize_truncated_buffer);

    // Group B — CRC32
    RUN_TEST(B1_crc32_known_vector);
    RUN_TEST(B2_crc32_empty);
    RUN_TEST(B3_crc32_incremental);

    // Group C — Index JSON
    RUN_TEST(C1_index_build_empty);
    RUN_TEST(C2_index_with_one_case);
    RUN_TEST(C3_index_unknown_field_tolerated);
    RUN_TEST(C4_index_corrupt_json_detected);
    RUN_TEST(C5_index_serialize_buffer_overflow);
    RUN_TEST(C6_case_type_mapping_and_unknown_fallback);

    // Group D — FIFO
    RUN_TEST(D1_save_under_cap);
    RUN_TEST(D2_save_exactly_at_cap);
    RUN_TEST(D3_save_overflow_one);
    RUN_TEST(D4_save_overflow_burst);
    RUN_TEST(D5_ohca_training_independent_caps);
    RUN_TEST(D6_seq_monotonic_after_delete);
    RUN_TEST(D7_save_at_global_capacity_evicts_oldest_same_type);
    RUN_TEST(D8_save_at_global_capacity_evicts_oldest_training);

    // Group E — Recovery
    RUN_TEST(E1_missing_index_rebuild_from_files);
    RUN_TEST(E2_orphan_event_file_purged);
    RUN_TEST(E3_orphan_index_entry_dropped);
    RUN_TEST(E4_tmp_file_recovery);
    RUN_TEST(E5_summary_reconstruction_after_persist);
    RUN_TEST(E6_tmp_valid_payload_purged_on_init);
    RUN_TEST(E7_non_bin_files_ignored_during_rebuild);

    // Group F — Load / list
    RUN_TEST(F1_load_events_by_id);
    RUN_TEST(F2_load_nonexistent_id);
    RUN_TEST(F3_list_returns_newest_first);

    // Group G — delete / format / init guards（review-pr B 補洞）
    RUN_TEST(G1_delete_existing_case);
    RUN_TEST(G2_delete_nonexistent_id);
    RUN_TEST(G3_delete_then_save_seq_monotonic);
    RUN_TEST(G4_format_clears_all);
    RUN_TEST(G5_init_null_backend_fails);

    return UNITY_END();
}
