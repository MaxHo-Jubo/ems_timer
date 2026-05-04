// EMS DoseSync Pro — Phase B Unit Test: 案件總覽聚合
//
// 對應規格：
//   - SoT V1 §11（案件總覽：11.2 EPI 詳細 / 11.3 電擊詳細 / 11.4 藥物紀錄）
//   - pm-dev-spec §3 / §6
//   - 測試計劃 §4.4（案件總覽 ohca_case_summary_t）
//
// 涵蓋：
//   - 初始化清零
//   - aggregate 單筆事件：EPI/電擊/Amio 各 type 累加正確欄位
//   - 計數規則：依 event.count 累加（不是筆數）
//   - 時間欄位：本機事件更新 first/last；補登事件不影響
//   - 邊界：無本機 EPI → first/last = 0
//   - 邊界：僅補登 EPI → epi_total > 0、first/last = 0
//   - 電擊不記 first（V1 §11.3）
//   - caseSummary_build：從事件陣列建構
#include <unity.h>
#include "ems_supp_model.h"
#include "ems_case_summary.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  Group 1: 初始化
// ============================================================

static void test_init_clears_all_fields() {
    ohca_case_summary_t s;
    // 髒值
    s.epi_local            = 99;
    s.epi_total            = 99;
    s.shock_local          = 99;
    s.amio_total           = 99;
    s.first_epi_local_ms   = 12345;
    s.last_epi_local_ms    = 67890;
    s.last_shock_local_ms  = 11111;
    s.last_amio_ms         = 22222;
    s.case_start_ms        = 1;
    s.case_end_ms          = 2;

    caseSummary_init(&s);

    TEST_ASSERT_EQUAL_UINT16(0, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_total);
    TEST_ASSERT_EQUAL_UINT16(0, s.shock_local);
    TEST_ASSERT_EQUAL_UINT16(0, s.shock_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(0, s.shock_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(0, s.shock_total);
    TEST_ASSERT_EQUAL_UINT16(0, s.amio_total);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_shock_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_amio_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.case_start_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.case_end_ms);
}

// ============================================================
//  Group 2: aggregate 本機 EPI（更新 first/last 時間欄位）
// ============================================================

static void test_aggregate_local_epi_increments_count_and_sets_times() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, /*ts*/ 1000ULL, /*elapsed*/ 240000);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(1, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(1, s.epi_total);
    TEST_ASSERT_EQUAL_UINT64(1000ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(1000ULL, s.last_epi_local_ms);
}

static void test_aggregate_multiple_local_epi_first_stays_last_updates() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 1000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 2, EVT_EPI_LOCAL, 240000ULL, 240000);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 3, EVT_EPI_LOCAL, 480000ULL, 480000);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(3, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(3, s.epi_total);
    TEST_ASSERT_EQUAL_UINT64(1000ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(480000ULL, s.last_epi_local_ms);
}

// ============================================================
//  Group 3: aggregate 補登 EPI（依 count 累加，不影響 first/last 本機時間）
// ============================================================

static void test_aggregate_pre_handover_epi_x3_adds_3_to_count_no_time() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, /*count*/ 3,
                   /*recorded_at*/ 5000ULL, /*elapsed*/ 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(0, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(3, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(3, s.epi_total);
    // 補登不影響本機 EPI 時間
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_epi_local_ms);
}

static void test_aggregate_pure_supp_epi_x2() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PURE, 2, 5000ULL, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(2, s.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(2, s.epi_total);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.first_epi_local_ms);
}

// ============================================================
//  Group 4: aggregate 電擊（V1 §11.3：不記 first，僅 last）
// ============================================================

static void test_aggregate_local_shock_only_updates_last_not_first() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_SHOCK_LOCAL, 7000ULL, 7000);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(1, s.shock_local);
    TEST_ASSERT_EQUAL_UINT16(1, s.shock_total);
    TEST_ASSERT_EQUAL_UINT64(7000ULL, s.last_shock_local_ms);
    // 結構中無 first_shock_local_ms 欄位（V1 §11.3 規定）
}

static void test_aggregate_multiple_shock_last_updates() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_SHOCK_LOCAL, 1000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 2, EVT_SHOCK_LOCAL, 5000ULL, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(2, s.shock_local);
    TEST_ASSERT_EQUAL_UINT64(5000ULL, s.last_shock_local_ms);
}

static void test_aggregate_supp_shock_pre_handover_x2() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_SHOCK_PRE_HANDOVER, 2, 0, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(2, s.shock_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(2, s.shock_total);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_shock_local_ms);  // 補登不影響
}

static void test_aggregate_supp_shock_pure_x1() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_SHOCK_PURE, 1, 0, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(1, s.shock_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(1, s.shock_total);
}

