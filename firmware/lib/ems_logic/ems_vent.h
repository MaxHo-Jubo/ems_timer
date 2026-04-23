// EMS Timer — 通氣節拍器決策純函式
// 不依賴 Arduino.h / millis()，可在 native 測試環境直接編譯
//
// 對應 PM 規格：
//   pm-dev-spec.md §4.2 節律提醒引擎 — 6 秒 CPR 節拍 ±50ms（僅通氣模式啟用）
//   pm-flow-spec.md §2 節律提醒循環
//
// 設計：抽出「何時該 BEEP」的純邏輯，主迴圈 updateVentMetronome() 只負責
//       guard（模式/狀態）+ side effect（triggerBeep / 更新 lastTickMs）
#pragma once
#include <cstdint>

namespace ems {

/** 通氣節拍週期（PM 規格：每 6 秒） */
constexpr uint32_t DEFAULT_VENT_TICK_MS = 6000UL;

/**
 * 節拍決策輸出：告訴呼叫方本 cycle 是否要觸發 BEEP
 * 呼叫方在 fireBeep 為 true 時：(1) triggerBeep (2) 將 lastTickMs 更新為 now
 */
struct VentTickAction {
    bool fireBeep;  // 本 cycle 需觸發 BEEP（短嗶 + 可選震動）
};

/**
 * 通氣節拍器決策：根據當前時間與上次 tick 時間，判斷本 cycle 是否要 fire
 * 純函式：無副作用、不呼叫 millis()、不印 Serial；完全由參數決定輸出
 *
 * 行為規則：
 *   - lastTickMs == 0 → 節拍器剛啟動 / 剛從 PAUSE 恢復，立即 fire 第一次 tick
 *   - now - lastTickMs >= tickIntervalMs → 到週期，fire
 *   - 其他 → 不 fire
 *
 * millis() wraparound：uint32_t 減法會自動處理（49.7 天週期無縫銜接）。
 *
 * @param now             目前時間戳（ms）
 * @param lastTickMs      上次 tick 時間戳；0 代表節拍器尚未啟動
 * @param tickIntervalMs  tick 週期（預設 6000ms，±50ms 精度由呼叫頻率保證）
 * @return VentTickAction 本 cycle 行為
 */
VentTickAction decideVentTickAction(
    uint32_t now,
    uint32_t lastTickMs,
    uint32_t tickIntervalMs = DEFAULT_VENT_TICK_MS);

} // namespace ems
