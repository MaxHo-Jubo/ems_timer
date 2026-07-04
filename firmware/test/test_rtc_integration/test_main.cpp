// Integration test: RTC backend × time_sync × case start/end epoch — Wave 3
// 執行：pio test -e native -f test_rtc_integration
//
// 對齊 plan：docs/ds3231-integration-plan.md §4.1（boot seed）/ §5.1（write-back）/ §8 Wave 3
//
// 涵蓋（以 MockRtcBackend 模擬 DS3231）：
//   1. MockBackend 基本契約（advance / set_present / set_epoch round-trip）
//   2. Boot seed 三分支（ems_rtc_glue::rtc_try_seed）：
//        (a) backend 在線 + RTC 已有有效時間 → Seeded → current_epoch_ms 回 RTC 時間
//        (b) backend 在線 + RTC 未設過（now() !valid）→ NotSet → current_epoch_ms 回 0
//        (c) backend 不在線（Null）→ NotSet → current_epoch_ms 回 0
//   3. BLE write-back（ems_rtc_glue::rtc_write_back）：Applied 後反向寫回 RTC

#include <unity.h>

#include "ems_rtc.h"
#include "ems_rtc_glue.h"
#include "null_backend.h"
#include "ems_time_sync.h"
#include "mock_rtc_backend.h"

using namespace ems;

// 共用 fixture
static TimeSyncState g_state;
static MockRtcBackend g_mock;

void setUp() {
    time_sync_init(&g_state);
    g_mock = MockRtcBackend{};  // reset to defaults（present=true, reading invalid）
}

void tearDown() {}

// ============================================================
// §1 MockBackend 基本契約
// ============================================================

static void test_mock_backend_basic_contract() {
    MockRtcBackend mock;
    TEST_ASSERT_TRUE(mock.is_present());       // 預設在線
    TEST_ASSERT_FALSE(mock.now().valid);       // 預設 present 但未設有效時間

    TEST_ASSERT_EQUAL(SetResult::Ok, mock.set_epoch_ms(1713715200000ULL));
    TEST_ASSERT_TRUE(mock.now().valid);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, mock.now().epoch_ms);

    mock.advance_ms(5000);
    TEST_ASSERT_EQUAL_UINT64(1713715205000ULL, mock.now().epoch_ms);

    mock.set_present(false);
    TEST_ASSERT_FALSE(mock.is_present());
    TEST_ASSERT_EQUAL(SetResult::NotPresent, mock.set_epoch_ms(1713720000000ULL));  // absent → NotPresent
}

// ============================================================
// §2 Boot seed 三分支
// ============================================================

static void test_boot_present_and_set_seeds_software_clock() {
    g_mock.set_present(true);
    g_mock.set_reading(true, 1713715200000ULL);  // 2024-04-21 UTC，已設有效時間

    const RtcSeedResult seed = rtc_try_seed(
        g_mock, &g_state, /*now_millis=*/2000,
        TIME_SYNC_MIN_EPOCH_MS, TIME_SYNC_MAX_EPOCH_MS);

    TEST_ASSERT_EQUAL(RtcSeedOutcome::Seeded, seed.outcome);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, seed.epoch_ms);
    TEST_ASSERT_TRUE(g_state.synced);
    // current_epoch_ms 在同一 millis=2000 應等於 RTC 讀值
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             time_sync_current_epoch_ms(&g_state, 2000));
    // 模擬 case 開始於 millis=12000（boot 後 10 秒）
    TEST_ASSERT_EQUAL_UINT64(1713715210000ULL,
                             time_sync_current_epoch_ms(&g_state, 12000));
}

static void test_boot_present_but_unset_does_not_seed() {
    g_mock.set_present(true);
    // now() 預設 {valid=false,0}（模擬 DS3231 在線但未設過時間）

    const RtcSeedResult seed = rtc_try_seed(
        g_mock, &g_state, /*now_millis=*/2000,
        TIME_SYNC_MIN_EPOCH_MS, TIME_SYNC_MAX_EPOCH_MS);

    TEST_ASSERT_EQUAL(RtcSeedOutcome::NotSet, seed.outcome);
    TEST_ASSERT_FALSE(g_state.synced);
    // current_epoch_ms 回 0（spec §4.1 未對時 fallback）
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 12000));
}

static void test_boot_not_present_does_not_seed() {
    NullRtcBackend null_backend;

    const RtcSeedResult seed = rtc_try_seed(
        null_backend, &g_state, /*now_millis=*/2000,
        TIME_SYNC_MIN_EPOCH_MS, TIME_SYNC_MAX_EPOCH_MS);

    // Null.now() → {valid=false} → NotSet（不在線不 seed）
    TEST_ASSERT_EQUAL(RtcSeedOutcome::NotSet, seed.outcome);
    TEST_ASSERT_FALSE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(0, time_sync_current_epoch_ms(&g_state, 12000));
}

