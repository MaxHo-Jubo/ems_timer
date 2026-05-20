// EMS DoseSync Pro — Impl-Phase F: 配對碼 lib unit test
//
// 對應 lib：firmware/lib/ems_pairing/
// 對應計畫：docs/phase-f-web-validation-plan.md §7.1
// 對應 todo：tasks/phase-f-todo.md F-1
// 執行：pio test -e native -f test_pairing
//
// 涵蓋：
//   - generate() 產 4 位 ASCII digit + 120_000ms TTL
//   - verify() 返回 PairingVerifyResult enum，區分 4 種失敗原因
//   - InvalidInput（nullptr / 長度錯）不計 failure_count（避免 protocol bug 耗 user 額度）
//   - Mismatch + Expired 計 failure_count++
//   - is_expired() 邊界 now == expires_at（inclusive）
//   - 1ms 內連續 generate() 必須產生不同碼（monotonic counter）
//   - 3 次失敗 lockout：第 4 次即使送對碼也 LockedOut
//   - generate() 重置 failure_count（lockout 解除）
//   - now_ms == 0 edge case
//   - 成功 verify 不歸零既有 failure_count
#include <unity.h>
#include <cstring>
#include "ems_pairing.h"

using namespace ems;

void setUp()    {}
void tearDown() {}

// 4-char 配對碼長度（不含 '\0'）— Mismatch 路徑用
static constexpr size_t kInputLen = PAIRING_CODE_LEN;

// 把 code.digits 的第一位翻成不同 digit，產生一定不撞對的錯誤碼
static void make_wrong_code(const PairingCode& code, char* out_buf) {
    std::strncpy(out_buf, code.digits, PAIRING_CODE_BUF_SIZE);
    out_buf[0] = (code.digits[0] == '0') ? '1' : '0';
}

// ============================================================
//  generate() 基本性質
// ============================================================

/** generate 產 4 位 ASCII digit，buffer 結尾為 '\0' */
static void test_generate_produces_4_ascii_digits() {
    PairingCode code = pairing_generate(1000);
    TEST_ASSERT_EQUAL_size_t(4, strlen(code.digits));
    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(code.digits[i] >= '0' && code.digits[i] <= '9');
    }
    TEST_ASSERT_EQUAL_CHAR('\0', code.digits[4]);
}

/** generate TTL = now + 120_000ms */
static void test_generate_sets_ttl_120000ms() {
    PairingCode code = pairing_generate(50000);
    TEST_ASSERT_EQUAL_UINT64(50000 + 120000, code.expires_at_ms);
}

/** generate failure_count 初始 = 0 */
static void test_generate_resets_failure_count() {
    PairingCode code = pairing_generate(1000);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** 連續 generate（即使同 now_ms）必須產生不同碼 */
static void test_generate_consecutive_codes_differ() {
    PairingCode a = pairing_generate(1000);
    PairingCode b = pairing_generate(1000);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(a.digits, b.digits));
}

/** generate now_ms == 0：expires_at = 120_000，verify 立即可用 */
static void test_generate_now_ms_zero() {
    PairingCode code = pairing_generate(0);
    TEST_ASSERT_EQUAL_UINT64(PAIRING_TTL_MS, code.expires_at_ms);
    TEST_ASSERT_FALSE(pairing_is_expired(code, 0));
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 0);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Ok, r);
}

// ============================================================
//  verify() happy path
// ============================================================

/** 正確碼 + 未過期 → Ok，failure_count 不變 */
static void test_verify_correct_unexpired_returns_ok() {
    PairingCode code = pairing_generate(1000);
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Ok, r);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** 累積 failures 後成功 verify 不歸零 failure_count（保契約） */
static void test_verify_success_preserves_accumulated_failure_count() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);

    pairing_verify(code, wrong, kInputLen, 60000);  // Mismatch, count = 1
    pairing_verify(code, wrong, kInputLen, 60000);  // Mismatch, count = 2
    TEST_ASSERT_EQUAL_UINT8(2, code.failure_count);

    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Ok, r);
    // 成功不歸零 → 仍為 2
    TEST_ASSERT_EQUAL_UINT8(2, code.failure_count);
}

