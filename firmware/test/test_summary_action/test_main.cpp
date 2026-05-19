// EMS DoseSync Pro — summary_action 純函式 unit tests

#include <unity.h>
#include "summary_action.h"

using ems::SubmenuCursor;
using ems::SummaryAction;
using ems::decide_summary_action;

// ============================================================
//  TIMELINE cursor
// ============================================================

/** OHCA 現場 SUMMARY + TIMELINE cursor → TIMELINE_SHOW */
static void test_timeline_ohca_mode_returns_show() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::TIMELINE, false, false, false);
    TEST_ASSERT_EQUAL(SummaryAction::TIMELINE_SHOW, a);
}

/** 歷史模式 SUMMARY + TIMELINE cursor → TIMELINE_NOOP_HISTORY */
static void test_timeline_history_mode_returns_noop() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::TIMELINE, true, false, false);
    TEST_ASSERT_EQUAL(SummaryAction::TIMELINE_NOOP_HISTORY, a);
}

/** 歷史模式 + already_in_sync 仍回 NOOP（timeline 不受 sync 影響） */
static void test_timeline_history_mode_ignores_sync_state() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::TIMELINE, true, true, false);
    TEST_ASSERT_EQUAL(SummaryAction::TIMELINE_NOOP_HISTORY, a);
}

// ============================================================
//  SYNC cursor — 未同步案件
// ============================================================

/** OHCA SUMMARY + SYNC + 未同步 → START_SYNC */
static void test_sync_ohca_not_synced_returns_start() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, false, false, false);
    TEST_ASSERT_EQUAL(SummaryAction::START_SYNC, a);
}

/** 歷史模式 + SYNC + 未同步 → START_SYNC */
static void test_sync_history_not_synced_returns_start() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, true, false, false);
    TEST_ASSERT_EQUAL(SummaryAction::START_SYNC, a);
}

// ============================================================
//  SYNC cursor — 已同步案件（SoT §16.7 再次同步確認）
// ============================================================

/** OHCA + 已同步 → CONFIRM_RESYNC */
static void test_sync_ohca_already_synced_returns_confirm() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, false, false, true);
    TEST_ASSERT_EQUAL(SummaryAction::CONFIRM_RESYNC, a);
}

/** 歷史模式 + 已同步 → CONFIRM_RESYNC */
static void test_sync_history_already_synced_returns_confirm() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, true, false, true);
    TEST_ASSERT_EQUAL(SummaryAction::CONFIRM_RESYNC, a);
}

// ============================================================
//  SYNC cursor — re-entry guard（優先於 already_synced）
// ============================================================

/** 已在 sync 中 + 未同步 → BLOCKED */
static void test_sync_already_in_sync_returns_blocked() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, false, true, false);
    TEST_ASSERT_EQUAL(SummaryAction::SYNC_BLOCKED_REENTRY, a);
}

/** 已在 sync 中 + 已同步 → BLOCKED（re-entry 優先） */
static void test_sync_already_in_sync_and_synced_returns_blocked() {
    SummaryAction a = decide_summary_action(
        SubmenuCursor::SYNC, false, true, true);
    TEST_ASSERT_EQUAL(SummaryAction::SYNC_BLOCKED_REENTRY, a);
}

// ============================================================
//  Unknown cursor 防呆
// ============================================================

/** 非法 cursor 值 → UNKNOWN_CURSOR */
static void test_unknown_cursor_returns_unknown() {
    SummaryAction a = decide_summary_action(
        static_cast<SubmenuCursor>(99), false, false, false);
    TEST_ASSERT_EQUAL(SummaryAction::UNKNOWN_CURSOR, a);
}

// ============================================================
//  Test runner
// ============================================================

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    // TIMELINE
    RUN_TEST(test_timeline_ohca_mode_returns_show);
    RUN_TEST(test_timeline_history_mode_returns_noop);
    RUN_TEST(test_timeline_history_mode_ignores_sync_state);
    // SYNC — not synced
    RUN_TEST(test_sync_ohca_not_synced_returns_start);
    RUN_TEST(test_sync_history_not_synced_returns_start);
    // SYNC — already synced (§16.7)
    RUN_TEST(test_sync_ohca_already_synced_returns_confirm);
    RUN_TEST(test_sync_history_already_synced_returns_confirm);
    // SYNC — re-entry guard
    RUN_TEST(test_sync_already_in_sync_returns_blocked);
    RUN_TEST(test_sync_already_in_sync_and_synced_returns_blocked);
    // Unknown
    RUN_TEST(test_unknown_cursor_returns_unknown);
    return UNITY_END();
}
