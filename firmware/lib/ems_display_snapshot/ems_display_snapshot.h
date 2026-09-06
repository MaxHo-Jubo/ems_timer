/**
 * EMS Timer — DisplaySnapshot 純邏輯抽出
 *
 * 為什麼存在：Phase E 歷史頁面按 UP/DOWN 無重繪 bug 的根因是 `DisplaySnapshot`
 * 漏掉 `historyCursor` 欄位 → memcmp 看不到變化 → updateDisplay 早 return。
 * 把 struct + 映射邏輯抽到獨立 header 後，native 環境可寫 L2 regression test：
 *   - 每個 input 欄位獨立改變 → snapshot 必跟著變
 *   - 每個 bit flag 對應唯一 mask
 *   - memcmp 比較行為穩定（padding 在 native gcc / xtensa-gcc 一致）
 *
 * 設計原則：捕捉時間相關的衍生值（countdownSec / ventBeat / alarmingFlashOn）
 * 由呼叫端先算好，避免 lib 依賴 millis()/OHCA_EPI_CYCLE_MS_DEFAULT 等 runtime / 巨集常數。
 *
 * 對齊：
 *   - src/main.cpp 1682-1742（搬過來的本體）
 *   - docs/EMS_DoseSync_Pro_Test_Plan_V1.md §1 測試金字塔 L2
 *   - tasks/testing-infrastructure.md #2
 */
#ifndef EMS_DISPLAY_SNAPSHOT_H
#define EMS_DISPLAY_SNAPSHOT_H

#include <stdint.h>
#include <string.h>  // memcmp


/**
 * DisplaySnapshot：每次 updateDisplay 開頭擷取一份，與 last snapshot memcmp 去重。
 *
 * 欄位變化 = 必觸發重繪。新增 user-visible 狀態時：
 *   1. 加欄位到此 struct
 *   2. 在 DisplaySnapshotInputs 加對應 input
 *   3. 在 captureSnapshot() 做映射
 *   4. test_display_snapshot/ 加 regression case
 */
struct DisplaySnapshot {
    uint8_t  globalState;
    uint8_t  ohcaState;
    uint8_t  ohcaSubState;
    uint8_t  syncState;          ///< Phase F MVP2：ems::SyncState 列舉值（0=IDLE）
    uint8_t  mainMenuCursor;
    uint8_t  backfillCursor;     ///< QuickMenu/Backfill/Drug 共用同一變數
    uint8_t  summarySubmenuCursor;  ///< Phase F MVP2-Followup：OHCA 案件總覽 sub-menu cursor（SoT V1 §11.1）
    uint8_t  endCheckCursor;     ///< OHCA_END_CHECK 3 項 cursor
    uint32_t countdownSec;       ///< OHCA 倒數/超時當前顯示秒數
    uint8_t  ventBeat;           ///< 6sec 通氣節奏目前秒（0-5）
    uint8_t  ventVolume;
    bool     ventPaused;
    uint16_t historyCursor;      ///< Phase E：歷史列表 cursor
    uint16_t historyScrollOffset;///< Phase E：歷史列表分頁起點
    uint8_t  trainingSetupCursor; ///< W3：Training 倒數選擇游標
    uint8_t  historyTypeCursor;   ///< W6：歷史分類層游標
    uint8_t  trainingHistoryOptionsCursor; ///< W7：Training 歷史操作選單游標
    uint8_t  trainingSaveCursor;  ///< W5：Training 保存/不保存游標（0=保存 / 1=不保存）
    uint8_t  storageFailure;    ///< W9：儲存失敗（0=無 / 1=OHCA 失敗 / 2=Training 失敗）
    uint8_t  settingsCursor;    ///< Phase G：系統設定選單游標（SETTINGS_CURSOR_*，範圍 0~6：
                                 ///< SoT §19.1 八項扣除 2026-09-06 移除的螢幕亮度）
    uint16_t settingsScrollOffset; ///< Phase G：設定選單捲動視窗起點；漏此欄位會讓捲動後的
                                    ///< 選單畫面不重繪（同 historyScrollOffset 的既有 pattern）
    uint8_t  settingsEditorValue; ///< Impl-Phase G：編輯畫面當前顯示的數值（系統音量/通氣音量
                                    ///< 兩者共用一個欄位，同時只會有一個編輯畫面顯示中）；漏此欄位會讓
                                    ///< UP/DOWN 調整後畫面停留在舊數值不重繪（settingsCursor/
                                    ///< settingsEditorMode 皆不變，同型 bug）
    uint8_t  batteryPercent;     ///< Phase H：電量 0~100；255 = 燃料計不在線（0 是合法讀數，不可共用）
    uint8_t  batteryChargeState; ///< Phase H：ems::ChargeState 列舉值（0=Unknown 1=Charging 2=Discharging 3=Idle）
    uint16_t batteryMillivolts;  ///< Phase H Task 13：電壓 mV。batteryPercent 只在整數百分比變動時才變，
                                  ///< 電壓連續變化，漏掉此欄位會讓電池資訊畫面顯示過期電壓且不觸發重繪
    uint32_t flags;              ///< bit-packed prompt/overlay 狀態（W8 用滿 16 bit → Phase G 擴為 32）
};


