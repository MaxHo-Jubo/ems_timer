// EMS DoseSync Pro — Impl-Phase F: case sync 狀態機 unit test
//
// 對應 lib：firmware/lib/ems_sync_dispatcher/
// 對應計畫：docs/phase-f-web-validation-plan.md §7.3 + SoT §16.5
// 對應 todo：tasks/phase-f-todo.md F-3
// 執行：pio test -e native -f test_sync_dispatcher
//
// 涵蓋：
//   - Happy path：IDLE → AWAITING_CONNECT → AWAITING_INPUT → AWAITING_MAIN_KEY → SENDING → DONE → IDLE
//   - Failure paths：120s pairing timeout / 3 次錯碼 lockout / 30s 主鍵 timeout / BLE 斷線
//   - 返回鍵：非 SENDING 階段可中止（SENDING 階段忽略）
//   - 同步進度：chunk_acked 推進 sent_chunks_count
//   - Boundary 事件 dispatch：未定義 transition 不爆 state
#include <unity.h>
#include "ems_sync_dispatcher.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// 共用 helper：建立並啟動到指定 state
static SyncContext make_ctx() {
    SyncContext ctx;
    sync_dispatcher_init(&ctx, /*now_ms=*/1000);
    return ctx;
}

static void drive_to_awaiting_input(SyncContext* ctx, uint64_t now_ms) {
    sync_dispatcher_dispatch(ctx, SyncEvent::START, now_ms);
    sync_dispatcher_dispatch(ctx, SyncEvent::BLE_CONNECTED, now_ms);
}

static void drive_to_awaiting_main_key(SyncContext* ctx, uint64_t now_ms) {
    drive_to_awaiting_input(ctx, now_ms);
    // 用 ctx 內 pairing_code.digits 當正確輸入
    sync_dispatcher_dispatch_input(ctx, ctx->pairing_code.digits, PAIRING_CODE_LEN, now_ms);
}

// ============================================================
//  初始狀態
// ============================================================

static void test_init_state_is_idle() {
    SyncContext ctx = make_ctx();
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.sent_chunks_count);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.total_chunks);
}

// ============================================================
//  Happy path
// ============================================================

static void test_idle_start_goes_to_awaiting_connect() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 1000);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_CONNECT, ctx.state);
}

static void test_awaiting_connect_ble_connected_goes_to_awaiting_input() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_CONNECTED, 1500);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_INPUT, ctx.state);
    // 進 AWAITING_INPUT 時應 generate pairing code
    TEST_ASSERT_EQUAL_UINT64(1500 + PAIRING_TTL_MS, ctx.pairing_code.expires_at_ms);
}

static void test_awaiting_input_correct_code_goes_to_awaiting_main_key() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch_input(&ctx, ctx.pairing_code.digits, PAIRING_CODE_LEN, 2000);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_MAIN_KEY, ctx.state);
}

static void test_awaiting_main_key_press_goes_to_sending() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 5);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    TEST_ASSERT_EQUAL(SyncState::SENDING, ctx.state);
}

static void test_sending_chunk_acked_advances_count() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 3);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);

    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3100);
    TEST_ASSERT_EQUAL_UINT32(1, ctx.sent_chunks_count);
    TEST_ASSERT_EQUAL(SyncState::SENDING, ctx.state);  // 還沒完

    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3200);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3300);
    TEST_ASSERT_EQUAL_UINT32(3, ctx.sent_chunks_count);
    TEST_ASSERT_EQUAL(SyncState::DONE, ctx.state);  // 全 acked → DONE
}

static void test_done_tick_after_1s_returns_to_idle() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 1);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3100);
    TEST_ASSERT_EQUAL(SyncState::DONE, ctx.state);

    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, 3100 + DONE_DISPLAY_MS - 1);
    TEST_ASSERT_EQUAL(SyncState::DONE, ctx.state);  // 還沒到

    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, 3100 + DONE_DISPLAY_MS);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

// ============================================================
//  Failure paths
// ============================================================

static void test_awaiting_input_wrong_code_stays_in_state() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch_input(&ctx, "0000", PAIRING_CODE_LEN, 1500);
    sync_dispatcher_dispatch_input(&ctx, "0001", PAIRING_CODE_LEN, 1600);
    // 兩次錯誤但未到 lockout，仍 AWAITING_INPUT
    TEST_ASSERT_EQUAL(SyncState::AWAITING_INPUT, ctx.state);
}

static void test_awaiting_input_3_wrong_codes_goes_to_error() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch_input(&ctx, "0000", PAIRING_CODE_LEN, 1500);
    sync_dispatcher_dispatch_input(&ctx, "0001", PAIRING_CODE_LEN, 1600);
    sync_dispatcher_dispatch_input(&ctx, "0002", PAIRING_CODE_LEN, 1700);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

static void test_awaiting_input_120s_timeout_goes_to_error() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);  // generate 在 1000，expires 121000
    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, 121000);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

static void test_awaiting_main_key_30s_timeout_goes_to_error() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    uint64_t enter_main_key_ms = ctx.last_state_change_ms;
    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, enter_main_key_ms + MAIN_KEY_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

