#pragma once
/**
 * app_globals.h — EMS Timer 韌體共享標頭
 *
 * 集中所有：型別定義、硬體常數、enum/struct、extern 全域宣告、跨檔函式宣告。
 * 各 .cpp 檔 include 此標頭取得完整可見性；mutable globals 定義留在 main.cpp。
 */

// ── System ──────────────────────────────────────────────────────
#include <Arduino.h>
#include <SPI.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── Project libs ────────────────────────────────────────────────
#include "ems_ohca_state.h"
#include "ems_ohca_countdown.h"
#include "ems_two_step_confirm.h"
#include "ems_supp_model.h"
#include "ems_case_summary.h"
#include "ems_vent_metronome.h"
#include "ems_storage_logic.h"
#include "ems_storage_fs.h"
#include "ems_display_snapshot.h"
#include "ems_time_sync.h"
#include "ems_sync_dispatcher.h"
#include "ems_rtc.h"
#include "summary_action.h"
#include <ArduinoJson.h>
#include "ble_nus.h"
#include "ems_zh_24_vlw.h"

using namespace ems;

// ════════════════════════════════════════════════════════════════
// LovyanGFX 硬體配置（ESP32-S3 GP-SPI2 + DMA + ST7789）
// ════════════════════════════════════════════════════════════════

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
public:
    LGFX(void) {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 3;
            cfg.pin_mosi = 2;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 1;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 21;
            cfg.pin_rst  = 47;
            cfg.pin_busy = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable  = false;
            cfg.invert    = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

class FrameSprite : public lgfx::LGFX_Sprite {
public:
    explicit FrameSprite(lgfx::LovyanGFX* parent) : LGFX_Sprite(parent) {}
    void clearDisplay() { fillScreen(0x0000); }
};

// ════════════════════════════════════════════════════════════════
// 硬體常數（gpio-allocation.md §5.2）
// ════════════════════════════════════════════════════════════════

/** TFT 解析度（native portrait） */
constexpr uint16_t TFT_WIDTH     = 240;
constexpr uint16_t TFT_HEIGHT    = 320;
/** TFT 邏輯解析度（rotation=3 橫向後使用） */
constexpr int16_t  SCREEN_W      = 320;
constexpr int16_t  SCREEN_H      = 240;

// ── 顯示設計 tokens（對齊 docs/demo/index.html） ──

/** 背景色：純黑 */
constexpr uint16_t COLOR_BG          = 0x0000;
/** 主文字：白 */
constexpr uint16_t COLOR_TEXT_PRIMARY = 0xFFFF;
/** 次要資訊：灰 */
constexpr uint16_t COLOR_TEXT_MUTED  = 0x9492;
/** 暗灰 */
constexpr uint16_t COLOR_TEXT_DIM    = 0x6B4D;
/** 執行中綠 */
constexpr uint16_t COLOR_ACCENT_OK   = 0x2604;
/** 警告琥珀 */
constexpr uint16_t COLOR_ACCENT_WARN = 0xFDE0;
/** 警報紅 */
constexpr uint16_t COLOR_ACCENT_ALERT = 0xEA44;
/** 暗紅閃爍 */
constexpr uint16_t COLOR_FLASH_VENT   = 0x5800;

// ── OHCA 倒數畫面 layout ──

/** ALARMING 背景閃爍半週期 ms */
constexpr uint32_t OHCA_FLASH_HALF_MS  = 300;
constexpr int16_t  OHCA_BADGE_Y        = 14;
constexpr int16_t  OHCA_CHINESE_BADGE_TOP_Y = 8;
constexpr int16_t  OHCA_TIME_VISUAL_UP = 20;
constexpr int16_t  OHCA_LABEL_Y        = 165;
constexpr int16_t  OHCA_WAIT_COUNTER_Y = 200;
constexpr int16_t  OHCA_COUNTER_BOTTOM = 18;

/** 倒數 partial sprite erase bbox */
constexpr int16_t  OHCA_TIME_PUSH_X    = 14;
constexpr int16_t  OHCA_TIME_PUSH_Y    = 48;
constexpr int16_t  OHCA_TIME_PUSH_W    = 292;
constexpr int16_t  OHCA_TIME_PUSH_H    = 104;

/** 確認 bar 高度 */
constexpr int16_t  DIALOG_BAR_H        = 44;
/** Vent 結束提示 bar 高度 */
constexpr int16_t  VENT_BAR_H          = 32;

// ── GPIO 腳位 ──

constexpr int8_t   TFT_CS_PIN    = 21;
constexpr int8_t   TFT_DC_PIN    = 1;
constexpr int8_t   TFT_RST_PIN   = 47;
constexpr int8_t   TFT_MOSI_PIN  = 2;
constexpr int8_t   TFT_SCLK_PIN  = 3;
constexpr uint8_t  I2C_SDA_PIN   = 42;
constexpr uint8_t  I2C_SCL_PIN   = 41;
constexpr uint8_t  BUZZER_PIN    = 14;

#define ENABLE_VIBRATION 0
#if ENABLE_VIBRATION
constexpr uint8_t VIBRATION_PIN = 21;
#endif

// ── 8 按鍵配置 ──

constexpr uint8_t BTN_COUNT = 8;
const uint8_t BTN_PINS[BTN_COUNT] = {
    4, 5, 6, 7, 15, 16, 17, 18,
};

constexpr uint8_t BTN_PRIMARY = 0;
constexpr uint8_t BTN_UP      = 1;
constexpr uint8_t BTN_DOWN    = 2;
constexpr uint8_t BTN_POWER   = 3;
constexpr uint8_t BTN_RECORD  = 4;
constexpr uint8_t BTN_BACK    = 5;
constexpr uint8_t BTN_EPI     = 6;
constexpr uint8_t BTN_SHOCK   = 7;

constexpr uint16_t DEBOUNCE_MS         = 80;
constexpr uint16_t SHORT_PRESS_MAX_MS  = 1500;

const uint16_t LONG_PRESS_MS_PER_BTN[8] = {
    3000, 0, 0, 0, 0, 0, 1000, 1000,
};

// ── BLE 常數 ──

// BLE 廣播裝置名；網頁端 Web Bluetooth 以 namePrefix "DSP-" 過濾掃描（F-4b）
constexpr const char* BLE_DEVICE_NAME = "DSP-0001";
constexpr size_t BLE_ACK_BUF_MAX = 512;

// case_sync metadata 常數（MVP3 佔位值，量產時由裝置配置流程寫入 NVS）。
// SYNC_DEVICE_ID 目前與 BLE_DEVICE_NAME 同為 "DSP-0001"，但兩者語意不同（BLE 廣播名
// vs 案件 metadata 裝置 ID）、量產可各自配置，故刻意不互相別名。
constexpr const char* SYNC_DEVICE_ID   = "DSP-0001";       // 裝置硬體 ID
constexpr const char* SYNC_DEVICE_NAME = SYNC_DEVICE_ID;   // 救護車編號（未配置，暫別名裝置 ID）
constexpr const char* SYNC_FW_VERSION  = "v0.6-phaseF";    // 韌體版本

// ════════════════════════════════════════════════════════════════
// 案件模式（W2：Training = OHCA 訓練版，共用核心狀態機）
// 定義於 ems_storage_logic.h（storage 層共用），此處僅宣告 alias
// ════════════════════════════════════════════════════════════════

// ── Training 倒數週期常數（取代硬編碼 30000/60000/240000） ──

/** Training 30 秒倒數 */
constexpr uint32_t TRAINING_CYCLE_30S  = 30000;
/** Training 60 秒倒數 */
constexpr uint32_t TRAINING_CYCLE_60S  = 60000;
/** Training 240 秒倒數（與 OHCA 預設相同） */
constexpr uint32_t TRAINING_CYCLE_240S = 240000;

// ════════════════════════════════════════════════════════════════
// 全域狀態機 enum
// ════════════════════════════════════════════════════════════════

enum GlobalState : uint8_t {
    GLOBAL_MAIN_MENU            = 0,
    GLOBAL_OHCA                 = 1,
    GLOBAL_VENT                 = 2,
    GLOBAL_TRAINING_SETUP       = 3,
    GLOBAL_HISTORY_PLACEHOLDER  = 4,
    GLOBAL_SETTINGS_PLACEHOLDER = 5,
    GLOBAL_SYNC                 = 6,
};

constexpr uint8_t MAIN_MENU_COUNT = 5;
const char* const MAIN_MENU_LABELS[MAIN_MENU_COUNT] = {
    "OHCA 案件",
    "6 秒通氣節奏",
    "訓練模式",
    "歷史紀錄",
    "系統設定",
};

// ── OHCA 子狀態 ──

constexpr uint32_t START_FLASH_DURATION_MS = 1000;
constexpr uint32_t ARMED_PROMPT_MS = 5000;
constexpr uint16_t MAX_EVENTS = 50;

enum OhcaSubState : uint8_t {
    SUBSTATE_NONE                  = 0,
    SUBSTATE_DRUG_MENU             = 1,
    SUBSTATE_BACKFILL_TYPE         = 2,
    SUBSTATE_BACKFILL_COUNT        = 3,
    SUBSTATE_BACKFILL_CONFIRM      = 4,
    SUBSTATE_BACKFILL_SUCCESS      = 5,
    SUBSTATE_AMIO_CONFIRM          = 6,
    SUBSTATE_TIMELINE              = 7,
    SUBSTATE_QUICK_MENU            = 8,
    SUBSTATE_TRAINING_SAVE         = 9,  // W5：Training 保存/不保存選單
    SUBSTATE_HISTORY_CATEGORY      = 10, // W6：歷史分類層（OHCA / Training）
    SUBSTATE_TRAINING_HISTORY_OPT  = 11, // W7：Training 歷史操作選單
    SUBSTATE_DELETE_CONFIRM        = 12, // W7：刪除二次確認
    SUBSTATE_RESET_CONFIRM         = 13, // W8：重置訓練確認
};

enum BackfillCategory : uint8_t {
    BACKFILL_CAT_EPI   = 0,
    BACKFILL_CAT_SHOCK = 1,
};

constexpr uint32_t BACKFILL_SUCCESS_MS = 2000;

enum SummarySubmenuItem : uint8_t {
    SUMMARY_SUBMENU_TIMELINE = 0,
    SUMMARY_SUBMENU_SYNC     = 1,
    SUMMARY_SUBMENU_COUNT
};

constexpr int16_t SUMMARY_SUBMENU_ROW_H          = 22;
constexpr int16_t SUMMARY_SUBMENU_LEFT_PAD       = 28;
constexpr int16_t SUMMARY_SUBMENU_CURSOR_GLYPH_W = 16;
constexpr int16_t SUMMARY_SUBMENU_BOTTOM_MARGIN  = 4;

// 對齊 ui_ohca.cpp drawOhcaSummary 的 SUBMENU_Y_BASE 算式（SUMMARY 不顯示底部
// counter row，故 OHCA_COUNTER_BOTTOM 不參與保留空間計算）。
static_assert(
    SCREEN_H - SUMMARY_SUBMENU_BOTTOM_MARGIN
        - SUMMARY_SUBMENU_ROW_H * SUMMARY_SUBMENU_COUNT >= 0,
    "OHCA SUMMARY sub-menu rows overflow screen"
);

constexpr uint8_t HISTORY_VISIBLE_ROWS = 5;

enum EndCheckCursor : uint8_t {
    END_CHECK_CURSOR_CONFIRM  = 0,
    END_CHECK_CURSOR_BACKFILL = 1,
    END_CHECK_CURSOR_CANCEL   = 2,
};

// ── Phase C 常數 ──

constexpr uint32_t VENT_BACK_HINT_MS = 2000;

// ── Flash overlay struct ──

struct FlashState {
    bool     active;
    uint32_t startMs;
    uint16_t durationMs;
    char     title[40];
    char     subtitle[40];
    uint16_t titleColor;
    float    titleSize;
    float    subtitleSize;
};
constexpr uint16_t FLASH_DEFAULT_MS = 1200;

constexpr float FLASH_TITLE_SIZE_DEFAULT    = 2.25f;
constexpr float FLASH_TITLE_SIZE_LONG       = 1.9f;
constexpr float FLASH_TITLE_SIZE_XLONG      = 1.4f;
constexpr float FLASH_SUBTITLE_SIZE_DEFAULT = 1.5f;
constexpr float FLASH_SUBTITLE_SIZE_LONG    = 1.2f;

// ── SyncTarget struct ──

struct SyncTarget {
    char           id[EMS_STORAGE_ID_LEN];
    CaseMode       type;
    void clear() { memset(id, 0, sizeof(id)); type = CASE_MODE_OHCA; }
    bool empty() const { return id[0] == '\0'; }
};

// ── 蜂鳴 / 反色 常數 ──

constexpr uint16_t OLED_FLASH_MS = 200;
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 200;

// ── WS2812 ──

constexpr uint8_t WS2812_PIN = 48;

// ════════════════════════════════════════════════════════════════
// extern 全域變數宣告（定義在 main.cpp）
// ════════════════════════════════════════════════════════════════

// 狀態機
extern GlobalState globalState;
extern uint8_t mainMenuCursor;
// W2：案件模式（OHCA / Training）
extern CaseMode g_case_mode;
// W2：Training 倒數週期（TRAINING_CYCLE_30S / TRAINING_CYCLE_60S / TRAINING_CYCLE_240S）
extern uint32_t g_training_epi_cycle_ms;

/**
 * 取得當前案件的 EPI 倒數週期（單一真相來源）
 *
 * 倒數 tick、snapshot、各渲染路徑皆須經此取值，避免散落硬編碼 240000
 * 導致 Training 選 30s/60s 時「狀態機用實際週期、畫面用預設週期」不一致。
 *
 * @return Training 模式回傳使用者選定值；OHCA 模式回傳預設 OHCA_EPI_CYCLE_MS_DEFAULT（240s）
 */
inline uint32_t activeEpiCycleMs() {
    return (g_case_mode == CASE_MODE_TRAINING) ? g_training_epi_cycle_ms
                                               : OHCA_EPI_CYCLE_MS_DEFAULT;
}
// W3：Training 倒數選擇游標（0=30s / 1=60s / 2=240s）
extern uint8_t trainingSetupCursor;
// W5：Training 保存選單游標（0=保存 / 1=不保存）
extern uint8_t trainingSaveCursor;

// OHCA
extern ohca_state_t ohcaState;
extern uint32_t     ohcaLastEpiMs;
extern uint32_t     ohcaPrevSinceMs;
extern uint32_t     startFlashStartMs;
extern bool         alarmMuted;
extern uint32_t     caseStartMs;
extern uint64_t     caseStartEpochMs;

// 兩段確認
extern two_step_confirm_t epiConfirm;
extern two_step_confirm_t shockConfirm;
extern two_step_confirm_t amioConfirm;
extern bool     showEpiArmedPrompt;
extern uint32_t epiArmedPromptStart;
extern bool     showShockArmedPrompt;
extern uint32_t shockArmedPromptStart;
extern bool     showAmioArmedPrompt;
extern uint32_t amioArmedPromptStart;

// 事件紀錄
extern ems_event_t events[MAX_EVENTS];
extern uint16_t    eventCount;
extern uint32_t    nextEventId;

// Phase E 持久化
extern IStorageBackend g_storage_be;
extern bool g_storage_ready;
extern bool g_locked_saved;

// Phase F BLE
extern ems::BleNus g_ble;
extern ems::TimeSyncState g_ts_state;

// Dev-Phase 3 RTC（DS3231 @ I2C 0x68）
//   - I2C_SDA_PIN / I2C_SCL_PIN：上面 GPIO 區已宣告（42 / 41，TFT 升級後釋出）
//   - SYNCED_AT_EPOCH_FLOOR_MS：ems_storage_logic.h 已宣告（2020-01-01 floor，UI 同源）
//   - 對齊 docs/ds3231-integration-plan.md §4.1
// 永不為 nullptr：setup() 內未抓到 DS3231 也會掛 NullRtcBackend
extern ems::RtcBackend* g_rtc;

// Phase F 同步
extern ems::SyncContext   g_sync_ctx;
extern ems::SyncReturnTo  g_sync_return_to;
extern SyncTarget g_sync_target;
extern uint64_t g_ohca_live_synced_at_ms;

// 歷史紀錄 UI
extern case_meta_t historyCases[EMS_STORAGE_OHCA_CAP];
extern uint16_t historyCount;
extern uint16_t historyCursor;
extern uint16_t historyScrollOffset;
extern bool     historySummaryMode;
// W6：歷史分類層
extern uint8_t  historyTypeCursor;   // 0=OHCA / 1=Training
extern CaseMode g_history_type;  // 當前歷史載入類型
// W7：Training 歷史操作選單
extern uint8_t trainingHistoryOptionsCursor;
extern bool    trainingDeleteConfirm; // W7：刪除二次確認顯示中
// W8：重置訓練確認
extern bool    trainingResetConfirm;  // W8：重置訓練二次確認顯示中

// Phase B 子流程
extern OhcaSubState ohcaSubState;
extern BackfillCategory backfillCategory;
extern supp_type_t      backfillSuppType;
extern uint8_t          backfillCursor;
extern uint8_t          backfillCount;
extern uint32_t         backfillSuccessShownMs;
extern uint8_t summarySubmenuCursor;
extern uint16_t timelineScrollOffset;

// Phase C
extern uint8_t  ventVolume;
extern uint32_t ventStartMs;
extern uint32_t ventPrevSinceMs;
extern bool     ventEndCheckShown;
extern bool     ventBackHintShown;
extern uint32_t ventBackHintStartMs;
extern bool     ventPaused;
extern bool     ventPreShown;
extern bool     ohcaVentOverlayEnabled;
extern bool     ohcaVentPaused;

// END_CHECK / SUMMARY
extern EndCheckCursor endCheckCursor;
extern bool endConfirmShown;
extern bool resyncConfirmShown;   // Phase F §16.7：已同步案件再次同步的確認 dialog 顯示中
extern uint16_t summaryScrollOffset;

// Flash overlay
extern FlashState flashState;

// 按鈕狀態
extern uint8_t  lastBtnState[BTN_COUNT];
extern uint32_t lastPressMs[BTN_COUNT];
extern uint32_t btnPressStartMs[BTN_COUNT];
extern bool     btnLongFired[BTN_COUNT];

// 顯示
extern LGFX tft;
extern FrameSprite display;
extern bool g_vlw_loaded;

// 蜂鳴器
extern uint8_t  beepPulsesRemaining;
extern uint32_t beepNextToggleMs;
extern bool     beepActive;
extern uint16_t beepOnMs;
extern uint16_t beepOffMs;
extern bool     buzzContinuous;

// OLED 反色
extern bool     oledInverted;
extern uint32_t oledInvertStartMs;
extern uint16_t oledInvertDurationMs;

// 顯示節流
extern uint32_t lastDisplayUpdateMs;

// DisplaySnapshot
extern DisplaySnapshot lastDisplaySnapshot;

// ════════════════════════════════════════════════════════════════
// inline 工具函式
// ════════════════════════════════════════════════════════════════

/** SyncReturnTo → GlobalState 轉換 */
inline GlobalState sync_return_to_global(ems::SyncReturnTo r) {
    switch (r) {
        case ems::SyncReturnTo::OHCA:    return GLOBAL_OHCA;
        case ems::SyncReturnTo::HISTORY: return GLOBAL_HISTORY_PLACEHOLDER;
        case ems::SyncReturnTo::MAIN:    return GLOBAL_MAIN_MENU;
    }
    return GLOBAL_MAIN_MENU;
}

/** GlobalState → SyncReturnTo 轉換 */
inline ems::SyncReturnTo global_to_sync_return(GlobalState g) {
    switch (g) {
        case GLOBAL_OHCA:                 return ems::SyncReturnTo::OHCA;
        case GLOBAL_HISTORY_PLACEHOLDER:  return ems::SyncReturnTo::HISTORY;
        default:
            Serial.printf("[SYNC] WARN unexpected globalState %u for sync_return, fallback MAIN\n",
                          (unsigned)g);
            return ems::SyncReturnTo::MAIN;
    }
}

/** vlw 字型 lazy reload helper */
inline void useZhFont() {
    if (!g_vlw_loaded) {
        return;
    }
    auto* f = display.getFont();
    if (f == nullptr || f->getType() != lgfx::v1::IFont::font_type_t::ft_vlw) {
        if (!display.loadFont(ems_zh_24_vlw)) {
            Serial.println("[FONT] lazy reload FAILED, disable VLW");
            g_vlw_loaded = false;
        }
    }
}

// ════════════════════════════════════════════════════════════════
// 函式宣告（跨檔可見）
// ════════════════════════════════════════════════════════════════

// ── input_handler.cpp ──
void handleButtons();
void onShortPress(uint8_t btnIdx);
void onLongPress(uint8_t btnIdx);
void handleSummarySubmenuPrimary();

// ── ohca_logic.cpp ──
void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms);
void updateOhcaTick();
void recordLocalEvent(ems_event_type_t type);
void recordSuppEvent(supp_type_t supp_type, uint8_t count);
void resetSubState();
void enterDrugMenu();
void enterShockBackfillMenu();
void applyVentOutput(const vent_output_t& out);
void updateVentTick();
void enterMainMenu();
void exitOhcaCase();
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void stopBeep();
void updateBeepMachine();
void triggerOledFlash(uint16_t durationMs);
void updateOledFlashMachine();
void applyOhcaOutput(const ohca_output_t& out);
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor,
                  float titleSize = FLASH_TITLE_SIZE_DEFAULT,
                  float subtitleSize = FLASH_SUBTITLE_SIZE_DEFAULT);

