#include "app_globals.h"


/**
 * 清除 OHCA 內 6 秒給氣狀態（V1 §14.12 案件結束 → 自動停止）。
 * 不清 ventStartMs 會讓 captureDisplaySnapshot 持續算 ventBeat → snapshot
 * dedup miss → LOCKED/SUMMARY 畫面每秒全螢幕重畫造成視覺閃爍。
 */
static void resetOhcaVentState() {
    ohcaVentOverlayEnabled = false;
    ohcaVentPaused         = false;
    ventStartMs            = 0;
}


// ============================================================
// OHCA event dispatcher（包裝 nextOhcaState）
// ============================================================

/**
 * OHCA 事件分派器：包裝 nextOhcaState，並處理 END_CHECK source 快取、
 * 跨 session 隔離與 Phase E 進 LOCKED 時的案件持久化（含失敗自動重試）。
 * @param event     OHCA 事件類型（按鍵長按、計時 tick、確認/取消等）
 * @param since_ms  事件相對時間（毫秒），交由 nextOhcaState 判斷狀態轉移
 */
void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms) {
    // 進 END_CHECK 前的 source state — END_CANCEL 取消時用此還原。
    // SoT V1 §10.1：source 不一定是 OVERTIME；於此處由 prev 自動 cache，caller 無需感知。
    static ohca_state_t end_check_source = OHCA_STATE_OVERTIME;
    ohca_state_t prev = ohcaState;

    // STEP 01: 新 OHCA case 起始 dispatch (prev = MAIN_MENU) 時 reset cache
    //   避免上次出勤殘留的 source 被本次 END_CANCEL 誤用（跨 session 隔離）
    if (prev == OHCA_STATE_MAIN_MENU) {
        end_check_source = OHCA_STATE_OVERTIME;
    }

    // STEP 02: 進 END_CHECK 前 capture 合法 source；非進行中 prev 不寫入 cache
    if (event == OHCA_EVT_MAIN_BTN_LONG_3S && isOhcaInProgress(prev)) {
        end_check_source = prev;
    }

    ohcaState = nextOhcaState(prev, event, since_ms, end_check_source);
    if (ohcaState != prev) {
        Serial.printf("[OHCA] %u -> %u (evt=%u)\n", prev, ohcaState, event);
    }

    // STEP 03: END_CANCEL 命中無效 source — lib 已 fallback OVERTIME，於此 log 供 field debug
    if (event == OHCA_EVT_END_CANCEL && !isOhcaInProgress(end_check_source)) {
        Serial.printf("[OHCA] WARN END_CANCEL invalid source=%u, fallback OVERTIME\n",
                      end_check_source);
    }

    // STEP 04: Phase E — 首次進 LOCKED 持久化案件，離開 LOCKED 時 reset 防重複旗標
    if (ohcaState == OHCA_STATE_LOCKED
        && prev != OHCA_STATE_LOCKED
        && g_storage_ready
        && !g_locked_saved) {
        // 起訖時戳：未對時 → 0；RTC seed 或 BLE Apply 後 → 真實 epoch
        // （見 docs/ds3231-integration-plan.md §6.1 / spec §4.1）
        const uint64_t case_start_epoch = caseStartEpochMs;  // 取自 input_handler.cpp:110
        const uint64_t case_end_epoch   =
            ems::time_sync_current_epoch_ms(&g_ts_state, millis());
        // 警告救護員：對時前發生的 case 仍存到雲端但時戳為 0，App 端需用 elapsed_ms 重建
        if (case_start_epoch == 0 || case_end_epoch == 0) {
            Serial.printf("[STORAGE] WARN unsynced case saved with epoch=0 "
                          "(start=%llu end=%llu); App 將靠 elapsed_ms 重建時間\n",
                          (unsigned long long)case_start_epoch,
                          (unsigned long long)case_end_epoch);
        }
        bool ok = storage_save_case(&g_storage_be, EMS_CASE_TYPE_OHCA,
                                    events, eventCount,
                                    case_start_epoch,
                                    case_end_epoch);
        Serial.printf("[STORAGE] save case (%u events) %s\n",
                      eventCount, ok ? "OK" : "FAILED");
        // C-4 最小修：只有成功才設 true。原本 unconditional = true 導致失敗永不重試 +
        // 救護員以為紀錄齊全（silent data loss）。失敗時 g_locked_saved 留 false，
        // 下個 tick 進來條件仍成立 → 自動 retry。
        // 完整修法（紅色 UI 警告 + 蜂鳴 + 重試按鈕 + 失敗哲學 A/B/C）需 PM 對齊，
        // 見 tasks/todo.md §🔧 Group 2C C-4。
        g_locked_saved = ok;
        if (!ok) {
            Serial.println("[STORAGE] WARN save failed; will auto-retry next tick "
                           "(C-4 minimal fix; PM-aligned UI feedback TBD)");
        }
    }
    if (ohcaState != OHCA_STATE_LOCKED
        && ohcaState != OHCA_STATE_SUMMARY) {
        g_locked_saved = false;
    }

    // STEP 05: V1 §14.12 首次進 LOCKED = 案件結束 → 自動停止 6 秒給氣 timer。
    if (ohcaState == OHCA_STATE_LOCKED && prev != OHCA_STATE_LOCKED) {
        resetOhcaVentState();
    }
}

