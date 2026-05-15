// EMS DoseSync Pro — Impl-Phase F: BLE RX queue unit test
//
// 對應 lib：firmware/lib/ble_rx_queue/
// 對應計畫：docs/phase-f-web-validation-plan.md §7.2 + feedback memory ble_callback_non_blocking
// 對應 todo：tasks/phase-f-todo.md F-2
// 執行：pio test -e native -f test_ble_rx_queue
//
// 涵蓋：
//   - init 後 empty=true, full=false, size=0
//   - push N frames + pop 順序保持（FIFO）
//   - 滿 queue：push 回 false，pop 後可再 push
//   - 空 queue：pop 回 false
//   - frame 超過 max_frame_size：push 回 false
//   - 環狀回繞索引算術
#include <unity.h>
#include <cstring>
#include "ble_rx_queue.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// ============================================================
//  初始狀態
// ============================================================

/** init 後 queue 空 */
static void test_init_empty() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    TEST_ASSERT_TRUE(rx_queue_empty(&q));
    TEST_ASSERT_FALSE(rx_queue_full(&q));
    TEST_ASSERT_EQUAL_size_t(0, rx_queue_size(&q));
}

// ============================================================
//  Push / Pop 基本
// ============================================================

/** push 1 → size=1 / empty=false / full=false */
static void test_push_one() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t payload[] = {'1', '2', '3', '4'};
    TEST_ASSERT_TRUE(rx_queue_push(&q, payload, 4));
    TEST_ASSERT_FALSE(rx_queue_empty(&q));
    TEST_ASSERT_EQUAL_size_t(1, rx_queue_size(&q));
}

/** push 然後 pop 拿回相同 bytes + len */
static void test_push_pop_roundtrip() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t payload[] = {'a', 'b', 'c'};
    rx_queue_push(&q, payload, 3);

    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(rx_queue_pop(&q, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(3, out_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, out, 3);
    TEST_ASSERT_TRUE(rx_queue_empty(&q));
}

/** FIFO 順序：push 1,2,3 → pop 拿到 1,2,3 */
static void test_fifo_order() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t a[] = {1}, b[] = {2}, c[] = {3};
    rx_queue_push(&q, a, 1);
    rx_queue_push(&q, b, 1);
    rx_queue_push(&q, c, 1);

    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_EQUAL_UINT8(2, out[0]);
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_EQUAL_UINT8(3, out[0]);
}

// ============================================================
//  邊界 — 滿 / 空
// ============================================================

/** 空 queue pop 回 false，out 不變 */
static void test_pop_empty_returns_false() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 999;  // 哨兵
    TEST_ASSERT_FALSE(rx_queue_pop(&q, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(999, out_len);  // 未被改寫
}

/** push 到 capacity → full=true，下一個 push 回 false */
static void test_push_until_full() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t one[] = {0xAA};
    for (size_t i = 0; i < BLE_RX_QUEUE_CAPACITY; ++i) {
        TEST_ASSERT_TRUE(rx_queue_push(&q, one, 1));
    }
    TEST_ASSERT_TRUE(rx_queue_full(&q));
    TEST_ASSERT_EQUAL_size_t(BLE_RX_QUEUE_CAPACITY, rx_queue_size(&q));
    TEST_ASSERT_FALSE(rx_queue_push(&q, one, 1));  // 滿了拒絕
}

/** dropped counter 在 push-full 時累計，init 後歸零 */
static void test_dropped_counter_tracks_full_drops() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    TEST_ASSERT_EQUAL_UINT32(0, q.dropped);

    const uint8_t one[] = {0xAA};
    for (size_t i = 0; i < BLE_RX_QUEUE_CAPACITY; ++i) {
        rx_queue_push(&q, one, 1);
    }
    // 滿之後 3 次 push 都應計 dropped
    rx_queue_push(&q, one, 1);
    rx_queue_push(&q, one, 1);
    rx_queue_push(&q, one, 1);
    TEST_ASSERT_EQUAL_UINT32(3, q.dropped);

    // caller bug（nullptr）不計 dropped
    rx_queue_push(&q, nullptr, 1);
    TEST_ASSERT_EQUAL_UINT32(3, q.dropped);
}

/** frame size 244（現役 MTU 247 - 3 ATT header）push/pop 完整 */
static void test_push_frame_244_mtu247_current() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    uint8_t payload[244];
    std::memset(payload, 0x33, sizeof(payload));
    TEST_ASSERT_TRUE(rx_queue_push(&q, payload, 244));

    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_EQUAL_size_t(244, out_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, out, 244);
}