/// Snapshot flag bits（每個 bit 對應一個 prompt/overlay 狀態）
enum DisplaySnapshotFlag : uint32_t {
    SNAP_FLAG_EPI_ARMED       = 0x0001,
    SNAP_FLAG_SHOCK_ARMED     = 0x0002,
    SNAP_FLAG_AMIO_ARMED      = 0x0004,
    SNAP_FLAG_OHCA_VENT       = 0x0008,
    SNAP_FLAG_VENT_END_CHECK  = 0x0010,
    SNAP_FLAG_ALARM_MUTED     = 0x0020,
    SNAP_FLAG_VENT_BACK_HINT  = 0x0040,
    SNAP_FLAG_ALARMING_FLASH  = 0x0080,
    SNAP_FLAG_END_CONFIRM     = 0x0100,
    SNAP_FLAG_FLASH_ACTIVE    = 0x0200,
    SNAP_FLAG_VENT_PRE        = 0x0400,
    SNAP_FLAG_HISTORY_SUMMARY = 0x0800,
    SNAP_FLAG_BLE_CONNECTED   = 0x1000,  // Phase F MVP1：BLE client 連線中
    SNAP_FLAG_RESYNC_CONFIRM  = 0x2000,  // Phase F：已同步案件再次同步的確認 dialog（SoT §16.7）
    SNAP_FLAG_DELETE_CONFIRM  = 0x4000,  // W7：刪除二次確認顯示中
    SNAP_FLAG_RESET_CONFIRM   = 0x8000,  // W8：重置訓練二次確認顯示中
    // ↑ uint16_t 的 16 個 bit 至此用罄，以下為 Phase G 擴充至 uint32_t 後的新 bit
    SNAP_FLAG_SETTINGS_EDITOR = 0x00010000,  // Phase G：設定值編輯畫面顯示中
    SNAP_FLAG_SETTINGS_RESTORE_CONFIRM = 0x00020000,  // Phase G：恢復預設確認對話框顯示中
    SNAP_FLAG_BATTERY_LOW_BLINK = 0x00040000,  // Phase H：低電量 1Hz 閃爍的當前相位（每 500ms 由呼叫端翻轉）
    SNAP_FLAG_LOW_BATTERY_NOTICE = 0x00080000,  // Phase H：§13.16 低電量提示顯示中（3 秒）
    SNAP_FLAG_LOW_BATTERY_START_CONFIRM = 0x00100000,  // Phase H：§20.3 低電量開案確認框顯示中
    SNAP_FLAG_SETTINGS_BATTERY_INFO = 0x00200000,  // Phase H：電池資訊子畫面顯示中（Task 13）
    SNAP_FLAG_SETTINGS_DEVICE_INFO = 0x00400000,   // Impl-Phase G：裝置資訊子畫面顯示中
    SNAP_FLAG_SETTINGS_DEVICE_NAME_SUB = 0x00800000, // Impl-Phase G：裝置名稱子畫面顯示中
};