// ============================================================
// OHCA tick driver：START_FLASH timeout + COUNTDOWN/.../OVERTIME 推進
// ============================================================

/** OHCA tick 驅動：處理 START_FLASH 1 秒逾時，並推進 COUNTDOWN/WARNING/ALARMING/OVERTIME 倒數階段與輸出 */
void updateOhcaTick() {
    if (globalState != GLOBAL_OHCA) return;
    uint32_t now = millis();

    // STEP 01: START_FLASH 1s timeout
    if (ohcaState == OHCA_STATE_START_FLASH) {
        if (now - startFlashStartMs >= START_FLASH_DURATION_MS) {
            dispatchOhcaEvent(OHCA_EVT_FLASH_TIMEOUT, 0);
        }
        return;
    }

    // STEP 02: 倒數 group — 計算 since 並 dispatch TIMER_TICK + 套用輸出
    if (ohcaState == OHCA_STATE_COUNTDOWN  ||
        ohcaState == OHCA_STATE_WARNING    ||
        ohcaState == OHCA_STATE_ALARMING   ||
        ohcaState == OHCA_STATE_OVERTIME) {

        uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);

        // STEP 02.01: 取出當前 phase，推進 phase
        ohca_phase_t curPhase = mapStateToPhase(ohcaState);
        ohca_phase_t newPhase = advanceOhcaPhase(curPhase, since);

        // STEP 02.02: 計算輸出（含邊緣偵測）
        ohca_output_t out = decideOhcaOutput(newPhase, ohcaPrevSinceMs, since);
        applyOhcaOutput(out);

        // STEP 02.03: 同步狀態機
        ohca_state_t mapped = mapPhaseToState(newPhase);
        if (mapped != ohcaState) {
            Serial.printf("[OHCA] phase auto: %u -> %u (since=%u)\n",
                          ohcaState, mapped, since);
            ohcaState = mapped;
            // STEP 02.04: 進入新 phase 重置消音標記（讓新階段提示能再出現）
            if (mapped == OHCA_STATE_OVERTIME) {
                alarmMuted = false;  // OVERTIME 的 15s 短嗍不應被舊 mute 擋掉
            }
        }
        ohcaPrevSinceMs = since;
    }
}

// ============================================================
// 紀錄事件（Phase A：in-memory only；Phase E 才 LittleFS 持久化）
// ============================================================

