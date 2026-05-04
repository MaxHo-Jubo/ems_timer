// EMS DoseSync Pro — Phase B Unit Test: 補登資料模型
//
// 對應規格：
//   - SoT V1 §9（補登邏輯）/ §11（案件總覽，event_t 來源）
//   - pm-dev-spec §6（event_t 編號段位 + struct 規格）
//   - 測試計劃 §4.1（補登次數選擇）/ §4.2（補登事件規則）
//
// 涵蓋：
//   - 事件型別 enum 編號（封版 0x01~0x07，不可重排）
//   - 補登次數驗證：接手前 1~5、純補登 1~3，邊界 0/6/4 拒絕
//   - supp_type → event_type 映射
//   - buildSuppEvent：actual_time_null=true、count 設定正確
//   - buildLocalEvent：actual_time_null=false、count=1
//   - 事件分類 helper：isBackfill / isLocal / isEpi / isShock / isAmio
#include <unity.h>
#include "ems_supp_model.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  Group 1: 事件型別 enum 編號封版（pm-dev-spec §6）
// ============================================================

static void test_event_type_enum_values_locked() {
    // V1 核心段位 0x01~0x07 不可重排
    TEST_ASSERT_EQUAL_UINT8(0x01, EVT_EPI_LOCAL);
    TEST_ASSERT_EQUAL_UINT8(0x02, EVT_SHOCK_LOCAL);
    TEST_ASSERT_EQUAL_UINT8(0x03, EVT_AMIODARONE);
    TEST_ASSERT_EQUAL_UINT8(0x04, EVT_EPI_PRE_HANDOVER);
    TEST_ASSERT_EQUAL_UINT8(0x05, EVT_EPI_PURE_SUPP);
    TEST_ASSERT_EQUAL_UINT8(0x06, EVT_SHOCK_PRE_HANDOVER);
    TEST_ASSERT_EQUAL_UINT8(0x07, EVT_SHOCK_PURE_SUPP);
}

// ============================================================
//  Group 2: 補登次數上限（V1 §9.6 / Test Plan §4.1）
// ============================================================

static void test_supp_count_max_pre_handover_is_5() {
    TEST_ASSERT_EQUAL_UINT8(5, suppCountMax(SUPP_TYPE_EPI_PRE_HANDOVER));
    TEST_ASSERT_EQUAL_UINT8(5, suppCountMax(SUPP_TYPE_SHOCK_PRE_HANDOVER));
}

static void test_supp_count_max_pure_supp_is_3() {
    TEST_ASSERT_EQUAL_UINT8(3, suppCountMax(SUPP_TYPE_EPI_PURE));
    TEST_ASSERT_EQUAL_UINT8(3, suppCountMax(SUPP_TYPE_SHOCK_PURE));
}

// ============================================================
//  Group 3: isSuppCountValid 邊界（Test Plan §4.1）
// ============================================================

// 接手前 EPI / 接手前電擊：1~5
static void test_pre_handover_valid_at_min_1() {
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_EPI_PRE_HANDOVER, 1));
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_SHOCK_PRE_HANDOVER, 1));
}

static void test_pre_handover_valid_at_max_5() {
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_EPI_PRE_HANDOVER, 5));
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_SHOCK_PRE_HANDOVER, 5));
}

static void test_pre_handover_zero_rejected() {
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_EPI_PRE_HANDOVER, 0));
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_SHOCK_PRE_HANDOVER, 0));
}

static void test_pre_handover_six_rejected() {
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_EPI_PRE_HANDOVER, 6));
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_SHOCK_PRE_HANDOVER, 6));
}

// 純補登 EPI / 純補登電擊：1~3
static void test_pure_supp_valid_at_min_1() {
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_EPI_PURE, 1));
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_SHOCK_PURE, 1));
}

static void test_pure_supp_valid_at_max_3() {
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_EPI_PURE, 3));
    TEST_ASSERT_TRUE(isSuppCountValid(SUPP_TYPE_SHOCK_PURE, 3));
}

static void test_pure_supp_zero_rejected() {
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_EPI_PURE, 0));
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_SHOCK_PURE, 0));
}

static void test_pure_supp_four_rejected() {
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_EPI_PURE, 4));
    TEST_ASSERT_FALSE(isSuppCountValid(SUPP_TYPE_SHOCK_PURE, 4));
}

// ============================================================
//  Group 4: supp_type → event_type 映射
// ============================================================

static void test_supp_type_to_event_type_mapping() {
    TEST_ASSERT_EQUAL_INT(EVT_EPI_PRE_HANDOVER,
                          suppTypeToEventType(SUPP_TYPE_EPI_PRE_HANDOVER));
    TEST_ASSERT_EQUAL_INT(EVT_EPI_PURE_SUPP,
                          suppTypeToEventType(SUPP_TYPE_EPI_PURE));
    TEST_ASSERT_EQUAL_INT(EVT_SHOCK_PRE_HANDOVER,
                          suppTypeToEventType(SUPP_TYPE_SHOCK_PRE_HANDOVER));
    TEST_ASSERT_EQUAL_INT(EVT_SHOCK_PURE_SUPP,
                          suppTypeToEventType(SUPP_TYPE_SHOCK_PURE));
}