// ============================================================
//  Group 5: aggregate Amiodarone（每筆 count=1，多筆寫多筆）
// ============================================================

static void test_aggregate_single_amio() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_AMIODARONE, 9000ULL, 9000);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(1, s.amio_total);
    TEST_ASSERT_EQUAL_UINT64(9000ULL, s.last_amio_ms);
}

static void test_aggregate_multiple_amio_last_updates() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_AMIODARONE, 1000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 2, EVT_AMIODARONE, 2000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 3, EVT_AMIODARONE, 3000ULL, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(3, s.amio_total);
    TEST_ASSERT_EQUAL_UINT64(3000ULL, s.last_amio_ms);
}

// ============================================================
//  Group 6: 邊界 — 無本機 EPI / 僅補登 EPI（Test Plan §4.4.2）
// ============================================================

/** 無任何 EPI → first/last 都是 0 */
static void test_no_epi_first_last_are_zero() {
    ohca_case_summary_t s;
    caseSummary_init(&s);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_total);
}

/** 僅補登 EPI（無本機）→ epi_total > 0 但 first/last_local = 0 */
static void test_only_supp_epi_no_local_first_last_zero() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, 5, 1000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildSuppEvent(&e, 2, SUPP_TYPE_EPI_PURE, 3, 2000ULL, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(8, s.epi_total);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_local);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_epi_local_ms);
}