// ============================================================
// §3 BLE write-back：Applied 後 rtc_write_back 反向寫回 RTC（對齊 plan §5.1）
//    R1 後直接測 production helper ems_rtc_glue::rtc_write_back（非測試複製品）。
//    對時用 time_sync_seed_from_rtc 建立與 BLE Applied 同等的 synced state
//    （同一條 offset 公式），聚焦驗證 write-back 行為。
// ============================================================

static void test_ble_apply_writes_back_to_present_rtc() {
    g_mock.set_present(true);
    g_mock.set_reading(false, 0);  // DS3231 present 但未設過

    // 對時（apply at millis=1000）
    time_sync_seed_from_rtc(&g_state, 1713715200000ULL, /*apply_millis=*/1000);

    // production write-back helper（寫回時刻 millis=1000）
    const RtcWriteBackResult wb =
        rtc_write_back(&g_mock, &g_state, /*now_millis=*/1000);

    TEST_ASSERT_EQUAL(SetResult::Ok, wb.result);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, wb.epoch_ms);
    // 寫回後 RTC 應 valid 且值 = 對時當下 epoch
    TEST_ASSERT_TRUE(g_mock.now().valid);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL, g_mock.now().epoch_ms);
}

static void test_ble_apply_write_back_skipped_when_backend_absent() {
    NullRtcBackend null_backend;
    time_sync_seed_from_rtc(&g_state, 1713715200000ULL, /*apply_millis=*/1000);

    const RtcWriteBackResult wb =
        rtc_write_back(&null_backend, &g_state, /*now_millis=*/1000);

    // 對時仍生效（state synced），只是 RTC 寫回 no-op → NotPresent
    TEST_ASSERT_EQUAL(SetResult::NotPresent, wb.result);
    TEST_ASSERT_TRUE(g_state.synced);
    TEST_ASSERT_EQUAL_UINT64(1713715200000ULL,
                             time_sync_current_epoch_ms(&g_state, 1000));
}

static void test_ble_apply_write_back_tolerates_millis_drift_between_two_reads() {
    // R6：production main.cpp 的 time_sync_handle 與 rtc_write_back 各讀一次 millis()，
    //     兩次之間 main loop 已前進。rtc_write_back 用 caller 傳入的「寫回時刻」millis
    //     算 current_epoch_ms；若有人改成重用 apply 時的值 → 寫回落後 → 此 test 會紅。
    g_mock.set_present(true);
    g_mock.set_reading(false, 0);  // DS3231 present 但未設過

    // 對時 apply 在 millis=1000
    time_sync_seed_from_rtc(&g_state, 1713715200000ULL, /*apply_millis=*/1000);
    // write-back 讀「第二次」millis=1005（loop 已前進 5ms），走 production helper
    const RtcWriteBackResult wb =
        rtc_write_back(&g_mock, &g_state, /*writeback_millis=*/1005);

    TEST_ASSERT_EQUAL(SetResult::Ok, wb.result);
    // 寫回值反映 write-back 當下 = epoch + (1005 - 1000) = 1713715200005（單調前進 5ms）
    TEST_ASSERT_EQUAL_UINT64(1713715200005ULL, wb.epoch_ms);
    TEST_ASSERT_EQUAL_UINT64(1713715200005ULL, g_mock.now().epoch_ms);
}

// ============================================================
// §4 案件起訖 epoch 捕捉矩陣（R4：pr-test-analyzer #2 partial-sync gap）
//    鏡射 src/ohca_logic.cpp:59-74 存檔路徑：
//      start = caseStartEpochMs（案件開始時捕捉並存住，後續對時不回填）
//      end   = time_sync_current_epoch_ms(state, millis())（進 LOCKED live 捕捉）
//      unsynced_warn = (start==0 || end==0) → 提醒 App 靠 elapsed_ms 重建
//    R1 抽出 lib helper 後，本段可改為直接呼叫該 helper。
// ============================================================

struct CaseEpochs {
    uint64_t start_ms;       // 案件開始捕捉值（已存住，不隨後續對時改變）
    uint64_t end_ms;         // 進 LOCKED 時 live 捕捉值
    bool     unsynced_warn;  // 任一為 0 → 未對時警告
};

// case_start_captured = 案件「開始當下」已捕捉並存住的 epoch（後續對時不回填）
static CaseEpochs make_case_epochs(uint64_t case_start_captured,
                                   TimeSyncState* state,
                                   uint64_t end_millis) {
    const uint64_t end_ms = time_sync_current_epoch_ms(state, end_millis);
    return {case_start_captured, end_ms,
            (case_start_captured == 0 || end_ms == 0)};
}

static void test_case_epochs_both_unsynced_saves_zero_pair_and_warns() {
    // 狀態 1/3：全程未對時 → start 捕捉=0、end live=0，warn 觸發
    const uint64_t start = time_sync_current_epoch_ms(&g_state, /*t_start=*/3000);
    const CaseEpochs e = make_case_epochs(start, &g_state, /*end=*/20000);

    TEST_ASSERT_EQUAL_UINT64(0, e.start_ms);
    TEST_ASSERT_EQUAL_UINT64(0, e.end_ms);
    TEST_ASSERT_TRUE(e.unsynced_warn);
}

