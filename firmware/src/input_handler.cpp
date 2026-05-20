#include "app_globals.h"


/**
 * 掃描所有實體按鍵並分派短按 / 長按事件。
 *
 * 每輪 loop 呼叫一次：逐顆讀取 GPIO 狀態，press 與 release 邊緣共用
 * 同一 DEBOUNCE_MS 門檻防抖；release 時若按住時間短於 SHORT_PRESS_MAX_MS
 * 觸發 onShortPress；按住中達到該鍵 LONG_PRESS_MS_PER_BTN 門檻時觸發
 * 一次 onLongPress。
 */
void handleButtons() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        uint8_t cur = digitalRead(BTN_PINS[i]);

        if (cur != lastBtnState[i]) {
            // STEP 01: 邊緣事件
            // STEP 01.00: 統一 debounce — press 與 release 邊緣共用同一個門檻，
            //   避免 TFT 慢渲染拉長時間窗時 bounce 邊緣穿過原本只擋 press 的防抖造成 double-fire
            if (now - lastPressMs[i] < DEBOUNCE_MS) {
                continue;
            }
            lastPressMs[i] = now;

            if (cur == LOW) {
                // STEP 01.01: 按下（press start）
                btnPressStartMs[i] = now;
                btnLongFired[i]    = false;
            } else {
                // STEP 01.02: 放開（release）
                if (btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                    uint32_t held = now - btnPressStartMs[i];
                    if (held < SHORT_PRESS_MAX_MS) {
                        onShortPress(i);
                    }
                }
                btnPressStartMs[i] = 0;
            }
            lastBtnState[i] = cur;
        } else {
            // STEP 02: 按住中 — 依該按鈕 threshold 觸發長按（fire on threshold reach）
            uint16_t threshold = LONG_PRESS_MS_PER_BTN[i];
            if (threshold > 0 && cur == LOW &&
                btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                if (now - btnPressStartMs[i] >= threshold) {
                    btnLongFired[i] = true;
                    onLongPress(i);
                }
            }
        }
    }
}

// ============================================================
// 短按 dispatcher
// ============================================================

/**
 * 短按事件 dispatcher。
 *
 * 依當前 globalState（主功能表 / VENT / 歷史 / 佔位畫面 / SYNC / OHCA）
 * 與 OHCA sub-state，將按鍵轉成游標移動、選單進出、確認或記錄等行為。
 *
 * @param btnIdx 觸發短按的按鍵索引（BTN_PRIMARY / BTN_UP / BTN_DOWN /
 *               BTN_BACK / BTN_EPI / BTN_SHOCK / BTN_POWER / BTN_RECORD）
 */
