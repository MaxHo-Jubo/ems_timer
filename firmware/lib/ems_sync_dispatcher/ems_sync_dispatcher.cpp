// EMS DoseSync Pro — Impl-Phase F: case sync 狀態機實作
//
// 對應 header：ems_sync_dispatcher.h
// 對應 test： firmware/test/test_sync_dispatcher/test_main.cpp

#include "ems_sync_dispatcher.h"

namespace ems {

namespace {

// 進入新 state 的共用 helper：更新 state + 重置進入時間。
inline void enter(SyncContext* ctx, SyncState next, uint64_t now_ms) {
    ctx->state = next;
    ctx->last_state_change_ms = now_ms;
}

// 進入 ERROR 的 helper（多處共用）
inline void enter_error(SyncContext* ctx, uint64_t now_ms) {
    enter(ctx, SyncState::ERROR, now_ms);
}

// 進入 IDLE 的 helper（成功完成 / 中止 / ERROR 結束後）
inline void enter_idle(SyncContext* ctx, uint64_t now_ms) {
    enter(ctx, SyncState::IDLE, now_ms);
    ctx->sent_chunks_count = 0;
    ctx->total_chunks = 0;
}

// 共通：BLE 斷線在任一活躍 state 都 → ERROR；IDLE 忽略。
inline bool handle_ble_disconnect(SyncContext* ctx, uint64_t now_ms) {
    if (ctx->state != SyncState::IDLE) {
        enter_error(ctx, now_ms);
        return true;
    }
    return false;
}

// 共通：BACK 鍵在非 SENDING / 非 IDLE 的活躍 state → IDLE。
inline bool handle_back_key(SyncContext* ctx, uint64_t now_ms) {
    if (ctx->state == SyncState::SENDING || ctx->state == SyncState::IDLE) {
        return false;  // 忽略
    }
    enter_idle(ctx, now_ms);
    return true;
}

}  // namespace

void sync_dispatcher_init(SyncContext* ctx, uint64_t now_ms) {
    ctx->state = SyncState::IDLE;
    ctx->last_state_change_ms = now_ms;
    ctx->sent_chunks_count = 0;
    ctx->total_chunks = 0;
    // pairing_code 未 generate 前是 garbage，狀態機保證進 AWAITING_INPUT 才會用到
}

bool sync_dispatcher_dispatch(SyncContext* ctx, SyncEvent event, uint64_t now_ms) {
    // STEP 01: 記住進入前的 state，用於偵測 transition
    const SyncState prev = ctx->state;

    // STEP 02: 全域事件：BLE 斷線 + BACK 鍵 跨 state 處理優先
    if (event == SyncEvent::BLE_DISCONNECTED) {
        handle_ble_disconnect(ctx, now_ms);
        return ctx->state != prev;
    }
    if (event == SyncEvent::BACK_KEY_PRESS) {
        handle_back_key(ctx, now_ms);
        return ctx->state != prev;
    }

    // STEP 03: 按 state 分流 transition
    switch (ctx->state) {
        case SyncState::IDLE:
            // STEP 03.01: IDLE 只接 START，其他事件忽略
            if (event == SyncEvent::START) {
                enter(ctx, SyncState::AWAITING_CONNECT, now_ms);
            }
            break;

        case SyncState::AWAITING_CONNECT:
            // STEP 03.02: 等 BLE 連線；連上 → AWAITING_INPUT（順帶 generate 配對碼）
            if (event == SyncEvent::BLE_CONNECTED) {
                ctx->pairing_code = pairing_generate(now_ms);
                enter(ctx, SyncState::AWAITING_INPUT, now_ms);
            }
            break;

        case SyncState::AWAITING_INPUT:
            // STEP 03.03: 等網頁送配對碼（走 dispatch_input）；TICK 檢查 pairing 過期
            if (event == SyncEvent::TICK && pairing_is_expired(ctx->pairing_code, now_ms)) {
                enter_error(ctx, now_ms);
            }
            break;

        case SyncState::AWAITING_MAIN_KEY:
            // STEP 03.04: 主鍵按下 → SENDING；TICK 檢查 30s timeout
            if (event == SyncEvent::MAIN_KEY_PRESS) {
                ctx->sent_chunks_count = 0;
                enter(ctx, SyncState::SENDING, now_ms);
            } else if (event == SyncEvent::TICK &&
                       now_ms - ctx->last_state_change_ms >= MAIN_KEY_TIMEOUT_MS) {
                enter_error(ctx, now_ms);
            }
            break;

        case SyncState::SENDING:
            // STEP 03.05: chunk acked 推進 count；達 total → DONE
            if (event == SyncEvent::CHUNK_ACKED) {
                ++ctx->sent_chunks_count;
                if (ctx->sent_chunks_count >= ctx->total_chunks) {
                    enter(ctx, SyncState::DONE, now_ms);
                }
            }
            break;

        case SyncState::DONE:
            // STEP 03.06: 顯示 DONE_DISPLAY_MS 後退 IDLE
            if (event == SyncEvent::TICK &&
                now_ms - ctx->last_state_change_ms >= DONE_DISPLAY_MS) {
                enter_idle(ctx, now_ms);
            }
            break;

        case SyncState::ERROR:
            // STEP 03.07: 顯示 ERROR_DISPLAY_MS 後退 IDLE
            if (event == SyncEvent::TICK &&
                now_ms - ctx->last_state_change_ms >= ERROR_DISPLAY_MS) {
                enter_idle(ctx, now_ms);
            }
            break;
    }

    return ctx->state != prev;
}

DispatchInputResult sync_dispatcher_dispatch_input(SyncContext* ctx, const char* input,
                                                   size_t input_len, uint64_t now_ms) {
    // STEP 01: 僅 AWAITING_INPUT 處理輸入；其他 state 回 IgnoredWrongState 給 caller
    if (ctx->state != SyncState::AWAITING_INPUT) {
        return DispatchInputResult::IgnoredWrongState;
    }

    // STEP 02: pairing_verify 成功 → transition + 回 Accepted
    PairingVerifyResult r = pairing_verify(ctx->pairing_code, input, input_len, now_ms);
    if (r == PairingVerifyResult::Ok) {
        enter(ctx, SyncState::AWAITING_MAIN_KEY, now_ms);
        return DispatchInputResult::Accepted;
    }

    // STEP 03: 失敗後 lockout 門檻覆蓋 LockedOut + Mismatch 第 3 次（pairing_verify 先檢
    //          查 lockout 再 +1，所以第 3 次仍回 Mismatch；用 is_locked_out 兜底）
    if (pairing_is_locked_out(ctx->pairing_code)) {
        enter_error(ctx, now_ms);
        return DispatchInputResult::LockedOut;
    }

    // STEP 04: 單次 Mismatch / Expired / InvalidInput 未到 lockout → 保持 AWAITING_INPUT，
    //          等下次輸入或 TICK timeout（120s pairing TTL）處理
    return DispatchInputResult::Rejected;
}

void sync_dispatcher_set_total_chunks(SyncContext* ctx, uint32_t total) {
    ctx->total_chunks = total;
}

}  // namespace ems
