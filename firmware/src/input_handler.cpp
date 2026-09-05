#include "app_globals.h"

// Wave 1：系統設定 UI（brightness/volume getter/setter；含 advanceSettingsCursorAndScroll()
// ——settingsScrollOffset 跟 settingsCursor 的成對更新，見下方 BTN_UP/BTN_DOWN 分支）
#include "ui_settings.h"

// Wave 1：系統設定 NVS 讀寫（settings_state_t, settings_init, settings_reset_defaults）
#include "ems_settings.h"

// Wave 2：案件模式（CaseMode）
#include "ems_storage_logic.h"

// Wave 1：系統設定選單狀態（settingsCursor / editor / confirm）
// UP/DOWN 的游標 wrap-around 與捲動視窗跟隨交給 ui_settings.h 的
// advanceSettingsCursorAndScroll() 一次處理（內部呼叫 wrapSettingsCursor() +
// clampScrollOffset()），這裡只在 BTN_UP/BTN_DOWN 分支呼叫它，不在此另外複製
// 一份算式或常數，也不會有機會只更新 cursor／scroll_offset 其中一個。

// Dev-Phase G: 設定 UI 狀態（定義於 main.cpp）
extern uint8_t settingsCursor;        // 設定選單游標（SETTINGS_CURSOR_*，範圍 0~7，Impl-Phase G
                                       //   擴充至 SoT §19.1 完整 8 項；本檔 BTN_PRIMARY 分派
                                       //   已涵蓋 cursor 0~7（Task 3 補齊 5~7：App連線設定／
                                       //   Type-C連線／裝置資訊）
extern uint16_t settingsScrollOffset; // 設定選單捲動視窗起點，跟 settingsCursor 成對更新，
                                       //   見下方 BTN_UP/BTN_DOWN 分支
extern bool    settingsEditorMode;    // true = 編輯模式（左右鍵調整數值）
extern bool    settingsRestoreConfirm; // true = 恢復預設確認對話框顯示中
extern bool    settingsBatteryInfoMode; // Phase H：true = 電池資訊子畫面顯示中（Task 13）
extern bool    settingsDeviceInfoMode;  // Impl-Phase G：true = 裝置資訊子畫面顯示中
extern bool    settingsDeviceNameSubMode; // Impl-Phase G：true = 裝置名稱子畫面顯示中

// 系統設定 NVS state（開機由 main.cpp settings_init 載入，調值時 settings_write 寫回）
settings_state_t g_settings_state;

// 前置宣告：進入 BLE 同步流程（START_SYNC 與 §16.7 resync 確認後共用，定義見下方）
static void enterSyncFlow();

// 前置宣告：建立並啟動一筆 OHCA 案件（主選單直接進案與 §20.3 低電量確認後進案共用，定義見下方）
static void startOhcaCase();

// 前置宣告：進入 VENT_PRE 預覽畫面（主選單「6 秒通氣節奏」入口呼叫，定義見下方）
static void startVentPreview();

// 前置宣告：建立並啟動一筆 Training 案件（Training 設定畫面選定週期後直接進案與 §20.3
// 低電量確認後進案共用，定義見下方）
static void startTrainingCase(uint32_t cycle_ms);

// 前置宣告：真正啟動 6 秒通氣節奏（VENT_PRE → running），直接啟動與 §20.3 低電量
// 確認後啟動兩處共用，定義見下方
static void startVentActive();

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
 * 依游標查表取得目前可調設定的顯示值，供 main.cpp 的 DisplaySnapshot 擷取共用
 * 同一份 cursor→getter 映射（kSettingsSlots），取代另外散寫一份 if/ternary 分支
 * ——後者是本檔 adjustCurrentSetting() 已有的邏輯，新增第二份等於同一映射存在
 * 兩處，改其中一處容易漏改另一處。
 *
 * @param cursor SETTINGS_CURSOR_*；不在 kSettingsSlots 之列時（如裝置名稱等
 *               不可調項目）回傳 0——呼叫端只在 settingsEditorMode 為 true 時
 *               取用此值，而 settingsEditorMode 只會在 cursor 落於可調範圍內
 *               才被設為 true（見本檔 BTN_PRIMARY 分派），故 0 這條路徑目前
 *               不可達，不需另加 fail-fast。
 * @return 該設定目前顯示值
 */
