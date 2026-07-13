// EMS DoseSync Pro — Phase A: OHCA 子狀態機實作
// 對應 header：ems_ohca_state.h
#include "ems_ohca_state.h"

namespace ems {

ohca_phase_t mapStateToPhase(ohca_state_t s) {
    switch (s) {
        case OHCA_STATE_WAIT_FIRST_EPI: return OHCA_PHASE_WAIT_FIRST_EPI;
        case OHCA_STATE_COUNTDOWN:      return OHCA_PHASE_COUNTDOWN;
        case OHCA_STATE_WARNING:        return OHCA_PHASE_WARNING;
        case OHCA_STATE_ALARMING:       return OHCA_PHASE_ALARMING;
        case OHCA_STATE_OVERTIME:       return OHCA_PHASE_OVERTIME;
        // 其他 state（MAIN_MENU / START_FLASH / END_CHECK / LOCKED / SUMMARY）
        // 與 EPI 計時無關，回傳 WAIT_FIRST_EPI 視為「無計時推進」
        default: return OHCA_PHASE_WAIT_FIRST_EPI;
    }
}

ohca_state_t mapPhaseToState(ohca_phase_t p) {
    switch (p) {
        case OHCA_PHASE_WAIT_FIRST_EPI: return OHCA_STATE_WAIT_FIRST_EPI;
        case OHCA_PHASE_COUNTDOWN:      return OHCA_STATE_COUNTDOWN;
        case OHCA_PHASE_WARNING:        return OHCA_STATE_WARNING;
        case OHCA_PHASE_ALARMING:       return OHCA_STATE_ALARMING;
        case OHCA_PHASE_OVERTIME:       return OHCA_STATE_OVERTIME;
    }
    return OHCA_STATE_WAIT_FIRST_EPI;
}

ohca_state_t nextOhcaState(ohca_state_t current,
                           ohca_event_t event,
                           uint32_t     since_last_epi_ms,
                           ohca_state_t end_check_source) {
    // STEP 01: LOCKED 拒絕所有事件，但允許翻頁到 SUMMARY
    if (current == OHCA_STATE_LOCKED) {
        if (event == OHCA_EVT_TO_SUMMARY) {
            return OHCA_STATE_SUMMARY;
        }
        return OHCA_STATE_LOCKED;
    }

    // STEP 02: SUMMARY 為終端狀態（V1 不提供回退）
    if (current == OHCA_STATE_SUMMARY) {
        return OHCA_STATE_SUMMARY;
    }

    // STEP 03: 啟動倒數白名單（§23.2）
    //   電擊 / Amio 確認絕對不重啟倒數，無論在哪個 state
    if (event == OHCA_EVT_SHOCK_CONFIRMED || event == OHCA_EVT_AMIO_CONFIRMED) {
        return current;
    }

    // STEP 04: 各 state 個別 transition
    switch (current) {
        // STEP 04.01: MAIN_MENU
        case OHCA_STATE_MAIN_MENU:
            if (event == OHCA_EVT_MAIN_BTN_SHORT) {
                return OHCA_STATE_START_FLASH;
            }
            return current;

        // STEP 04.02: START_FLASH（1s 提示，自動轉 WAIT_FIRST_EPI）
        //   過場期間使用者沒機會長按；不接受 MAIN_BTN_LONG_3S
        case OHCA_STATE_START_FLASH:
            if (event == OHCA_EVT_FLASH_TIMEOUT) {
                return OHCA_STATE_WAIT_FIRST_EPI;
            }
            return current;

        // STEP 04.03: WAIT_FIRST_EPI（待第一次本機 EPI）
        case OHCA_STATE_WAIT_FIRST_EPI:
            if (event == OHCA_EVT_EPI_CONFIRMED) {
                return OHCA_STATE_COUNTDOWN;
            }
            // STEP 04.03.01: 主鍵長按 3s → 結束前檢查（SoT V1 §10.1：正式 OHCA 中皆可）
            if (event == OHCA_EVT_MAIN_BTN_LONG_3S) {
                return OHCA_STATE_END_CHECK;
            }
            return current;  // TIMER_TICK 不推進；其他事件 noop

        // STEP 04.04: 倒數 group（COUNTDOWN / WARNING / ALARMING / OVERTIME）
        case OHCA_STATE_COUNTDOWN:
        case OHCA_STATE_WARNING:
        case OHCA_STATE_ALARMING:
        case OHCA_STATE_OVERTIME:
            // STEP 04.04.01: 本機 EPI 確認 → 重啟倒數（caller 須將 since 重置為 0）
            if (event == OHCA_EVT_EPI_CONFIRMED) {
                return OHCA_STATE_COUNTDOWN;
            }
            // STEP 04.04.02: 計時推進 → delegate 至 advanceOhcaPhase
            //   註：live tick 走 ohca_logic.cpp updateOhcaTick（以 activeEpiCycleMs 為單一真相）；
            //       此 TIMER_TICK 分支目前僅 unit test 呼叫，故傳預設週期
            if (event == OHCA_EVT_TIMER_TICK) {
                ohca_phase_t cur_phase  = mapStateToPhase(current);
                ohca_phase_t next_phase = advanceOhcaPhase(cur_phase, since_last_epi_ms, OHCA_EPI_CYCLE_MS_DEFAULT);
                return mapPhaseToState(next_phase);
            }
            // STEP 04.04.03: 主鍵長按 3s → 結束前檢查（SoT V1 §10.1：任意進行中 phase）
            if (event == OHCA_EVT_MAIN_BTN_LONG_3S) {
                return OHCA_STATE_END_CHECK;
            }
            // STEP 04.04.04: 其他事件（含 MAIN_BTN_SHORT、SHOCK/AMIO 已於 STEP 03 攔截）noop
            //   ALARMING / OVERTIME 的主鍵短按消音由 caller (main.cpp) 處理蜂鳴器，state 不轉
            return current;

        // STEP 04.05: END_CHECK（結束前檢查）
        case OHCA_STATE_END_CHECK:
            if (event == OHCA_EVT_END_CONFIRM) {
                return OHCA_STATE_LOCKED;
            }
            if (event == OHCA_EVT_END_CANCEL) {
                // STEP 04.05.01: 還原到 caller 提供的 source state
                //   非法值 fallback OVERTIME — 確保 END_CANCEL 永遠落在合法進行中 phase
                //   即使 caller 傳 garbage / 未初始化也不會把案件推進非法 state
                return isOhcaInProgress(end_check_source) ? end_check_source
                                                          : OHCA_STATE_OVERTIME;
            }
            // STEP 04.05.02: 本機 EPI 確認可在 END_CHECK 中重啟倒數（不應該被 END_CHECK 卡住）
            if (event == OHCA_EVT_EPI_CONFIRMED) {
                return OHCA_STATE_COUNTDOWN;
            }
            return current;

        default:
            return current;
    }
}

OhcaVentState ohcaVentAfterTransition(ohca_state_t prev,
                                      ohca_state_t next,
                                      OhcaVentState current) {
    // V1 §14.12：首次進 LOCKED（案件結束）→ 停 6 秒給氣：overlay 關、暫停清、timer 歸零。
    // 只在「首次進」清（prev != LOCKED）；LOCKED→LOCKED / LOCKED→SUMMARY / 其他轉移都不動，
    // 避免在 LOCKED 內每 tick 重清、或離開 LOCKED 時誤清（B5 2026-05-25 修正的不變式）。
    if (next == OHCA_STATE_LOCKED && prev != OHCA_STATE_LOCKED) {
        return {false, false, 0};
    }
    return current;
}

} // namespace ems