void onShortPress(uint8_t btnIdx) {
    Serial.printf("[BTN] short %u (state=%u/%u)\n", btnIdx, globalState, ohcaState);

    // ===== 主功能表 =====
    if (globalState == GLOBAL_MAIN_MENU) {
        switch (btnIdx) {
            case BTN_UP:
                mainMenuCursor = (mainMenuCursor + MAIN_MENU_COUNT - 1) % MAIN_MENU_COUNT;
                break;
            case BTN_DOWN:
                mainMenuCursor = (mainMenuCursor + 1) % MAIN_MENU_COUNT;
                break;
            case BTN_PRIMARY:
                // STEP 01: 進入對應子模組
                switch (mainMenuCursor) {
                    case 0:  // OHCA Case
                        globalState = GLOBAL_OHCA;
                        dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, 0);
                        startFlashStartMs = millis();
                        caseStartMs       = millis();
                        caseStartEpochMs  = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 對時前 = 0
                        eventCount        = 0;
                        nextEventId       = 1;
                        ohcaLastEpiMs     = 0;
                        ohcaPrevSinceMs   = 0;
                        alarmMuted        = false;
                        g_ohca_live_synced_at_ms = 0;  // SoT §16.6 新 case 起始為「App未同步」
                        resetSubState();
                        Serial.println("[OHCA] Case start (START_FLASH)");
                        break;
                    case 1:  // 6 秒通氣節奏（獨立模式）
                        // A8：先進 VENT_PRE「按主鍵開始」preview，主鍵按下後才正式啟動
                        globalState        = GLOBAL_VENT;
                        ventStartMs        = 0;            // 尚未啟動
                        ventPrevSinceMs    = 0;
                        ventEndCheckShown  = false;
                        ventBackHintShown  = false;
                        ventPaused         = false;
                        ventPreShown       = true;          // A8：等使用者按主鍵
                        Serial.println("[VENT] enter PRE (preview)");
                        break;
                    case 2: globalState = GLOBAL_TRAINING_PLACEHOLDER; break;
                    case 3:  // 歷史紀錄（Phase E）
                        // 進入時重抓 list（每次進都最新；最新案件在 index 0）
                        if (g_storage_ready) {
                            historyCount = storage_list(&g_storage_be,
                                                        EMS_CASE_TYPE_OHCA,
                                                        historyCases,
                                                        EMS_STORAGE_OHCA_CAP);
                        } else {
                            historyCount = 0;
                        }
                        historyCursor       = 0;
                        historyScrollOffset = 0;
                        historySummaryMode  = false;
                        globalState         = GLOBAL_HISTORY_PLACEHOLDER;
                        break;
                    case 4:  // 系統設定（Phase G placeholder）
                        globalState = GLOBAL_SETTINGS_PLACEHOLDER;
                        break;
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Phase C: GLOBAL_VENT 獨立模式（V1 §13）=====
    if (globalState == GLOBAL_VENT) {
        uint32_t now = millis();
        // A8: VENT_PRE preview 畫面（按主鍵開始）
        if (ventPreShown) {
            if (btnIdx == BTN_UP) {
                ventVolume = clampVentVolume((int16_t)ventVolume + 1);
                return;
            }
            if (btnIdx == BTN_DOWN) {
                ventVolume = clampVentVolume((int16_t)ventVolume - 1);
                return;
            }
            if (btnIdx == BTN_BACK) {
                // 返回鍵 → 直接回主功能表（preview 還沒啟動，可直接走）
                ventPreShown = false;
                enterMainMenu();
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 主鍵 → 正式啟動（V1 §13.5 啟動規則：秒數從 1 開始）
                ventPreShown = false;
                ventStartMs  = now;
                ventPrevSinceMs = 0;
                Serial.println("[VENT] PRE -> running");
                return;
            }
            return;
        }
        // STEP 01: 結束確認對話框中
        if (ventEndCheckShown) {
            if (btnIdx == BTN_PRIMARY) {
                Serial.println("[VENT] standalone end confirmed");
                stopBeep();
                ventEndCheckShown = false;
                enterMainMenu();
                return;
            }
            if (btnIdx == BTN_BACK) {
                ventEndCheckShown = false;
                return;
            }
            return;
        }
        // STEP 02: 主畫面按鍵
        if (btnIdx == BTN_UP) {
            ventVolume = clampVentVolume((int16_t)ventVolume + 1);
            return;
        }
        if (btnIdx == BTN_DOWN) {
            ventVolume = clampVentVolume((int16_t)ventVolume - 1);
            return;
        }
        if (btnIdx == BTN_BACK) {
            // V1 §13.15：執行中按返回鍵不直接結束 → 提示「請長按主鍵」
            ventBackHintShown   = true;
            ventBackHintStartMs = now;
            return;
        }
        if (btnIdx == BTN_PRIMARY) {
            // V1 §13.11 / §13.12：主鍵短按 toggle 暫停/繼續
            // 繼續時 §13.12 規定秒數重新從 1 開始 → 重置 ventStartMs
            ventPaused = !ventPaused;
            if (!ventPaused) {
                ventStartMs     = now;
                ventPrevSinceMs = 0;
            } else {
                stopBeep();
            }
            Serial.printf("[VENT] paused=%d\n", ventPaused);
            return;
        }
        return;
    }

    // ===== Phase E：歷史紀錄（已實作，覆蓋共用佔位處理） =====
    if (globalState == GLOBAL_HISTORY_PLACEHOLDER) {
        // STEP 01: SUMMARY 子畫面 — sub-menu cursor + BACK 回列表（SoT §10.4「可同步至 App」涵蓋歷史）
        if (historySummaryMode) {
            switch (btnIdx) {
                case BTN_UP:
                    summarySubmenuCursor =
                        (summarySubmenuCursor + SUMMARY_SUBMENU_COUNT - 1) % SUMMARY_SUBMENU_COUNT;
                    break;
                case BTN_DOWN:
                    summarySubmenuCursor =
                        (summarySubmenuCursor + 1) % SUMMARY_SUBMENU_COUNT;
                    break;
                case BTN_PRIMARY:
                    handleSummarySubmenuPrimary();
                    break;
                case BTN_BACK:
                    historySummaryMode = false;
                    eventCount = 0;     // 清掉 history 載回來的事件，避免汙染下個 OHCA case
                    break;
                default:
                    // EPI/SHOCK/RECORD/POWER 在歷史 SUMMARY 無作用，留 trace 避免「裝置死當」誤判
                    Serial.printf("[HIST_SUMMARY] ignored btn=%u\n", btnIdx);
                    break;
            }
            return;
        }
        // STEP 02: 列表模式
        switch (btnIdx) {
            case BTN_UP:
                if (historyCursor > 0) {
                    historyCursor--;
                }
                if (historyCursor < historyScrollOffset) {
                    historyScrollOffset = historyCursor;
                }
                break;
            case BTN_DOWN:
                if (historyCursor + 1 < historyCount) {
                    historyCursor++;
                }
                if (historyCursor >= historyScrollOffset + HISTORY_VISIBLE_ROWS) {
                    historyScrollOffset = historyCursor - (HISTORY_VISIBLE_ROWS - 1);
                }
                break;
            case BTN_BACK:
                enterMainMenu();
                break;
            case BTN_PRIMARY:
                // STEP 02.01: 選定後載入該案件、跳到 SUMMARY 子畫面
                if (historyCount > 0 && g_storage_ready) {
                    uint16_t loaded = 0;
                    bool ok = storage_load_events(&g_storage_be,
                                                  EMS_CASE_TYPE_OHCA,
                                                  historyCases[historyCursor].id,
                                                  events, MAX_EVENTS, &loaded);
                    if (ok) {
                        eventCount = loaded;
                        historySummaryMode   = true;
                        summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                        Serial.printf("[STORAGE] loaded case %s (%u events)\n",
                                      historyCases[historyCursor].id,
                                      loaded);
                    } else {
                        Serial.printf("[STORAGE] load failed for %s\n",
                                      historyCases[historyCursor].id);
                    }
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Phase X 佔位畫面：返回鍵回主功能表 =====
    if (globalState >= GLOBAL_TRAINING_PLACEHOLDER && globalState <= GLOBAL_SETTINGS_PLACEHOLDER) {
        if (btnIdx == BTN_BACK) {
            enterMainMenu();
        }
        return;
    }

    // ===== Phase F MVP2：同步資料 =====
    //   - BTN_PRIMARY 在 AWAITING_MAIN_KEY → MAIN_KEY_PRESS（dispatcher 推進 SENDING）
    //   - BTN_BACK 在非 SENDING → BACK_KEY_PRESS（dispatcher 回 IDLE → 主功能表）
    if (globalState == GLOBAL_SYNC) {
        if (btnIdx == BTN_PRIMARY && g_sync_ctx.state == ems::SyncState::AWAITING_MAIN_KEY) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::MAIN_KEY_PRESS, millis());
        } else if (btnIdx == BTN_BACK && g_sync_ctx.state != ems::SyncState::SENDING) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BACK_KEY_PRESS, millis());
        }
        return;
    }

    // ===== OHCA 模式 =====
    if (globalState != GLOBAL_OHCA) return;

    uint32_t now = millis();

    // ===== Phase B/C sub-state 子流程處理 =====
    if (ohcaSubState != SUBSTATE_NONE) {
        // STEP 01: SUCCESS 顯示中：忽略所有按鍵（自動 2s 後消失，由 loop 處理）
        if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS) return;

        // STEP 01.5: QUICK_MENU（Phase C，返回鍵入口；V1 §14.9 動態 3/4 項）
        if (ohcaSubState == SUBSTATE_QUICK_MENU) {
            // V1 §14.9 選項（B3：加「案件簡版總覽」對齊 demo）：
            //   未開啟：[0] Enable [1] Summary [2] Back
            //   已開啟：[0] Pause/Resume [1] Disable [2] Summary [3] Back
            uint8_t cnt = ohcaVentOverlayEnabled ? 4 : 3;
            if (btnIdx == BTN_UP) {
                backfillCursor = (backfillCursor + cnt - 1) % cnt;
                return;
            }
            if (btnIdx == BTN_DOWN) {
                backfillCursor = (backfillCursor + 1) % cnt;
                return;
            }
            if (btnIdx == BTN_BACK) { resetSubState(); return; }
            if (btnIdx == BTN_PRIMARY) {
                const uint8_t summaryIdx = ohcaVentOverlayEnabled ? 2 : 1;
                const uint8_t backIdx    = ohcaVentOverlayEnabled ? 3 : 2;
                if (backfillCursor == summaryIdx) {
                    // B3：案件簡版總覽 — demo 略過，flash 提示
                    triggerFlash("簡版總覽", "結束案件後看完整總覽", 2000, COLOR_TEXT_PRIMARY,
                                 FLASH_TITLE_SIZE_DEFAULT, FLASH_SUBTITLE_SIZE_LONG);
                    Serial.println("[OHCA] quick: summary placeholder");
                    resetSubState();
                    return;
                }
                if (backfillCursor == backIdx) {
                    // 返回 OHCA → resetSubState 即可
                    resetSubState();
                    return;
                }
                if (!ohcaVentOverlayEnabled) {
                    // backfillCursor == 0：Enable 6s vent
                    ohcaVentOverlayEnabled = true;
                    ohcaVentPaused         = false;
                    ventStartMs            = millis();
                    ventPrevSinceMs        = 0;
                    triggerFlash("6 秒給氣", "已開啟", 800, COLOR_ACCENT_OK);
                    Serial.println("[OHCA] vent overlay = ON");
                } else {
                    if (backfillCursor == 0) {
                        // toggle Pause / Resume（V1 §14.10 / §14.11）
                        ohcaVentPaused = !ohcaVentPaused;
                        if (!ohcaVentPaused) {
                            ventStartMs     = millis();
                            ventPrevSinceMs = 0;
                            triggerFlash("6 秒給氣", "已繼續", 800, COLOR_ACCENT_OK);
                        } else {
                            stopBeep();
                            triggerFlash("6 秒給氣", "已暫停", 800, COLOR_ACCENT_WARN);
                        }
                        Serial.printf("[OHCA] vent paused = %d\n", ohcaVentPaused);
                    } else if (backfillCursor == 1) {
                        // Disable 6s vent
                        ohcaVentOverlayEnabled = false;
                        ohcaVentPaused         = false;
                        stopBeep();
                        triggerFlash("6 秒給氣", "已關閉", 800, COLOR_TEXT_MUTED);
                        Serial.println("[OHCA] vent overlay = OFF");
                    }
                }
                resetSubState();
                return;
            }
            return;
        }

        // STEP 02: TIMELINE：上下鍵翻頁、返回鍵回 SUMMARY
        if (ohcaSubState == SUBSTATE_TIMELINE) {
            if (btnIdx == BTN_UP   && timelineScrollOffset > 0)         timelineScrollOffset--;
            if (btnIdx == BTN_DOWN && timelineScrollOffset + 4 < eventCount) timelineScrollOffset++;
            if (btnIdx == BTN_BACK)                                      ohcaSubState = SUBSTATE_NONE;
            return;
        }

        // STEP 03: AMIO_CONFIRM：BTN_PRIMARY 兩段確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_AMIO_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                resetSubState();
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                bool ok = twoStepConfirm_press(&amioConfirm, now);
                if (ok) {
                    showAmioArmedPrompt = false;
                    recordLocalEvent(EVT_AMIODARONE);
                    dispatchOhcaEvent(OHCA_EVT_AMIO_CONFIRMED, 0);  // 不重啟倒數
                    triggerBeep(1, 80, 0);
                    // A4：對齊 demo flash('Amiodarone 已紀錄', '')
                    triggerFlash("Amiodarone 已紀錄", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK,
                                 FLASH_TITLE_SIZE_XLONG, FLASH_SUBTITLE_SIZE_DEFAULT);
                    Serial.println("[OHCA] Amio confirmed");
                    resetSubState();
                } else {
                    showAmioArmedPrompt  = true;
                    amioArmedPromptStart = now;
                }
            }
            return;
        }

        // STEP 04: DRUG_MENU：上下鍵切換「補登 EPI / Amiodarone」；主鍵確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_DRUG_MENU) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                backfillCursor ^= 1;  // 0 ↔ 1
                return;
            }
            if (btnIdx == BTN_BACK) { resetSubState(); return; }
            if (btnIdx == BTN_PRIMARY) {
                if (backfillCursor == 0) {
                    // STEP 04.01: 進補登 EPI 流程（先選類型）
                    backfillCategory = BACKFILL_CAT_EPI;
                    backfillSuppType = SUPP_TYPE_EPI_PRE_HANDOVER;
                    backfillCursor   = 0;
                    ohcaSubState     = SUBSTATE_BACKFILL_TYPE;
                } else {
                    // STEP 04.02: 進 Amio 兩段確認
                    twoStepConfirm_init(&amioConfirm, TWO_STEP_DEFAULT_TIMEOUT_MS);
                    showAmioArmedPrompt = false;
                    ohcaSubState        = SUBSTATE_AMIO_CONFIRM;
                }
            }
            return;
        }

        // STEP 05: BACKFILL_TYPE：上下鍵切換「接手前 / 純補登」；主鍵確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_BACKFILL_TYPE) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                backfillCursor ^= 1;
                if (backfillCategory == BACKFILL_CAT_EPI) {
                    backfillSuppType = (backfillCursor == 0)
                                     ? SUPP_TYPE_EPI_PRE_HANDOVER
                                     : SUPP_TYPE_EPI_PURE;
                } else {
                    backfillSuppType = (backfillCursor == 0)
                                     ? SUPP_TYPE_SHOCK_PRE_HANDOVER
                                     : SUPP_TYPE_SHOCK_PURE;
                }
                return;
            }
            if (btnIdx == BTN_BACK) {
                // 從 EPI 入口進來 → 退回 DRUG_MENU；從電擊入口進來 → 退出
                if (backfillCategory == BACKFILL_CAT_EPI) {
                    ohcaSubState   = SUBSTATE_DRUG_MENU;
                    backfillCursor = 0;
                } else {
                    resetSubState();
                }
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                backfillCount = 1;  // 預設 1
                ohcaSubState  = SUBSTATE_BACKFILL_COUNT;
                return;
            }
            return;
        }

        // STEP 06: BACKFILL_COUNT：上下鍵增減次數；主鍵 → CONFIRM；返回鍵 → TYPE
        if (ohcaSubState == SUBSTATE_BACKFILL_COUNT) {
            uint8_t maxN = suppCountMax(backfillSuppType);
            if (btnIdx == BTN_UP   && backfillCount < maxN) backfillCount++;
            if (btnIdx == BTN_DOWN && backfillCount > 1)    backfillCount--;
            if (btnIdx == BTN_BACK) {
                ohcaSubState   = SUBSTATE_BACKFILL_TYPE;
                backfillCursor = 0;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                ohcaSubState = SUBSTATE_BACKFILL_CONFIRM;
            }
            return;
        }

        // STEP 07: BACKFILL_CONFIRM：主鍵 → 寫入；返回鍵 → 取消回 COUNT
        if (ohcaSubState == SUBSTATE_BACKFILL_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                ohcaSubState = SUBSTATE_BACKFILL_COUNT;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                recordSuppEvent(backfillSuppType, backfillCount);
                triggerBeep(1, 80, 0);
                ohcaSubState           = SUBSTATE_BACKFILL_SUCCESS;
                backfillSuccessShownMs = now;
                Serial.printf("[OHCA] supp recorded: type=%u count=%u\n",
                              backfillSuppType, backfillCount);
            }
            return;
        }
        return;
    }

    // ===== OHCA 主畫面（無 sub-state） =====
    switch (btnIdx) {
        case BTN_PRIMARY:
            // STEP 01: ALARMING/OVERTIME 主鍵 — 消音（不轉 state）
            if (ohcaState == OHCA_STATE_ALARMING || ohcaState == OHCA_STATE_OVERTIME) {
                alarmMuted = true;
                stopBeep();
                Serial.println("[OHCA] alarm muted");
                return;
            }
            // STEP 02: END_CHECK 主鍵 — 依 cursor 行為
            if (ohcaState == OHCA_STATE_END_CHECK) {
                // A7：二次確認對話顯示中 → 主鍵 = 真鎖定
                if (endConfirmShown) {
                    endConfirmShown = false;
                    dispatchOhcaEvent(OHCA_EVT_END_CONFIRM, 0);
                    // A5：對齊 demo flash('案件結束並鎖定', '已存入歷史紀錄')
                    triggerFlash("案件結束並鎖定", "已存入歷史紀錄", FLASH_DEFAULT_MS, COLOR_ACCENT_ALERT,
                                 FLASH_TITLE_SIZE_LONG, FLASH_SUBTITLE_SIZE_DEFAULT);
                    Serial.println("[OHCA] case LOCKED (after END_CONFIRM dialog)");
                    return;
                }
                if (endCheckCursor == END_CHECK_CURSOR_CONFIRM) {
                    // A7：開二次確認對話（不直接鎖定）
                    endConfirmShown = true;
                    Serial.println("[OHCA] END_CONFIRM dialog opened");
                    return;
                }
                if (endCheckCursor == END_CHECK_CURSOR_BACKFILL) {
                    // B1：前往補登 — 先回到原 phase，再進 drug menu
                    dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                    enterDrugMenu();
                    Serial.println("[OHCA] END_CHECK -> backfill (drug menu)");
                    return;
                }
                // CANCEL
                dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                return;
            }
            // STEP 03: LOCKED 主鍵 — 翻 SUMMARY
            if (ohcaState == OHCA_STATE_LOCKED) {
                dispatchOhcaEvent(OHCA_EVT_TO_SUMMARY, 0);
                summaryScrollOffset  = 0;
                summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                return;
            }
            // STEP 04: SUMMARY 主鍵 — 觸發 sub-menu 當前 cursor 對應行為（SoT V1 §11.1）
            if (ohcaState == OHCA_STATE_SUMMARY) {
                handleSummarySubmenuPrimary();
                return;
            }
            break;

        case BTN_UP:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                // 3 項循環：CONFIRM(0) ↔ BACKFILL(1) ↔ CANCEL(2)
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 2) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                summarySubmenuCursor =
                    (summarySubmenuCursor + SUMMARY_SUBMENU_COUNT - 1) % SUMMARY_SUBMENU_COUNT;
            }
            break;

        case BTN_DOWN:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 1) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                summarySubmenuCursor = (summarySubmenuCursor + 1) % SUMMARY_SUBMENU_COUNT;
            }
            break;

        case BTN_BACK:
            // A1：兩段確認對話顯示中 → 返回鍵 = 取消對話（modal 行為）
            if (showEpiArmedPrompt || showShockArmedPrompt) {
                showEpiArmedPrompt   = false;
                showShockArmedPrompt = false;
                Serial.println("[OHCA] confirm dialog cancelled (BACK)");
                return;
            }
            // STEP 01: SUMMARY 返回主功能表
            if (ohcaState == OHCA_STATE_SUMMARY) {
                exitOhcaCase();
                return;
            }
            // STEP 02: END_CHECK 返回鍵
            if (ohcaState == OHCA_STATE_END_CHECK) {
                // A7：二次確認對話顯示中 → 返回 = 退回 END_CHECK 主畫面
                if (endConfirmShown) {
                    endConfirmShown = false;
                    return;
                }
                // 一般 = 取消（回原 phase）
                dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                return;
            }
            // STEP 03: Phase C — 案件進行中（非 ALARMING）按返回鍵 → 快速功能選單
            //   ALARMING 中不允許離開警報（V1 §14.8 EPI 到期優先權 / Test Plan §5.2.2）
            if (ohcaState == OHCA_STATE_WAIT_FIRST_EPI ||
                ohcaState == OHCA_STATE_COUNTDOWN     ||
                ohcaState == OHCA_STATE_WARNING       ||
                ohcaState == OHCA_STATE_OVERTIME) {
                ohcaSubState   = SUBSTATE_QUICK_MENU;
                backfillCursor = 0;
                Serial.println("[OHCA] enter QUICK_MENU");
                return;
            }
            break;

        case BTN_EPI: {
            // STEP 01: LOCKED 拒絕
            if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
            // STEP 02: 兩段確認
            bool confirmed = twoStepConfirm_press(&epiConfirm, now);
            if (confirmed) {
                showEpiArmedPrompt = false;
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                dispatchOhcaEvent(OHCA_EVT_EPI_CONFIRMED, since);
                ohcaLastEpiMs   = now;
                ohcaPrevSinceMs = 0;
                alarmMuted      = false;
                stopBeep();
                recordLocalEvent(EVT_EPI_LOCAL);
                triggerBeep(1, 80, 0);  // 短確認音
                // A2：對齊 demo flash('EPI 已紀錄', '重新倒數 4 分鐘')
                triggerFlash("EPI 已紀錄", "重新倒數 4 分鐘", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                Serial.println("[OHCA] EPI confirmed");
            } else {
                showEpiArmedPrompt   = true;
                epiArmedPromptStart  = now;
                Serial.println("[OHCA] EPI armed (please re-press)");
            }
            break;
        }

        case BTN_SHOCK: {
            if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
            bool confirmed = twoStepConfirm_press(&shockConfirm, now);
            if (confirmed) {
                showShockArmedPrompt = false;
                dispatchOhcaEvent(OHCA_EVT_SHOCK_CONFIRMED, 0);  // 不重啟倒數
                recordLocalEvent(EVT_SHOCK_LOCAL);
                triggerBeep(1, 80, 0);
                // A3：對齊 demo flash('電擊已紀錄', '')
                triggerFlash("電擊已紀錄", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                Serial.println("[OHCA] Shock confirmed");
            } else {
                showShockArmedPrompt   = true;
                shockArmedPromptStart  = now;
                Serial.println("[OHCA] Shock armed (please re-press)");
            }
            break;
        }

        case BTN_POWER:
        case BTN_RECORD:
            // Phase H / Phase 1.5 — noop
            break;
    }
}

