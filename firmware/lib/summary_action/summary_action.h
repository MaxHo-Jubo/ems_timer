// EMS DoseSync Pro — summary sub-menu 決策純函式
//
// 從 main.cpp handleSummarySubmenuPrimary 抽出純邏輯，
// 讓 dispatch 決策可 unit test（不依賴 globalState / Serial / BLE 等副作用）。
// main.cpp caller 根據回傳 action 執行副作用。

#pragma once

#include <cstdint>

namespace ems {

/** sub-menu cursor 值（對齊 main.cpp SummarySubmenuItem） */
enum class SubmenuCursor : uint8_t {
    TIMELINE = 0,
    SYNC     = 1,
};

/** decideSummaryAction 回傳的動作列舉 */
enum class SummaryAction : uint8_t {
    TIMELINE_SHOW          = 0,  // 開啟事件時間軸子畫面
    TIMELINE_NOOP_HISTORY  = 1,  // 歷史模式 timeline 未實作，noop
    START_SYNC             = 2,  // 進入同步流程（GLOBAL_SYNC + dispatch START）
    CONFIRM_RESYNC         = 3,  // 已同步案件再次同步 → 顯示確認 dialog（SoT §16.7）
    SYNC_BLOCKED_REENTRY   = 4,  // 已在 GLOBAL_SYNC 期間，re-entry 擋下
    UNKNOWN_CURSOR         = 5,  // 未知 cursor 值（enum 擴充防呆）
};

/**
 * 決定 summary sub-menu 主鍵按下後的動作。
 *
 * @param cursor         當前 sub-menu 游標
 * @param history_mode   是否為歷史模式 SUMMARY（vs OHCA 現場結束 SUMMARY）
 * @param already_in_sync 當前 globalState 是否已是 GLOBAL_SYNC
 * @param already_synced  該案件已同步過（synced_at_ms >= EPOCH_FLOOR）
 * @return SummaryAction 告知 caller 該執行哪個副作用
 */
SummaryAction decide_summary_action(SubmenuCursor cursor,
                                    bool history_mode,
                                    bool already_in_sync,
                                    bool already_synced);

/**
 * 同步結束後的返回目的地（narrow type，取代 GlobalState 全集）。
 *
 * GlobalState 有 7 個值，但合法 return 目的地只有 3 個。
 * 使用 narrow enum 讓編譯器擋住非法值（如 GLOBAL_SYNC 自指）。
 */
enum class SyncReturnTo : uint8_t {
    OHCA    = 0,  // OHCA 現場結束 SUMMARY
    HISTORY = 1,  // 歷史模式 SUMMARY
    MAIN    = 2,  // 主功能表（fallback 保險）
};

}  // namespace ems