static void test_ble_disconnected_from_awaiting_input_goes_to_error() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 2000);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

static void test_ble_disconnected_from_sending_goes_to_error() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 5);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 3100);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

static void test_error_tick_returns_to_idle() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 2000);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);

    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, 2000 + ERROR_DISPLAY_MS);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

// ============================================================
//  返回鍵中止
// ============================================================

static void test_back_key_from_awaiting_connect_returns_to_idle() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 1100);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

static void test_back_key_from_awaiting_input_returns_to_idle() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 1500);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

static void test_back_key_from_awaiting_main_key_returns_to_idle() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 2500);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

static void test_back_key_ignored_in_sending() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 3);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 3100);
    TEST_ASSERT_EQUAL(SyncState::SENDING, ctx.state);  // 不可中止
}

// ============================================================
//  防呆：未定義 transition 不爆 state
// ============================================================

static void test_unhandled_event_in_idle_stays_idle() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 1000);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

static void test_pairing_code_input_in_idle_ignored() {
    SyncContext ctx = make_ctx();
    DispatchInputResult r = sync_dispatcher_dispatch_input(&ctx, "1234", PAIRING_CODE_LEN, 1000);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
    TEST_ASSERT_EQUAL(DispatchInputResult::IgnoredWrongState, r);
}

// ============================================================
//  Test gaps from review-pr round 2
// ============================================================

/** dispatch_input 回傳值 Accepted */
static void test_dispatch_input_returns_accepted_on_correct() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    DispatchInputResult r = sync_dispatcher_dispatch_input(&ctx, ctx.pairing_code.digits,
                                                            PAIRING_CODE_LEN, 2000);
    TEST_ASSERT_EQUAL(DispatchInputResult::Accepted, r);
}

/** dispatch_input 回傳值 Rejected（單次錯碼未到 lockout） */
static void test_dispatch_input_returns_rejected_on_wrong() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    DispatchInputResult r = sync_dispatcher_dispatch_input(&ctx, "0000", PAIRING_CODE_LEN, 2000);
    TEST_ASSERT_EQUAL(DispatchInputResult::Rejected, r);
}

/** dispatch_input 回傳值 LockedOut（第 3 次錯碼） */
static void test_dispatch_input_returns_lockedout_on_third_wrong() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch_input(&ctx, "0000", PAIRING_CODE_LEN, 1500);
    sync_dispatcher_dispatch_input(&ctx, "0001", PAIRING_CODE_LEN, 1600);
    DispatchInputResult r = sync_dispatcher_dispatch_input(&ctx, "0002", PAIRING_CODE_LEN, 1700);
    TEST_ASSERT_EQUAL(DispatchInputResult::LockedOut, r);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

/** CHUNK_ACKED 在 IDLE state 不變 state 也不爆 sent_chunks_count */
static void test_chunk_acked_in_idle_ignored() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 1000);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.sent_chunks_count);
}

/** CHUNK_ACKED 在 AWAITING_INPUT 不變 state */
static void test_chunk_acked_in_awaiting_input_ignored() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 2000);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_INPUT, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.sent_chunks_count);
}

/** START 事件在 AWAITING_CONNECT 被忽略（不重複啟動） */
static void test_start_in_awaiting_connect_ignored() {
    SyncContext ctx = make_ctx();
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 1000);
    uint64_t first_change = ctx.last_state_change_ms;
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 5000);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_CONNECT, ctx.state);
    TEST_ASSERT_EQUAL_UINT64(first_change, ctx.last_state_change_ms);
}

/** START 在 ERROR 期間被忽略 */
static void test_start_in_error_ignored() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 2000);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
    sync_dispatcher_dispatch(&ctx, SyncEvent::START, 2500);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);  // 不重啟，等 ERROR_DISPLAY_MS
}

/** ERROR 期間保留 sent_chunks_count 給 UI 顯示「3/5 已送失敗」 */
static void test_error_state_preserves_sent_count_for_ui() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 5);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3100);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3200);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3300);
    TEST_ASSERT_EQUAL_UINT32(3, ctx.sent_chunks_count);

    sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 3400);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
    // 進度保留供 UI 讀取
    TEST_ASSERT_EQUAL_UINT32(3, ctx.sent_chunks_count);
    TEST_ASSERT_EQUAL_UINT32(5, ctx.total_chunks);

    // ERROR → IDLE 才歸零
    sync_dispatcher_dispatch(&ctx, SyncEvent::TICK, 3400 + ERROR_DISPLAY_MS);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.sent_chunks_count);
    TEST_ASSERT_EQUAL_UINT32(0, ctx.total_chunks);
}

/** total_chunks=0 degenerate：MAIN_KEY_PRESS → SENDING，第一個 CHUNK_ACKED 立即 DONE
 *  （caller bug：caller 忘設 total，此時應該不會送 CHUNK_ACKED，但若送了不該爆） */