// ============================================================
//  verify() 失敗路徑 — Mismatch（計失敗）
// ============================================================

/** 錯誤碼 → Mismatch，failure_count++ */
static void test_verify_wrong_code_returns_mismatch() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);
    PairingVerifyResult r = pairing_verify(code, wrong, kInputLen, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Mismatch, r);
    TEST_ASSERT_EQUAL_UINT8(1, code.failure_count);
}

// ============================================================
//  verify() 失敗路徑 — Expired（計失敗）
// ============================================================

/** 過期 → Expired（即使碼正確），failure_count++ */
static void test_verify_expired_returns_expired() {
    PairingCode code = pairing_generate(1000);
    // now = 1000 + 120000 + 1 = 121001 (已過期)
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 121001);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Expired, r);
    TEST_ASSERT_EQUAL_UINT8(1, code.failure_count);
}

// ============================================================
//  verify() 失敗路徑 — InvalidInput（不計失敗）
// ============================================================

/** input nullptr → InvalidInput，failure_count 不變（protocol bug 不耗 quota） */
static void test_verify_null_input_returns_invalid_input_no_count() {
    PairingCode code = pairing_generate(1000);
    PairingVerifyResult r = pairing_verify(code, nullptr, kInputLen, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::InvalidInput, r);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** input_len 過短 → InvalidInput，failure_count 不變 */
static void test_verify_short_input_returns_invalid_input_no_count() {
    PairingCode code = pairing_generate(1000);
    PairingVerifyResult r = pairing_verify(code, "123", 3, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::InvalidInput, r);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** input_len 過長 → InvalidInput，failure_count 不變 */
static void test_verify_long_input_returns_invalid_input_no_count() {
    PairingCode code = pairing_generate(1000);
    PairingVerifyResult r = pairing_verify(code, "12345", 5, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::InvalidInput, r);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** input_len == 0 → InvalidInput，failure_count 不變 */
static void test_verify_empty_input_returns_invalid_input_no_count() {
    PairingCode code = pairing_generate(1000);
    PairingVerifyResult r = pairing_verify(code, "", 0, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::InvalidInput, r);
    TEST_ASSERT_EQUAL_UINT8(0, code.failure_count);
}

/** input 非 null-terminated 但 input_len 正確 → 正常比對（不爆 OOB） */
static void test_verify_non_null_terminated_input_works() {
    PairingCode code = pairing_generate(1000);
    char raw[PAIRING_CODE_LEN];  // 沒有 '\0'
    std::memcpy(raw, code.digits, PAIRING_CODE_LEN);
    PairingVerifyResult r = pairing_verify(code, raw, PAIRING_CODE_LEN, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Ok, r);
}

// ============================================================
//  is_expired() 邊界
// ============================================================

/** is_expired now < expires → false */
static void test_is_expired_before_expiry_returns_false() {
    PairingCode code = pairing_generate(1000);  // expires_at = 121000
    TEST_ASSERT_FALSE(pairing_is_expired(code, 120999));
}

/** is_expired now == expires → true（inclusive boundary） */
static void test_is_expired_at_expiry_returns_true() {
    PairingCode code = pairing_generate(1000);  // expires_at = 121000
    TEST_ASSERT_TRUE(pairing_is_expired(code, 121000));
}

/** is_expired now > expires → true */
static void test_is_expired_after_expiry_returns_true() {
    PairingCode code = pairing_generate(1000);
    TEST_ASSERT_TRUE(pairing_is_expired(code, 121001));
}

// ============================================================
//  Lockout：3 次失敗後即使送對碼也 LockedOut
// ============================================================

/** 累積 3 次失敗 → is_locked_out = true，第 4 次送對碼回 LockedOut */
static void test_three_failures_trigger_lockout() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);

    pairing_verify(code, wrong, kInputLen, 60000);  // 1
    pairing_verify(code, wrong, kInputLen, 60000);  // 2
    pairing_verify(code, wrong, kInputLen, 60000);  // 3
    TEST_ASSERT_TRUE(pairing_is_locked_out(code));
    TEST_ASSERT_EQUAL_UINT8(3, code.failure_count);

    // 第 4 次送對碼也應 LockedOut（不是 Ok）
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 60000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::LockedOut, r);
}