/** 紀錄本機即時事件（EVT_EPI_LOCAL / EVT_SHOCK_LOCAL / EVT_AMIODARONE） */
void recordLocalEvent(ems_event_type_t type) {
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[EVT] full, drop");
        return;
    }
    // Phase F MVP1：對時後 timestamp_ms = 真實 epoch；未對時 = 0（spec §4.1）
    //   elapsed_ms 仍走 millis()，不受對時影響（spec §0）
    uint32_t now     = millis();
    uint64_t now_ts  = ems::time_sync_current_epoch_ms(&g_ts_state, now);
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    buildLocalEvent(&events[eventCount], nextEventId++, type,
                    /*timestamp_ms*/ now_ts, elapsed);
    eventCount++;
}

/** 紀錄補登事件（接手前 / 純補登 EPI / 電擊） */
void recordSuppEvent(supp_type_t supp_type, uint8_t count) {
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[EVT] full, drop");
        return;
    }
    if (!isSuppCountValid(supp_type, count)) {
        Serial.printf("[EVT] invalid supp count: type=%u count=%u\n", supp_type, count);
        return;
    }
    // Phase F MVP1：對時後 recorded_at_ms = 真實 epoch；未對時 = 0（spec §4.1）
    uint32_t now     = millis();
    uint64_t now_ts  = ems::time_sync_current_epoch_ms(&g_ts_state, now);
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    buildSuppEvent(&events[eventCount], nextEventId++, supp_type, count,
                   /*recorded_at_ms*/ now_ts, elapsed);
    eventCount++;
}

/** 重置 Phase B 補登 / Amio sub-state 暫存 */
void resetSubState() {
    ohcaSubState           = SUBSTATE_NONE;
    backfillCursor         = 0;
    backfillCount          = 1;
    backfillSuccessShownMs = 0;
    timelineScrollOffset   = 0;
    showAmioArmedPrompt    = false;
}

/** 進入藥物選單（長按 EPI 鍵入口） */
void enterDrugMenu() {
    ohcaSubState   = SUBSTATE_DRUG_MENU;
    backfillCursor = 0;
    Serial.println("[OHCA] enter DRUG_MENU");
}

/** 進入電擊補登選單（長按電擊鍵入口） */
void enterShockBackfillMenu() {
    ohcaSubState     = SUBSTATE_BACKFILL_TYPE;
    backfillCategory = BACKFILL_CAT_SHOCK;
    backfillSuppType = SUPP_TYPE_SHOCK_PRE_HANDOVER;
    backfillCursor   = 0;
    Serial.println("[OHCA] enter SHOCK BACKFILL TYPE menu");
}

// ============================================================
//  Phase C: 6 秒通氣節奏 tick + output applier
// ============================================================

/** 套用 vent 輸出至蜂鳴 + 反色（不負責顯示文字，由 draw 函式畫秒數） */
void applyVentOutput(const vent_output_t& out) {
    if (out.buzz_emphasis) {
        // 第 1 拍：強提示 (250ms on)
        triggerBeep(1, 250, 0);
    } else if (out.buzz_short) {
        // 第 2~6 拍：短提示 (60ms on)
        triggerBeep(1, 60, 0);
    }
    if (out.screen_invert_red) {
        triggerOledFlash(OLED_FLASH_MS);  // 重複觸發會被 oledInverted guard 擋掉
    }
}

/**
 * Tick 驅動 vent 輸出
 * - GLOBAL_VENT 獨立模式：永遠跑；ventEndCheckShown 中暫停
 * - GLOBAL_OHCA + ohcaVentOverlayEnabled：跑；ALARMING 時靜音（vol=0 effect）
 *   pm-dev-spec §7.2 / V1 §14.8：EPI 高優先打斷 → vent 立即靜音（EPI 到期優先權）
 */
