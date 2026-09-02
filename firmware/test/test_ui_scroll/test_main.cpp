// EMS DoseSync Pro — Impl-Phase G: 捲動視窗 clamp 共用邏輯
//
// 動機：歷史紀錄清單（input_handler.cpp 內 historyScrollOffset）與
// Task 2 的系統設定選單捲動都需要同一種「游標移出可見視窗時視窗跟著捲」的判斷。
// 依 EXTRACT-SHARED-HELPER 規則抽出共用純函式 clampScrollOffset()。
// Task 3 會改寫兩處呼叫點都改用此函式，統一維護。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.2

#include <unity.h>
#include "ui_scroll.h"

// visible_rows == 0 觸發 abort() 中止（fail-fast）後，測試行程本身會被 SIGABRT
// 終止；用 fork() 讓違規呼叫在子行程執行，父行程只驗證子行程的終止方式，
// 這樣測試二進位檔本身不會跟著崩潰，其餘 RUN_TEST 仍能繼續跑完。
// 注意：fork()/waitpid() 是 POSIX API，本檔只在 env:native（host 編譯）跑，
// 不會被拉進 ESP32 韌體或 on-target 測試環境（見 platformio.ini）。
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

// ============================================================
//  Fixture 常數：測試參數與期望值
// ============================================================

// 可見視窗大小（一次顯示 5 列）
static const uint8_t FIXTURE_VISIBLE_ROWS = 5;

// 游標位置
static const uint16_t CURSOR_MID_WINDOW = 4;        // 視窗中間
static const uint16_t CURSOR_TOP_EDGE = 2;          // 視窗上邊界
static const uint16_t CURSOR_BOTTOM_EDGE = 6;       // 視窗下邊界
static const uint16_t CURSOR_ABOVE_WINDOW = 1;      // 視窗上方
static const uint16_t CURSOR_ZERO = 0;              // 清單頂端
static const uint16_t CURSOR_BELOW_WINDOW = 7;      // 視窗下方
static const uint16_t CURSOR_MAX_LIST = 7;          // 8 項清單的最後項目

// 視窗起點（offset）
static const uint16_t OFFSET_MID = 2;               // 視窗起於第 2 項
static const uint16_t OFFSET_ZERO = 0;              // 視窗起於第 0 項
static const uint16_t OFFSET_HIGH = 3;              // 較高的起點

// 期望的捲動結果與邊界值
static const uint16_t EXPECTED_OFFSET_AFTER_SCROLL_DOWN = 3;  // 游標在位置 7 時視窗往下捲至起點 3，讓 cursor 成為新視窗最後一項
static const uint8_t FIXTURE_VISIBLE_ROWS_FULL_LIST = 8;      // 涵蓋整個清單的可見列數，用來驗證「視窗足以容納所有項目」邊界情境
static const uint8_t INVALID_VISIBLE_ROWS_ZERO = 0;           // 刻意傳入的無效值，驗證 visible_rows == 0 時觸發 abort() 中止
static const uint16_t EXPECTED_OFFSET_NEAR_MAX = 65531;       // 接近 UINT16_MAX 時的期望 offset（cursor 65535 - (visible_rows 5 - 1) = 65531）

// fork() 死亡測試（test_visible_rows_zero_triggers_abort）子行程若違規呼叫
// 沒有真的觸發 abort() 中止，就會執行到這個 _exit——用具名值而非裸數字標示
// 「這個結果本身就代表測試該抓的迴歸」，父行程會明確比對這個值（見該測試
// STEP 03），不只是靠「不是 SIGABRT」間接推論。
static const int EXIT_CODE_ABORT_DID_NOT_FIRE = 42;

/**
 * setUp — Unity 框架每個測試前呼叫的必要 hook；本檔測試無需初始化，維持空實作。
 */
void setUp()    {}

/**
 * tearDown — Unity 框架每個測試後呼叫的必要 hook；本檔測試無需清理，維持空實作。
 */
void tearDown() {}

// ============================================================
//  游標在可見視窗內 → offset 不變
// ============================================================

/**
 * test_cursor_within_window_offset_unchanged — 驗證游標在視窗中間時 offset 不變。
 *
 * 視窗 [2, 7)，游標在位置 4（中間），應回傳 2。
 */
static void test_cursor_within_window_offset_unchanged() {
    // STEP 01: 檢查游標在視窗內時 offset 保持不變
    TEST_ASSERT_EQUAL_UINT16(OFFSET_MID, clampScrollOffset(CURSOR_MID_WINDOW, OFFSET_MID, FIXTURE_VISIBLE_ROWS));
}

/**
 * test_cursor_at_window_top_edge_offset_unchanged — 驗證游標在視窗上邊界時 offset 不變。
 *
 * 視窗 [2, 7)，游標恰在起點 2（下界含），應回傳 2。
 */
static void test_cursor_at_window_top_edge_offset_unchanged() {
    // STEP 01: 檢查游標恰為視窗第一項時 offset 保持不變
    TEST_ASSERT_EQUAL_UINT16(OFFSET_MID, clampScrollOffset(CURSOR_TOP_EDGE, OFFSET_MID, FIXTURE_VISIBLE_ROWS));
}