/** pop out nullptr → false（caller bug 防呆） */
static void test_pop_null_out_returns_false() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t one[] = {0xAA};
    rx_queue_push(&q, one, 1);
    size_t out_len = 0;
    TEST_ASSERT_FALSE(rx_queue_pop(&q, nullptr, &out_len));
}

/** pop out_len nullptr → false */
static void test_pop_null_out_len_returns_false() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t one[] = {0xAA};
    rx_queue_push(&q, one, 1);
    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    TEST_ASSERT_FALSE(rx_queue_pop(&q, out, nullptr));
}

/** 滿了 pop 一個後可再 push（ring 繞回） */
static void test_pop_after_full_allows_push() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t one[] = {0xAA};
    for (size_t i = 0; i < BLE_RX_QUEUE_CAPACITY; ++i) {
        rx_queue_push(&q, one, 1);
    }
    TEST_ASSERT_TRUE(rx_queue_full(&q));

    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_FALSE(rx_queue_full(&q));
    TEST_ASSERT_TRUE(rx_queue_push(&q, one, 1));
    TEST_ASSERT_TRUE(rx_queue_full(&q));  // 又滿
}

// ============================================================
//  Frame size 邊界
// ============================================================

/** frame == MAX → push ok */
static void test_push_max_frame() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    uint8_t payload[BLE_RX_MAX_FRAME_BYTES];
    std::memset(payload, 0x55, sizeof(payload));
    TEST_ASSERT_TRUE(rx_queue_push(&q, payload, BLE_RX_MAX_FRAME_BYTES));

    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;
    rx_queue_pop(&q, out, &out_len);
    TEST_ASSERT_EQUAL_size_t(BLE_RX_MAX_FRAME_BYTES, out_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, out, BLE_RX_MAX_FRAME_BYTES);
}

/** frame > MAX → push 回 false，size 不變 */
static void test_push_oversized_rejected() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    uint8_t fake[BLE_RX_MAX_FRAME_BYTES + 1];
    TEST_ASSERT_FALSE(rx_queue_push(&q, fake, BLE_RX_MAX_FRAME_BYTES + 1));
    TEST_ASSERT_EQUAL_size_t(0, rx_queue_size(&q));
}

/** frame nullptr → push 回 false */
static void test_push_null_rejected() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    TEST_ASSERT_FALSE(rx_queue_push(&q, nullptr, 4));
}

/** frame len = 0 → push 回 false（empty frame 無意義） */
static void test_push_zero_len_rejected() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    uint8_t one = 0;
    TEST_ASSERT_FALSE(rx_queue_push(&q, &one, 0));
}

// ============================================================
//  Ring 繞回索引
// ============================================================

/** push 滿 + pop 全清 + push 滿 → ring 繞回不爆 */
static void test_ring_wrap_around() {
    ble_rx_queue_t q;
    rx_queue_init(&q);
    const uint8_t one[] = {0xAA};
    uint8_t out[BLE_RX_MAX_FRAME_BYTES];
    size_t out_len = 0;

    // 兩輪 push-滿 + pop-空
    for (int round = 0; round < 2; ++round) {
        for (size_t i = 0; i < BLE_RX_QUEUE_CAPACITY; ++i) {
            TEST_ASSERT_TRUE(rx_queue_push(&q, one, 1));
        }
        for (size_t i = 0; i < BLE_RX_QUEUE_CAPACITY; ++i) {
            TEST_ASSERT_TRUE(rx_queue_pop(&q, out, &out_len));
        }
        TEST_ASSERT_TRUE(rx_queue_empty(&q));
    }
}

// ============================================================
//  Test runner
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_init_empty);
    RUN_TEST(test_push_one);
    RUN_TEST(test_push_pop_roundtrip);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_pop_empty_returns_false);
    RUN_TEST(test_push_until_full);
    RUN_TEST(test_dropped_counter_tracks_full_drops);
    RUN_TEST(test_push_frame_244_mtu247_current);
    RUN_TEST(test_pop_null_out_returns_false);
    RUN_TEST(test_pop_null_out_len_returns_false);
    RUN_TEST(test_pop_after_full_allows_push);
    RUN_TEST(test_push_max_frame);
    RUN_TEST(test_push_oversized_rejected);
    RUN_TEST(test_push_null_rejected);
    RUN_TEST(test_push_zero_len_rejected);
    RUN_TEST(test_ring_wrap_around);
    return UNITY_END();
}