// ============================================================
// 長按 3s dispatcher（OHCA OVERTIME 結束前檢查觸發）
// ============================================================

/**
 * 長按事件 dispatcher（每次按住達門檻只觸發一次）。
 *
 * 主鍵長按：VENT 模式開結束確認對話框、OHCA 進行中進入 END_CHECK；
 * EPI 鍵長按進入藥物選單；電擊鍵長按進入電擊補登選單。
 *
 * @param btnIdx 觸發長按的按鍵索引（BTN_PRIMARY / BTN_EPI / BTN_SHOCK；
 *               其餘按鍵無對應長按行為）
 */
void onLongPress(uint8_t btnIdx) {
    Serial.printf("[BTN] long %u (state=%u/%u/sub=%u)\n",
                  btnIdx, globalState, ohcaState, ohcaSubState);

    // STEP 01: 主鍵 3s 長按
    if (btnIdx == BTN_PRIMARY) {
        // STEP 01.01: GLOBAL_VENT 獨立模式 → 結束確認對話框（V1 §13.14 結束獨立 6 秒通氣節奏）
        if (globalState == GLOBAL_VENT && !ventEndCheckShown) {
            ventEndCheckShown = true;
            stopBeep();
            Serial.println("[VENT] standalone end check");
            return;
        }
        // STEP 01.02: GLOBAL_OHCA 進行中任意 phase → END_CHECK（SoT V1 §10.1）
        //   排除 START_FLASH / END_CHECK / LOCKED / SUMMARY（A.S8 步驟 4 要求 noop）
        if (globalState == GLOBAL_OHCA && ohcaSubState == SUBSTATE_NONE &&
            isOhcaInProgress(ohcaState)) {
            dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_LONG_3S, 0);
            endCheckCursor   = END_CHECK_CURSOR_CANCEL;
            endConfirmShown  = false;  // A7：reset 二次確認對話旗標
            stopBeep();
            Serial.println("[OHCA] enter END_CHECK");
            return;
        }
        // STEP 01.03: OHCA 但條件不滿足（substate / 排除清單）— field debug log
        if (globalState == GLOBAL_OHCA) {
            Serial.printf("[OHCA] long-press ignored (state=%u sub=%u)\n",
                          ohcaState, ohcaSubState);
        }
        return;
    }

    // STEP 02: EPI 鍵 1s 長按 — 進入藥物選單（Phase B）
    if (btnIdx == BTN_EPI) {
        if (globalState != GLOBAL_OHCA) return;
        if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
        if (ohcaSubState != SUBSTATE_NONE) return;
        enterDrugMenu();
        return;
    }

    // STEP 03: 電擊鍵 1s 長按 — 進入電擊補登選單（Phase B）
    if (btnIdx == BTN_SHOCK) {
        if (globalState != GLOBAL_OHCA) return;
        if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
        if (ohcaSubState != SUBSTATE_NONE) return;
        enterShockBackfillMenu();
        return;
    }
}