/**
 * test_cursor_at_window_bottom_edge_offset_unchanged — 驗證游標在視窗下邊界時 offset 不變。
 *
 * 視窗 [2, 7)，游標恰在終點 6（offset + visible_rows - 1），應回傳 2。
 */
static void test_cursor_at_window_bottom_edge_offset_unchanged() {
    // STEP 01: 檢查游標恰為視窗最後一項時 offset 保持不變
    TEST_ASSERT_EQUAL_UINT16(OFFSET_MID, clampScrollOffset(CURSOR_BOTTOM_EDGE, OFFSET_MID, FIXTURE_VISIBLE_ROWS));
}

// ============================================================
//  游標移出視窗上緣 → 視窗跟著往上捲，游標成為新視窗第一項
// ============================================================

/**
 * test_cursor_above_window_scrolls_up — 驗證游標在視窗上方時視窗往上捲。
 *
 * 視窗 [2, 7)，游標在位置 1（上方），應視窗往上捲至 [1, 6)，回傳 1。
 */
static void test_cursor_above_window_scrolls_up() {
    // STEP 01: 檢查游標高於視窗時視窗跟著往上移
    TEST_ASSERT_EQUAL_UINT16(CURSOR_ABOVE_WINDOW, clampScrollOffset(CURSOR_ABOVE_WINDOW, OFFSET_MID, FIXTURE_VISIBLE_ROWS));
}

/**
 * test_cursor_at_zero_above_nonzero_offset_scrolls_to_zero — 驗證游標在清單頂端時視窗捲至頂端。
 *
 * 視窗起於位置 3，游標在位置 0，應視窗往上捲至 [0, 5)，回傳 0。
 */
static void test_cursor_at_zero_above_nonzero_offset_scrolls_to_zero() {
    // STEP 01: 檢查游標在清單頂端時視窗也捲至頂端
    TEST_ASSERT_EQUAL_UINT16(CURSOR_ZERO, clampScrollOffset(CURSOR_ZERO, OFFSET_HIGH, FIXTURE_VISIBLE_ROWS));
}

// ============================================================
//  游標移出視窗下緣 → 視窗跟著往下捲，游標成為新視窗最後一項
// ============================================================

/**
 * test_cursor_below_window_scrolls_down — 驗證游標在視窗下方時視窗往下捲。
 *
 * 視窗 [2, 7)，游標在位置 7，視窗容不下 → 新視窗 [3, 8)，回傳 3。
 */
static void test_cursor_below_window_scrolls_down() {
    // STEP 01: 檢查游標低於視窗時視窗跟著往下移
    TEST_ASSERT_EQUAL_UINT16(EXPECTED_OFFSET_AFTER_SCROLL_DOWN, clampScrollOffset(CURSOR_BELOW_WINDOW, OFFSET_MID, FIXTURE_VISIBLE_ROWS));
}

/**
 * test_cursor_at_last_item_scrolls_to_show_it_last — 驗證游標在清單末項時視窗往下捲使其可見。
 *
 * 8 項清單（cursor 0~7），游標在位置 7（最後項），視窗在頂端 [0, 5)，
 * 無法容納 → 新視窗 [3, 8)，回傳 3。
 */
static void test_cursor_at_last_item_scrolls_to_show_it_last() {
    // STEP 01: 檢查游標在清單最末時視窗往下捲以顯示
    TEST_ASSERT_EQUAL_UINT16(EXPECTED_OFFSET_AFTER_SCROLL_DOWN, clampScrollOffset(CURSOR_MAX_LIST, OFFSET_ZERO, FIXTURE_VISIBLE_ROWS));
}

// ============================================================
//  邊界：visible_rows 涵蓋全部項目時 offset 保持不變
// ============================================================

/**
 * test_visible_rows_covers_entire_list_offset_unchanged — 驗證視窗足以容納所有項目時 offset 不變。
 *
 * 8 項清單，visible_rows = FIXTURE_VISIBLE_ROWS_FULL_LIST（涵蓋全部），游標在任何
 * 位置時 offset 應維持原值。此測試傳入 offset = OFFSET_ZERO，游標 = CURSOR_MID_WINDOW，
 * 應回傳 OFFSET_ZERO（offset 維持不變）。
 */
static void test_visible_rows_covers_entire_list_offset_unchanged() {
    // STEP 01: 檢查當視窗大小涵蓋整個清單時 offset 保持不變
    TEST_ASSERT_EQUAL_UINT16(OFFSET_ZERO, clampScrollOffset(CURSOR_MID_WINDOW, OFFSET_ZERO, FIXTURE_VISIBLE_ROWS_FULL_LIST));
}

// ============================================================
//  邊界：invalid input（visible_rows == 0）觸發 abort() 中止（fail-fast）
// ============================================================