/**
 * captureSnapshot() 的純輸入。
 *
 * 時間相關衍生值（countdownSec / ventBeat / alarmingFlashOn）由呼叫端算好，
 * 避免 lib 依賴 millis() 與 OHCA / Vent 常數。
 */
struct DisplaySnapshotInputs {
    // STEP 01: 1:1 映射欄位
    uint8_t  globalState     = 0;
    uint8_t  ohcaState       = 0;
    uint8_t  ohcaSubState    = 0;
    uint8_t  syncState       = 0;  // Phase F MVP2：ems::SyncState 列舉值
    uint8_t  mainMenuCursor  = 0;
    uint8_t  backfillCursor  = 0;
    uint8_t  summarySubmenuCursor = 0;  // Phase F MVP2-Followup
    uint8_t  endCheckCursor  = 0;
    uint8_t  ventVolume      = 0;
    bool     ventPaused      = false;
    uint16_t historyCursor       = 0;
    uint16_t historyScrollOffset = 0;
    uint8_t  trainingSetupCursor = 0;  ///< W3：Training 倒數選擇游標（0=30s / 1=60s / 2=240s）
    uint8_t  historyTypeCursor   = 0;  ///< W6：歷史分類層游標（0=OHCA / 1=Training）
    uint8_t  trainingHistoryOptionsCursor = 0;  ///< W7：Training 歷史操作選單游標
    uint8_t  trainingSaveCursor = 0;  ///< W5：Training 保存/不保存游標（0=保存 / 1=不保存）
    uint8_t  storageFailure     = 0;  ///< W9：儲存失敗狀態（0=無 / 1=OHCA / 2=Training）
    uint8_t  settingsCursor     = 0;  ///< Phase G：系統設定選單游標（SETTINGS_CURSOR_*，範圍 0~7，
                                       ///< SoT §19.1 完整 8 項；cursor 5~7 尚未接線 BTN_PRIMARY 分派，見 Task 3）
    uint16_t settingsScrollOffset = 0; ///< Phase G：設定選單捲動視窗起點
    uint8_t  settingsEditorValue  = 0; ///< Impl-Phase G：編輯畫面當前顯示的數值

    // STEP 02: 衍生值（呼叫端先算）
    uint32_t countdownSec    = 0;
    uint8_t  ventBeat        = 0;
    bool     alarmingFlashOn = false;

    // STEP 03: bool flags → bit packed
    bool showEpiArmedPrompt    = false;
    bool showShockArmedPrompt  = false;
    bool showAmioArmedPrompt   = false;
    bool ohcaVentOverlayEnabled = false;
    bool ventEndCheckShown     = false;
    bool alarmMuted            = false;
    bool ventBackHintShown     = false;
    bool endConfirmShown       = false;
    bool flashStateActive      = false;
    bool ventPreShown          = false;
    bool historySummaryMode    = false;
    bool bleConnected          = false;  // Phase F MVP1：g_client_connected 鏡射
    bool     resyncConfirmShown    = false;  // Phase F：§16.7 再次同步確認 dialog 顯示中
    bool     trainingDeleteConfirm = false;  // W7：Training 刪除二次確認顯示中
    bool     trainingResetConfirm  = false;  // W8：重置訓練二次確認顯示中
    bool     settingsEditorMode     = false;  // Phase G：設定值編輯畫面顯示中
    bool     settingsRestoreConfirm = false;  // Phase G：恢復預設確認對話框顯示中
    bool     settingsBatteryInfo    = false;  // Phase H：電池資訊子畫面顯示中（Task 13）
    bool     settingsDeviceInfo     = false;  // Impl-Phase G：裝置資訊子畫面顯示中
    bool     settingsDeviceNameSub  = false;  // Impl-Phase G：裝置名稱子畫面顯示中

