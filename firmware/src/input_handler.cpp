#include "app_globals.h"

// Wave 1：系統設定 UI（brightness/volume getter/setter）
#include "ui_settings.h"

// Wave 1：系統設定 NVS 讀寫（settings_state_t, settings_init, settings_reset_defaults）
#include "ems_settings.h"

// Wave 2：案件模式（CaseMode）
#include "ems_storage_logic.h"

// Wave 1：系統設定選單狀態（settingsCursor / editor / confirm）
#define SETTINGS_MENU_COUNT    4   // 裝置名稱 / 亮度 / 系統音量 / 通氣音量

// Dev-Phase G: 設定 UI 狀態（定義於 main.cpp）
extern uint8_t settingsCursor;        // 設定選單游標（0=裝置名稱 / 1=亮度 / 2=系統音量 / 3=通氣音量）
extern bool    settingsEditorMode;    // true = 編輯模式（左右鍵調整數值）
extern bool    settingsRestoreConfirm; // true = 恢復預設確認對話框顯示中

// 系統設定 NVS state（開機由 main.cpp settings_init 載入，調值時 settings_write 寫回）
settings_state_t g_settings_state;

// 前置宣告：進入 BLE 同步流程（START_SYNC 與 §16.7 resync 確認後共用，定義見下方）
static void enterSyncFlow();

/**
 * 一個可調設定的完整描述：游標索引、NVS 鍵、值域、存取函式。
 *
 * 把「亮度/系統音量/通氣音量」這組對應關係集中在一張表，取代原本散在
 * BTN_UP / BTN_DOWN 兩個 switch 共 6 段幾乎逐字相同的程式碼。
 * 新增可調設定只需在表中加一列。
 */
typedef struct {
    uint8_t  cursor;   ///< SETTINGS_CURSOR_*
    uint8_t  key;      ///< SETTING_KEY_*（NVS 欄位）
    uint8_t  min;
    uint8_t  max;
    uint8_t  (*get)();
    void     (*set)(uint8_t);
} settings_slot_t;

static const settings_slot_t kSettingsSlots[] = {
    { SETTINGS_CURSOR_BRIGHTNESS, SETTING_KEY_BRIGHTNESS,
      SETTINGS_BRIGHTNESS_MIN, SETTINGS_BRIGHTNESS_MAX, getBrightness, setBrightness },
    { SETTINGS_CURSOR_SYSTEM_VOL, SETTING_KEY_SYSTEM_VOL,
      SETTINGS_VOLUME_MIN, SETTINGS_VOLUME_MAX, getSystemVolume, setSystemVolume },
    { SETTINGS_CURSOR_VENT_VOL, SETTING_KEY_VENT_VOL,
      SETTINGS_VENT_VOLUME_MIN, SETTINGS_VENT_VOLUME_MAX, getVentVolume, setVentVolume },
};

/**
 * 依游標調整當前設定值，clamp 在值域內並寫回 NVS。
 *
 * @param delta 增減量（+1 = UP / -1 = DOWN）
 */
static void adjustCurrentSetting(int8_t delta) {
    // STEP 01: 查表找出當前游標對應的設定；游標停在裝置名稱等不可調項目時直接略過
    const settings_slot_t* slot = nullptr;
    for (size_t i = 0; i < sizeof(kSettingsSlots) / sizeof(kSettingsSlots[0]); i++) {
        if (kSettingsSlots[i].cursor == settingsCursor) {
            slot = &kSettingsSlots[i];
            break;
        }
    }
    if (slot == nullptr) {
        return;
    }

    // STEP 02: 以 int16_t 計算避免 uint8_t 在 min=0 時 -1 下溢成 255
    int16_t next = (int16_t)slot->get() + delta;
    if (next < (int16_t)slot->min) {
        next = slot->min;
    }
    if (next > (int16_t)slot->max) {
        next = slot->max;
    }

    // STEP 03: 更新 UI 值並持久化，寫入失敗必須留痕（原本回傳值被直接丟棄）
    slot->set((uint8_t)next);
    if (!settings_write(&g_settings_state, slot->key, (uint8_t)next)) {
        Serial.printf("[SETTINGS] ERROR 寫入失敗 key=0x%02X value=%d\n", slot->key, (int)next);
    }
}