/** Lockout 後 failure_count 不再累積（避免 overflow） */
static void test_lockout_does_not_increment_failure_count_further() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);

    for (int i = 0; i < 10; ++i) {
        pairing_verify(code, wrong, kInputLen, 60000);
    }
    TEST_ASSERT_EQUAL_UINT8(PAIRING_MAX_FAILURES, code.failure_count);
}

/** Lockout guard 優先於過期 / Mismatch — 送對碼也 LockedOut，不被當 Ok 漏過 */
static void test_lockout_blocks_correct_code_after_expiry() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);
    for (int i = 0; i < 3; ++i) {
        pairing_verify(code, wrong, kInputLen, 60000);
    }
    TEST_ASSERT_TRUE(pairing_is_locked_out(code));

    // 即使在 expired 時間 + 對碼，仍應回 LockedOut（lockout 優先序最高）
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 200000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::LockedOut, r);
}

/** generate 新碼解除 lockout */
static void test_regenerate_clears_lockout() {
    PairingCode code = pairing_generate(1000);
    char wrong[PAIRING_CODE_BUF_SIZE];
    make_wrong_code(code, wrong);

    for (int i = 0; i < 3; ++i) {
        pairing_verify(code, wrong, kInputLen, 60000);
    }
    TEST_ASSERT_TRUE(pairing_is_locked_out(code));

    code = pairing_generate(200000);
    TEST_ASSERT_FALSE(pairing_is_locked_out(code));
    PairingVerifyResult r = pairing_verify(code, code.digits, kInputLen, 250000);
    TEST_ASSERT_EQUAL(PairingVerifyResult::Ok, r);
}

// ============================================================
//  pairing_remaining_sec — SoT §16.4 倒數秒數（Phase F MVP2-Followup simplify R4）
// ============================================================

static void test_remaining_sec_happy_path() {
    PairingCode code = pairing_generate(0);  // expires_at = 120000
    TEST_ASSERT_EQUAL_UINT32(90, pairing_remaining_sec(code, 30000));
}

static void test_remaining_sec_at_expiry_returns_zero() {
    PairingCode code = pairing_generate(0);  // expires_at = 120000
    TEST_ASSERT_EQUAL_UINT32(0, pairing_remaining_sec(code, 120000));
}

static void test_remaining_sec_past_expiry_returns_zero() {
    PairingCode code = pairing_generate(0);  // expires_at = 120000
    TEST_ASSERT_EQUAL_UINT32(0, pairing_remaining_sec(code, 200000));
}

static void test_remaining_sec_full_ttl_just_after_generate() {
    PairingCode code = pairing_generate(0);  // expires_at = 120000
    TEST_ASSERT_EQUAL_UINT32(120, pairing_remaining_sec(code, 0));
}

static void test_remaining_sec_zeroed_code_returns_zero() {
    // 防禦：未初始化（全 0）的 PairingCode 應回 0 而非 underflow（expires_at_ms=0 <= now）
    PairingCode code{};
    TEST_ASSERT_EQUAL_UINT32(0, pairing_remaining_sec(code, 5000));
}

// ============================================================
//  pairing_remaining_attempts — pair_status remaining_attempts 欄位（Phase F MVP3）
// ============================================================

