// EMS DoseSync Pro — Impl-Phase G: UTF-8 字元邊界純邏輯
//
// 動機：drawDeviceInfo()（ui_screens.cpp）裝置名稱超長時逐字元裁切尾端的迴圈，
// 抽出「往回退一個完整 UTF-8 字元」這段純位元運算成 utf8PrevCharBoundary()
// （ems_utf8.h），native test 涵蓋 ASCII／3-byte CJK／邊界在 index 0／混合字串
// 四種情境，外加違反契約（idx==0／s==nullptr）時 abort() 中止的死亡測試
// （Phase G 全分支整合 review 抓到的 IMPORTANT：先前這段邏輯只在 ui_screens.cpp
// 內，native build 排除 src/，完全無回歸保護；後續 review 又抓到 CRITICAL：
// 契約檢查只寫在文件裡、函式內部沒有真的擋，違規呼叫會 size_t 下溢並越界讀取）。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §4.3

#include <unity.h>
#include <string.h>
#include "ems_utf8.h"

// fork() 死亡測試（test_zero_idx_triggers_abort / test_null_string_triggers_abort）
// 子行程若違規呼叫沒有真的觸發 abort() 中止，就會執行到這個 _exit——用具名值
// 而非裸數字標示「這個結果本身就代表測試該抓的迴歸」，父行程會明確比對這個值，
// 不只是靠「不是 SIGABRT」間接推論。手法比照 ui_scroll.h 既有的
// test_visible_rows_zero_triggers_abort()（test_ui_scroll/test_main.cpp）。
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================
//  Fixture 常數：測試字串與期望的邊界索引
// ============================================================

// ASCII-only 字串（4 個 1-byte 字元），總長 4 bytes
static const char* const FIXTURE_ASCII_STRING = "ABCD";
static const size_t ASCII_STRING_LEN = 4;
static const size_t EXPECTED_ASCII_BOUNDARY = 3;  // 退掉尾端 'D'（1 byte），剩 "ABC"

// 單一 3-byte CJK 字元「測」（U+6E2C = 0xE6 0xB8 0xAC），總長 3 bytes
static const char* const FIXTURE_CJK_STRING = "\xE6\xB8\xAC";
static const size_t CJK_STRING_LEN = 3;
static const size_t EXPECTED_CJK_BOUNDARY = 0;  // 整個 3-byte 字元一次退完，回到字串開頭

// 單一 ASCII 字元「A」，總長 1 byte——用來驗證「退到 idx==0 前最後一次合法呼叫」
// 這個邊界（idx=1 呼叫仍合法，退到 0 後呼叫端就該停止，不再呼叫）
static const char* const FIXTURE_SINGLE_ASCII = "A";
static const size_t SINGLE_ASCII_LEN = 1;
static const size_t EXPECTED_INDEX_ZERO_BOUNDARY = 0;

// 混合字串「A測B」：'A'(1 byte) + 測(3 bytes) + 'B'(1 byte)，總長 5 bytes
// 用相鄰字串常數字面值串接（"...AC" "B"）而非單一字面值——'B' 本身也是合法
// hex 數字字元（A-F），緊接在 \xAC 後面會被吃進同一個 hex escape 變成
// \xACB（超出 char 範圍，編譯期報錯 "hex escape sequence out of range"），
// 拆成兩個字面值讓編譯器在 \xAC 處確實終止該 escape。
static const char* const FIXTURE_MIXED_STRING = "A\xE6\xB8\xAC" "B";
static const size_t MIXED_STRING_LEN = 5;
static const size_t EXPECTED_MIXED_BOUNDARY_AFTER_TRAILING_ASCII = 4;  // 退掉尾端 'B'，剩 "A測"
static const size_t EXPECTED_MIXED_BOUNDARY_AFTER_CJK = 1;             // 再退掉「測」，剩 "A"

// 違反契約呼叫用的無效輸入
static const size_t INVALID_IDX_ZERO = 0;  // idx == 0：字串已裁到空，不該再呼叫

// 死亡測試子行程若沒有真的被 abort() 中止會走到的哨兵值
static const int EXIT_CODE_ABORT_DID_NOT_FIRE = 42;

