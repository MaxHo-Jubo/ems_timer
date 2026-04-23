// EMS Timer — 純邏輯函式（Time / Pause correction）
// 不依賴 Arduino.h；可在 native 測試環境直接編譯
//
// Source-of-Truth 對齊狀態（2026-04-23 更新）
// elapsed_ms 欄位已於 2026-04-23 由 PM 正式採方案 A 納入 docs/pm-dev-spec.md §4.5
// event_t 定義中（uint32_t elapsed_ms）。本模組為該欄位的計算權威實作。
// App 端（EMS DoseSync）依賴此欄位顯示任務時間軸；韌體 / BLE dump / LittleFS
// 持久化皆以本函式計算結果為準。
#pragma once
#include <cstdint>

namespace ems {

// 裝置狀態常數（與 main.cpp DeviceState 對齊）
constexpr uint8_t STATE_IDLE    = 0;
constexpr uint8_t STATE_RUNNING = 1;
constexpr uint8_t STATE_PAUSE   = 2;
constexpr uint8_t STATE_END     = 3;

/**
 * 計算任務的有效已進行時間（扣除所有暫停時間）
 * Pure function：完全由參數決定輸出，無副作用、無 millis() 呼叫
 * @param now              目前時間戳（ms）
 * @param taskStartMs      任務開始的時間戳；0 代表任務尚未開始
 * @param pauseStartMs     當前暫停起點（僅 PAUSE 狀態下被讀取）
 * @param totalPausedMs    累計的暫停時間（不含當前這段暫停）
 * @param state            當前裝置狀態（STATE_*）
 * @return elapsed ms；尚未開始任務時回傳 0
 */
uint32_t computeTaskElapsedMs(uint32_t now,
                              uint32_t taskStartMs,
                              uint32_t pauseStartMs,
                              uint32_t totalPausedMs,
                              uint8_t  state);

} // namespace ems
