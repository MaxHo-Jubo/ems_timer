// EMS DoseSync Pro — Impl-Phase F: BLE chunker unit test
//
// 對應 lib：firmware/lib/ble_chunker/
// 對應計畫：docs/phase-f-web-validation-plan.md §7.2（chunk 切分邏輯）+ §12 風險（MTU 預設 23B → 247B）
// 對應 todo：tasks/phase-f-todo.md F-2
// 執行：pio test -e native -f test_ble_chunker
//
// 涵蓋：
//   - chunk_count() — 給 payload_len + chunk_size 算出總 chunks 數
//   - chunk_at() — 給 chunk index 取出該 chunk 的 (offset, len) range
//   - MTU 邊界：chunk_size = ATT_MTU - 3 (ATT header)
//   - empty payload / 剛好一個 chunk / 整除 / 不整除 / 巨大 payload
#include <unity.h>
#include "ble_chunker.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  chunk_size_from_mtu
// ============================================================

/** ATT_MTU 23 → chunk_size 20（- 3 ATT header） */
static void test_chunk_size_default_mtu() {
    TEST_ASSERT_EQUAL_size_t(20, chunk_size_from_mtu(23));
}

/** ATT_MTU 247 → chunk_size 244 */
static void test_chunk_size_high_mtu() {
    TEST_ASSERT_EQUAL_size_t(244, chunk_size_from_mtu(247));
}

/** ATT_MTU < 4 → 0（防呆，回 0 表示不可用） */
static void test_chunk_size_invalid_mtu() {
    TEST_ASSERT_EQUAL_size_t(0, chunk_size_from_mtu(3));
    TEST_ASSERT_EQUAL_size_t(0, chunk_size_from_mtu(0));
}

/** ATT_MTU == ATT_MIN_USABLE_MTU 邊界（off-by-one 防護） */
static void test_chunk_size_min_usable_mtu_boundary() {
    TEST_ASSERT_EQUAL_size_t(1, chunk_size_from_mtu(ATT_MIN_USABLE_MTU));  // 4 - 3 = 1
}

// ============================================================
//  chunk_count
// ============================================================

/** payload 0 byte → 0 chunks（沒東西可送） */
static void test_chunk_count_empty_payload() {
    TEST_ASSERT_EQUAL_size_t(0, chunk_count(0, 20));
}

/** payload 1 byte → 1 chunk */
static void test_chunk_count_single_byte() {
    TEST_ASSERT_EQUAL_size_t(1, chunk_count(1, 20));
}

/** payload 整除 chunk_size → 剛好 N chunks */
static void test_chunk_count_exact_multiple() {
    TEST_ASSERT_EQUAL_size_t(5, chunk_count(100, 20));   // 5 × 20
    TEST_ASSERT_EQUAL_size_t(1, chunk_count(20, 20));    // 1 × 20
}

/** payload 不整除 → ceil 多一個 chunk */
static void test_chunk_count_remainder() {
    TEST_ASSERT_EQUAL_size_t(6, chunk_count(101, 20));   // 5 × 20 + 1
    TEST_ASSERT_EQUAL_size_t(2, chunk_count(21, 20));    // 1 × 20 + 1
}

/** chunk_size = 0 → 0（防呆，避免除以零） */
static void test_chunk_count_zero_chunk_size() {
    TEST_ASSERT_EQUAL_size_t(0, chunk_count(100, 0));
}

/** 巨大 payload（5KB JSON）+ MTU 247 chunk 244 → 21 chunks */
static void test_chunk_count_large_payload() {
    TEST_ASSERT_EQUAL_size_t(21, chunk_count(5000, 244));  // ceil(5000/244) = 21
}

// ============================================================
//  chunk_at — 取出第 i 個 chunk 的 (offset, len)
// ============================================================

/** chunk_at(0) → (0, chunk_size) */
static void test_chunk_at_first() {
    chunk_range_t r = chunk_at(101, 20, 0);
    TEST_ASSERT_EQUAL_size_t(0, r.offset);
    TEST_ASSERT_EQUAL_size_t(20, r.len);
}

/** chunk_at(中間) → (i * chunk_size, chunk_size) */
static void test_chunk_at_middle() {
    chunk_range_t r = chunk_at(101, 20, 3);
    TEST_ASSERT_EQUAL_size_t(60, r.offset);
    TEST_ASSERT_EQUAL_size_t(20, r.len);
}

/** 不整除最後一個 chunk → len = remainder */
static void test_chunk_at_last_remainder() {
    chunk_range_t r = chunk_at(101, 20, 5);  // chunks 0..5，最後 1 byte
    TEST_ASSERT_EQUAL_size_t(100, r.offset);
    TEST_ASSERT_EQUAL_size_t(1, r.len);
}

/** 整除最後一個 chunk → len = chunk_size */
static void test_chunk_at_last_exact() {
    chunk_range_t r = chunk_at(100, 20, 4);
    TEST_ASSERT_EQUAL_size_t(80, r.offset);
    TEST_ASSERT_EQUAL_size_t(20, r.len);
}

/** chunk_at index 超過 count → len = 0（防呆） */
static void test_chunk_at_out_of_range() {
    chunk_range_t r = chunk_at(101, 20, 6);  // 只有 6 chunks (idx 0..5)
    TEST_ASSERT_EQUAL_size_t(0, r.len);
}

/** chunk_at index = 0 + payload 0 → len = 0（empty payload 防呆） */
static void test_chunk_at_empty_payload() {
    chunk_range_t r = chunk_at(0, 20, 0);
    TEST_ASSERT_EQUAL_size_t(0, r.len);
}

// ============================================================
//  整合：迭代所有 chunks 重組 payload 不漏不重
// ============================================================

/** 迭代所有 chunks 累計 offset+len 必須覆蓋完整 payload */
static void test_iterate_covers_full_payload() {
    constexpr size_t payload_len = 1234;
    constexpr size_t chunk_sz = 244;
    size_t total = chunk_count(payload_len, chunk_sz);
    size_t covered = 0;
    for (size_t i = 0; i < total; ++i) {
        chunk_range_t r = chunk_at(payload_len, chunk_sz, i);
        TEST_ASSERT_EQUAL_size_t(covered, r.offset);  // offset 連續無洞
        TEST_ASSERT_GREATER_THAN(0, r.len);            // 每片都有東西
        covered += r.len;
    }
    TEST_ASSERT_EQUAL_size_t(payload_len, covered);
}

// ============================================================
//  Test runner
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_chunk_size_default_mtu);
    RUN_TEST(test_chunk_size_high_mtu);
    RUN_TEST(test_chunk_size_invalid_mtu);
    RUN_TEST(test_chunk_size_min_usable_mtu_boundary);
    RUN_TEST(test_chunk_count_empty_payload);
    RUN_TEST(test_chunk_count_single_byte);
    RUN_TEST(test_chunk_count_exact_multiple);
    RUN_TEST(test_chunk_count_remainder);
    RUN_TEST(test_chunk_count_zero_chunk_size);
    RUN_TEST(test_chunk_count_large_payload);
    RUN_TEST(test_chunk_at_first);
    RUN_TEST(test_chunk_at_middle);
    RUN_TEST(test_chunk_at_last_remainder);
    RUN_TEST(test_chunk_at_last_exact);
    RUN_TEST(test_chunk_at_out_of_range);
    RUN_TEST(test_chunk_at_empty_payload);
    RUN_TEST(test_iterate_covers_full_payload);
    return UNITY_END();
}