/**
 * test_visible_rows_zero_triggers_abort — 驗證 visible_rows == 0 時函式以
 * abort() 中止呼叫端行程，而不是靜默回傳看似合法的 offset。
 *
 * visible_rows = 0 是違反函式契約的呼叫端錯誤。用 fork() 讓這個違規呼叫在子行程
 * 執行：子行程若真的被 abort() 中止，會被 SIGABRT 終止而不會執行到後面的
 * _exit(EXIT_CODE_ABORT_DID_NOT_FIRE)；父行程用 waitpid() 檢查子行程的終止方式，
 * 藉此在不讓整個測試二進位檔跟著崩潰的前提下驗證 fail-fast 行為。
 */
static void test_visible_rows_zero_triggers_abort() {
    // STEP 01: fork 出子行程執行違規呼叫，本行程（父行程）保持存活
    pid_t pid = fork();
    TEST_ASSERT_TRUE_MESSAGE(pid >= 0, "fork() failed, cannot run death test");

    if (pid == 0) {
        // STEP 01.01: 子行程——呼叫應觸發 abort() 中止；若沒中止則走到 _exit
        //   標記「abort() 沒有真的發生」這個迴歸情境，讓父行程斷言失敗
        clampScrollOffset(CURSOR_MID_WINDOW, OFFSET_MID, INVALID_VISIBLE_ROWS_ZERO);
        _exit(EXIT_CODE_ABORT_DID_NOT_FIRE);
    }

    // STEP 02: 父行程等子行程結束——EINTR 時重試而非把中斷誤判為子行程終止
    //   方式（waitpid 被訊號打斷回傳 -1 時 status 不會被寫入，直接往下判斷
    //   會用未初始化語意的舊值誤判成「不是被訊號終止」）
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    TEST_ASSERT_EQUAL_INT_MESSAGE(pid, waited, "waitpid() failed unexpectedly");

    // STEP 03: 驗證子行程是被 SIGABRT 訊號終止（abort() 觸發的標準終止方式）。
    //   兩個失敗分支明確區分「abort() 沒觸發（走到哨兵 _exit）」跟「被其他
    //   非 SIGABRT 方式終止」，訊息比單純判斷 WIFSIGNALED 更精確好除錯。
    if (WIFSIGNALED(status)) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(SIGABRT, WTERMSIG(status), "expected SIGABRT from abort() failure");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CODE_ABORT_DID_NOT_FIRE) {
        TEST_FAIL_MESSAGE("child exited normally instead of being aborted — abort() did not fire (regression: contract check bypassed, e.g. by -DNDEBUG)");
    } else {
        TEST_FAIL_MESSAGE("child terminated in an unexpected way (neither SIGABRT nor the abort-did-not-fire sentinel exit code)");
    }
}

// ============================================================
//  邊界：cursor/offset 接近 UINT16_MAX 時防止溢位
// ============================================================

/**
 * test_cursor_offset_near_max_no_overflow — 驗證接近 UINT16_MAX 時比較不會溢位。
 *
 * cursor 和 offset 接近 UINT16_MAX 時，cursor >= offset + visible_rows 的加法會溢位。
 * 函式應改用 cursor - offset >= visible_rows 避免溢位。
 * 此測試：offset 和 cursor 都接近 MAX，cursor - offset == visible_rows（相等也算
 * 超出視窗，屬邊界情況），應往下捲。
 */
static void test_cursor_offset_near_max_no_overflow() {
    // STEP 01: 驗證接近 UINT16_MAX 時不會因為加法溢位而判斷錯誤
    uint16_t high_offset = 65530;    // 接近 UINT16_MAX
    uint16_t high_cursor = 65535;    // UINT16_MAX
    // cursor - offset = 5，而 visible_rows = 5，所以 cursor - offset >= visible_rows（5 >= 5）
    // 應觸發「下捲」邏輯，回傳 cursor - (visible_rows - 1) = 65535 - 4 = 65531
    TEST_ASSERT_EQUAL_UINT16(EXPECTED_OFFSET_NEAR_MAX, clampScrollOffset(high_cursor, high_offset, FIXTURE_VISIBLE_ROWS));
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

    // STEP 02: 執行窗內位置測試
    RUN_TEST(test_cursor_within_window_offset_unchanged);
    RUN_TEST(test_cursor_at_window_top_edge_offset_unchanged);
    RUN_TEST(test_cursor_at_window_bottom_edge_offset_unchanged);

    // STEP 03: 執行視窗上捲測試
    RUN_TEST(test_cursor_above_window_scrolls_up);
    RUN_TEST(test_cursor_at_zero_above_nonzero_offset_scrolls_to_zero);

    // STEP 04: 執行視窗下捲測試
    RUN_TEST(test_cursor_below_window_scrolls_down);
    RUN_TEST(test_cursor_at_last_item_scrolls_to_show_it_last);

    // STEP 05: 執行邊界情境測試
    RUN_TEST(test_visible_rows_covers_entire_list_offset_unchanged);
    RUN_TEST(test_visible_rows_zero_triggers_abort);
    RUN_TEST(test_cursor_offset_near_max_no_overflow);

    // STEP 06: 完成測試並回傳結果
    return UNITY_END();
}