static void test_case_epochs_fully_synced_saves_real_pair_no_warn() {
    // 狀態 2/3：case 前已對時（seed millis=1000 / epoch=1713715200000）
    time_sync_seed_from_rtc(&g_state, 1713715200000ULL, 1000);
    const uint64_t start = time_sync_current_epoch_ms(&g_state, /*t_start=*/3000);
    const CaseEpochs e = make_case_epochs(start, &g_state, /*end=*/20000);

    TEST_ASSERT_EQUAL_UINT64(1713715202000ULL, e.start_ms);  // offset 1713715199000 + 3000
    TEST_ASSERT_EQUAL_UINT64(1713715219000ULL, e.end_ms);    // offset 1713715199000 + 20000
    TEST_ASSERT_FALSE(e.unsynced_warn);
    TEST_ASSERT_TRUE(e.end_ms > e.start_ms);  // 結束晚於開始（單調）
}

static void test_case_epochs_partial_sync_start_zero_end_real_warns() {
    // 狀態 3/3 ⭐gap：case 開始未對時（start=0），進行中 BLE 對時抵達，結束已對時
    const uint64_t start = time_sync_current_epoch_ms(&g_state, /*t_start=*/3000);
    TEST_ASSERT_EQUAL_UINT64(0, start);  // 開始當下未對時

    // 案件進行中對時抵達（apply at millis=10000 → offset 1713715190000）
    time_sync_seed_from_rtc(&g_state, 1713715200000ULL, 10000);

    const CaseEpochs e = make_case_epochs(start, &g_state, /*end=*/20000);
    TEST_ASSERT_EQUAL_UINT64(0, e.start_ms);               // start 仍是開始捕捉的 0（不回填）
    TEST_ASSERT_EQUAL_UINT64(1713715210000ULL, e.end_ms);  // offset 1713715190000 + 20000，live 真實 epoch
    TEST_ASSERT_TRUE(e.unsynced_warn);                     // start==0 → warn
    // 註：反向（start 已對時、end 未對時）不可達 — 對時一旦成立即單調保持至案件結束
}

// ============================================================
// §5 RtcReading valid 與 floor/ceiling 正交（#8：R2 sentinel contract 網）
//    舊 now_epoch_ms()==0 sentinel 把「未設時間」與「合法 epoch」conflate。
//    RtcReading.valid 專表「有無有效時間」，與範圍檢查是兩件事。此 test 釘死
//    「!valid ≠ below-floor」，給 R2/未來重構一個必須保住的語意目標。
// ============================================================

static void test_rtc_reading_invalid_is_distinct_from_below_floor() {
    // (a) present 但未設 → !valid → NotSet（走「未設時間」分支）
    g_mock.set_present(true);
    g_mock.set_reading(false, 0);
    TEST_ASSERT_FALSE(g_mock.now().valid);
    const RtcSeedResult a = rtc_try_seed(
        g_mock, &g_state, /*now_millis=*/2000,
        TIME_SYNC_MIN_EPOCH_MS, TIME_SYNC_MAX_EPOCH_MS);
    TEST_ASSERT_EQUAL(RtcSeedOutcome::NotSet, a.outcome);
    TEST_ASSERT_FALSE(g_state.synced);

    // (b) present 且「有讀到時間」但越界（below floor，1970 epoch=1000）
    //     → valid=TRUE（關鍵：below-floor 仍算有效讀值，非 sentinel），僅被範圍
    //       檢查擋下 → 得 OutOfRange，與 (a) 的 NotSet 是不同 outcome
    time_sync_init(&g_state);
    g_mock.set_reading(true, 1000);
    TEST_ASSERT_TRUE(g_mock.now().valid);
    const RtcSeedResult b = rtc_try_seed(
        g_mock, &g_state, /*now_millis=*/2000,
        TIME_SYNC_MIN_EPOCH_MS, TIME_SYNC_MAX_EPOCH_MS);
    TEST_ASSERT_EQUAL(RtcSeedOutcome::OutOfRange, b.outcome);  // ★ valid 但越界，非 NotSet
    TEST_ASSERT_FALSE(g_state.synced);
}

// ============================================================
// main
// ============================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    RUN_TEST(test_mock_backend_basic_contract);

    RUN_TEST(test_boot_present_and_set_seeds_software_clock);
    RUN_TEST(test_boot_present_but_unset_does_not_seed);
    RUN_TEST(test_boot_not_present_does_not_seed);

    RUN_TEST(test_ble_apply_writes_back_to_present_rtc);
    RUN_TEST(test_ble_apply_write_back_skipped_when_backend_absent);
    RUN_TEST(test_ble_apply_write_back_tolerates_millis_drift_between_two_reads);

    RUN_TEST(test_case_epochs_both_unsynced_saves_zero_pair_and_warns);
    RUN_TEST(test_case_epochs_fully_synced_saves_real_pair_no_warn);
    RUN_TEST(test_case_epochs_partial_sync_start_zero_end_real_warns);

    RUN_TEST(test_rtc_reading_invalid_is_distinct_from_below_floor);

    return UNITY_END();
}