// ============================================================
//  Group 5: buildSuppEvent — actual_time_null=true（Test Plan §4.2.1）
// ============================================================

static void test_build_supp_event_pre_handover_epi_x3() {
    ems_event_t e = {};
    buildSuppEvent(&e, /*event_id*/ 7, SUPP_TYPE_EPI_PRE_HANDOVER,
                   /*count*/ 3, /*recorded_at*/ 1234567890ULL, /*elapsed*/ 60000);
    TEST_ASSERT_EQUAL_UINT32(7, e.event_id);
    TEST_ASSERT_EQUAL_INT(EVT_EPI_PRE_HANDOVER, e.type);
    TEST_ASSERT_EQUAL_UINT64(1234567890ULL, e.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(60000, e.elapsed_ms);
    TEST_ASSERT_EQUAL_UINT8(3, e.count);
    TEST_ASSERT_TRUE(e.actual_time_null);
}

static void test_build_supp_event_pure_shock_x2() {
    ems_event_t e = {};
    buildSuppEvent(&e, 11, SUPP_TYPE_SHOCK_PURE, 2, 100ULL, 0);
    TEST_ASSERT_EQUAL_INT(EVT_SHOCK_PURE_SUPP, e.type);
    TEST_ASSERT_EQUAL_UINT8(2, e.count);
    TEST_ASSERT_TRUE(e.actual_time_null);
}

static void test_build_supp_event_count_5_pre_handover() {
    ems_event_t e = {};
    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, 5, 0ULL, 0);
    TEST_ASSERT_EQUAL_UINT8(5, e.count);
    TEST_ASSERT_TRUE(e.actual_time_null);
}

// ============================================================
//  Group 6: buildLocalEvent — actual_time_null=false, count=1
// ============================================================

static void test_build_local_epi_event_count_1_no_null() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 5000ULL, 240000);
    TEST_ASSERT_EQUAL_INT(EVT_EPI_LOCAL, e.type);
    TEST_ASSERT_EQUAL_UINT64(5000ULL, e.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(240000, e.elapsed_ms);
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_FALSE(e.actual_time_null);
}

static void test_build_local_shock_event() {
    ems_event_t e = {};
    buildLocalEvent(&e, 2, EVT_SHOCK_LOCAL, 6000ULL, 60000);
    TEST_ASSERT_EQUAL_INT(EVT_SHOCK_LOCAL, e.type);
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_FALSE(e.actual_time_null);
}

static void test_build_local_amio_event() {
    ems_event_t e = {};
    buildLocalEvent(&e, 3, EVT_AMIODARONE, 7000ULL, 90000);
    TEST_ASSERT_EQUAL_INT(EVT_AMIODARONE, e.type);
    TEST_ASSERT_EQUAL_UINT8(1, e.count);
    TEST_ASSERT_FALSE(e.actual_time_null);
}

// ============================================================
//  Group 7: 事件分類 helper
// ============================================================

static void test_is_backfill_event_true_for_supp_types() {
    ems_event_t e = {};

    buildSuppEvent(&e, 1, SUPP_TYPE_EPI_PRE_HANDOVER, 1, 0, 0);
    TEST_ASSERT_TRUE(isBackfillEvent(&e));

    buildSuppEvent(&e, 2, SUPP_TYPE_EPI_PURE, 1, 0, 0);
    TEST_ASSERT_TRUE(isBackfillEvent(&e));

    buildSuppEvent(&e, 3, SUPP_TYPE_SHOCK_PRE_HANDOVER, 1, 0, 0);
    TEST_ASSERT_TRUE(isBackfillEvent(&e));

    buildSuppEvent(&e, 4, SUPP_TYPE_SHOCK_PURE, 1, 0, 0);
    TEST_ASSERT_TRUE(isBackfillEvent(&e));
}

static void test_is_backfill_event_false_for_local_types() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 0, 0);
    TEST_ASSERT_FALSE(isBackfillEvent(&e));

    buildLocalEvent(&e, 2, EVT_SHOCK_LOCAL, 0, 0);
    TEST_ASSERT_FALSE(isBackfillEvent(&e));

    buildLocalEvent(&e, 3, EVT_AMIODARONE, 0, 0);
    TEST_ASSERT_FALSE(isBackfillEvent(&e));
}

