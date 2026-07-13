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
    uint16_t flags;              ///< bit-packed prompt/overlay 狀態
};


/// Snapshot flag bits（每個 bit 對應一個 prompt/overlay 狀態）
enum DisplaySnapshotFlag : uint16_t {
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

    return s;
}


/// snapshot 相同 → 不需重繪。
inline bool snapshotsEqual(const DisplaySnapshot& a, const DisplaySnapshot& b) {
    return memcmp(&a, &b, sizeof(DisplaySnapshot)) == 0;
}

#endif  // EMS_DISPLAY_SNAPSHOT_H