static void test_zero_total_chunks_first_ack_triggers_done() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    // 刻意不呼叫 set_total_chunks，total_chunks 仍為 0
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    TEST_ASSERT_EQUAL(SyncState::SENDING, ctx.state);
    sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 3100);
    // sent=1 >= total=0 → DONE
    TEST_ASSERT_EQUAL(SyncState::DONE, ctx.state);
}

// ============================================================
//  dispatch bool return value
// ============================================================

/** START 從 IDLE → AWAITING_CONNECT 回 true */
static void test_dispatch_returns_true_on_transition() {
    SyncContext ctx = make_ctx();
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::START, 1000);
    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL(SyncState::AWAITING_CONNECT, ctx.state);
}

/** CHUNK_ACKED 在 IDLE 沒 transition 回 false */
static void test_dispatch_returns_false_on_no_transition() {
    SyncContext ctx = make_ctx();
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::CHUNK_ACKED, 1000);
    TEST_ASSERT_FALSE(r);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

/** BLE_DISCONNECTED 從活躍 state → ERROR 回 true */
static void test_dispatch_returns_true_on_ble_disconnect_transition() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 2000);
    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL(SyncState::ERROR, ctx.state);
}

/** BLE_DISCONNECTED 從 IDLE 沒 transition 回 false */
static void test_dispatch_returns_false_on_ble_disconnect_from_idle() {
    SyncContext ctx = make_ctx();
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::BLE_DISCONNECTED, 1000);
    TEST_ASSERT_FALSE(r);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

/** BACK_KEY 從 SENDING 沒 transition 回 false */
static void test_dispatch_returns_false_on_back_key_in_sending() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_main_key(&ctx, 1000);
    sync_dispatcher_set_total_chunks(&ctx, 3);
    sync_dispatcher_dispatch(&ctx, SyncEvent::MAIN_KEY_PRESS, 3000);
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 3100);
    TEST_ASSERT_FALSE(r);
    TEST_ASSERT_EQUAL(SyncState::SENDING, ctx.state);
}

/** BACK_KEY 從 AWAITING_INPUT → IDLE 回 true */
static void test_dispatch_returns_true_on_back_key_transition() {
    SyncContext ctx = make_ctx();
    drive_to_awaiting_input(&ctx, 1000);
    bool r = sync_dispatcher_dispatch(&ctx, SyncEvent::BACK_KEY_PRESS, 2000);
    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL(SyncState::IDLE, ctx.state);
}

// ============================================================
//  Test runner
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_init_state_is_idle);
    RUN_TEST(test_idle_start_goes_to_awaiting_connect);
    RUN_TEST(test_awaiting_connect_ble_connected_goes_to_awaiting_input);
    RUN_TEST(test_awaiting_input_correct_code_goes_to_awaiting_main_key);
    RUN_TEST(test_awaiting_main_key_press_goes_to_sending);
    RUN_TEST(test_sending_chunk_acked_advances_count);
    RUN_TEST(test_done_tick_after_1s_returns_to_idle);
    RUN_TEST(test_awaiting_input_wrong_code_stays_in_state);
    RUN_TEST(test_awaiting_input_3_wrong_codes_goes_to_error);
    RUN_TEST(test_awaiting_input_120s_timeout_goes_to_error);
    RUN_TEST(test_awaiting_main_key_30s_timeout_goes_to_error);
    RUN_TEST(test_ble_disconnected_from_awaiting_input_goes_to_error);
    RUN_TEST(test_ble_disconnected_from_sending_goes_to_error);
    RUN_TEST(test_error_tick_returns_to_idle);
    RUN_TEST(test_back_key_from_awaiting_connect_returns_to_idle);
    RUN_TEST(test_back_key_from_awaiting_input_returns_to_idle);
    RUN_TEST(test_back_key_from_awaiting_main_key_returns_to_idle);
    RUN_TEST(test_back_key_ignored_in_sending);
    RUN_TEST(test_unhandled_event_in_idle_stays_idle);
    RUN_TEST(test_pairing_code_input_in_idle_ignored);
    RUN_TEST(test_dispatch_input_returns_accepted_on_correct);
    RUN_TEST(test_dispatch_input_returns_rejected_on_wrong);
    RUN_TEST(test_dispatch_input_returns_lockedout_on_third_wrong);
    RUN_TEST(test_chunk_acked_in_idle_ignored);
    RUN_TEST(test_chunk_acked_in_awaiting_input_ignored);
    RUN_TEST(test_start_in_awaiting_connect_ignored);
    RUN_TEST(test_start_in_error_ignored);
    RUN_TEST(test_error_state_preserves_sent_count_for_ui);
    RUN_TEST(test_zero_total_chunks_first_ack_triggers_done);
    // dispatch bool return value
    RUN_TEST(test_dispatch_returns_true_on_transition);
    RUN_TEST(test_dispatch_returns_false_on_no_transition);
    RUN_TEST(test_dispatch_returns_true_on_ble_disconnect_transition);
    RUN_TEST(test_dispatch_returns_false_on_ble_disconnect_from_idle);
    RUN_TEST(test_dispatch_returns_false_on_back_key_in_sending);
    RUN_TEST(test_dispatch_returns_true_on_back_key_transition);
    return UNITY_END();
}