// ── main.cpp ──
void updateDisplay();

// ── sync_send.cpp ──
void syncSendingPrepare();
void syncSendingPump();

// ── ui_ohca.cpp ──
void drawOhcaStartFlash();
void drawOhcaWaitFirstEpi();
void drawOhcaCountdownCommon(uint32_t time_ms, uint16_t timeColor, const char* label, bool flashOn);
void drawOhcaCountdownTimeOnly(uint8_t ohcaState);
void drawOhcaEndCheck();
void drawOhcaLocked();
void drawOhcaSummary();
void drawTwoStepArmedOverlay(const char* what);
void drawOhcaConfirmDialog(uint8_t evType);
void drawOhcaEndConfirmDialog();
void drawResyncConfirmDialog();
void drawFlashOverlay();
void drawOhcaVentOverlay(int y_top);
void drawTrainingWatermark();

// ── shared helpers ──
void drawCenteredText(const char* text, int16_t y, uint16_t color);
void drawSubmenuNavHint();

/** 共用垂直選單 widget：標題 + 分隔線 + 動態選項列 + 底部 hint */
void drawVerticalMenu(const char* title, const char* labels[], uint8_t count,
                      uint8_t cursor, uint16_t rowH, int16_t yStart,
                      const char* hint);

// ── ui_screens.cpp ──
void drawMainMenu();
void drawSyncScreen();
void drawHistoryList();
void drawPlaceholder(const char* title, const char* phase);
void drawTrainingSetup();
void drawTrainingSave();
void drawTrainingHistoryOptions();
void drawConfirmDialog(const char* title, const char* body);
void drawDrugMenu();
void drawBackfillType();
void drawBackfillCount();
void drawBackfillConfirm();
void drawBackfillSuccess();
void drawAmioConfirmPrompt();
void drawTimeline();
void drawVentPre();
void drawVentStandalone();
void drawVentEndCheck();
void drawQuickMenu();