/** 補登後再加本機 → first/last 從本機事件起算 */
static void test_supp_then_local_first_starts_from_local() {
    ohca_case_summary_t s;
    caseSummary_init(&s);

    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, 2, 1000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 2, EVT_EPI_LOCAL, 5000ULL, 0);
    caseSummary_aggregate(&s, &e);
    buildLocalEvent(&e, 3, EVT_EPI_LOCAL, 245000ULL, 0);
    caseSummary_aggregate(&s, &e);

    TEST_ASSERT_EQUAL_UINT16(2, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(2, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(4, s.epi_total);
    TEST_ASSERT_EQUAL_UINT64(5000ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(245000ULL, s.last_epi_local_ms);
}

// ============================================================
//  Group 7: caseSummary_build — 從事件陣列一次建構
// ============================================================

static void test_build_empty_array_all_zero() {
    ohca_case_summary_t s;
    caseSummary_build(&s, nullptr, 0, /*case_start*/ 1000ULL, /*case_end*/ 2000ULL);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_total);
    TEST_ASSERT_EQUAL_UINT16(0, s.shock_total);
    TEST_ASSERT_EQUAL_UINT16(0, s.amio_total);
    TEST_ASSERT_EQUAL_UINT64(1000ULL, s.case_start_ms);
    TEST_ASSERT_EQUAL_UINT64(2000ULL, s.case_end_ms);
}

/**
 * 模擬 Test Plan B.S5 的場景：
 *   - 1 次本機 EPI
 *   - 3 次接手前 EPI（一筆 count=3）
 *   - 1 次純補登電擊（一筆 count=1）
 *   - 1 次 Amio
 * 預期：epi_total=4, shock_total=1, amio_total=1, first_epi_local=步驟 1 EPI 時間
 */
static void test_build_phase_b_s5_scenario() {
    ems_event_t evs[4];
    buildLocalEvent(&evs[0], 1, EVT_EPI_LOCAL,    /*ts*/ 100000ULL, 0);
    buildSuppEvent (&evs[1], 2, SUPP_TYPE_EPI_PRE_HANDOVER, 3, 50000ULL, 0);
    buildSuppEvent (&evs[2], 3, SUPP_TYPE_SHOCK_PURE, 1, 60000ULL, 0);
    buildLocalEvent(&evs[3], 4, EVT_AMIODARONE, 200000ULL, 0);

    ohca_case_summary_t s;
    caseSummary_build(&s, evs, 4, /*start*/ 0, /*end*/ 300000ULL);

    TEST_ASSERT_EQUAL_UINT16(1, s.epi_local);
    TEST_ASSERT_EQUAL_UINT16(3, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(0, s.epi_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(4, s.epi_total);

    TEST_ASSERT_EQUAL_UINT16(0, s.shock_local);
    TEST_ASSERT_EQUAL_UINT16(1, s.shock_pure_supp);
    TEST_ASSERT_EQUAL_UINT16(1, s.shock_total);

    TEST_ASSERT_EQUAL_UINT16(1, s.amio_total);

    TEST_ASSERT_EQUAL_UINT64(100000ULL, s.first_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(100000ULL, s.last_epi_local_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.last_shock_local_ms);  // 無本機電擊
    TEST_ASSERT_EQUAL_UINT64(200000ULL, s.last_amio_ms);
    TEST_ASSERT_EQUAL_UINT64(0ULL, s.case_start_ms);
    TEST_ASSERT_EQUAL_UINT64(300000ULL, s.case_end_ms);
}

/** build 即使 out 帶髒值也會先 init */
static void test_build_initializes_out_first() {
    ohca_case_summary_t s;
    s.epi_local = 99;  // 髒值
    s.amio_total = 99;

    ems_event_t e;
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 1000ULL, 0);
    caseSummary_build(&s, &e, 1, 0ULL, 0ULL);

    TEST_ASSERT_EQUAL_UINT16(1, s.epi_local);  // 不應為 100
    TEST_ASSERT_EQUAL_UINT16(0, s.amio_total); // 應被 init 清零
}

// ============================================================
//  Group 8: 計數規則 — count=N 視為 N 次（不是筆數）
// ============================================================

/** 一筆 count=5 應計 5 次 */
static void test_single_event_count_5_aggregates_as_5() {
    ohca_case_summary_t s;
    caseSummary_init(&s);
    ems_event_t e;
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, 5, 0, 0);
    caseSummary_aggregate(&s, &e);
    TEST_ASSERT_EQUAL_UINT16(5, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(5, s.epi_total);
}

/** 五筆 count=1 也應計 5 次（與一筆 count=5 等效） */
static void test_five_events_count_1_aggregates_as_5() {
    ohca_case_summary_t s;
    caseSummary_init(&s);
    for (int i = 0; i < 5; i++) {
        ems_event_t e;
        buildSuppEvent(&e, i + 1, SUPP_TYPE_EPI_PRE_HANDOVER, 1, i * 1000ULL, 0);
        caseSummary_aggregate(&s, &e);
    }
    TEST_ASSERT_EQUAL_UINT16(5, s.epi_pre_handover);
    TEST_ASSERT_EQUAL_UINT16(5, s.epi_total);
}

// ============================================================
//  Group 9: Timeline 排序（Test Plan §4.4.3 + V1 §11.5）
// ============================================================

/** 空陣列：不寫任何索引（測試只驗 n=0 不爆炸） */
static void test_timeline_empty_array_no_op() {
    uint16_t idx[1] = { 99 };
    caseSummary_buildTimeline(idx, nullptr, 0);
    TEST_ASSERT_EQUAL_UINT16(99, idx[0]);  // 未被修改
}

/** 單一事件：索引 0 直接對應 */
static void test_timeline_single_event() {
    ems_event_t e[1];
    buildLocalEvent(&e[0], 1, EVT_EPI_LOCAL, 1000ULL, 0);
    uint16_t idx[1];
    caseSummary_buildTimeline(idx, e, 1);
    TEST_ASSERT_EQUAL_UINT16(0, idx[0]);
}

/** 多個本機事件依 timestamp_ms 由小到大 */
static void test_timeline_local_events_sorted_by_timestamp() {
    ems_event_t e[3];
    buildLocalEvent(&e[0], 1, EVT_EPI_LOCAL,    /*ts*/ 5000ULL, 0);
    buildLocalEvent(&e[1], 2, EVT_SHOCK_LOCAL,  /*ts*/ 1000ULL, 0);
    buildLocalEvent(&e[2], 3, EVT_AMIODARONE,   /*ts*/ 3000ULL, 0);
    uint16_t idx[3];
    caseSummary_buildTimeline(idx, e, 3);
    TEST_ASSERT_EQUAL_UINT16(1, idx[0]);  // ts=1000
    TEST_ASSERT_EQUAL_UINT16(2, idx[1]);  // ts=3000
    TEST_ASSERT_EQUAL_UINT16(0, idx[2]);  // ts=5000
}

/** 補登事件依 recorded_at（亦存於 timestamp_ms）排序 */
static void test_timeline_supp_events_sorted_by_recorded_at() {
    ems_event_t e[2];
    buildSuppEvent(&e[0], 1, SUPP_TYPE_EPI_PRE_HANDOVER, 3, /*recorded_at*/ 8000ULL, 0);
    buildSuppEvent(&e[1], 2, SUPP_TYPE_SHOCK_PURE,        1, /*recorded_at*/ 2000ULL, 0);
    uint16_t idx[2];
    caseSummary_buildTimeline(idx, e, 2);
    TEST_ASSERT_EQUAL_UINT16(1, idx[0]);  // 2000
    TEST_ASSERT_EQUAL_UINT16(0, idx[1]);  // 8000
}

/** 本機與補登混合排序（皆依 timestamp_ms） */
static void test_timeline_mixed_local_and_supp_sorted_by_ts() {
    ems_event_t e[4];
    buildLocalEvent(&e[0], 1, EVT_EPI_LOCAL,                    10000ULL, 0);
    buildSuppEvent (&e[1], 2, SUPP_TYPE_EPI_PRE_HANDOVER,    2,  3000ULL, 0);
    buildLocalEvent(&e[2], 3, EVT_SHOCK_LOCAL,                  7000ULL, 0);
    buildSuppEvent (&e[3], 4, SUPP_TYPE_SHOCK_PURE,          1,  5000ULL, 0);
    uint16_t idx[4];
    caseSummary_buildTimeline(idx, e, 4);
    TEST_ASSERT_EQUAL_UINT16(1, idx[0]);  // 3000 supp
    TEST_ASSERT_EQUAL_UINT16(3, idx[1]);  // 5000 supp
    TEST_ASSERT_EQUAL_UINT16(2, idx[2]);  // 7000 local
    TEST_ASSERT_EQUAL_UINT16(0, idx[3]);  // 10000 local
}

/** 同 timestamp_ms 依 event_id 流水號排序（tie-break） */
static void test_timeline_same_timestamp_ordered_by_event_id() {
    ems_event_t e[3];
    buildLocalEvent(&e[0], /*event_id*/ 7, EVT_EPI_LOCAL,    1000ULL, 0);
    buildLocalEvent(&e[1], /*event_id*/ 3, EVT_SHOCK_LOCAL,  1000ULL, 0);
    buildLocalEvent(&e[2], /*event_id*/ 5, EVT_AMIODARONE,   1000ULL, 0);
    uint16_t idx[3];
    caseSummary_buildTimeline(idx, e, 3);
    // 同 ts 依 event_id：3, 5, 7
    TEST_ASSERT_EQUAL_UINT16(1, idx[0]);  // event_id 3
    TEST_ASSERT_EQUAL_UINT16(2, idx[1]);  // event_id 5
    TEST_ASSERT_EQUAL_UINT16(0, idx[2]);  // event_id 7
}

/** 排序穩定性：同 ts + 同 event_id 不應發生（event_id 是流水號），但若發生不破壞 */
static void test_timeline_already_sorted_no_op() {
    ems_event_t e[3];
    buildLocalEvent(&e[0], 1, EVT_EPI_LOCAL,    1000ULL, 0);
    buildLocalEvent(&e[1], 2, EVT_SHOCK_LOCAL,  2000ULL, 0);
    buildLocalEvent(&e[2], 3, EVT_AMIODARONE,   3000ULL, 0);
    uint16_t idx[3];
    caseSummary_buildTimeline(idx, e, 3);
    TEST_ASSERT_EQUAL_UINT16(0, idx[0]);
    TEST_ASSERT_EQUAL_UINT16(1, idx[1]);
    TEST_ASSERT_EQUAL_UINT16(2, idx[2]);
}

// ============================================================
//  main
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // Group 1: init
    RUN_TEST(test_init_clears_all_fields);

    // Group 2: local EPI aggregate
    RUN_TEST(test_aggregate_local_epi_increments_count_and_sets_times);
    RUN_TEST(test_aggregate_multiple_local_epi_first_stays_last_updates);

    // Group 3: supp EPI aggregate
    RUN_TEST(test_aggregate_pre_handover_epi_x3_adds_3_to_count_no_time);
    RUN_TEST(test_aggregate_pure_supp_epi_x2);

    // Group 4: shock aggregate
    RUN_TEST(test_aggregate_local_shock_only_updates_last_not_first);
    RUN_TEST(test_aggregate_multiple_shock_last_updates);
    RUN_TEST(test_aggregate_supp_shock_pre_handover_x2);
    RUN_TEST(test_aggregate_supp_shock_pure_x1);

    // Group 5: Amio aggregate
    RUN_TEST(test_aggregate_single_amio);
    RUN_TEST(test_aggregate_multiple_amio_last_updates);

    // Group 6: 邊界
    RUN_TEST(test_no_epi_first_last_are_zero);
    RUN_TEST(test_only_supp_epi_no_local_first_last_zero);
    RUN_TEST(test_supp_then_local_first_starts_from_local);

    // Group 7: build
    RUN_TEST(test_build_empty_array_all_zero);
    RUN_TEST(test_build_phase_b_s5_scenario);
    RUN_TEST(test_build_initializes_out_first);

    // Group 8: count rule
    RUN_TEST(test_single_event_count_5_aggregates_as_5);
    RUN_TEST(test_five_events_count_1_aggregates_as_5);

    // Group 9: Timeline sort
    RUN_TEST(test_timeline_empty_array_no_op);
    RUN_TEST(test_timeline_single_event);
    RUN_TEST(test_timeline_local_events_sorted_by_timestamp);
    RUN_TEST(test_timeline_supp_events_sorted_by_recorded_at);
    RUN_TEST(test_timeline_mixed_local_and_supp_sorted_by_ts);
    RUN_TEST(test_timeline_same_timestamp_ordered_by_event_id);
    RUN_TEST(test_timeline_already_sorted_no_op);

    return UNITY_END();
}