/**
 * 掃描 storage，更新裝置名稱鎖定狀態（g_device_name_locked）。
 *
 * 只在進入設定選單時呼叫一次——lock 狀態僅由「儲存新案件」或「同步完成」改變，
 * 兩者都不可能在設定選單內發生，因此不需每次重繪都掃 storage。
 *
 * 判準：任一 mode 存在未同步案件即鎖定。理由見 §2.2.5（改名會讓未同步的舊案件
 * 帶著新名字送出，與錄製當下的裝置不符）。
 */
static void refreshDeviceNameLock() {
    // STEP 01: 借用 static 掃描緩衝，避免在 stack 上放 EMS_STORAGE_OHCA_CAP 筆 meta
    static case_meta_t s_lock_scan[EMS_STORAGE_OHCA_CAP];

    // STEP 02: OHCA 與 Training 兩種案件都要檢查，任一有未同步即鎖定
    const CaseMode modes[] = { CASE_MODE_OHCA, CASE_MODE_TRAINING };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        const uint16_t n = storage_list(&g_storage_be, modes[i], s_lock_scan, EMS_STORAGE_OHCA_CAP);
        if (storage_has_unsynced_case(s_lock_scan, n)) {
            g_device_name_locked = true;
            Serial.printf("[SETTINGS] device name locked — mode %d 有未同步案件\n", (int)modes[i]);
            return;
        }
    }

    // STEP 03: 全部已同步 → 解鎖
    g_device_name_locked = false;
}


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

    // ===== SoT §16.7：已同步案件再次同步的確認 modal（攔截所有按鍵） =====
    //   resyncConfirmShown 僅由 handleSummarySubmenuPrimary 在 SUMMARY 內設起，
    //   故此處全域攔截不會誤食其他畫面的按鍵。主鍵確認 / 返回取消，其餘忽略。
    if (resyncConfirmShown) {
        if (btnIdx == BTN_PRIMARY) {
            resyncConfirmShown = false;
            Serial.println("[SYNC] resync confirmed");
            enterSyncFlow();
        } else if (btnIdx == BTN_BACK) {
            resyncConfirmShown = false;
            Serial.println("[SYNC] resync cancelled (BACK)");
        } else {
            // 忽略鍵留 trace，避免救護現場「按了沒反應」被誤判為裝置死當
            // （對齊歷史 SUMMARY default 分支的 [HIST_SUMMARY] ignored 慣例）
            Serial.printf("[SYNC] resync modal ignored btn=%u\n", btnIdx);
        }
        return;
    }

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
                        g_case_mode = CASE_MODE_OHCA;  // 還原真實案件模式（訓練後不殘留 TRAINING）
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
                    case 2: globalState = GLOBAL_TRAINING_SETUP; break;
                    case 3:  // 歷史紀錄（Phase E + W6 分類）
                        // W6：進入時先顯示分類層（OHCA / Training）
                        historyTypeCursor   = 0;
                        g_history_type      = CASE_MODE_OHCA;
                        historyCount        = 0;
                        historyCursor       = 0;
                        historyScrollOffset = 0;
                        historySummaryMode  = false;
                        globalState         = GLOBAL_HISTORY_PLACEHOLDER;
                        ohcaSubState        = SUBSTATE_HISTORY_CATEGORY;
                        break;
                    case 4:  // 系統設定（Phase G）
                        refreshDeviceNameLock();  // 進選單前掃一次未同步案件，決定裝置名稱可否修改
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
        // STEP 01.5: W6 歷史分類層（OHCA / Training）
        if (ohcaSubState == SUBSTATE_HISTORY_CATEGORY) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                historyTypeCursor = (historyTypeCursor + 1) % 2;  // 0↔1 循環
                return;
            }
            if (btnIdx == BTN_BACK) {
                enterMainMenu();
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 依 cursor 載入對應類型
                g_history_type = (historyTypeCursor == 0) ? CASE_MODE_OHCA : CASE_MODE_TRAINING;
                if (g_storage_ready) {
                    // 傳緩衝區實際容量（historyCases[] = OHCA_CAP=50）；OHCA 歷史勿誤截成 TRAINING_CAP(20)
                    historyCount = storage_list(&g_storage_be,
                                                g_history_type,
                                                historyCases,
                                                EMS_STORAGE_OHCA_CAP);
                } else {
                    historyCount = 0;
                }
                historyCursor       = 0;
                historyScrollOffset = 0;
                historySummaryMode  = false;
                ohcaSubState        = SUBSTATE_NONE;
                Serial.printf("[HIST] type=%u count=%u\n", (unsigned)g_history_type, (unsigned)historyCount);
            }
            return;
        }

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
                // STEP 02.01: 選定後依類型分流：Training → 操作選單；OHCA → 載入 SUMMARY
                if (historyCount > 0 && g_storage_ready) {
                    if (g_history_type == CASE_MODE_TRAINING) {
                        // W7：Training 歷史 → 操作選單（不直接載入）
                        ohcaSubState        = SUBSTATE_TRAINING_HISTORY_OPT;
                        trainingHistoryOptionsCursor = 0;
                        Serial.println("[HIST] Training options menu");
                    } else {
                        // OHCA：載入該案件、跳到 SUMMARY 子畫面
                        uint16_t loaded = 0;
                        bool ok = storage_load_events(&g_storage_be,
                                                       g_history_type,
                                                       historyCases[historyCursor].id,
                                                      events, MAX_EVENTS, &loaded);
                        if (ok) {
                            eventCount = loaded;
                            historySummaryMode   = true;
                            summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                            resyncConfirmShown   = false;  // 進 SUMMARY 初始化：無殘留 §16.7 dialog
                            Serial.printf("[STORAGE] loaded case %s (%u events)\n",
                                          historyCases[historyCursor].id,
                                          loaded);
                        } else {
                            Serial.printf("[STORAGE] load failed for %s\n",
                                          historyCases[historyCursor].id);
                        }
                    }
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== W3：GLOBAL_TRAINING_SETUP 倒數選擇畫面 =====
    if (globalState == GLOBAL_TRAINING_SETUP) {
        switch (btnIdx) {
            case BTN_UP:
                trainingSetupCursor = (trainingSetupCursor + 2) % 3;  // 2→0 循環
                break;
            case BTN_DOWN:
                trainingSetupCursor = (trainingSetupCursor + 1) % 3;  // 0→1→2 循環
                break;
            case BTN_BACK:
                enterMainMenu();
                break;
            case BTN_PRIMARY: {
                // STEP 01: 依 cursor 設定倒數週期 + 模式
                switch (trainingSetupCursor) {
                    case 0: g_training_epi_cycle_ms = TRAINING_CYCLE_30S;  break;  // 30 秒
                    case 1: g_training_epi_cycle_ms = TRAINING_CYCLE_60S;  break;  // 60 秒
                    case 2: g_training_epi_cycle_ms = TRAINING_CYCLE_240S; break;  // 240 秒
                }
                g_case_mode = CASE_MODE_TRAINING;
                Serial.printf("[TRAINING] cycle=%u mode=TRAINING\n", g_training_epi_cycle_ms);
                // STEP 02: case-start 初始化（複製自 case 0 並額外重置 ohcaState；case 0 變更時需手動同步此處）
                ohcaState         = OHCA_STATE_MAIN_MENU;
                ohcaLastEpiMs     = 0;
                ohcaPrevSinceMs   = 0;
                alarmMuted        = false;
                eventCount        = 0;
                nextEventId       = 1;
                caseStartMs       = millis();
                caseStartEpochMs  = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 對時前 = 0；不補會帶入前次案件殘留 epoch
                g_ohca_live_synced_at_ms = 0;  // SoT §16.6 新 case 起始為「App未同步」
                // STEP 03: 切到 GLOBAL_OHCA（中段完全複用 OHCA 狀態機）
                globalState       = GLOBAL_OHCA;
                dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, 0);
                startFlashStartMs = millis();
                resetSubState();  // 清子狀態游標/prompt，避免帶入前次案件殘值
                Serial.println("[TRAINING] enter GLOBAL_OHCA (START_FLASH)");
                break;
            }
            default:
                break;
        }
        return;
    }

    // ===== 系統設定選單 =====
    if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
        // STEP 01: 恢復預設確認對話框中
        if (settingsRestoreConfirm) {
            if (btnIdx == BTN_PRIMARY) {
                settings_state_t gstate;
                settings_init(&gstate);
                settings_reset_defaults(&gstate);
                setBrightness(gstate.brightness);
                setSystemVolume(gstate.system_volume);
                setVentVolume(gstate.vent_volume);
                settingsRestoreConfirm = false;
                Serial.println("[SETTINGS] defaults restored");
                return;
            }
            if (btnIdx == BTN_BACK) {
                cancelRestore();
                settingsRestoreConfirm = false;
                Serial.println("[SETTINGS] restore cancelled");
                return;
            }
            return;
        }

        // STEP 02: 編輯器模式（數值調整 — 硬體無左右鍵，用 UP/DOWN）
        if (settingsEditorMode) {
            if (btnIdx == BTN_UP) {
                adjustCurrentSetting(+1);
                return;
            }
            if (btnIdx == BTN_DOWN) {
                adjustCurrentSetting(-1);
                return;
            }
            if (btnIdx == BTN_BACK) {
                settingsEditorMode = false;
                return;
            }
            return;
        }

        // STEP 03: 主選單模式
        switch (btnIdx) {
            case BTN_UP:
                settingsCursor = (settingsCursor + SETTINGS_MENU_COUNT - 1) % SETTINGS_MENU_COUNT;
                break;
            case BTN_DOWN:
                settingsCursor = (settingsCursor + 1) % SETTINGS_MENU_COUNT;
                break;
            case BTN_BACK:
                enterMainMenu();
                break;
            case BTN_PRIMARY:
                if (settingsCursor == SETTINGS_CURSOR_DEVICE_NAME) {
                    // §2.2.5：有未同步案件 → 裝置名稱鎖定，主鍵不可進入
                    if (g_device_name_locked) {
                        Serial.println("[SETTINGS] device name locked — 有未同步案件");
                    } else {
                        Serial.println("[SETTINGS] device name — show sub（子畫面尚未接線，見 §2.2.3）");
                    }
                } else if (settingsCursor >= SETTINGS_CURSOR_BRIGHTNESS &&
                           settingsCursor <= SETTINGS_CURSOR_VENT_VOL) {
                    // 三個可調項目行為一致：進入編輯模式
                    settingsEditorMode = true;
                }
                break;
            default:
                break;
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
            // W8：Training 加「重置訓練」選項
            const bool is_training = (g_case_mode == CASE_MODE_TRAINING);
            // V1 §14.9 選項（B3：加「案件簡版總覽」對齊 demo）：
            //   未開啟：[0] Enable [1] Summary [2] Back (+ Training)
            //   已開啟：[0] Pause/Resume [1] Disable [2] Summary [3] Back (+ Training)
            uint8_t cnt = ohcaVentOverlayEnabled ? (is_training ? 5 : 4) : (is_training ? 4 : 3);
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
                const uint8_t resetIdx   = cnt - 1;  // Training 重置訓練（最後一項）
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
                // W8：Training 重置訓練確認
                if (is_training && backfillCursor == resetIdx) {
                    trainingResetConfirm = true;
                    ohcaSubState         = SUBSTATE_RESET_CONFIRM;
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

        // STEP 02.5: W7 TRAINING_HISTORY_OPT：Training 歷史操作選單
        if (ohcaSubState == SUBSTATE_TRAINING_HISTORY_OPT) {
            if (btnIdx == BTN_UP) {
                trainingHistoryOptionsCursor = (trainingHistoryOptionsCursor + 3) % 4;
                return;
            }
            if (btnIdx == BTN_DOWN) {
                trainingHistoryOptionsCursor = (trainingHistoryOptionsCursor + 1) % 4;
                return;
            }
            if (btnIdx == BTN_BACK) {
                ohcaSubState = SUBSTATE_NONE;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 0=查看總覽 / 1=同步 / 2=刪除 / 3=返回
                if (trainingHistoryOptionsCursor == 0) {
                    // 查看總覽：載入事件 → SUMMARY
                    uint16_t loaded = 0;
                    bool ok = storage_load_events(&g_storage_be,
                                                   g_history_type,
                                                   historyCases[historyCursor].id,
                                                  events, MAX_EVENTS, &loaded);
                    if (ok) {
                        eventCount = loaded;
                        historySummaryMode   = true;
                        summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                        resyncConfirmShown   = false;
                    }
                    ohcaSubState = SUBSTATE_NONE;
                } else if (trainingHistoryOptionsCursor == 1) {
                    // 同步至 App
                    g_sync_target.clear();
                    strncpy(g_sync_target.id,
                            historyCases[historyCursor].id,
                            sizeof(g_sync_target.id) - 1);
                    g_sync_target.type = g_history_type;
                    enterSyncFlow();
                    ohcaSubState = SUBSTATE_NONE;
                } else if (trainingHistoryOptionsCursor == 2) {
                    // 刪除：顯示二次確認
                    trainingDeleteConfirm = true;
                    ohcaSubState         = SUBSTATE_DELETE_CONFIRM;
                } else {
                    // 返回
                    ohcaSubState = SUBSTATE_NONE;
                }
                return;
            }
        }

        // STEP 02.6: W7 DELETE_CONFIRM：刪除二次確認
        if (ohcaSubState == SUBSTATE_DELETE_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                trainingDeleteConfirm = false;
                ohcaSubState          = SUBSTATE_NONE;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 執行刪除
                bool ok = storage_delete(&g_storage_be,
                                         g_history_type,
                                         historyCases[historyCursor].id);
                Serial.printf("[STORAGE] delete %s %s %s\n",
                              g_history_type == CASE_MODE_OHCA ? "OHCA" : "TRAINING",
                              historyCases[historyCursor].id,
                              ok ? "OK" : "FAILED");
                trainingDeleteConfirm = false;
                if (ok) {
                    // 重新抓 list（傳緩衝區實際容量，OHCA 歷史勿誤截成 20）
                    if (g_storage_ready) {
                        historyCount = storage_list(&g_storage_be,
                                                    g_history_type,
                                                    historyCases,
                                                    EMS_STORAGE_OHCA_CAP);
                    } else {
                        historyCount = 0;
                    }
                    historyCursor       = 0;
                    historyScrollOffset = 0;
                }
                ohcaSubState = SUBSTATE_NONE;
                return;
            }
        }

        // STEP 02.7: W8 RESET_CONFIRM：重置訓練二次確認
        if (ohcaSubState == SUBSTATE_RESET_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                trainingResetConfirm = false;
                ohcaSubState         = SUBSTATE_NONE;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 重置訓練：清空事件並比照案件入口重新起始（狀態機回 START_FLASH → WAIT_FIRST_EPI，真正重新計時）
                eventCount   = 0;
                nextEventId  = 1;
                ohcaLastEpiMs = 0;
                ohcaPrevSinceMs = 0;
                alarmMuted    = false;
                showEpiArmedPrompt = false;
                showShockArmedPrompt = false;
                showAmioArmedPrompt = false;
                caseStartMs   = millis();
                caseStartEpochMs = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 重新計時：重設案件起始 epoch
                g_locked_saved = false;  // 重置後的新案件允許再次存檔
                ohcaState     = OHCA_STATE_MAIN_MENU;
                dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, 0);  // 推進 START_FLASH → WAIT_FIRST_EPI（避免停在無渲染的 MAIN_MENU 死畫面）
                startFlashStartMs = millis();
                triggerFlash("已重置訓練", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                trainingResetConfirm = false;
                ohcaSubState         = SUBSTATE_NONE;
                Serial.println("[TRAINING] reset confirmed");
                return;
            }
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

        // STEP 03b: TRAINING_SAVE：上下鍵切換保存/不保存；主鍵確認；返回鍵不保存
        if (ohcaSubState == SUBSTATE_TRAINING_SAVE) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                trainingSaveCursor ^= 1;  // 0 ↔ 1
                return;
            }
            if (btnIdx == BTN_BACK) {
                trainingSaveCursor = 1;  // 返回鍵 = 不保存（下方合併分支處理離開）
            }
            if (btnIdx == BTN_PRIMARY || btnIdx == BTN_BACK) {
                if (trainingSaveCursor == 0) {
                    // 保存：寫入 storage
                    if (g_storage_ready && !g_locked_saved) {
                        const ems::CaseEpochs ce =
                            ems::compute_case_epochs(caseStartEpochMs, &g_ts_state, millis());
                        bool ok = storage_save_case(&g_storage_be, CASE_MODE_TRAINING,
                                                    events, eventCount,
                                                    ce.start_ms,
                                                    ce.end_ms);
                        Serial.printf("[STORAGE] save training case (%u events) %s\n",
                                      eventCount, ok ? "OK" : "FAILED");
                        g_locked_saved = ok;
                        // W9：儲存失敗狀態（Training 路徑）
                        g_storage_failure = ok ? 0 : 2;  // 2 = Training 保存失敗
                    }
                }
                // 無論保存與否，都進入 SUMMARY 顯示
                ohcaState = OHCA_STATE_SUMMARY;
                ohcaSubState = SUBSTATE_NONE;
                resetSubState();
                return;
            }
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
                resyncConfirmShown   = false;  // 進 SUMMARY 初始化：無殘留 §16.7 dialog
                return;
            }
            // STEP 04: SUMMARY 主鍵 — 觸發 sub-menu 當前 cursor 對應行為（SoT V1 §11.1）
            if (ohcaState == OHCA_STATE_SUMMARY) {
                // W9：儲存失敗重試（主鍵在 SUMMARY 且 g_storage_failure != 0 時重試存檔）
                if (g_storage_failure != 0) {
                    const ems::CaseEpochs ce =
                        ems::compute_case_epochs(caseStartEpochMs, &g_ts_state, millis());
                    bool ok = storage_save_case(&g_storage_be, g_case_mode,
                                                events, eventCount,
                                                ce.start_ms,
                                                ce.end_ms);
                    Serial.printf("[STORAGE] retry save (%u events) %s\n",
                                  eventCount, ok ? "OK" : "FAILED");
                    if (ok) {
                        g_locked_saved = true;
                        g_storage_failure = 0;
                        triggerFlash("已存入歷史", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                    } else {
                        g_storage_failure = (g_case_mode == CASE_MODE_OHCA) ? 1 : 2;
                        Serial.println("[STORAGE] WARN retry failed; keep warning");
                    }
                    return;
                }
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
            // W7 / W8：刪除 / 重置確認對話中 → 返回鍵 = 取消
            if (trainingDeleteConfirm) {
                trainingDeleteConfirm = false;
                ohcaSubState          = SUBSTATE_NONE;
                return;
            }
            if (trainingResetConfirm) {
                trainingResetConfirm = false;
                ohcaSubState         = SUBSTATE_NONE;
                return;
            }
            // A1：兩段確認對話顯示中 → 返回鍵 = 取消對話（modal 行為）
            if (showEpiArmedPrompt || showShockArmedPrompt) {
                showEpiArmedPrompt   = false;
                showShockArmedPrompt = false;
                Serial.println("[OHCA] confirm dialog cancelled (BACK)");
                return;
            }
            // STEP 01: SUMMARY 返回主功能表
            if (ohcaState == OHCA_STATE_SUMMARY) {
                resyncConfirmShown = false;  // 離開 SUMMARY：清 §16.7 dialog 旗標（lifecycle hygiene）
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

    // §16.7 resync 確認 modal 顯示中：長按一律忽略，與 onShortPress 開頭攔截相呼應
    // （dialog 無長按操作，故長按全忽略），確保「modal 期間攔截所有按鍵」不變式成立
    if (resyncConfirmShown) {
        Serial.printf("[SYNC] resync modal ignored long btn=%u\n", btnIdx);
        return;
    }

    // STEP 01.5: 系統設定選單 → 主鍵長按彈出恢復預設確認對話框
    if (btnIdx == BTN_PRIMARY && globalState == GLOBAL_SETTINGS_PLACEHOLDER &&
        !settingsEditorMode && !settingsRestoreConfirm) {
        settingsRestoreConfirm = true;
        Serial.println("[SETTINGS] restore confirm dialog");
        return;
    }

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
 * 進入 BLE 同步流程：記返回目的地與同步目標、切 GLOBAL_SYNC、dispatch START。
 *
 * START_SYNC（未同步案件直接同步）與 §16.7 resync 確認後的路徑共用此函式。
 * dispatch 被拒則 rollback 回呼叫端 SUMMARY（OHCA 案件總覽或歷史總覽）。
 */
static void enterSyncFlow() {
    // STEP 01: 記下返回目的地（SoT §16.5 同步完成自動回案件總覽）
    g_sync_return_to = global_to_sync_return(globalState);
    // STEP 02: 記下同步目標（SoT §16.6 寫回 storage 用）
    g_sync_target.clear();
    if (historySummaryMode && historyCursor < historyCount) {
        strncpy(g_sync_target.id,
                historyCases[historyCursor].id,
                sizeof(g_sync_target.id) - 1);
        g_sync_target.type = historyCases[historyCursor].type;
    } else if (g_storage_ready) {
        // W6：fallback 依當前歷史類型載入最新案件
        case_meta_t latest;
        if (storage_list(&g_storage_be, g_history_type, &latest, 1) > 0) {
            strncpy(g_sync_target.id, latest.id, sizeof(g_sync_target.id) - 1);
        } else {
            Serial.printf("[SYNC] WARN no %s case to tag synced_at\n",
                          g_history_type == CASE_MODE_OHCA ? "OHCA" : "TRAINING");
        }
    }
    // STEP 03: 切 GLOBAL_SYNC 並 dispatch START（已連線則補 dispatch BLE_CONNECTED 推進狀態）
    globalState = GLOBAL_SYNC;
    const bool started = ems::sync_dispatcher_dispatch(
        &g_sync_ctx, ems::SyncEvent::START, millis());
    if (started && g_ble.connected()) {
        ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BLE_CONNECTED, millis());
    }
    // STEP 04: dispatch rejected → rollback（bool 介面直接知道是否被接受）
    if (!started) {
        Serial.printf("[SYNC] dispatcher rejected START, rollback to %u\n",
                      (unsigned)g_sync_return_to);
        globalState = sync_return_to_global(g_sync_return_to);
        return;
    }
    Serial.printf("[SYNC] enter state=%u (return_to=%u)\n",
                  (unsigned)g_sync_ctx.state, (unsigned)g_sync_return_to);
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
            // SoT §16.7：已同步案件再次同步 → 開二次確認 dialog（不直接進同步）。
            // 主鍵確認 / 返回取消由 onShortPress 開頭的 resync modal 攔截處理。
            resyncConfirmShown = true;
            Serial.println("[SYNC] already synced, resync confirm dialog opened");
            return;

        case ems::SummaryAction::START_SYNC:
            enterSyncFlow();
            return;

        case ems::SummaryAction::SYNC_BLOCKED_REENTRY:
            Serial.println("[SYNC] re-entry detected, keep g_sync_return_to");
            return;

        case ems::SummaryAction::UNKNOWN_CURSOR:
            Serial.printf("[SUMMARY] unhandled cursor=%u\n", summarySubmenuCursor);
            return;
    }
}

