// EMS DoseSync Pro — Impl-Phase F: case sync 狀態機（純函式 + 純資料）
//
// 對應計畫：docs/phase-f-web-validation-plan.md §7.3
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §16.5（雙保險：配對碼 + 主鍵確認）
// 對應 todo：tasks/phase-f-todo.md F-3
// 測試入口：firmware/test/test_sync_dispatcher/test_main.cpp
//
// 用途：
//   協調 ems_storage → ems_pairing → ble_nus 同步流程的狀態機。本 lib 只管純邏輯
//   transition + 計時，BLE / UI / storage 整合層在 src/case_sync_dispatcher.cpp 包裝。
//
// 狀態圖（對齊 SoT §16.5）：
//
//   IDLE
//    → AWAITING_CONNECT   (START)
//    → AWAITING_INPUT     (BLE_CONNECTED；entry action: generate pairing code)
//    → AWAITING_MAIN_KEY  (INPUT 正確)
//    → SENDING            (MAIN_KEY_PRESS；entry action: kick off chunked notify)
//    → DONE               (所有 chunk acked)
//    → IDLE               (DONE 顯示 DONE_DISPLAY_MS 後)
//
//   失敗分支：
//    - AWAITING_INPUT: 120s timeout / 3 次錯碼 lockout → ERROR
//    - AWAITING_MAIN_KEY: 30s timeout → ERROR
//    - 任何非 IDLE state: BLE_DISCONNECTED → ERROR
//    - ERROR: ERROR_DISPLAY_MS 後 → IDLE
//
//   中止：
//    - 非 SENDING state：BACK_KEY_PRESS → IDLE
//    - SENDING state：BACK_KEY 忽略（傳到一半不可半途而廢）

#pragma once

#include <cstddef>
#include <cstdint>

#include "ems_pairing.h"

namespace ems {

// 等主鍵 timeout：30 秒（plan §7.3）
constexpr uint64_t MAIN_KEY_TIMEOUT_MS = 30000;

// DONE 顯示時間：1 秒（plan §7.3 「顯示同步完成 1 秒」）
constexpr uint64_t DONE_DISPLAY_MS = 1000;

// ERROR 顯示時間：2 秒（plan §10 失敗 lockout 提示，留時間給使用者讀）
constexpr uint64_t ERROR_DISPLAY_MS = 2000;

/**
 * 狀態列舉。
 */
enum class SyncState : uint8_t {
    IDLE              = 0,  // 待機
    AWAITING_CONNECT  = 1,  // advertise + 等 BLE 連線
    AWAITING_INPUT    = 2,  // 配對碼已顯示 + 等網頁送回
    AWAITING_MAIN_KEY = 3,  // 碼對了 + 螢幕顯示「按主鍵開始同步」
    SENDING           = 4,  // 推送 JSON chunks 中
    DONE              = 5,  // 同步完成顯示 1 秒
    ERROR             = 6,  // 任一失敗，顯示 2 秒後退回 IDLE
};

/**
 * 事件列舉。
 *
 * 注意：配對碼輸入用 sync_dispatcher_dispatch_input() 另一條 API（需帶 byte buffer），
 * 不用 SyncEvent 入口（避免在 enum 上掛 payload，符合 C-style POD 風格）。
 */
enum class SyncEvent : uint8_t {
    START             = 0,  // 使用者按「同步」入口
    BLE_CONNECTED     = 1,  // BLE 連線成立
    BLE_DISCONNECTED  = 2,  // BLE 斷線
    MAIN_KEY_PRESS    = 3,  // 裝置主鍵 single-press
    BACK_KEY_PRESS    = 4,  // 返回鍵 single-press
    CHUNK_ACKED       = 5,  // App 回 ACK 收到一個 chunk
    TICK              = 6,  // 時間流逝（用於 timeout 判斷）
};

/**
 * dispatch_input 結果列舉。
 *
 * 用以告知 caller 是否被處理，避免 void return 的 silent ignore。
 */
enum class DispatchInputResult : uint8_t {
    Accepted          = 0,  // pairing 正確，已 transition 到 AWAITING_MAIN_KEY
    Rejected          = 1,  // pairing 錯/過期/格式錯 但未到 lockout，仍 AWAITING_INPUT
    LockedOut         = 2,  // 3 次失敗 → 已 transition 到 ERROR
    IgnoredWrongState = 3,  // 當前不在 AWAITING_INPUT，輸入被忽略
};

/**
 * 狀態機 context（純資料）。
 *
 * state                — 當前 state
 * pairing_code         — entry AWAITING_INPUT 時 generate；
 *                        AWAITING_MAIN_KEY 之後 stale 不再讀（保留只是惰性）
 * last_state_change_ms — 進入當前 state 的時間（用於計算 timeout）
 * sent_chunks_count    — SENDING 期間累計 ACK 數；
 *                        ERROR state 期間刻意保留以供 UI 顯示「3/5 已送失敗」；
 *                        ERROR → IDLE 才歸零
 * total_chunks         — SENDING 開始前由 sync_dispatcher_set_total_chunks() 設定；
 *                        歸零時機同 sent_chunks_count
 */
struct SyncContext {
    SyncState   state;
    PairingCode pairing_code;
    uint64_t    last_state_change_ms;
    uint32_t    sent_chunks_count;
    uint32_t    total_chunks;
};

/**
 * 初始化狀態機到 IDLE。
 */
void sync_dispatcher_init(SyncContext* ctx, uint64_t now_ms);

/**
 * 把單一事件丟給狀態機處理。
 *
 * 未定義 transition 不爆 state — 同 state 不變。caller（dispatcher 整合層）負責
 * 把 entry/exit action 接到實際 BLE / UI 副作用（用 sync_dispatcher_state_changed
 * 之類的 hook 或在每次 dispatch 後檢查 ctx.state 變化）。
 *
 * @param ctx     狀態機
 * @param event   事件
 * @param now_ms  當前時間（用於更新 last_state_change_ms + timeout 判斷）
 */
void sync_dispatcher_dispatch(SyncContext* ctx, SyncEvent event, uint64_t now_ms);

/**
 * 配對碼輸入專用 dispatch（payload 帶 byte buffer）。
 *
 * 行為：
 *   - 非 AWAITING_INPUT state：忽略，回 IgnoredWrongState（caller 可用此 log + 拒絕 NUS notify）
 *   - 內部呼叫 pairing_verify()，根據結果 transition + 對應 result：
 *     - Ok        → AWAITING_MAIN_KEY，回 Accepted
 *     - 觸發 lockout → ERROR，回 LockedOut
 *     - 其他失敗 → 保持 AWAITING_INPUT，回 Rejected
 *
 * @param ctx        狀態機
 * @param input      網頁端送來的 byte buffer（可為 nullptr，會被 pairing_verify 視為 InvalidInput）
 * @param input_len  buffer 長度（必須 == PAIRING_CODE_LEN 才會比對）
 * @param now_ms     當前時間
 * @return           DispatchInputResult 列舉值
 */
DispatchInputResult sync_dispatcher_dispatch_input(SyncContext* ctx, const char* input,
                                                   size_t input_len, uint64_t now_ms);

/**
 * 設定 SENDING 期間預期的 total chunks 數（由 caller 算出，本 lib 不算）。
 *
 * 應在 MAIN_KEY_PRESS 之前呼叫，以便 SENDING 期間判斷「全 acked → DONE」。
 *
 * @param ctx     狀態機
 * @param total   chunks 總數
 */
void sync_dispatcher_set_total_chunks(SyncContext* ctx, uint32_t total);

}  // namespace ems