uint8_t getCurrentSettingValue(uint8_t cursor) {
    for (size_t i = 0; i < sizeof(kSettingsSlots) / sizeof(kSettingsSlots[0]); i++) {
        if (kSettingsSlots[i].cursor == cursor) {
            return kSettingsSlots[i].get();
        }
    }
    return 0;
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
 * 是否有全域攔截型 modal（攔截所有按鍵）顯示中。handleButtons() 用來比對
 * onShortPress() 呼叫前後 modal 狀態是否改變；未來新增同類全域 modal（攔截所有按鍵、
 * 不看 globalState 就在 onShortPress() 開頭全域判斷的那種），只需要在這裡加一項 OR 條件。
 *
 * 已查證的前提：目前這兩個 modal 都只能由 onShortPress() 開啟或關閉——
 * onLongPress() 開頭就對兩者各有一段 early return，且函式內沒有任何
 * requestLowBatteryStartConfirm() 呼叫或 resyncConfirmShown 賦值。因此 handleButtons()
 * 只在 onShortPress() 前後比對即可涵蓋全部轉換。若未來有長按路徑會開啟同類 modal，
 * STEP 02 的長按分派也必須比照 STEP 01.03.01 加上前後比對，否則同輪後續按鍵會漏吞。
 *
 * @param  無參數
 * @return 是否有 modal 顯示中
 */
static bool isBlockingModalActive() {
    // STEP 01: 聚合目前所有「攔截全部按鍵」的 modal 顯示狀態
    return resyncConfirmShown || (g_lowBatteryConfirmTarget != ems::LowBatteryConfirmTarget::None);
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
            // STEP 01.01: 統一 debounce — press 與 release 邊緣共用同一個門檻，
            //   避免 TFT 慢渲染拉長時間窗時 bounce 邊緣穿過原本只擋 press 的防抖造成 double-fire
            if (now - lastPressMs[i] < DEBOUNCE_MS) {
                continue;
            }
            lastPressMs[i] = now;

            if (cur == LOW) {
                // STEP 01.02: 按下（press start）
                btnPressStartMs[i] = now;
                btnLongFired[i]    = false;
            } else {
                // STEP 01.03: 放開（release）
                if (btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                    uint32_t held = now - btnPressStartMs[i];
                    if (held < SHORT_PRESS_MAX_MS) {
                        // STEP 01.03.01: 呼叫前後比對全域 modal 狀態，涵蓋開啟與關閉
                        //   兩個方向的轉換——不是只有關閉。兩個方向都會讓「本輪後續按鍵
                        //   該落在哪個畫面」的答案在迴圈中途改變：關閉方向會穿透到 modal
                        //   關閉後的新畫面；開啟方向會穿透進一個剛開啟、連畫都還沒畫出來
                        //   的 modal（BTN_PRIMARY=0 開啟確認框、同輪 BTN_BACK=5 直接取消）。
                        //   modal 狀態不變時（原本就沒 modal、或按到 modal 內會被忽略的鍵）
                        //   後續按鍵照常逐一分派，不受影響。
                        const bool modalActiveBefore = isBlockingModalActive();
                        onShortPress(i);
                        const bool modalActiveAfter = isBlockingModalActive();
                        if (modalActiveBefore != modalActiveAfter) {
                            // STEP 01.03.01.01: 這次呼叫剛好讓 modal 開啟或關閉——本輪
                            //   其餘尚未掃描的按鍵直接同步物理狀態並清空追蹤，徹底吞掉，
                            //   不是延後（延後到下一輪一樣會穿透到轉換後的新畫面）。
                            //   仍按住的鍵也要把 btnPressStartMs 清為 0（不能寫 now）：
                            //   寫 now 等於幫它登記一次全新按壓，之後放開時 held 很小仍會
                            //   觸發短按、持續按住也可能觸發長按。清 0 後 STEP 01.03 與
                            //   STEP 02 的 `btnPressStartMs[j] > 0` 檢查都不成立，該鍵在
                            //   放開與持續按住兩種情況下都不產生任何事件，直到使用者真正
                            //   放開再重新按下（一次全新的 LOW 邊緣）才重新開始追蹤。
                            btnPressStartMs[i] = 0;
                            lastBtnState[i]    = cur;
                            //   `lastPressMs[j]` 也要一併同步成 now：吞鍵不更新它的話，
                            //   它會停留在該鍵上一次真正邊緣事件的舊時戳，之後若發生機械
                            //   彈跳，`now - lastPressMs[j]` 用過期時戳算出來的差值可能遠
                            //   超過 DEBOUNCE_MS，讓本該被防抖擋掉的彈跳邊緣被當成合法邊緣
                            //   （取到與穩態相反的電位時，會被誤認為一次全新按下而恢復追蹤，
                            //   吞鍵就在這條路徑上失效）。同步後防抖窗從吞鍵當下重新起算。
                            for (uint8_t j = i + 1; j < BTN_COUNT; j++) {
                                uint8_t curJ = digitalRead(BTN_PINS[j]);
                                lastBtnState[j]    = curJ;
                                btnPressStartMs[j] = 0;
                                btnLongFired[j]    = false;
                                lastPressMs[j]     = now;
                            }
                            return;
                        }
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

/** 案件剛開始（START_FLASH/開案瞬間）dispatchOhcaEvent() 的 since_ms 參數：沒有「距上次
 *  事件」的時間可算，固定傳 0。startOhcaCase()／startTrainingCase() 共用。 */
constexpr uint32_t OHCA_EVENT_SINCE_MS_AT_CASE_START = 0;

/**
 * 建立並啟動一筆 OHCA 案件。
 * 由主選單直接進案與 §20.3 低電量確認後進案兩處共用，避免兩份初始化邏輯分歧。
 * @param  無參數
 * @return void（原地寫入全域，無回傳值）
 */
static void startOhcaCase() {
    // STEP 01: 切換全域狀態與案件模式（訓練後不殘留 TRAINING）
    globalState = GLOBAL_OHCA;
    g_case_mode = CASE_MODE_OHCA;
    dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, OHCA_EVENT_SINCE_MS_AT_CASE_START);

    // STEP 02: 案件時間基準
    startFlashStartMs = millis();
    caseStartMs       = millis();
    caseStartEpochMs  = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 對時前 = 0

    // STEP 03: 事件與計數歸零
    eventCount      = 0;
    nextEventId     = 1;
    ohcaLastEpiMs   = 0;
    ohcaPrevSinceMs = 0;
    alarmMuted      = false;

    // STEP 04: SoT §16.6 新 case 起始為「App未同步」
    g_ohca_live_synced_at_ms = 0;
    resetSubState();
    Serial.println("[OHCA] Case start (START_FLASH)");
}

/**
 * 進入 VENT_PRE 預覽畫面（可調音量，尚未真正開始通氣）。
 * 由主選單「6 秒通氣節奏」入口呼叫。預覽畫面本身不耗電，不需 §20.3 低電量確認——
 * 真正開始通氣（耗電）是 VENT_PRE 內按主鍵那一刻，該處呼叫
 * requestLowBatteryStartConfirm() 由共用 helper 依 latch 狀態決定是否攔截
 * （呼叫端不自己判斷 g_battery_low，守衛收斂在 lib 內，見該函式 doc）。
 * @param  無參數
 * @return void（原地寫入全域，無回傳值）
 */
static void startVentPreview() {
    // STEP 01: 準備 preview 狀態，等使用者在 VENT_PRE 按主鍵才真正啟動（A8）
    globalState        = GLOBAL_VENT;
    ventStartMs        = 0;            // 尚未啟動
    ventPrevSinceMs    = 0;
    ventEndCheckShown  = false;
    ventBackHintShown  = false;
    ventPaused         = false;
    ventPreShown       = true;          // A8：等使用者按主鍵
    Serial.println("[VENT] enter PRE (preview)");
}

/**
 * 建立並啟動一筆 Training 案件（沿用 OHCA 狀態機，額外重置 ohcaState 起點）。
 * 由 Training 設定畫面選定週期後直接進案與 §20.3 低電量確認後進案兩處共用，避免
 * 兩份初始化邏輯分歧。
 *
 * 週期是明確參數而非函式內部偷讀 `g_training_epi_cycle_ms`：讓「呼叫前必須先設好那個
 * 全域」這個原本只靠註解要求的維護義務，變成看得見的函式簽名（2026-08-30 fix round 4 X）。
 * 兩個呼叫點都在呼叫當下傳 `g_training_epi_cycle_ms`，與原行為等價。
 *
 * 這只是文件化程度的改善，不是完整修法：週期值仍未被「捕捉」進 §20.3 確認框狀態、
 * 跨過確認框顯示期間安全攜帶。要做到那樣得在 `g_lowBatteryConfirmTarget` 之外再帶一份
 * payload，屬於更大的架構決策，且目前唯一使用者就是這一個入口，成本換不到對應的風險
 * 下降，故 park；待低電量確認框需要攜帶其他情境資料的第二個案例出現時再一併重新設計。
 *
 * @param cycle_ms 使用者已選定的 EPI 倒數週期（毫秒），呼叫端在 STEP 選擇畫面決定
 * @return void（原地寫入全域，無回傳值）
 */
static void startTrainingCase(uint32_t cycle_ms) {
    // STEP 01: 案件模式與週期
    g_training_epi_cycle_ms = cycle_ms;
    g_case_mode = CASE_MODE_TRAINING;
    Serial.printf("[TRAINING] cycle=%u mode=TRAINING\n", g_training_epi_cycle_ms);

    // STEP 02: case-start 初始化，與 startOhcaCase() 共用下列欄位（該函式 STEP 02/03 變更
    //          時需手動同步此處，欄位清單：ohcaLastEpiMs／ohcaPrevSinceMs／alarmMuted／
    //          eventCount／nextEventId／caseStartMs／caseStartEpochMs／
    //          g_ohca_live_synced_at_ms／globalState／dispatchOhcaEvent 呼叫／
    //          startFlashStartMs／resetSubState()）。額外多一行 ohcaState 重置——
    //          startOhcaCase() 沒有這行，靠 dispatchOhcaEvent() 自身的狀態機轉換；
    //          Training 需要明確重置起點，理由見 §L1 parked：合併成單一帶參數 helper
    //          可徹底解決兩份定義分歧的風險，屬於比本輪其他修正更大的架構決策，本輪不做。
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
    dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, OHCA_EVENT_SINCE_MS_AT_CASE_START);
    startFlashStartMs = millis();
    resetSubState();  // 清子狀態游標/prompt，避免帶入前次案件殘值
    Serial.println("[TRAINING] enter GLOBAL_OHCA (START_FLASH)");
}

/**
 * 真正啟動 6 秒通氣節奏（VENT_PRE → running）。由一般直接啟動與 §20.3 低電量確認後
 * 啟動兩處共用，避免兩份轉換邏輯分歧。
 * @param  無參數
 * @return void（原地寫入全域，無回傳值）
 */
static void startVentActive() {
    // STEP 01: V1 §13.5 啟動規則：秒數從 1 開始
    ventPreShown    = false;
    ventStartMs     = millis();
    ventPrevSinceMs = 0;
    Serial.println("[VENT] PRE -> running");
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

    // ===== SoT §20.3：低電量開案確認框（攔截所有按鍵，OHCA/VENT/Training 三個入口共用） =====
    //   g_lowBatteryConfirmTarget 依慣例只由 requestLowBatteryStartConfirm() 設起
    //   （型別上不強制，見 app_globals.h 宣告處註解），故此處全域攔截不會誤食
    //   其他畫面的按鍵。
    if (g_lowBatteryConfirmTarget != ems::LowBatteryConfirmTarget::None) {
        // STEP 01: 硬體按鍵索引映射成純函式看得懂的按鍵語意（純函式不依賴 app_globals.h 的硬體常數）
        const ems::ConfirmDialogAction action =
            (btnIdx == BTN_PRIMARY) ? ems::ConfirmDialogAction::Primary :
            (btnIdx == BTN_BACK)    ? ems::ConfirmDialogAction::Back :
                                       ems::ConfirmDialogAction::Other;

        // STEP 02: 呼叫純函式決策並套用新狀態——decide 前先存 target，因為套用
        //          decision.next_target 後 g_lowBatteryConfirmTarget 可能已變成 None，
        //          STEP 03 的 proceed 分支仍需要原本待啟動的目標
        const ems::LowBatteryConfirmTarget target = g_lowBatteryConfirmTarget;
        const ems::LowBatteryConfirmDecision decision =
            ems::low_battery_confirm_decide(target, action);
        g_lowBatteryConfirmTarget = decision.next_target;

        // STEP 03: 確認通過 → 依原目標啟動對應案件/通氣，三個目標都複用各自的直接啟動
        //          helper（另一個呼叫點是各自的直接啟動路徑，兩處共用避免轉換邏輯分歧）
        if (decision.proceed) {
            switch (target) {
                case ems::LowBatteryConfirmTarget::Ohca:
                    startOhcaCase();
                    break;
                case ems::LowBatteryConfirmTarget::Vent:
                    startVentActive();
                    break;
                case ems::LowBatteryConfirmTarget::Training:
                    startTrainingCase(g_training_epi_cycle_ms);
                    break;
                case ems::LowBatteryConfirmTarget::None:
                    break;  // 不可達（外層 if 已排除 None），滿足 switch 完整性
            }
        } else if (action == ems::ConfirmDialogAction::Back) {
            // STEP 04: 取消——維持原地不動。三個目標的取消行為都是「維持原地不動」
            //          （OHCA 是例外：它原本就沒進 GLOBAL_OHCA，本來就停在主選單，
            //          「不動」剛好等於「回主選單」；VENT/Training 則是留在
            //          VENT_PRE／Training 設定畫面，不可在此改動 globalState）
            Serial.printf("[BATTERY] low battery start confirm cancelled (target=%u)\n", (unsigned)target);
        } else if (action == ems::ConfirmDialogAction::Other) {
            // STEP 05: 其餘按鍵忽略，留 trace 避免「按了沒反應」被誤判為裝置死當
            Serial.printf("[BATTERY] low battery confirm ignored btn=%u\n", btnIdx);
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
                        // STEP 01.01: §20.3 — 低電量下開案需先確認，確認前不建立案件、不動 globalState
                        if (requestLowBatteryStartConfirm(ems::LowBatteryStartTarget::Ohca)) {
                            break;
                        }
                        startOhcaCase();
                        break;
                    case 1:  // 6 秒通氣節奏（獨立模式）— 預覽/調音量不耗電，不需低電量確認
                        startVentPreview();
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
                // §20.3：低電量下真正啟動通氣（開始耗電）需先確認，確認前維持 VENT_PRE
                //        原地不動（不像 OHCA 全程停在主選單，這裡本來就還在顯示 VENT_PRE）
                if (requestLowBatteryStartConfirm(ems::LowBatteryStartTarget::Vent)) {
                    return;
                }
                startVentActive();
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
                // EXTRACT-SHARED-HELPER 第二處呼叫點：捲動視窗跟隨游標的 clamp 邏輯
                // 改用共用 clampScrollOffset()（ui_scroll.h），不再各自 inline 一份，
                // 第一處呼叫點是設定選單 advanceSettingsCursorAndScroll() 內部呼叫。
                historyScrollOffset = clampScrollOffset(historyCursor, historyScrollOffset, HISTORY_VISIBLE_ROWS);
                break;
            case BTN_DOWN:
                if (historyCursor + 1 < historyCount) {
                    historyCursor++;
                }
                historyScrollOffset = clampScrollOffset(historyCursor, historyScrollOffset, HISTORY_VISIBLE_ROWS);
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
                // STEP 01: 依 cursor 設定倒數週期——使用者已經選過了，週期值要在低電量
                //          攔截之前先存好，不受確認框影響（取消時週期值留著沒關係，
                //          下次重選會覆蓋，或使用者退出設定畫面）
                switch (trainingSetupCursor) {
                    case 0: g_training_epi_cycle_ms = TRAINING_CYCLE_30S;  break;  // 30 秒
                    case 1: g_training_epi_cycle_ms = TRAINING_CYCLE_60S;  break;  // 60 秒
                    case 2: g_training_epi_cycle_ms = TRAINING_CYCLE_240S; break;  // 240 秒
                }
                // STEP 02: §20.3 — 低電量下開案需先確認，確認前不建立案件、不動 globalState
                if (requestLowBatteryStartConfirm(ems::LowBatteryStartTarget::Training)) {
                    break;
                }
                startTrainingCase(g_training_epi_cycle_ms);
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

        // STEP 03: 電池資訊子畫面中（唯讀導覽頁，無可調值 — 只有返回鍵離開）
        if (settingsBatteryInfoMode) {
            // STEP 03.01: 返回鍵離開子畫面，其餘按鍵在此唯讀畫面上一律忽略
            if (btnIdx == BTN_BACK) {
                settingsBatteryInfoMode = false;
                Serial.println("[SETTINGS] battery info — back to menu");
            }
            return;
        }

        // STEP 04: 裝置資訊子畫面中（唯讀導覽頁，同電池資訊 pattern）
        if (settingsDeviceInfoMode) {
            // STEP 04.01: 返回鍵離開子畫面，其餘按鍵在此唯讀畫面上一律忽略
            if (btnIdx == BTN_BACK) {
                settingsDeviceInfoMode = false;
                Serial.println("[SETTINGS] device info — back to menu");
            }
            return;
        }

        // STEP 04.5: 裝置名稱子畫面中（唯讀導覽頁，同電池/裝置資訊 pattern）
        if (settingsDeviceNameSubMode) {
            // STEP 04.5.01: 返回鍵離開子畫面，其餘按鍵在此唯讀畫面上一律忽略
            if (btnIdx == BTN_BACK) {
                settingsDeviceNameSubMode = false;
                Serial.println("[SETTINGS] device name sub — back to menu");
            }
            return;
        }

        // STEP 05: 主選單模式
        switch (btnIdx) {
            case BTN_UP: {
                // STEP 05.01: 游標與捲動視窗必須成對更新——c980927 的根因是這兩者
                //   原本各自 inline，只改了項目數卻沒人記得同步接上捲動視窗，游標
                //   移出可見視窗後畫面高亮消失（2026-09-02 codex Tier 3 補跑 review
                //   抓到並修正，見 89917d5）。改用 advanceSettingsCursorAndScroll()
                //   讓呼叫端拿到的是已配對好的結果，沒有機會只更新其中一個。
                SettingsCursorScroll next = advanceSettingsCursorAndScroll(settingsCursor, settingsScrollOffset, SETTINGS_CURSOR_DELTA_UP);
                settingsCursor = next.cursor;
                settingsScrollOffset = next.scroll_offset;
                break;
            }
            case BTN_DOWN: {
                // STEP 05.02: 同上，DOWN 方向
                SettingsCursorScroll next = advanceSettingsCursorAndScroll(settingsCursor, settingsScrollOffset, SETTINGS_CURSOR_DELTA_DOWN);
                settingsCursor = next.cursor;
                settingsScrollOffset = next.scroll_offset;
                break;
            }
            case BTN_BACK:
                enterMainMenu();
                break;
            case BTN_PRIMARY:
                if (settingsCursor == SETTINGS_CURSOR_DEVICE_NAME) {
                    // §2.2.5：有未同步案件 → 裝置名稱鎖定，主鍵不可進入
                    if (g_device_name_locked) {
                        Serial.println("[SETTINGS] device name locked — 有未同步案件");
                    } else {
                        settingsDeviceNameSubMode = true;
                        Serial.println("[SETTINGS] device name — show sub");
                    }
                } else if (settingsCursor >= SETTINGS_CURSOR_BRIGHTNESS &&
                           settingsCursor <= SETTINGS_CURSOR_VENT_VOL) {
                    // 三個可調項目行為一致：進入編輯模式
                    settingsEditorMode = true;
                } else if (settingsCursor == SETTINGS_CURSOR_BATTERY_INFO) {
                    // Task 13：進入電池資訊子畫面
                    settingsBatteryInfoMode = true;
                    Serial.println("[SETTINGS] battery info — show sub");
                } else if (settingsCursor == SETTINGS_CURSOR_APP_CONN) {
                    // Impl-Phase G：App連線設定尚未實作，顯示 placeholder
                    globalState = GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER;
                    Serial.println("[SETTINGS] app conn — placeholder");
                } else if (settingsCursor == SETTINGS_CURSOR_TYPEC_CONN) {
                    // Impl-Phase G：Type-C連線尚未實作，顯示 placeholder
                    globalState = GLOBAL_SETTINGS_TYPEC_PLACEHOLDER;
                    Serial.println("[SETTINGS] type-c conn — placeholder");
                } else if (settingsCursor == SETTINGS_CURSOR_DEVICE_INFO) {
                    // Impl-Phase G：進入裝置資訊子畫面
                    settingsDeviceInfoMode = true;
                    Serial.println("[SETTINGS] device info — show sub");
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Impl-Phase G：App連線設定／Type-C連線 placeholder =====
    //   比照 Training/History 主選單當年用過的 drawPlaceholder() pattern：
    //   任意鍵返回設定選單，不需要子畫面狀態機（沒有真正的畫面內容要記）。
    if (globalState == GLOBAL_SETTINGS_APP_CONN_PLACEHOLDER ||
        globalState == GLOBAL_SETTINGS_TYPEC_PLACEHOLDER) {
        globalState = GLOBAL_SETTINGS_PLACEHOLDER;
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

    // SoT §20.3 低電量開案確認框顯示中：長按一律忽略，與 onShortPress 開頭攔截相呼應
    // （dialog 無長按操作，故長按全忽略）。已查證具體壞情境：VENT 確認框顯示期間
    // globalState 仍是 GLOBAL_VENT 且 ventPreShown 仍是 true（round 1 設計故意不離開
    // VENT_PRE 畫面），若不攔截，下方 STEP 01.01 只看 globalState==GLOBAL_VENT &&
    // !ventEndCheckShown 就會把 ventEndCheckShown 設成 true——使用者在確認框顯示期間
    // 長按主鍵，之後短按「是」啟動通氣，畫面卻立刻進結束確認、updateVentTick() 停止輸出。
    if (g_lowBatteryConfirmTarget != ems::LowBatteryConfirmTarget::None) {
        Serial.printf("[BATTERY] low battery confirm ignored long btn=%u\n", btnIdx);
        return;
    }

    // STEP 01.5: 系統設定選單 → 主鍵長按彈出恢復預設確認對話框
    //   Task 13：電池資訊子畫面顯示中也要排除，否則在該唯讀子畫面長按主鍵會意外
    //   開啟一個與此畫面無關的恢復預設確認框。單純是「不要讓長按開啟它」，不是
    //   「開啟後沒有按鍵能關掉它」——若真的被開啟，onShortPress() 開頭的
    //   settingsRestoreConfirm 分支本來就會處理 BTN_PRIMARY／BTN_BACK 兩鍵關閉它。
    //   Impl-Phase G Task 3：裝置資訊子畫面比照電池資訊同理排除，同一類 bug。
    //   裝置名稱子畫面同理排除（三者同一 pattern）。
    if (btnIdx == BTN_PRIMARY && globalState == GLOBAL_SETTINGS_PLACEHOLDER &&
        !settingsEditorMode && !settingsRestoreConfirm && !settingsBatteryInfoMode &&
        !settingsDeviceInfoMode && !settingsDeviceNameSubMode) {
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