    // STEP 04: Phase H 電池欄位
    uint8_t  batteryPercent     = 255;    // Phase H：預設 255 = 不在線
    uint8_t  batteryChargeState = 0;      // Phase H：預設 Unknown
    uint16_t batteryMillivolts  = 0;      // Phase H Task 13：電壓 mV，預設 0（不在線時畫面不顯示此值）
    bool     batteryLowBlinkOn  = false;  // Phase H：低電量閃爍當前相位
    bool     lowBatteryNoticeVisible = false;  // Phase H：§13.16 提示顯示中
    bool     lowBatteryStartConfirmShown = false;  // Phase H：§20.3 低電量開案確認框顯示中
};


/**
 * 純函式：DisplaySnapshotInputs → DisplaySnapshot。
 *
 * 無副作用、不依賴 globals/Arduino.h，native 可直接覆蓋。
 */
inline DisplaySnapshot captureSnapshot(const DisplaySnapshotInputs& in) {
    DisplaySnapshot s = {};

    // STEP 01: 1:1 欄位拷貝
    s.globalState         = in.globalState;
    s.ohcaState           = in.ohcaState;
    s.ohcaSubState        = in.ohcaSubState;
    s.syncState           = in.syncState;
    s.mainMenuCursor      = in.mainMenuCursor;
    s.backfillCursor      = in.backfillCursor;
    s.summarySubmenuCursor = in.summarySubmenuCursor;
    s.endCheckCursor      = in.endCheckCursor;
    s.countdownSec        = in.countdownSec;
    s.ventBeat            = in.ventBeat;
    s.ventVolume          = in.ventVolume;
    s.ventPaused          = in.ventPaused;
    s.historyCursor       = in.historyCursor;
    s.historyScrollOffset = in.historyScrollOffset;
    s.trainingSetupCursor = in.trainingSetupCursor;
    s.historyTypeCursor   = in.historyTypeCursor;
    s.trainingHistoryOptionsCursor = in.trainingHistoryOptionsCursor;
    s.trainingSaveCursor  = in.trainingSaveCursor;
    s.storageFailure      = in.storageFailure;  // W9：儲存失敗狀態
    s.settingsCursor      = in.settingsCursor;  // Phase G：系統設定選單游標
    s.settingsScrollOffset = in.settingsScrollOffset;  // Phase G：設定選單捲動視窗起點
    s.settingsEditorValue = in.settingsEditorValue;    // Impl-Phase G：編輯畫面當前數值
    s.batteryPercent      = in.batteryPercent;      // Phase H
    s.batteryChargeState  = in.batteryChargeState;  // Phase H
    s.batteryMillivolts   = in.batteryMillivolts;   // Phase H Task 13

    // STEP 02: bool → bit-packed flags
    if (in.showEpiArmedPrompt)     s.flags |= SNAP_FLAG_EPI_ARMED;
    if (in.showShockArmedPrompt)   s.flags |= SNAP_FLAG_SHOCK_ARMED;
    if (in.showAmioArmedPrompt)    s.flags |= SNAP_FLAG_AMIO_ARMED;
    if (in.ohcaVentOverlayEnabled) s.flags |= SNAP_FLAG_OHCA_VENT;
    if (in.ventEndCheckShown)      s.flags |= SNAP_FLAG_VENT_END_CHECK;
    if (in.alarmMuted)             s.flags |= SNAP_FLAG_ALARM_MUTED;
    if (in.ventBackHintShown)      s.flags |= SNAP_FLAG_VENT_BACK_HINT;
    if (in.alarmingFlashOn)        s.flags |= SNAP_FLAG_ALARMING_FLASH;
    if (in.endConfirmShown)        s.flags |= SNAP_FLAG_END_CONFIRM;
    if (in.flashStateActive)       s.flags |= SNAP_FLAG_FLASH_ACTIVE;
    if (in.ventPreShown)           s.flags |= SNAP_FLAG_VENT_PRE;
    if (in.historySummaryMode)     s.flags |= SNAP_FLAG_HISTORY_SUMMARY;
    if (in.bleConnected)           s.flags |= SNAP_FLAG_BLE_CONNECTED;
    if (in.resyncConfirmShown)     s.flags |= SNAP_FLAG_RESYNC_CONFIRM;
    if (in.trainingDeleteConfirm)  s.flags |= SNAP_FLAG_DELETE_CONFIRM;
    if (in.trainingResetConfirm)   s.flags |= SNAP_FLAG_RESET_CONFIRM;
    if (in.settingsEditorMode)     s.flags |= SNAP_FLAG_SETTINGS_EDITOR;
    if (in.settingsRestoreConfirm) s.flags |= SNAP_FLAG_SETTINGS_RESTORE_CONFIRM;
    if (in.settingsBatteryInfo)    s.flags |= SNAP_FLAG_SETTINGS_BATTERY_INFO;
    if (in.settingsDeviceInfo)     s.flags |= SNAP_FLAG_SETTINGS_DEVICE_INFO;
    if (in.settingsDeviceNameSub) {
        s.flags |= SNAP_FLAG_SETTINGS_DEVICE_NAME_SUB;
    }
    if (in.batteryLowBlinkOn)      s.flags |= SNAP_FLAG_BATTERY_LOW_BLINK;
    if (in.lowBatteryNoticeVisible) s.flags |= SNAP_FLAG_LOW_BATTERY_NOTICE;
    if (in.lowBatteryStartConfirmShown) s.flags |= SNAP_FLAG_LOW_BATTERY_START_CONFIRM;

    return s;
}