/**
 * SUMMARY sub-menu 主鍵分派（SoT V1 §11.1）。
 *
 * 共用於 OHCA 結束鎖定後 SUMMARY（globalState=GLOBAL_OHCA）與歷史模式 SUMMARY
 * （globalState=GLOBAL_HISTORY_PLACEHOLDER + historySummaryMode）。
 *
 * 同步返回語意（SoT §16.5「自動回案件總覽」、§16.9 失敗亦留原處）：
 *   進 GLOBAL_SYNC 前記下 caller globalState 到 g_sync_return_to，
 *   loop observer 於 DONE/ERROR → IDLE 邊緣讀此值拉回原案件總覽。
 */
void handleSummarySubmenuPrimary() {
    // STEP 01: 計算當前案件是否已同步
    const uint64_t synced_at = historySummaryMode
        ? (historyCursor < historyCount ? historyCases[historyCursor].synced_at_ms : 0)
        : g_ohca_live_synced_at_ms;
    const bool is_synced = (synced_at >= SYNCED_AT_EPOCH_FLOOR_MS);

    // STEP 02: 純函式決策（不依賴副作用，可 unit test）
    static_assert(SUMMARY_SUBMENU_COUNT == 2, "update SubmenuCursor in summary_action.h");
    const ems::SummaryAction action = ems::decide_summary_action(
        static_cast<ems::SubmenuCursor>(summarySubmenuCursor),
        historySummaryMode,
        globalState == GLOBAL_SYNC,
        is_synced);

    // STEP 03: 根據 action 執行副作用
    switch (action) {
        case ems::SummaryAction::TIMELINE_SHOW:
            ohcaSubState         = SUBSTATE_TIMELINE;
            timelineScrollOffset = 0;
            return;

        case ems::SummaryAction::TIMELINE_NOOP_HISTORY:
            Serial.println("[SUMMARY] timeline disabled in history mode (noop)");
            return;

        case ems::SummaryAction::CONFIRM_RESYNC:
            // SoT §16.7：已同步案件 → TODO[phase-f-mvp3] 顯示確認 dialog
            // 目前直接 fallthrough 到 START_SYNC（dialog UI 待 TFT 實機實作）
            Serial.println("[SYNC] already synced, confirm resync (TODO dialog)");
            [[fallthrough]];

        case ems::SummaryAction::START_SYNC: {
            // STEP 02.01: 記下返回目的地（SoT §16.5 同步完成自動回案件總覽）
            g_sync_return_to = global_to_sync_return(globalState);
            // STEP 02.02: 記下同步目標（SoT §16.6 寫回 storage 用）
            g_sync_target.clear();
            if (historySummaryMode && historyCursor < historyCount) {
                strncpy(g_sync_target.id,
                        historyCases[historyCursor].id,
                        sizeof(g_sync_target.id) - 1);
                g_sync_target.type = historyCases[historyCursor].type;
            } else if (g_storage_ready) {
                case_meta_t latest;
                if (storage_list(&g_storage_be, EMS_CASE_TYPE_OHCA, &latest, 1) > 0) {
                    strncpy(g_sync_target.id, latest.id, sizeof(g_sync_target.id) - 1);
                } else {
                    Serial.println("[SYNC] WARN no LOCKED OHCA case to tag synced_at");
                }
            }
            // STEP 02.03: 進入同步流程
            globalState = GLOBAL_SYNC;
            const bool started = ems::sync_dispatcher_dispatch(
                &g_sync_ctx, ems::SyncEvent::START, millis());
            if (started && g_ble.connected()) {
                ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BLE_CONNECTED, millis());
            }
            // STEP 02.04: dispatch rejected → rollback（bool 介面直接知道是否被接受）
            if (!started) {
                Serial.printf("[SYNC] dispatcher rejected START, rollback to %u\n",
                              (unsigned)g_sync_return_to);
                globalState = sync_return_to_global(g_sync_return_to);
                return;
            }
            Serial.printf("[SYNC] enter state=%u (return_to=%u)\n",
                          (unsigned)g_sync_ctx.state, (unsigned)g_sync_return_to);
            return;
        }

        case ems::SummaryAction::SYNC_BLOCKED_REENTRY:
            Serial.println("[SYNC] re-entry detected, keep g_sync_return_to");
            return;

        case ems::SummaryAction::UNKNOWN_CURSOR:
            Serial.printf("[SUMMARY] unhandled cursor=%u\n", summarySubmenuCursor);
            return;
    }
}