static void test_is_local_event_complement_of_backfill() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 0, 0);
    TEST_ASSERT_TRUE(isLocalEvent(&e));
    TEST_ASSERT_FALSE(isBackfillEvent(&e));

    buildSuppEvent(&e, 2, SUPP_TYPE_EPI_PURE, 1, 0, 0);
    TEST_ASSERT_FALSE(isLocalEvent(&e));
    TEST_ASSERT_TRUE(isBackfillEvent(&e));
}

static void test_is_epi_event_covers_all_three_types() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 0, 0);
    TEST_ASSERT_TRUE(isEpiEvent(&e));

    buildSuppEvent(&e, 2, SUPP_TYPE_EPI_PRE_HANDOVER, 1, 0, 0);
    TEST_ASSERT_TRUE(isEpiEvent(&e));

    buildSuppEvent(&e, 3, SUPP_TYPE_EPI_PURE, 1, 0, 0);
    TEST_ASSERT_TRUE(isEpiEvent(&e));
}

static void test_is_epi_event_false_for_shock_amio() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_SHOCK_LOCAL, 0, 0);
    TEST_ASSERT_FALSE(isEpiEvent(&e));

    buildLocalEvent(&e, 2, EVT_AMIODARONE, 0, 0);
    TEST_ASSERT_FALSE(isEpiEvent(&e));

    buildSuppEvent(&e, 3, SUPP_TYPE_SHOCK_PURE, 1, 0, 0);
    TEST_ASSERT_FALSE(isEpiEvent(&e));
}

static void test_is_shock_event_covers_all_three_types() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_SHOCK_LOCAL, 0, 0);
    TEST_ASSERT_TRUE(isShockEvent(&e));

    buildSuppEvent(&e, 2, SUPP_TYPE_SHOCK_PRE_HANDOVER, 1, 0, 0);
    TEST_ASSERT_TRUE(isShockEvent(&e));

    buildSuppEvent(&e, 3, SUPP_TYPE_SHOCK_PURE, 1, 0, 0);
    TEST_ASSERT_TRUE(isShockEvent(&e));
}

static void test_is_shock_event_false_for_epi_amio() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_EPI_LOCAL, 0, 0);
    TEST_ASSERT_FALSE(isShockEvent(&e));

    buildLocalEvent(&e, 2, EVT_AMIODARONE, 0, 0);
    TEST_ASSERT_FALSE(isShockEvent(&e));
}

static void test_is_amio_event_only_for_amiodarone() {
    ems_event_t e = {};
    buildLocalEvent(&e, 1, EVT_AMIODARONE, 0, 0);
    TEST_ASSERT_TRUE(isAmioEvent(&e));

    buildLocalEvent(&e, 2, EVT_EPI_LOCAL, 0, 0);
    TEST_ASSERT_FALSE(isAmioEvent(&e));

    buildSuppEvent(&e, 3, SUPP_TYPE_EPI_PRE_HANDOVER, 1, 0, 0);
    TEST_ASSERT_FALSE(isAmioEvent(&e));
}

// ============================================================
//  main
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();

    // Group 1: enum lock
    RUN_TEST(test_event_type_enum_values_locked);

    // Group 2: count max
    RUN_TEST(test_supp_count_max_pre_handover_is_5);
    RUN_TEST(test_supp_count_max_pure_supp_is_3);

    // Group 3: count valid boundaries
    RUN_TEST(test_pre_handover_valid_at_min_1);
    RUN_TEST(test_pre_handover_valid_at_max_5);
    RUN_TEST(test_pre_handover_zero_rejected);
    RUN_TEST(test_pre_handover_six_rejected);
    RUN_TEST(test_pure_supp_valid_at_min_1);
    RUN_TEST(test_pure_supp_valid_at_max_3);
    RUN_TEST(test_pure_supp_zero_rejected);
    RUN_TEST(test_pure_supp_four_rejected);

    // Group 4: supp_type mapping
    RUN_TEST(test_supp_type_to_event_type_mapping);

    // Group 5: buildSuppEvent
    RUN_TEST(test_build_supp_event_pre_handover_epi_x3);
    RUN_TEST(test_build_supp_event_pure_shock_x2);
    RUN_TEST(test_build_supp_event_count_5_pre_handover);

    // Group 6: buildLocalEvent
    RUN_TEST(test_build_local_epi_event_count_1_no_null);
    RUN_TEST(test_build_local_shock_event);
    RUN_TEST(test_build_local_amio_event);

    // Group 7: classification helpers
    RUN_TEST(test_is_backfill_event_true_for_supp_types);
    RUN_TEST(test_is_backfill_event_false_for_local_types);
    RUN_TEST(test_is_local_event_complement_of_backfill);
    RUN_TEST(test_is_epi_event_covers_all_three_types);
    RUN_TEST(test_is_epi_event_false_for_shock_amio);
    RUN_TEST(test_is_shock_event_covers_all_three_types);
    RUN_TEST(test_is_shock_event_false_for_epi_amio);
    RUN_TEST(test_is_amio_event_only_for_amiodarone);

    return UNITY_END();
}