/** 新產生的碼 failure_count=0 → 剩餘 = PAIRING_MAX_FAILURES */
static void test_remaining_attempts_fresh_code_returns_max() {
    PairingCode code = pairing_generate(0);
    TEST_ASSERT_EQUAL_UINT8(PAIRING_MAX_FAILURES, pairing_remaining_attempts(code));
}

/** 失敗 1 次 → 剩餘 2（Rejected 路徑實際會出現的值） */
static void test_remaining_attempts_after_one_failure() {
    PairingCode code = pairing_generate(0);
    code.failure_count = 1;
    TEST_ASSERT_EQUAL_UINT8(2, pairing_remaining_attempts(code));
}

/** 失敗 2 次 → 剩餘 1（Rejected 路徑最後一次重試前的值） */
static void test_remaining_attempts_after_two_failures() {
    PairingCode code = pairing_generate(0);
    code.failure_count = 2;
    TEST_ASSERT_EQUAL_UINT8(1, pairing_remaining_attempts(code));
}

/** 失敗達門檻 → 剩餘 0（lockout 邊界） */
static void test_remaining_attempts_at_lockout_returns_zero() {
    PairingCode code = pairing_generate(0);
    code.failure_count = PAIRING_MAX_FAILURES;
    TEST_ASSERT_EQUAL_UINT8(0, pairing_remaining_attempts(code));
}

/** 防禦：failure_count 超過門檻仍回 0，不 underflow */
static void test_remaining_attempts_beyond_lockout_returns_zero() {
    PairingCode code = pairing_generate(0);
    code.failure_count = 99;
    TEST_ASSERT_EQUAL_UINT8(0, pairing_remaining_attempts(code));
}

// ============================================================
//  Test runner
// ============================================================

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_generate_produces_4_ascii_digits);
    RUN_TEST(test_generate_sets_ttl_120000ms);
    RUN_TEST(test_generate_resets_failure_count);
    RUN_TEST(test_generate_consecutive_codes_differ);
    RUN_TEST(test_generate_now_ms_zero);
    RUN_TEST(test_verify_correct_unexpired_returns_ok);
    RUN_TEST(test_verify_success_preserves_accumulated_failure_count);
    RUN_TEST(test_verify_wrong_code_returns_mismatch);
    RUN_TEST(test_verify_expired_returns_expired);
    RUN_TEST(test_verify_null_input_returns_invalid_input_no_count);
    RUN_TEST(test_verify_short_input_returns_invalid_input_no_count);
    RUN_TEST(test_verify_long_input_returns_invalid_input_no_count);
    RUN_TEST(test_verify_empty_input_returns_invalid_input_no_count);
    RUN_TEST(test_verify_non_null_terminated_input_works);
    RUN_TEST(test_is_expired_before_expiry_returns_false);
    RUN_TEST(test_is_expired_at_expiry_returns_true);
    RUN_TEST(test_is_expired_after_expiry_returns_true);
    RUN_TEST(test_three_failures_trigger_lockout);
    RUN_TEST(test_lockout_does_not_increment_failure_count_further);
    RUN_TEST(test_lockout_blocks_correct_code_after_expiry);
    RUN_TEST(test_regenerate_clears_lockout);
    RUN_TEST(test_remaining_sec_happy_path);
    RUN_TEST(test_remaining_sec_at_expiry_returns_zero);
    RUN_TEST(test_remaining_sec_past_expiry_returns_zero);
    RUN_TEST(test_remaining_sec_full_ttl_just_after_generate);
    RUN_TEST(test_remaining_sec_zeroed_code_returns_zero);
    RUN_TEST(test_remaining_attempts_fresh_code_returns_max);
    RUN_TEST(test_remaining_attempts_after_one_failure);
    RUN_TEST(test_remaining_attempts_after_two_failures);
    RUN_TEST(test_remaining_attempts_at_lockout_returns_zero);
    RUN_TEST(test_remaining_attempts_beyond_lockout_returns_zero);
    return UNITY_END();
}