void updateVentTick() {
    bool standalone = (globalState == GLOBAL_VENT) && !ventEndCheckShown && !ventPaused;
    bool ohcaOverlay = (globalState == GLOBAL_OHCA) && ohcaVentOverlayEnabled && !ohcaVentPaused;
    if (!standalone && !ohcaOverlay) return;

    uint32_t now   = millis();
    uint32_t since = (ventStartMs == 0) ? 0 : (now - ventStartMs);

    // ALARMING 時 vent 靜音（vol=0 即可達成「buzz 全關 + 視覺保留」）
    uint8_t effectiveVol = ventVolume;
    if (ohcaOverlay && ohcaState == OHCA_STATE_ALARMING) {
        effectiveVol = 0;
    }

    vent_output_t out = decideVentOutput(ventPrevSinceMs, since, effectiveVol);
    applyVentOutput(out);
    ventPrevSinceMs = since;
}

// ============================================================
// 退出 OHCA case（從 SUMMARY 返回主功能表）
// ============================================================

/** 切換全域狀態回主功能表（GLOBAL_MAIN_MENU） */
void enterMainMenu() {
    globalState = GLOBAL_MAIN_MENU;
}

/** 退出 OHCA case：回主功能表並重置所有 OHCA 執行期狀態（事件、倒數、捲動、通氣 overlay、蜂鳴） */
void exitOhcaCase() {
    enterMainMenu();
    ohcaState              = OHCA_STATE_MAIN_MENU;
    ohcaLastEpiMs          = 0;
    ohcaPrevSinceMs        = 0;
    eventCount             = 0;
    summaryScrollOffset    = 0;
    alarmMuted             = false;
    resetOhcaVentState();             // V1 §14.12 案件結束 → 6 秒給氣自動停止
    stopBeep();
}

// ============================================================
// 蜂鳴器非 blocking SM
// pulses = 255 視為「連續」（直到 stopBeep）
// ============================================================

/**
 * 啟動非阻塞蜂鳴狀態機（立即拉高蜂鳴器並排定首次切換）。
 * @param pulses  鳴叫次數；255 視為連續鳴叫直到 stopBeep
 * @param onMs    每次鳴叫的持續毫秒數
 * @param offMs   兩次鳴叫之間的靜音毫秒數
 */
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
    beepPulsesRemaining = pulses;
    beepOnMs            = onMs;
    beepOffMs           = offMs;
    beepActive          = true;
    digitalWrite(BUZZER_PIN, HIGH);
    beepNextToggleMs    = millis() + onMs;
}

/** 立即停止蜂鳴：清除剩餘鳴叫次數、連續旗標並拉低蜂鳴器 GPIO */
void stopBeep() {
    beepPulsesRemaining = 0;
    beepActive          = false;
    buzzContinuous      = false;
    digitalWrite(BUZZER_PIN, LOW);
}

/** 非阻塞蜂鳴狀態機 tick：依排定時間切換 ON/OFF 並遞減剩餘鳴叫次數（255 連續模式不遞減） */
void updateBeepMachine() {
    if (beepPulsesRemaining == 0 && !beepActive) return;
    uint32_t now = millis();
    if ((int32_t)(now - beepNextToggleMs) < 0) return;

    if (beepActive) {
        // STEP 01: ON → OFF（pulse 結束）
        digitalWrite(BUZZER_PIN, LOW);
        beepActive = false;
        if (beepPulsesRemaining != 255 && beepPulsesRemaining > 0) {
            beepPulsesRemaining--;
        }
        if (beepPulsesRemaining == 0) return;
        beepNextToggleMs = now + beepOffMs;
    } else {
        // STEP 02: OFF → ON（下一個 pulse 開始）
        digitalWrite(BUZZER_PIN, HIGH);
        beepActive       = true;
        beepNextToggleMs = now + beepOnMs;
    }
}

// ============================================================
// OLED 反色 SM（震動視覺替代）
// ============================================================

/**
 * 觸發螢幕反色閃爍（作為震動的視覺替代）；若已在閃爍中則略過不重複觸發。
 * @param durationMs  反色持續毫秒數，到時由 updateOledFlashMachine 還原
 */