/// snapshot 相同 → 不需重繪。
inline bool snapshotsEqual(const DisplaySnapshot& a, const DisplaySnapshot& b) {
    return memcmp(&a, &b, sizeof(DisplaySnapshot)) == 0;
}


/**
 * 除 countdownSec 外其餘欄位皆相同 → updateDisplay() 可只重繪倒數時間區塊（partial update）。
 *
 * updateDisplay() 原先手寫逐欄位比對，每次新增 snapshot 欄位都得記得同步加進那份清單，
 * Phase H 的兩個電池欄位就漏了這一處：倒數中電量若與 countdownSec 在同一 frame 變化，
 * 會走 partial 路徑而把新電量寫進 lastDisplaySnapshot，該次變化被永久吞掉。
 * 改為「把 countdownSec 對齊後整包 memcmp」，往後新增任何欄位都自動納入比對。
 *
 * 前提：DisplaySnapshot 是 trivially copyable 的 POD，且兩份 snapshot 都出自
 * captureSnapshot() 的 `DisplaySnapshot s = {};`（padding 已清零），與 snapshotsEqual()
 * 依賴的是同一組前提。
 *
 * @param a 本次 snapshot
 * @param b 上次 snapshot
 * @return true = 僅 countdownSec 可能不同（其餘全等，可走 partial）；false = 有其他欄位變化，須完整重繪
 */
inline bool snapshotsEqualExceptCountdown(const DisplaySnapshot& a, const DisplaySnapshot& b) {
    // STEP 01: 複製一份並把 countdownSec 對齊，讓該欄位不影響後續整包比對
    DisplaySnapshot probe = a;
    probe.countdownSec = b.countdownSec;

    // STEP 02: 其餘欄位整包比對
    return memcmp(&probe, &b, sizeof(DisplaySnapshot)) == 0;
}

#endif  // EMS_DISPLAY_SNAPSHOT_H