/**
 * setUp — Unity 框架每個測試前呼叫的必要 hook；本檔測試無需初始化，維持空實作。
 *
 * @return 無（void）
 */
void setUp()    {}

/**
 * tearDown — Unity 框架每個測試後呼叫的必要 hook；本檔測試無需清理，維持空實作。
 *
 * @return 無（void）
 */
void tearDown() {}

/**
 * test_ascii_boundary_steps_back_one_byte — 驗證 ASCII 字元只退 1 byte。
 *
 * "ABCD" 從 idx=4（字串結尾）退一個字元，ASCII 字元沒有 continuation byte，
 * 應直接回傳 3（只退 1 byte）。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_ascii_boundary_steps_back_one_byte() {
    // STEP 01: 檢查 fixture 字串長度符合預期，確保後續斷言的 idx 起點正確
    TEST_ASSERT_EQUAL_UINT32(ASCII_STRING_LEN, strlen(FIXTURE_ASCII_STRING));
    // STEP 02: 檢查 ASCII 字元只退 1 byte
    TEST_ASSERT_EQUAL_size_t(EXPECTED_ASCII_BOUNDARY,
        utf8PrevCharBoundary(FIXTURE_ASCII_STRING, ASCII_STRING_LEN));
}

/**
 * test_cjk_boundary_steps_back_whole_character — 驗證 3-byte CJK 字元整個退完。
 *
 * 「測」從 idx=3（字串結尾）退一個字元，先退到 continuation byte 0xAC，
 * 再退到 continuation byte 0xB8，最後退到 lead byte 0xE6（idx==0）才停，
 * 應回傳 0（3 個 byte 全部退掉，不留下半個字元）。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_cjk_boundary_steps_back_whole_character() {
    // STEP 01: 檢查 fixture 字串長度符合預期，確保後續斷言的 idx 起點正確
    TEST_ASSERT_EQUAL_UINT32(CJK_STRING_LEN, strlen(FIXTURE_CJK_STRING));
    // STEP 02: 檢查 3-byte CJK 字元整個字元一次退完
    TEST_ASSERT_EQUAL_size_t(EXPECTED_CJK_BOUNDARY,
        utf8PrevCharBoundary(FIXTURE_CJK_STRING, CJK_STRING_LEN));
}

/**
 * test_boundary_at_index_zero_does_not_underflow — 驗證退到字串開頭前的最後一次
 * 合法呼叫正確停在 0。
 *
 * 單一 ASCII 字元「A」從 idx=1（唯一合法輸入，字串長度本身）退一個字元，
 * 應回傳 0；idx=1 時迴圈條件 `idx > 0` 在退到 0 後為 false 應立即停止，
 * 不會繼續讀取負索引（size_t 下溢會變成極大值，若邊界條件寫錯會導致存取
 * 越界記憶體）。idx==0 本身違反契約，改由下方 test_zero_idx_triggers_abort
 * 的死亡測試涵蓋。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_boundary_at_index_zero_does_not_underflow() {
    // STEP 01: 檢查 fixture 字串長度符合預期，確保後續斷言的 idx 起點正確
    TEST_ASSERT_EQUAL_UINT32(SINGLE_ASCII_LEN, strlen(FIXTURE_SINGLE_ASCII));
    // STEP 02: 檢查退到字串開頭時正確停在 0，不下溢
    TEST_ASSERT_EQUAL_size_t(EXPECTED_INDEX_ZERO_BOUNDARY,
        utf8PrevCharBoundary(FIXTURE_SINGLE_ASCII, SINGLE_ASCII_LEN));
}

/**
 * test_mixed_string_peels_one_character_at_a_time — 驗證混合字串逐字元裁切。
 *
 * 「A測B」模擬 drawDeviceInfo() 實際使用情境：呼叫端逐次縮短候選長度直到
 * 符合寬度限制。第一次從 idx=5（結尾）退掉尾端 ASCII 'B' 應回傳 4（剩
 * "A測"）；第二次從 idx=4 退掉「測」整個 3-byte 字元應回傳 1（剩 "A"），
 * 證明退 ASCII 字元不會誤退相鄰的多 byte 字元，反之亦然。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_mixed_string_peels_one_character_at_a_time() {
    // STEP 01: 檢查 fixture 字串長度符合預期，確保後續斷言的 idx 起點正確
    TEST_ASSERT_EQUAL_UINT32(MIXED_STRING_LEN, strlen(FIXTURE_MIXED_STRING));

    // STEP 02: 第一次裁切退掉尾端 ASCII 'B'（1 byte）
    size_t after_first_peel = utf8PrevCharBoundary(FIXTURE_MIXED_STRING, MIXED_STRING_LEN);
    TEST_ASSERT_EQUAL_size_t(EXPECTED_MIXED_BOUNDARY_AFTER_TRAILING_ASCII, after_first_peel);

    // STEP 03: 第二次裁切退掉「測」整個 3-byte 字元，只留下開頭的 'A'
    size_t after_second_peel = utf8PrevCharBoundary(FIXTURE_MIXED_STRING, after_first_peel);
    TEST_ASSERT_EQUAL_size_t(EXPECTED_MIXED_BOUNDARY_AFTER_CJK, after_second_peel);
}

/**
 * test_zero_idx_triggers_abort — 驗證 idx == 0 時函式以 abort() 中止呼叫端行程，
 * 而不是靜默下溢成 SIZE_MAX 後越界讀取。
 *
 * idx == 0 是違反函式契約的呼叫端錯誤（字串已裁到空，不該再呼叫）。用 fork()
 * 讓這個違規呼叫在子行程執行：子行程若真的被 abort() 中止，會被 SIGABRT
 * 終止而不會執行到後面的 _exit(EXIT_CODE_ABORT_DID_NOT_FIRE)；父行程用
 * waitpid() 檢查子行程的終止方式，藉此在不讓整個測試二進位檔跟著崩潰的
 * 前提下驗證 fail-fast 行為。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_zero_idx_triggers_abort() {
    // STEP 01: fork 出子行程執行違規呼叫，本行程（父行程）保持存活
    pid_t pid = fork();
    TEST_ASSERT_TRUE_MESSAGE(pid >= 0, "fork() failed, cannot run death test");

    if (pid == 0) {
        // STEP 01.01: 子行程——呼叫應觸發 abort() 中止；若沒中止則走到 _exit
        //   標記「abort() 沒有真的發生」這個迴歸情境，讓父行程斷言失敗
        utf8PrevCharBoundary(FIXTURE_ASCII_STRING, INVALID_IDX_ZERO);
        _exit(EXIT_CODE_ABORT_DID_NOT_FIRE);
    }

    // STEP 02: 父行程等子行程結束——EINTR 時重試而非把中斷誤判為子行程終止
    int status = 0;    // 子行程的終止狀態，交給 WIFSIGNALED/WTERMSIG/WIFEXITED/WEXITSTATUS 解讀
    pid_t waited;       // waitpid() 實際回傳的 PID，用來確認等到的就是自己剛 fork 出的子行程
    do {
        // STEP 02.01: 逐次呼叫 waitpid()，被訊號中斷（EINTR）就重試，不誤判為子行程已終止
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    TEST_ASSERT_EQUAL_INT_MESSAGE(pid, waited, "waitpid() failed unexpectedly");

    // STEP 03: 驗證子行程是被 SIGABRT 訊號終止（abort() 觸發的標準終止方式）
    if (WIFSIGNALED(status)) {
        // STEP 03.01: 正常情境——確認終止訊號正是 SIGABRT，不是其他訊號
        TEST_ASSERT_EQUAL_INT_MESSAGE(SIGABRT, WTERMSIG(status), "expected SIGABRT from abort() failure");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CODE_ABORT_DID_NOT_FIRE) {
        // STEP 03.02: 迴歸情境——子行程正常結束並走到哨兵 _exit，代表 abort() 沒有真的觸發
        TEST_FAIL_MESSAGE("child exited normally instead of being aborted — abort() did not fire (regression: contract check bypassed, e.g. by -DNDEBUG)");
    } else {
        // STEP 03.03: 其他未預期的終止方式，明確標示與上兩種分支不同
        TEST_FAIL_MESSAGE("child terminated in an unexpected way (neither SIGABRT nor the abort-did-not-fire sentinel exit code)");
    }
}

/**
 * test_null_string_triggers_abort — 驗證 s == nullptr 時函式以 abort() 中止
 * 呼叫端行程，而不是在 `s[idx]` 解參考時才產生一般性的區段錯誤（沒有明確
 * 錯誤訊號可供除錯）。手法同上一個死亡測試。
 *
 * @return 無（void）；斷言失敗由 Unity 框架記錄
 */