void triggerOledFlash(uint16_t durationMs) {
    if (oledInverted) return;  // 已在閃 — 不重複觸發
    oledInverted          = true;
    oledInvertStartMs     = millis();
    oledInvertDurationMs  = durationMs;
    tft.invertDisplay(true);
}

/** 螢幕反色閃爍狀態機 tick：反色時間到期後還原顯示為正常色 */
void updateOledFlashMachine() {
    if (!oledInverted) return;
    if (millis() - oledInvertStartMs >= oledInvertDurationMs) {
        oledInverted = false;
        tft.invertDisplay(false);
    }
}

// ============================================================
// 套用 ohca_output_t 到實際 GPIO
// ============================================================

/**
 * 將 ohca_output_t 決策結果套用到實際硬體輸出（連續/短鳴蜂鳴、螢幕閃紅、震動馬達）。
 * @param out  OHCA 輸出決策結構，由 decideOhcaOutput 產生
 */
void applyOhcaOutput(const ohca_output_t& out) {
    // STEP 01: 連續發報（ALARMING）
    if (out.buzz_alarm_continuous && !alarmMuted) {
        if (!buzzContinuous) {
            buzzContinuous = true;
            triggerBeep(255, 200, 100);  // 連續 200ms on / 100ms off
        }
    } else if (buzzContinuous) {
        buzzContinuous = false;
        stopBeep();
    }

    // STEP 02: 短嗍（WARNING / OVERTIME 邊緣觸發）
    if (out.buzz_short && !alarmMuted) {
        triggerBeep(1, 80, 0);
    }

    // STEP 03: OLED 閃紅（震動視覺替代）
    if (out.screen_flash) {
        triggerOledFlash(OLED_FLASH_MS);
    }

    // STEP 04: 震動馬達（佔位）
#if ENABLE_VIBRATION
    if (out.vibrate) {
        digitalWrite(VIBRATION_PIN, HIGH);
        delay(50);
        digitalWrite(VIBRATION_PIN, LOW);
    }
#endif
}

// ============================================================
// 顯示繪製
// ============================================================

/**
 * 觸發 flash 過場提示（對齊 demo flash() helper）。
 * @param title         主標題（非 NULL）
 * @param subtitle      副標題（NULL 或 "" 略過）
 * @param duration_ms   顯示毫秒（典型 FLASH_DEFAULT_MS=1200）
 * @param titleColor    主標題色（典型 COLOR_ACCENT_OK / COLOR_TEXT_PRIMARY）
 * @param titleSize     主標 vlw multiplier（預設 FLASH_TITLE_SIZE_DEFAULT=2.25；
 *                      ~7-char 以上易超寬，傳 FLASH_TITLE_SIZE_LONG=1.9）
 * @param subtitleSize  副標 vlw multiplier（預設 FLASH_SUBTITLE_SIZE_DEFAULT=1.5；
 *                      ~10-char 以上易超寬，傳 FLASH_SUBTITLE_SIZE_LONG=1.2）
 */
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor,
                  float titleSize, float subtitleSize) {
    // STEP 01: 寫入 lifecycle 與 visual 欄位（render 由 drawFlashOverlay 讀取）
    flashState.active       = true;
    flashState.startMs      = millis();
    flashState.durationMs   = duration_ms;
    flashState.titleColor   = titleColor;
    flashState.titleSize    = titleSize;
    flashState.subtitleSize = subtitleSize;

    // STEP 02: 文字欄位 bounded copy（NULL 視為 ""，超出 buf 截斷不 overflow）
    strncpy(flashState.title,    title    ? title    : "", sizeof(flashState.title)    - 1);
    strncpy(flashState.subtitle, subtitle ? subtitle : "", sizeof(flashState.subtitle) - 1);
    flashState.title[sizeof(flashState.title)       - 1] = '\0';
    flashState.subtitle[sizeof(flashState.subtitle) - 1] = '\0';

    // STEP 03: serial trace（debug 用，可看到 flash 觸發順序）
    Serial.printf("[FLASH] %s | %s\n", flashState.title, flashState.subtitle);
}