static void test_null_string_triggers_abort() {
    // STEP 01: fork 出子行程執行違規呼叫，本行程（父行程）保持存活
    pid_t pid = fork();
    TEST_ASSERT_TRUE_MESSAGE(pid >= 0, "fork() failed, cannot run death test");

    if (pid == 0) {
        // STEP 01.01: 子行程——呼叫應觸發 abort() 中止；若沒中止則走到 _exit
        //   標記「abort() 沒有真的發生」這個迴歸情境，讓父行程斷言失敗
        utf8PrevCharBoundary(nullptr, ASCII_STRING_LEN);
        _exit(EXIT_CODE_ABORT_DID_NOT_FIRE);
    }

    // STEP 02: 父行程等子行程結束——EINTR 時重試而非把中斷誤判為子行程終止
    int status = 0;    // 子行程的終止狀態，交給 WIFSIGNALED/WTERMSIG/WIFEXITED/WEXITSTATUS 解讀
    pid_t waited;       // waitpid() 實際回傳的 PID，用來確認等到的就是自己剛 fork 出的子行程
    do {
        // STEP 02.01: 逐次呼叫 waitpid()，被訊號中斷（EINTR）就重試，不誤判為子行程已終止
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    TEST_ASSERT_EQUAL_INT_MESSAGE(pid, waited, "waitpid() failed unexpectedly");

    // STEP 03: 驗證子行程是被 SIGABRT 訊號終止（abort() 觸發的標準終止方式）
    if (WIFSIGNALED(status)) {
        // STEP 03.01: 正常情境——確認終止訊號正是 SIGABRT，不是其他訊號
        TEST_ASSERT_EQUAL_INT_MESSAGE(SIGABRT, WTERMSIG(status), "expected SIGABRT from abort() failure");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CODE_ABORT_DID_NOT_FIRE) {
        // STEP 03.02: 迴歸情境——子行程正常結束並走到哨兵 _exit，代表 abort() 沒有真的觸發
        TEST_FAIL_MESSAGE("child exited normally instead of being aborted — abort() did not fire (regression: contract check bypassed, e.g. by -DNDEBUG)");
    } else {
        // STEP 03.03: 其他未預期的終止方式，明確標示與上兩種分支不同
        TEST_FAIL_MESSAGE("child terminated in an unexpected way (neither SIGABRT nor the abort-did-not-fire sentinel exit code)");
    }
}

/**
 * main — Unity 測試主程式。
 *
 * 初始化 Unity 框架，依序執行所有 RUN_TEST，最後回傳測試結果。
 *
 * @param argc  命令列參數個數
 * @param argv  命令列參數陣列
 * @return      測試成功回傳 0，失敗回傳非零
 */
int main(int argc, char** argv) {
    // STEP 01: 初始化 Unity 框架
    UNITY_BEGIN();

    // STEP 02: 執行四種邊界情境測試（ASCII／3-byte CJK／index 0 邊界／混合字串）
    RUN_TEST(test_ascii_boundary_steps_back_one_byte);
    RUN_TEST(test_cjk_boundary_steps_back_whole_character);
    RUN_TEST(test_boundary_at_index_zero_does_not_underflow);
    RUN_TEST(test_mixed_string_peels_one_character_at_a_time);

    // STEP 03: 執行違反契約時 abort() 中止的死亡測試
    RUN_TEST(test_zero_idx_triggers_abort);
    RUN_TEST(test_null_string_triggers_abort);

    // STEP 04: 完成測試並回傳結果
    return UNITY_END();
}
