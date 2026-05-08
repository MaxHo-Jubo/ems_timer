/**
 * EMS Timer — Phase A 韌體：OHCA 核心
 *
 * SoT 對齊：
 *   - docs/EMS_DoseSync_Pro_Prototype_V1.md §3 主功能表 / §5~§11 OHCA
 *   - docs/pm-dev-spec.md §3~§5（OHCA 子狀態機 / EPI 倒數 / 兩段確認）
 *   - docs/gpio-allocation.md（GPIO 分配 SSOT）
 *
 * Phase A 範圍（pm-dev-spec §四）：
 *   ✅ 主功能表 5 項（Phase A 僅實作 OHCA case 入口；其他顯示「Phase X 待實作」）
 *   ✅ OHCA 子狀態機（10 態，delegate 至 lib/ems_ohca）
 *   ✅ EPI 4 分鐘倒數引擎（純函式 lib，TIMER_TICK 驅動）
 *   ✅ EPI / 電擊兩段確認（5s timeout）
 *   ✅ 三模態輸出：蜂鳴（短嗍 / 連續發報）/ OLED 反色（震動視覺替代）/ 震動（佔位）
 *   ❌ 補登 / Amio（Phase B）
 *   ❌ 6 秒通氣節奏（Phase C）
 *   ❌ Training（Phase D）
 *   ❌ 持久化 / 歷史紀錄（Phase E）
 *   ❌ BLE 同步（Phase F）
 *   ❌ 系統設定（Phase G）
 *   ❌ 電源管理（Phase H）
 *
 * 接線（gpio-allocation.md）：
 *   主按鍵    → GPIO 4   返回鍵    → GPIO 16
 *   上鍵      → GPIO 5   EPI 鍵    → GPIO 17
 *   下鍵      → GPIO 6   電擊鍵    → GPIO 18
 *   Power 鍵  → GPIO 7   蜂鳴器    → GPIO 14
 *   錄音鍵    → GPIO 15  震動馬達  → GPIO 21（佔位，ENABLE_VIBRATION=0）
 *   OLED      → GPIO 42 (SDA) / 41 (SCL)
 */

#include <Arduino.h>
#include <SPI.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "ems_ohca_state.h"
#include "ems_ohca_countdown.h"
#include "ems_two_step_confirm.h"
#include "ems_supp_model.h"
#include "ems_case_summary.h"
#include "ems_vent_metronome.h"

using namespace ems;

// ============================================================
// 顯示遷移橋接：SH110X → ST7789（Step 1，最小變更，layout 待 Step 2 重排）
//   - SH110X 的 1-bit 色（WHITE=1 / BLACK=0）→ TFT RGB565
//   - SH110X 的 buffer flush model（要 display()）→ ST7789 立即寫入（display() no-op）
//   - SH110X 的 clearDisplay()（清 buffer）→ ST7789 fillScreen(BLACK)
//   詳細決策見 docs/tft-migration-plan.md 與 .claude/.../project_tft_ui_design_target.md
// ============================================================

#define SH110X_WHITE  0xFFFF  /**< RGB565 白色（取代舊 1-bit 1） */
#define SH110X_BLACK  0x0000  /**< RGB565 黑色（取代舊 1-bit 0） */

/**
 * LovyanGFX 硬體配置：ESP32-S3 GP-SPI2 + DMA + ST7789 240×320 panel。
 * Pin 號碼與 TFT_*_PIN 常數同步（gpio-allocation.md §5.2）。
 */
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
            cfg.invert    = false;  // 蝦皮紅板 polarity（同 Adafruit_ST7789 invertDisplay(false)）
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

/**
 * SH110X 相容包裝：補 clearDisplay() / display() no-op 給 1300+ 行舊呼叫點。
 * push 由 updateDisplay 結尾透過 pushSprite(&tft, 0, 0) DMA 推到實體 TFT。
 */
class FrameSprite : public lgfx::LGFX_Sprite {
public:
    FrameSprite(lgfx::LovyanGFX* parent) : LGFX_Sprite(parent) {}
    void clearDisplay() { fillScreen(0x0000); }
    void display() { /* push handled at updateDisplay end */ }
};

// ============================================================
// 硬體常數（gpio-allocation.md §5.2）
// ============================================================

/** TFT 解析度（native portrait） */
static const uint16_t TFT_WIDTH     = 240;
static const uint16_t TFT_HEIGHT    = 320;
/** TFT 邏輯解析度（rotation=1 橫向後使用） */
static const int16_t  SCREEN_W      = 320;
static const int16_t  SCREEN_H      = 240;
/** Step 1 過渡：保留 OLED_WIDTH/HEIGHT 別名讓尚未重排的舊 layout 程式碼仍能編譯（Step 2 逐畫面替換） */
static const uint16_t OLED_WIDTH    = 128;
static const uint16_t OLED_HEIGHT   = 64;

// ============================================================
// 顯示設計 tokens（對齊 docs/demo/index.html）
// 規格：黑底 + 白主字 + 灰次要 + 急救色（綠/琥珀/紅）+ Courier monospace 大時間
// 詳見 .claude/.../project_tft_ui_design_target.md
// ============================================================

/** 背景色：純黑 #000000（demo `body { background: #000 }`） */
static const uint16_t COLOR_BG          = 0x0000;
/** 主文字：白 #ffffff */
static const uint16_t COLOR_TEXT_PRIMARY = 0xFFFF;
/** 次要資訊：灰 ≈ #94a3b8（demo `.scr-title color`） */
static const uint16_t COLOR_TEXT_MUTED  = 0x9492;
/** 暗灰：#64748b（demo `.scr-counter color`） */
static const uint16_t COLOR_TEXT_DIM    = 0x6B4D;
/** 執行中綠：≈ #22c55e（demo `.scr-mode color`） */
static const uint16_t COLOR_ACCENT_OK   = 0x2604;
/** 警告琥珀：≈ #fbbf24（demo `.scr-time-amber`） */
static const uint16_t COLOR_ACCENT_WARN = 0xFDE0;
/** 警報紅：≈ #ef4444（demo `.scr-time-red`） */
static const uint16_t COLOR_ACCENT_ALERT = 0xEA44;
/** 暗紅閃爍：#5a0000（demo `flash-vent` 整片暗紅） */
static const uint16_t COLOR_FLASH_VENT   = 0x5800;

/** OHCA 倒數畫面 layout（drawOhcaCountdownCommon 使用） */
/** ALARMING 背景閃爍半週期 ms（demo flashRed 0.6s 全週期 / 2） */
static const uint32_t OHCA_FLASH_HALF_MS  = 300;
/** 頂部 "OHCA" badge baseline Y（default font size 2，~14px 字高） */
static const int16_t  OHCA_BADGE_Y        = 14;
/** 大時間視覺上偏 px（middle datum 中心 y = SCREEN_H/2 - VISUAL_UP，留下方標籤空間） */
static const int16_t  OHCA_TIME_VISUAL_UP = 20;
/** 標籤距大時間 middle 下方 px（要 > 大時間半高 + padding，否則撞時間 bbox） */
static const int16_t  OHCA_LABEL_GAP_PX   = 60;
/** 底部 EPI/Shock 計數行距底邊 px */
static const int16_t  OHCA_COUNTER_BOTTOM = 18;

/** 倒數 partial sprite erase bbox（FreeMonoBold24pt7b size 2 → 280×96 + margin） */
static const int16_t  OHCA_TIME_PUSH_X    = 14;
static const int16_t  OHCA_TIME_PUSH_Y    = 48;
static const int16_t  OHCA_TIME_PUSH_W    = 292;
static const int16_t  OHCA_TIME_PUSH_H    = 104;
/** TFT SPI 腳位（避開 N16R8 octal PSRAM 佔用的 GPIO 35-37 + 板上 WS2812 GPIO 48） */
static const int8_t   TFT_CS_PIN    = 21;
static const int8_t   TFT_DC_PIN    = 1;   /**< 原 48 → 1：避開板上 WS2812 RGB LED（2026-05-08 實機踩雷） */
static const int8_t   TFT_RST_PIN   = 47;
static const int8_t   TFT_MOSI_PIN  = 2;
static const int8_t   TFT_SCLK_PIN  = 3;
/** I2C bus（OLED 已移除，腳位保留給未來 DS3231 RTC / CO 感測器擴充） */
static const uint8_t  I2C_SDA_PIN   = 42;
static const uint8_t  I2C_SCL_PIN   = 41;

/** 蜂鳴器 GPIO */
static const uint8_t BUZZER_PIN     = 14;

/** 震動馬達 GPIO（gpio-allocation.md：原 GPIO 16 已封給返回鍵） */
#define ENABLE_VIBRATION 0
#if ENABLE_VIBRATION
static const uint8_t VIBRATION_PIN = 21;
#endif

// ============================================================
// 8 按鍵配置（依 SoT V1 §4.1 + gpio-allocation.md）
// ============================================================

static const uint8_t BTN_COUNT = 8;
static const uint8_t BTN_PINS[BTN_COUNT] = {
    4,   // 主按鍵
    5,   // 上鍵
    6,   // 下鍵
    7,   // Power 鍵
    15,  // 錄音鍵（noop 佔位）
    16,  // 返回鍵
    17,  // EPI 鍵（針筒圖案）
    18,  // 電擊鍵（閃電圖案）
};

/** 按鈕索引語意 */
static const uint8_t BTN_PRIMARY = 0;
static const uint8_t BTN_UP      = 1;
static const uint8_t BTN_DOWN    = 2;
static const uint8_t BTN_POWER   = 3;
static const uint8_t BTN_RECORD  = 4;
static const uint8_t BTN_BACK    = 5;
static const uint8_t BTN_EPI     = 6;
static const uint8_t BTN_SHOCK   = 7;

/** Debounce / 短按 / 長按時間（ms） */
static const uint16_t DEBOUNCE_MS         = 80;
static const uint16_t SHORT_PRESS_MAX_MS  = 1500;

/**
 * 各按鈕的長按 threshold（ms）；0 = 該按鈕不支援長按
 *   BTN_PRIMARY 3000ms：OHCA 任意進行中 phase → END_CHECK（SoT V1 §10.1）
 *   BTN_EPI     1000ms：藥物選單入口（Phase B）
 *   BTN_SHOCK   1000ms：電擊補登選單入口（Phase B）
 */
static const uint16_t LONG_PRESS_MS_PER_BTN[8] = {
    3000,  // BTN_PRIMARY
    0,     // BTN_UP
    0,     // BTN_DOWN
    0,     // BTN_POWER
    0,     // BTN_RECORD
    0,     // BTN_BACK
    1000,  // BTN_EPI
    1000,  // BTN_SHOCK
};

// ============================================================
// 全域狀態機（pm-dev-spec §2）
// ============================================================

enum GlobalState : uint8_t {
    GLOBAL_MAIN_MENU            = 0,
    GLOBAL_OHCA                 = 1,
    GLOBAL_VENT                 = 2,  // Phase C 獨立 6 秒通氣節奏
    GLOBAL_TRAINING_PLACEHOLDER = 3,  // Phase D 待實作
    GLOBAL_HISTORY_PLACEHOLDER  = 4,  // Phase E 待實作
    GLOBAL_SETTINGS_PLACEHOLDER = 5,  // Phase G 待實作
};
static GlobalState globalState = GLOBAL_MAIN_MENU;

/** 主功能表 5 項（SoT V1 §3.1） */
static const uint8_t MAIN_MENU_COUNT = 5;
static const char* const MAIN_MENU_LABELS[MAIN_MENU_COUNT] = {
    "OHCA 案件",
    "6 秒通氣節奏",
    "訓練模式",
    "歷史紀錄",
    "系統設定",
};
static uint8_t mainMenuCursor = 0;

// ============================================================
// OHCA 子狀態（delegate 至 lib/ems_ohca）
// ============================================================

static ohca_state_t ohcaState         = OHCA_STATE_MAIN_MENU;
static uint32_t     ohcaLastEpiMs     = 0;     // 上次 EPI 確認的 millis()
static uint32_t     ohcaPrevSinceMs   = 0;     // 上一輪 since 值（給邊緣偵測）
static uint32_t     startFlashStartMs = 0;     // START_FLASH 1s 計時
static bool         alarmMuted        = false; // ALARMING/OVERTIME 是否已被消音
static uint32_t     caseStartMs       = 0;     // case 開始 millis()

/** START_FLASH 階段顯示 1s */
static const uint32_t START_FLASH_DURATION_MS = 1000;

/** 兩段確認 instances（EPI / 電擊 / Amio 各自獨立） */
static two_step_confirm_t epiConfirm;
static two_step_confirm_t shockConfirm;
static two_step_confirm_t amioConfirm;

/** 兩段確認 armed 提示視窗 */
static bool     showEpiArmedPrompt    = false;
static uint32_t epiArmedPromptStart   = 0;
static bool     showShockArmedPrompt  = false;
static uint32_t shockArmedPromptStart = 0;
static bool     showAmioArmedPrompt   = false;
static uint32_t amioArmedPromptStart  = 0;
static const uint32_t ARMED_PROMPT_MS = 5000;

// ============================================================
// 事件紀錄（升級為 ems_event_t，對齊 pm-dev-spec §6）
// Phase E 才做持久化；Phase B 仍為 in-memory
// ============================================================

static const uint16_t MAX_EVENTS = 50;
static ems_event_t events[MAX_EVENTS];
static uint16_t    eventCount   = 0;
static uint32_t    nextEventId  = 1;  // 案件內流水號，從 1 起

// ============================================================
// Phase B 子流程狀態機（補登 / Amio / Timeline）
// ============================================================

enum OhcaSubState : uint8_t {
    SUBSTATE_NONE                  = 0,  // 無子流程，主畫面
    SUBSTATE_DRUG_MENU             = 1,  // 長按 EPI 鍵進入：補登 EPI / Amiodarone
    SUBSTATE_BACKFILL_TYPE         = 2,  // 接手前 / 純補登 二選一
    SUBSTATE_BACKFILL_COUNT        = 3,  // 次數選擇 1~5 或 1~3
    SUBSTATE_BACKFILL_CONFIRM      = 4,  // 「確認補登？... 成立後不可撤銷」
    SUBSTATE_BACKFILL_SUCCESS      = 5,  // 「補登成功」顯示 2s
    SUBSTATE_AMIO_CONFIRM          = 6,  // Amio 兩段確認
    SUBSTATE_TIMELINE              = 7,  // Timeline 子畫面（從 SUMMARY 進入）
    SUBSTATE_QUICK_MENU            = 8,  // Phase C: OHCA 中按返回鍵進入快速功能選單
};
static OhcaSubState ohcaSubState = SUBSTATE_NONE;

/** 補登流程暫存：類別（EPI / 電擊）與類型（接手前 / 純補登）與次數 */
enum BackfillCategory : uint8_t {
    BACKFILL_CAT_EPI   = 0,
    BACKFILL_CAT_SHOCK = 1,
};
static BackfillCategory backfillCategory   = BACKFILL_CAT_EPI;
static supp_type_t      backfillSuppType   = SUPP_TYPE_EPI_PRE_HANDOVER;
static uint8_t          backfillCursor     = 0;  // 子選單 cursor
static uint8_t          backfillCount      = 1;  // 1..suppCountMax
static uint32_t         backfillSuccessShownMs = 0;  // 顯示「成功」起點
static const uint32_t   BACKFILL_SUCCESS_MS = 2000;

/** Timeline 子畫面 scroll offset */
static uint16_t timelineScrollOffset = 0;

// ============================================================
// Phase C: 6 秒通氣節奏狀態
// ============================================================

static uint8_t  ventVolume         = VENT_VOLUME_DEFAULT;  // 通氣音量（Phase G 才做 NVS 持久化）
static uint32_t ventStartMs        = 0;                    // 進入 vent 模式時的 millis()
static uint32_t ventPrevSinceMs    = 0;                    // 邊緣偵測用
static bool     ventEndCheckShown  = false;                // 獨立模式長按 3s 結束確認對話框
static bool     ventBackHintShown  = false;                // 「請長按主鍵」提示
static uint32_t ventBackHintStartMs = 0;
static const uint32_t VENT_BACK_HINT_MS = 2000;
static bool     ventPaused         = false;                // V1 §13.11 / §13.12 獨立 vent 暫停旗標
static bool     ventPreShown       = false;                // A8：VENT_PRE「按主鍵開始」preview 畫面（demo 對齊）

/** OHCA 內 6 秒通氣輔助區塊開關（V1 §14.9 開啟、暫停、繼續與關閉） */
static bool ohcaVentOverlayEnabled = false;
/** V1 §14.10 / §14.11：OHCA 內 6 秒通氣暫停旗標（透過快速功能切換） */
static bool ohcaVentPaused         = false;

// ============================================================
// END_CHECK 與 SUMMARY 子狀態
// ============================================================

enum EndCheckCursor : uint8_t {
    END_CHECK_CURSOR_CONFIRM  = 0,  // 完成並結束案件
    END_CHECK_CURSOR_BACKFILL = 1,  // 前往補登
    END_CHECK_CURSOR_CANCEL   = 2,  // 返回案件
};
static EndCheckCursor endCheckCursor = END_CHECK_CURSOR_CANCEL;
/** A7：END_CHECK 選「完成並結束」後彈出二次確認對話（demo OHCA_END_CONFIRM） */
static bool endConfirmShown = false;

static uint16_t summaryScrollOffset = 0;

// ============================================================
// Flash overlay（demo 對齊：1.2s 全螢幕過場提示）
// ============================================================
struct FlashState {
    bool     active;
    uint32_t startMs;
    uint16_t durationMs;
    char     title[40];
    char     subtitle[40];
    uint16_t titleColor;
};
static FlashState flashState = {};
static const uint16_t FLASH_DEFAULT_MS = 1200;

// ============================================================
// 按鈕狀態
// ============================================================

static uint8_t  lastBtnState[BTN_COUNT];
static uint32_t lastPressMs[BTN_COUNT]      = { 0 };
static uint32_t btnPressStartMs[BTN_COUNT]  = { 0 };
static bool     btnLongFired[BTN_COUNT]     = { false };

// ============================================================
// OLED + 蜂鳴 / 反色 SM
// ============================================================

/** 實體 TFT 控制（init / SPI / rotation / DMA push） */
LGFX tft;
/** 全頁 RAM sprite — 所有 draw functions 寫到這裡，updateDisplay 結尾 pushSprite DMA 一次推到 tft */
FrameSprite display(&tft);

// 蜂鳴器：脈衝模式（pulses=255 視為連續直到 stop）
static uint8_t  beepPulsesRemaining = 0;
static uint32_t beepNextToggleMs    = 0;
static bool     beepActive          = false;
static uint16_t beepOnMs            = 0;
static uint16_t beepOffMs           = 0;
static bool     buzzContinuous      = false;  // 連續發報模式中（ALARMING）

// OLED 反色閃爍 SM
static bool     oledInverted          = false;
static uint32_t oledInvertStartMs     = 0;
static uint16_t oledInvertDurationMs  = 0;
static const uint16_t OLED_FLASH_MS   = 200;

// 顯示節流
static uint32_t lastDisplayUpdateMs = 0;
static const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 200;

// ============================================================
// 函式宣告
// ============================================================

void handleButtons();
void onShortPress(uint8_t btnIdx);
void onLongPress(uint8_t btnIdx);
void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms);
void updateOhcaTick();
void recordLocalEvent(ems_event_type_t type);
void recordSuppEvent(supp_type_t supp_type, uint8_t count);
void enterDrugMenu();
void enterShockBackfillMenu();
void resetSubState();
void updateVentTick();
void applyVentOutput(const vent_output_t& out);
void drawVentPre();
void drawVentStandalone();
void drawVentEndCheck();
void drawQuickMenu();
void drawOhcaVentOverlay(int y_top);
void enterMainMenu();
void exitOhcaCase();

void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void stopBeep();
void updateBeepMachine();
void triggerOledFlash(uint16_t durationMs);
void updateOledFlashMachine();
void applyOhcaOutput(const ohca_output_t& out);

void updateDisplay();
void drawMainMenu();
void drawOhcaStartFlash();
void drawOhcaWaitFirstEpi();
void drawOhcaCountdownCommon(uint32_t time_ms, uint16_t timeColor, const char* label, bool flashOn);
static void drawOhcaCountdownTimeOnly(uint8_t ohcaState);
void drawOhcaEndCheck();
void drawOhcaLocked();
void drawOhcaSummary();
void drawTwoStepArmedOverlay(const char* what);
void drawOhcaConfirmDialog(uint8_t evType);
void drawOhcaEndConfirmDialog();
void drawFlashOverlay();
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor);
void drawPlaceholder(const char* title, const char* phase);
void drawDrugMenu();
void drawBackfillType();
void drawBackfillCount();
void drawBackfillConfirm();
void drawBackfillSuccess();
void drawAmioConfirmPrompt();
void drawTimeline();

// ============================================================
// setup() / loop()
// ============================================================

/** GOOUUU 板上 WS2812 RGB LED 接 GPIO 48；boot 階段可能被訊號 latch 成隨機顏色，主動發 (0,0,0) 關掉。 */
static const uint8_t WS2812_PIN = 48;

void setup() {
    Serial.begin(115200);
    // ESP32-S3 USB-CDC 列舉需 1~2 秒，太早 Serial.println 會被吃掉。
    // 等到 Serial 通了再印，方便除錯。
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 3000) {
        delay(10);
    }
    Serial.println("[BOOT] EMS Timer Phase A");

    // STEP 00: 關掉板上 WS2812（GPIO 48）。boot bootloader 可能 latch 成白色，必須主動 reset。
    //   neopixelWrite() 由 esp32-hal-rgb-led 提供（ESP32 Arduino core ≥ 2.0.7 內建）。
    neopixelWrite(WS2812_PIN, 0, 0, 0);

    // STEP 01: 蜂鳴器
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

#if ENABLE_VIBRATION
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);
#endif

    // STEP 02: 8 按鍵 INPUT_PULLUP
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        lastBtnState[i] = digitalRead(BTN_PINS[i]);
    }

    // STEP 03: TFT 初始化（LovyanGFX + DMA + sprite buffer）
    //   - tft.init()：SPI 80MHz @ DMA_CH_AUTO（class LGFX 內 cfg）
    //   - invert: false（蝦皮紅板 polarity，已寫死於 LGFX panel cfg）
    //   - display 為全頁 LGFX_Sprite（320×240 @ 16bpp = 153,600 bytes，
    //     優先嘗試 PSRAM；fail 退 internal RAM）
    //   - 所有 draw 寫到 display sprite，updateDisplay 結尾 pushSprite DMA 推到 tft
    tft.init();
    tft.setRotation(3);  // 3 = 橫向 320x240（LGFX 跟 Adafruit_ST7789 rotation index 差 180）
    tft.fillScreen(0x0000);

    display.setColorDepth(16);
    display.setPsram(true);  // PSRAM 優先（N16R8 8MB），fail 退 internal RAM
    if (!display.createSprite(SCREEN_W, SCREEN_H)) {
        Serial.println("[FATAL] sprite createSprite failed");
    }
    display.fillScreen(0x0000);

    // STEP 04: 兩段確認 init
    twoStepConfirm_init(&epiConfirm,   TWO_STEP_DEFAULT_TIMEOUT_MS);
    twoStepConfirm_init(&shockConfirm, TWO_STEP_DEFAULT_TIMEOUT_MS);

    // STEP 05: 初始顯示
    updateDisplay();
    Serial.println("[READY] MainMenu");
}

void loop() {
    handleButtons();
    updateOhcaTick();
    updateVentTick();

    uint32_t now = millis();
    twoStepConfirm_tick(&epiConfirm,   now);
    twoStepConfirm_tick(&shockConfirm, now);
    twoStepConfirm_tick(&amioConfirm,  now);

    // STEP 01: armed 提示 5s 自動消失
    if (showEpiArmedPrompt && now - epiArmedPromptStart >= ARMED_PROMPT_MS) {
        showEpiArmedPrompt = false;
    }
    if (showShockArmedPrompt && now - shockArmedPromptStart >= ARMED_PROMPT_MS) {
        showShockArmedPrompt = false;
    }
    if (showAmioArmedPrompt && now - amioArmedPromptStart >= ARMED_PROMPT_MS) {
        showAmioArmedPrompt = false;
    }

    // STEP 02: 補登成功提示 2s 後自動回主畫面
    if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS &&
        now - backfillSuccessShownMs >= BACKFILL_SUCCESS_MS) {
        resetSubState();
    }

    // Flash overlay timeout（demo flash() 對齊，duration 由 caller 指定）
    if (flashState.active && now - flashState.startMs >= flashState.durationMs) {
        flashState.active = false;
    }

    // STEP 03: vent 返回鍵提示 2s 自動消失
    if (ventBackHintShown && now - ventBackHintStartMs >= VENT_BACK_HINT_MS) {
        ventBackHintShown = false;
    }

    updateBeepMachine();
    updateOledFlashMachine();

    if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
        lastDisplayUpdateMs = now;
        updateDisplay();
    }
}

// ============================================================
// 按鈕處理：邊緣偵測 + debounce + 短按 / 長按 3s
// ============================================================

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
                        eventCount        = 0;
                        nextEventId       = 1;
                        ohcaLastEpiMs     = 0;
                        ohcaPrevSinceMs   = 0;
                        alarmMuted        = false;
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
                    case 3: globalState = GLOBAL_HISTORY_PLACEHOLDER;  break;
                    case 4: globalState = GLOBAL_SETTINGS_PLACEHOLDER; break;
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

    // ===== Phase X 佔位畫面：返回鍵回主功能表 =====
    if (globalState >= GLOBAL_TRAINING_PLACEHOLDER && globalState <= GLOBAL_SETTINGS_PLACEHOLDER) {
        if (btnIdx == BTN_BACK) {
            enterMainMenu();
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
                    triggerFlash("簡版總覽", "結束案件後看完整總覽", 2000, COLOR_TEXT_PRIMARY);
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
                    triggerFlash("Amiodarone 已紀錄", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
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
                    triggerFlash("案件結束並鎖定", "已存入歷史紀錄", FLASH_DEFAULT_MS, COLOR_ACCENT_ALERT);
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
                summaryScrollOffset = 0;
                return;
            }
            break;

        case BTN_UP:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                // 3 項循環：CONFIRM(0) ↔ BACKFILL(1) ↔ CANCEL(2)
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 2) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                // Phase B: SUMMARY 上鍵 → 進 Timeline 子畫面
                ohcaSubState         = SUBSTATE_TIMELINE;
                timelineScrollOffset = 0;
            }
            break;

        case BTN_DOWN:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 1) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                ohcaSubState         = SUBSTATE_TIMELINE;
                timelineScrollOffset = 0;
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

// ============================================================
// OHCA event dispatcher（包裝 nextOhcaState）
// ============================================================

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
}

// ============================================================
// OHCA tick driver：START_FLASH timeout + COUNTDOWN/.../OVERTIME 推進
// ============================================================

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
    uint32_t now     = millis();
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    // Phase A 無 RTC：用 millis() 當 timestamp_ms 佔位（Dev-Phase 3 升 DS3231 後改 epoch）
    buildLocalEvent(&events[eventCount], nextEventId++, type,
                    /*timestamp_ms*/ (uint64_t)now, elapsed);
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
    uint32_t now     = millis();
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    buildSuppEvent(&events[eventCount], nextEventId++, supp_type, count,
                   /*recorded_at_ms*/ (uint64_t)now, elapsed);
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

void enterMainMenu() {
    globalState = GLOBAL_MAIN_MENU;
}

void exitOhcaCase() {
    enterMainMenu();
    ohcaState              = OHCA_STATE_MAIN_MENU;
    ohcaLastEpiMs          = 0;
    ohcaPrevSinceMs        = 0;
    eventCount             = 0;
    summaryScrollOffset    = 0;
    alarmMuted             = false;
    ohcaVentOverlayEnabled = false;  // V1 §14.12 案件結束 → 6 秒通氣自動停止
    ohcaVentPaused         = false;
    stopBeep();
}

// ============================================================
// 蜂鳴器非 blocking SM
// pulses = 255 視為「連續」（直到 stopBeep）
// ============================================================

void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
    beepPulsesRemaining = pulses;
    beepOnMs            = onMs;
    beepOffMs           = offMs;
    beepActive          = true;
    digitalWrite(BUZZER_PIN, HIGH);
    beepNextToggleMs    = millis() + onMs;
}

void stopBeep() {
    beepPulsesRemaining = 0;
    beepActive          = false;
    buzzContinuous      = false;
    digitalWrite(BUZZER_PIN, LOW);
}

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

void triggerOledFlash(uint16_t durationMs) {
    if (oledInverted) return;  // 已在閃 — 不重複觸發
    oledInverted          = true;
    oledInvertStartMs     = millis();
    oledInvertDurationMs  = durationMs;
    tft.invertDisplay(true);
}

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
 * 顯示狀態快照：updateDisplay 每次比對與上次的差異，無變化則跳過全螢幕重畫。
 * TFT 沒有 framebuffer，每次 clearDisplay+redraw 都直寫 76,800 像素到 SPI bus，
 * 視覺上會看到掃描線。snapshot 比對只有當顯示內容真正改變時才重繪 → 解決閃爍。
 *
 * 涵蓋會影響顯示的所有狀態：global/ohca state、cursor、倒數秒數、vent 拍點、
 * armed prompts、各種 overlay flag。
 */
struct DisplaySnapshot {
    uint8_t  globalState;
    uint8_t  ohcaState;
    uint8_t  ohcaSubState;
    uint8_t  mainMenuCursor;
    uint32_t countdownSec;       /**< OHCA 倒數/超時當前顯示秒數（per-second granularity） */
    uint8_t  ventBeat;           /**< 6sec 通氣節奏目前秒（0-5） */
    uint8_t  ventVolume;
    bool     ventPaused;
    uint16_t flags;              /**< bit-packed prompt/overlay 狀態（擴成 uint16 容納 bit 8+） */
};

static DisplaySnapshot lastDisplaySnapshot = {};  // 全 0 初始 → 首次 updateDisplay 必觸發重繪

/** 當前顯示狀態 → DisplaySnapshot。 */
static DisplaySnapshot captureDisplaySnapshot() {
    DisplaySnapshot s = {};
    s.globalState     = (uint8_t)globalState;
    s.ohcaState       = (uint8_t)ohcaState;
    s.ohcaSubState    = (uint8_t)ohcaSubState;
    s.mainMenuCursor  = mainMenuCursor;

    if (ohcaLastEpiMs != 0) {
        const uint32_t since = millis() - ohcaLastEpiMs;
        s.countdownSec = (since < EPI_CYCLE_MS)
                       ? (EPI_CYCLE_MS - since) / 1000
                       : (since - EPI_CYCLE_MS) / 1000;
    }
    if (ventStartMs != 0 && !ventPaused) {
        const uint32_t since = millis() - ventStartMs;
        s.ventBeat = (uint8_t)computeVentBeat(since);
    }
    s.ventVolume = ventVolume;
    s.ventPaused = ventPaused;

    if (showEpiArmedPrompt)     s.flags |= 0x01;
    if (showShockArmedPrompt)   s.flags |= 0x02;
    if (showAmioArmedPrompt)    s.flags |= 0x04;
    if (ohcaVentOverlayEnabled) s.flags |= 0x08;
    if (ventEndCheckShown)      s.flags |= 0x10;
    if (alarmMuted)             s.flags |= 0x20;
    if (ventBackHintShown)      s.flags |= 0x40;
    if (endConfirmShown)        s.flags |= 0x100;  // A7：bit 8
    if (flashState.active)      s.flags |= 0x200;  // Batch 2：flash overlay bit 9
    if (ventPreShown)           s.flags |= 0x400;  // A8：VENT_PRE bit 10

    // ALARMING flash phase：bit 進 snapshot 讓 dedupe 在 ALARMING 期間每半週期觸發一次重繪
    // （demo flashRed 0.6s 全週期 → OHCA_FLASH_HALF_MS 半週期）
    const bool alarmingFlashPhase = (globalState == GLOBAL_OHCA)
                                 && (ohcaState == OHCA_STATE_ALARMING)
                                 && (((millis() / OHCA_FLASH_HALF_MS) & 1) != 0);
    if (alarmingFlashPhase) s.flags |= 0x80;
    return s;
}

void updateDisplay() {
    // STEP 00: snapshot 去重 — 顯示狀態無變化即跳過，避免無謂的全螢幕重畫造成掃描線閃爍。
    DisplaySnapshot now = captureDisplaySnapshot();
    if (memcmp(&now, &lastDisplaySnapshot, sizeof(DisplaySnapshot)) == 0) {
        return;
    }

    // STEP 01: partial update — 倒數每秒只 tick 一格時，整片 fillScreen 會出掃描線閃爍。
    // 偵測「在 OHCA 倒數同 state 內，僅 countdownSec 改變」→ 只重繪時間區塊，不動 badge/label/counter。
    // 排除 ALARMING（半週期閃 phase 跟著變，必須走 full path 重畫整片紅 bg）。
    // 排除 modal overlay（confirm dialog/flash）— partial 不會重畫 overlay，會被時間區塊 fillRect 蓋掉
    constexpr uint16_t MODAL_FLAGS_MASK = 0x01    // showEpiArmedPrompt
                                        | 0x02    // showShockArmedPrompt
                                        | 0x04    // showAmioArmedPrompt
                                        | 0x100   // endConfirmShown
                                        | 0x200;  // flashState.active
    const bool inCountdownGroup = (now.globalState == GLOBAL_OHCA)
                               && (now.ohcaState == OHCA_STATE_COUNTDOWN
                                   || now.ohcaState == OHCA_STATE_WARNING
                                   || now.ohcaState == OHCA_STATE_OVERTIME)
                               && (now.ohcaSubState == 0)
                               && ((now.flags & MODAL_FLAGS_MASK) == 0);
    const bool sameStateAsLast = (now.globalState     == lastDisplaySnapshot.globalState)
                              && (now.ohcaState       == lastDisplaySnapshot.ohcaState)
                              && (now.ohcaSubState    == lastDisplaySnapshot.ohcaSubState)
                              && (now.mainMenuCursor  == lastDisplaySnapshot.mainMenuCursor)
                              && (now.ventBeat        == lastDisplaySnapshot.ventBeat)
                              && (now.ventVolume      == lastDisplaySnapshot.ventVolume)
                              && (now.ventPaused      == lastDisplaySnapshot.ventPaused)
                              && (now.flags           == lastDisplaySnapshot.flags);
    if (inCountdownGroup
        && sameStateAsLast
        && (now.countdownSec != lastDisplaySnapshot.countdownSec)) {
        drawOhcaCountdownTimeOnly(now.ohcaState);
        // LGFX DMA pushSprite 整片 ~4ms @80MHz；sub-region push 雖更短，
        // 但 DMA 整片視覺已近瞬完成，留簡化邏輯
        display.pushSprite(0, 0);
        lastDisplaySnapshot = now;
        return;
    }

    Serial.printf("[REDRAW] gs=%u os=%u sub=%u cur=%u cdSec=%lu vBeat=%u flags=0x%04x\n",
                  now.globalState, now.ohcaState, now.ohcaSubState, now.mainMenuCursor,
                  (unsigned long)now.countdownSec, now.ventBeat, now.flags);
    lastDisplaySnapshot = now;

    display.clearDisplay();

    if (globalState == GLOBAL_MAIN_MENU) {
        drawMainMenu();
    } else if (globalState == GLOBAL_OHCA) {
        // Phase B: sub-state 子流程畫面優先
        if (ohcaSubState == SUBSTATE_QUICK_MENU)        { drawQuickMenu();       display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_DRUG_MENU)         { drawDrugMenu();        display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_TYPE)     { drawBackfillType();    display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_COUNT)    { drawBackfillCount();   display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_CONFIRM)  { drawBackfillConfirm(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS)  { drawBackfillSuccess(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_AMIO_CONFIRM)      { drawAmioConfirmPrompt(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_TIMELINE)          { drawTimeline();        display.pushSprite(0, 0); return; }

        switch (ohcaState) {
            case OHCA_STATE_START_FLASH:
                drawOhcaStartFlash();
                break;
            case OHCA_STATE_WAIT_FIRST_EPI:
                drawOhcaWaitFirstEpi();
                break;
            case OHCA_STATE_COUNTDOWN:
            case OHCA_STATE_WARNING:
            case OHCA_STATE_ALARMING:
            case OHCA_STATE_OVERTIME: {
                const uint32_t now    = millis();
                const uint32_t since  = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                const uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
                const uint32_t past   = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;
                // ALARMING 閃爍開關直接從 snapshot 讀，避免在 render 內二次取樣 millis()
                const bool alarmingFlashOn = (lastDisplaySnapshot.flags & 0x80) != 0;
                if (ohcaState == OHCA_STATE_COUNTDOWN) {
                    drawOhcaCountdownCommon(remain, COLOR_TEXT_PRIMARY, "下次給藥", false);
                } else if (ohcaState == OHCA_STATE_WARNING) {
                    drawOhcaCountdownCommon(remain, COLOR_ACCENT_WARN,  "請準備給藥", false);
                } else if (ohcaState == OHCA_STATE_ALARMING) {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "請給藥",     alarmingFlashOn);
                } else {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "請給藥",   false);
                }
                break;
            }
            case OHCA_STATE_END_CHECK:
                if (endConfirmShown) drawOhcaEndConfirmDialog();
                else                  drawOhcaEndCheck();
                break;
            case OHCA_STATE_LOCKED:
                drawOhcaLocked();
                break;
            case OHCA_STATE_SUMMARY:
                drawOhcaSummary();
                break;
            default:
                break;
        }

        // overlay：6 秒通氣輔助區塊（V1 §14；不蓋 ALARMING 訊息）
        if (ohcaVentOverlayEnabled &&
            ohcaState != OHCA_STATE_START_FLASH &&
            ohcaState != OHCA_STATE_END_CHECK   &&
            ohcaState != OHCA_STATE_LOCKED      &&
            ohcaState != OHCA_STATE_SUMMARY) {
            drawOhcaVentOverlay(/*y_top*/ 44);
        }

        // A1：兩段確認改用全螢幕對話（fillScreen 覆蓋背景，對齊 demo OHCA_CONFIRM）
        if (showEpiArmedPrompt)        drawOhcaConfirmDialog(EVT_EPI_LOCAL);
        else if (showShockArmedPrompt) drawOhcaConfirmDialog(EVT_SHOCK_LOCAL);

    } else if (globalState == GLOBAL_VENT) {
        if (ventPreShown)            drawVentPre();
        else if (ventEndCheckShown)  drawVentEndCheck();
        else                          drawVentStandalone();
    } else if (globalState == GLOBAL_TRAINING_PLACEHOLDER) {
        drawPlaceholder("訓練模式", "D 階段");
    } else if (globalState == GLOBAL_HISTORY_PLACEHOLDER) {
        drawPlaceholder("歷史紀錄", "E 階段");
    } else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
        drawPlaceholder("系統設定", "G 階段");
    }

    // Flash overlay 最上層覆蓋（demo flash() 對齊 — 蓋掉所有底下畫面）
    if (flashState.active) {
        drawFlashOverlay();
    }

    // STEP 99: pushSprite DMA 一次推到實體 TFT — 消除「fillScreen → 慢慢出文字」中間態
    display.pushSprite(0, 0);
}

void drawMainMenu() {
    // 螢幕已由 updateDisplay() 的 clearDisplay() 清為黑底，這裡不重複 fillScreen 避免雙閃。

    // STEP 01: 上方標題列 — "EMS DOSESYNC PRO" 英文用 default font size 2
    display.setFont(&fonts::Font0);
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setCursor(16, 12);
    display.print("EMS DOSESYNC PRO");

    // STEP 02: 標題下分隔線 y=36，灰色橫貫
    display.drawLine(16, 36, SCREEN_W - 16, 36, COLOR_TEXT_DIM);

    // STEP 03: 5 個選單項用 efontCN_24（24px CJK，size 1，UTF-8 自動解碼），y=58 起每 36px 一行
    //   - cursor 項：白底黑字（demo cursor highlight）
    //   - 非 cursor：黑底白字
    constexpr int16_t MENU_Y_START   = 58;
    constexpr int16_t MENU_ROW_H     = 36;
    constexpr int16_t MENU_TEXT_PAD  = 24;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;  // efont 24px 字在 36px row 內垂直置中

    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == mainMenuCursor) {
            // STEP 03.01: cursor highlight — 白底全寬橫條，黑字
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(MAIN_MENU_LABELS[i]);
    }
}

/** 共用：水平置中文字（top_center datum，y = top 對齊） */
static void drawCenteredText(const char* text, int16_t y, uint16_t color) {
    display.setTextColor(color);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString(text, SCREEN_W / 2, y);
}

void drawOhcaStartFlash() {
    // OHCA 案件啟動 1 秒提示：對齊 demo flash('案件開始', 'OHCA')
    // 主：「案件開始」綠色（efontTW_24 × 1.5 ≈ 36px）
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("案件開始", SCREEN_H / 2 - 24, COLOR_ACCENT_OK);

    // 副：「OHCA」灰色小字（Font0 size 3，與 OHCA badge 同樣式）
    display.setFont(&fonts::Font0);
    display.setTextSize(3);
    drawCenteredText("OHCA", SCREEN_H / 2 + 32, COLOR_TEXT_MUTED);
}

void drawOhcaWaitFirstEpi() {
    // 對齊 docs/demo/index.html 第二螢幕「待本機 EPI」layout（中文化 + 字級 1.2-1.5x）
    // 前置：caller 已 clearDisplay 為黑底

    // 頂部 OHCA 綠 badge（ASCII，default font size 3）
    display.setFont(&fonts::Font0);
    display.setTextSize(3);
    drawCenteredText("OHCA", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // 中央大字「待本機 EPI」（efontTW_24 × 1.5 ≈ 36px）
    // demo OHCA 螢幕無副標，故移除「(按兩次 EPI 確認)」hint
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("待本機 EPI", SCREEN_H / 2 - 16, COLOR_TEXT_MUTED);

    // 底部 EPI/電擊 計數（與 OHCA 倒數計數行一致）
    uint16_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if      (isEpiEvent(&events[i]))   epiN   += events[i].count;
        else if (isShockEvent(&events[i])) shockN += events[i].count;
    }
    char counter[32];
    snprintf(counter, sizeof(counter), "EPI %u｜電擊 %u", epiN, shockN);
    display.setTextSize(1);
    drawCenteredText(counter, SCREEN_H - OHCA_COUNTER_BOTTOM - 6, COLOR_TEXT_DIM);
}

/**
 * OHCA 倒數共用畫面（對齊 docs/demo/index.html OHCA 第二螢幕）。
 *
 * 320×240 layout：
 *   - 頂部 mode badge "OHCA"（綠），baseline y = OHCA_BADGE_Y
 *   - 中央大時間 mm:ss（FreeMonoBold24pt7b），顏色由 caller 指定
 *   - 時間下方標籤（default font size 2，灰色）
 *   - 底部 "EPI N  Shock N"（default font size 1，dim 灰）
 *   - flashOn=true 時整片背景填 COLOR_FLASH_VENT；flashOn 由 snapshot phase bit 決定（單一真相）
 *
 * 前置條件：caller（updateDisplay）已執行 display.clearDisplay() 把畫面填黑。
 */
void drawOhcaCountdownCommon(uint32_t time_ms, uint16_t timeColor, const char* label, bool flashOn) {
    // STEP 01: 背景 — flashOn 時填深紅，否則保持 caller clear 的黑底
    if (flashOn) {
        display.fillScreen(COLOR_FLASH_VENT);
    }

    // STEP 02: 頂部 mode badge "OHCA"（綠 top-center datum，PM 反饋字級放大）
    display.setFont(&fonts::Font0);
    display.setTextSize(3);
    display.setTextColor(COLOR_ACCENT_OK);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString("OHCA", SCREEN_W / 2, OHCA_BADGE_Y);

    // STEP 03: 中央大時間（FreeMonoBold24pt7b，middle-center datum 自動置中）
    char timeStr[8];
    const uint32_t total_sec = time_ms / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)(total_sec / 60),
             (unsigned long)(total_sec % 60));

    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);  // 96px 高（PM 反饋大時間放大；middle_center 自動居中）
    display.setTextColor(timeColor);
    display.setTextDatum(textdatum_t::middle_center);
    const int16_t time_y = SCREEN_H / 2 - OHCA_TIME_VISUAL_UP;
    display.drawString(timeStr, SCREEN_W / 2, time_y);

    // STEP 04: 時間下方標籤（efontTW_24 繁中 24px × 1.2 ≈ 29px，PM 反饋再大 1.2x）
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString(label, SCREEN_W / 2, time_y + OHCA_LABEL_GAP_PX);

    // STEP 05: 累加 EPI / Shock 事件次數（含補登 count）
    uint16_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if      (isEpiEvent(&events[i]))   epiN   += events[i].count;
        else if (isShockEvent(&events[i])) shockN += events[i].count;
    }

    // STEP 06: 底部計數行（bottom-center，efontTW_24 size 1 ≈ 14px ASCII，保持原視覺大小）
    char counter[32];     // UTF-8「電擊」6 byte，buf 從 24 加大避免飽和
    snprintf(counter, sizeof(counter), "EPI %u｜電擊 %u", epiN, shockN);
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_DIM);
    display.setTextDatum(textdatum_t::bottom_center);
    display.drawString(counter, SCREEN_W / 2, SCREEN_H - OHCA_COUNTER_BOTTOM);
}

/**
 * OHCA 倒數 partial update — 每秒 tick 只重畫時間區塊，badge / label / counter 不動。
 *
 * 解閃爍：避免每秒 fillScreen 整片 320×240 → 掃描線可見。只 fillRect 時間 bbox。
 * 限定條件由 caller 確保（同 state、僅 countdownSec 變化、非 ALARMING）。
 */
static void drawOhcaCountdownTimeOnly(uint8_t ohcaStateForTime) {
    const uint32_t now    = millis();
    const uint32_t since  = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
    const uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
    const uint32_t past   = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;

    uint32_t time_ms;
    uint16_t timeColor;
    if (ohcaStateForTime == OHCA_STATE_COUNTDOWN) {
        time_ms = remain; timeColor = COLOR_TEXT_PRIMARY;
    } else if (ohcaStateForTime == OHCA_STATE_WARNING) {
        time_ms = remain; timeColor = COLOR_ACCENT_WARN;
    } else {
        time_ms = past;   timeColor = COLOR_ACCENT_ALERT;  // OVERTIME
    }

    char timeStr[8];
    const uint32_t total_sec = time_ms / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)(total_sec / 60),
             (unsigned long)(total_sec % 60));

    // 先把 partial push 區域的 canvas 全黑（解 advance gap 殘留 — glyph bbox 之外
    // 的像素 setTextColor(fg,bg) 不會清，每秒寫一次會累積成白塊）
    display.fillRect(OHCA_TIME_PUSH_X, OHCA_TIME_PUSH_Y,
                     OHCA_TIME_PUSH_W, OHCA_TIME_PUSH_H, COLOR_BG);

    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);  // 對齊 drawOhcaCountdownCommon STEP 03
    display.setTextColor(timeColor);
    display.setTextDatum(textdatum_t::middle_center);
    const int16_t time_y = SCREEN_H / 2 - OHCA_TIME_VISUAL_UP;
    display.drawString(timeStr, SCREEN_W / 2, time_y);
}

void drawOhcaEndCheck() {
    // 標題（efontTW_24 size 1.2 ≈ 29px）
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束前檢查", OHCA_BADGE_Y, COLOR_ACCENT_WARN);

    // 三個選項：完成並結束案件 / 前往補登 / 返回案件（demo OHCA_END_CHECK）
    const int16_t y0     = 70;
    const int16_t row_h  = 40;

    auto drawOption = [&](const char* text, int16_t y, bool selected) {
        if (selected) {
            display.fillRect(20, y, SCREEN_W - 40, row_h, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setFont(&fonts::efontTW_24);
        display.setTextSize(1.2f, 1.2f);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString(text, SCREEN_W / 2, y + row_h / 2);
    };

    drawOption("完成並結束案件", y0,            endCheckCursor == END_CHECK_CURSOR_CONFIRM);
    drawOption("前往補登",       y0 + row_h,    endCheckCursor == END_CHECK_CURSOR_BACKFILL);
    drawOption("返回案件",       y0 + row_h*2,  endCheckCursor == END_CHECK_CURSOR_CANCEL);

    // 底部 hint
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    drawCenteredText("上下選擇　主鍵確認",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/**
 * 觸發 flash 過場提示（對齊 demo flash() helper）。
 * @param title       主標題（非 NULL）
 * @param subtitle    副標題（NULL 或 "" 略過）
 * @param duration_ms 顯示毫秒（典型 FLASH_DEFAULT_MS=1200）
 * @param titleColor  主標題色（典型 COLOR_ACCENT_OK / COLOR_TEXT_PRIMARY）
 */
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor) {
    flashState.active     = true;
    flashState.startMs    = millis();
    flashState.durationMs = duration_ms;
    flashState.titleColor = titleColor;
    strncpy(flashState.title,    title    ? title    : "", sizeof(flashState.title)    - 1);
    strncpy(flashState.subtitle, subtitle ? subtitle : "", sizeof(flashState.subtitle) - 1);
    flashState.title[sizeof(flashState.title)       - 1] = '\0';
    flashState.subtitle[sizeof(flashState.subtitle) - 1] = '\0';
    Serial.printf("[FLASH] %s | %s\n", flashState.title, flashState.subtitle);
}

/** Flash overlay render — 全螢幕黑底（覆蓋背景），主副標居中 */
void drawFlashOverlay() {
    display.fillScreen(COLOR_BG);
    display.setFont(&fonts::efontTW_24);
    const bool hasSub = (flashState.subtitle[0] != '\0');
    if (hasSub) {
        display.setTextSize(1.5f, 1.5f);
        drawCenteredText(flashState.title, SCREEN_H / 2 - 36, flashState.titleColor);
        display.setTextSize(1);
        drawCenteredText(flashState.subtitle, SCREEN_H / 2 + 24, COLOR_TEXT_MUTED);
    } else {
        display.setTextSize(1.5f, 1.5f);
        drawCenteredText(flashState.title, SCREEN_H / 2 - 18, flashState.titleColor);
    }
}

/** 對話框共用框架：8/8 margin → 304×224 大框（避免文字觸碰邊框） */
static void drawDialogFrame(uint16_t borderColor) {
    display.fillScreen(COLOR_BG);
    const int16_t margin = 8;
    const int16_t x = margin;
    const int16_t y = margin;
    const int16_t w = SCREEN_W - 2 * margin;   // 304
    const int16_t h = SCREEN_H - 2 * margin;   // 224
    display.drawRect(x,     y,     w,     h,     borderColor);
    display.drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
}

/**
 * A7：OHCA_END_CONFIRM 二次確認對話（END_CHECK 選「完成並結束」後彈出）
 * 對齊 demo OHCA_END_CONFIRM render
 */
void drawOhcaEndConfirmDialog() {
    drawDialogFrame(COLOR_ACCENT_ALERT);

    // 標題（紅色，efontTW_24 × 1.5 ≈ 36px）
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("確認結束案件？", 36, COLOR_ACCENT_ALERT);

    // 內文（efontTW_24 × 1.2 ≈ 29px）
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束後不可修改", 116, COLOR_TEXT_MUTED);

    // 底部分隔線（在 hint 上方）
    display.drawLine(20, 184, SCREEN_W - 20, 184, COLOR_TEXT_DIM);

    // hint
    display.setTextSize(1);
    drawCenteredText("主鍵確認　返回取消", 196, COLOR_TEXT_DIM);
}

/**
 * A1：OHCA 兩段確認全螢幕對話（取代 44px bar overlay）
 * 對齊 demo OHCA_CONFIRM render
 *
 * @param evType EVT_EPI_LOCAL / EVT_SHOCK_LOCAL（Amio 走 SUBSTATE_AMIO_CONFIRM）
 */
void drawOhcaConfirmDialog(uint8_t evType) {
    // 框架（demo overlay rgba(0,0,0,0.92) + 2px amber）
    drawDialogFrame(COLOR_ACCENT_WARN);

    // 標題（efontTW_24 × 1.5 ≈ 36px）
    const char* title = (evType == EVT_EPI_LOCAL)   ? "確認已給 EPI？"
                      : (evType == EVT_SHOCK_LOCAL) ? "確認已電擊？"
                      :                                "確認操作？";
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(title, 28, COLOR_TEXT_PRIMARY);

    // 內文兩行（efontTW_24 × 1.2 ≈ 29px）
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認後將建立時間戳", 92, COLOR_TEXT_MUTED);
    const char* body2 = (evType == EVT_EPI_LOCAL) ? "並重啟 4 分鐘倒數" : "不影響 EPI 倒數";
    drawCenteredText(body2, 128, COLOR_TEXT_MUTED);

    // 底部分隔線
    display.drawLine(20, 184, SCREEN_W - 20, 184, COLOR_TEXT_DIM);

    // hint
    display.setTextSize(1);
    const char* hint = (evType == EVT_EPI_LOCAL)   ? "再按 EPI 鍵確認　返回取消"
                     : (evType == EVT_SHOCK_LOCAL) ? "再按電擊鍵確認　返回取消"
                     :                                "主鍵確認　返回取消";
    drawCenteredText(hint, 196, COLOR_TEXT_DIM);
}

void drawOhcaLocked() {
    display.setFont(&fonts::efontTW_24);

    // 標題：紅「已鎖定」
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("已鎖定", OHCA_BADGE_Y, COLOR_ACCENT_ALERT);

    // 中央大字「案件結束」（size 2 ≈ 48px）
    display.setTextSize(2);
    drawCenteredText("案件結束", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    // 事件總數
    char buf[32];
    snprintf(buf, sizeof(buf), "事件 %u 筆", eventCount);
    display.setTextSize(1);
    drawCenteredText(buf, SCREEN_H / 2 + 40, COLOR_TEXT_MUTED);

    // 底部 hint
    drawCenteredText("主鍵　總覽",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

void drawOhcaSummary() {
    // 用 caseSummary 聚合（V1 §11）
    ohca_case_summary_t s;
    caseSummary_build(&s, events, eventCount, /*case_start*/ 0, /*case_end*/ 0);

    display.setFont(&fonts::efontTW_24);

    // 標題
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("案件總覽", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    char buf[64];

    // EPI 區塊
    snprintf(buf, sizeof(buf), "EPI 共 %u", s.epi_total);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(buf, 60, COLOR_TEXT_PRIMARY);
    snprintf(buf, sizeof(buf), "本機 %u｜接手前 %u｜純補登 %u",
             s.epi_local, s.epi_pre_handover, s.epi_pure_supp);
    display.setTextSize(1);
    drawCenteredText(buf, 92, COLOR_TEXT_MUTED);

    // 電擊 區塊
    snprintf(buf, sizeof(buf), "電擊 共 %u", s.shock_total);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(buf, 122, COLOR_TEXT_PRIMARY);
    snprintf(buf, sizeof(buf), "本機 %u｜接手前 %u｜純補登 %u",
             s.shock_local, s.shock_pre_handover, s.shock_pure_supp);
    display.setTextSize(1);
    drawCenteredText(buf, 154, COLOR_TEXT_MUTED);

    // Amio
    snprintf(buf, sizeof(buf), "Amiodarone %u", s.amio_total);
    display.setTextSize(1);
    drawCenteredText(buf, 184, COLOR_TEXT_MUTED);

    // 底部 hint
    drawCenteredText("返回　主功能表",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

void drawTwoStepArmedOverlay(const char* what) {
    // 底部全寬反色提示條（琥珀警示色，efontTW_24 × 1.2 ≈ 29px 黑字）
    const int16_t bar_h = 44;
    display.fillRect(0, SCREEN_H - bar_h, SCREEN_W, bar_h, COLOR_ACCENT_WARN);
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    display.setTextColor(COLOR_BG);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(what, SCREEN_W / 2, SCREEN_H - bar_h / 2);
}

void drawPlaceholder(const char* title, const char* phase) {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(title, OHCA_BADGE_Y, COLOR_TEXT_PRIMARY);

    display.setTextSize(2);
    drawCenteredText(phase, SCREEN_H / 2 - 40, COLOR_ACCENT_OK);

    display.setTextSize(1);
    drawCenteredText("尚未實作", SCREEN_H / 2 + 24, COLOR_TEXT_MUTED);
    drawCenteredText("返回　主功能表",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

// ============================================================
//  Phase B 顯示函式
// ============================================================

/** 藥物選單（V1 §9.2） */
void drawDrugMenu() {
    // 標題
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("EPI / 藥物選單", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // 2 項目
    const char* labels[2] = { "補登 EPI", "Amiodarone" };
    constexpr int16_t MENU_Y_START       = 78;
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    for (uint8_t i = 0; i < 2; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    drawCenteredText("上下選擇　主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 補登類型選擇（接手前 / 純補登） */
void drawBackfillType() {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    const char* header = (backfillCategory == BACKFILL_CAT_EPI) ? "補登 EPI" : "電擊補登";
    drawCenteredText(header, 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* labels[2];
    if (backfillCategory == BACKFILL_CAT_EPI) {
        labels[0] = "接手前 EPI"; labels[1] = "純補登 EPI";
    } else {
        labels[0] = "接手前電擊"; labels[1] = "純補登電擊";
    }

    constexpr int16_t MENU_Y_START       = 78;
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    for (uint8_t i = 0; i < 2; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    drawCenteredText("上下選擇　主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 補登次數選擇（V1 §9.6） */
void drawBackfillCount() {
    // 標題
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    const char* typeLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    drawCenteredText(typeLabel, 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // 範圍提示
    uint8_t maxN = suppCountMax(backfillSuppType);
    char rangeBuf[32];
    snprintf(rangeBuf, sizeof(rangeBuf), "次數（1~%u）", maxN);
    display.setTextSize(1);
    drawCenteredText(rangeBuf, 78, COLOR_TEXT_MUTED);

    // 大數字
    char numBuf[6];
    snprintf(numBuf, sizeof(numBuf), "%u", backfillCount);
    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(numBuf, SCREEN_W / 2, SCREEN_H / 2 + 8);

    // 底部 hint
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    drawCenteredText("上下調整　主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 補登確認對話框（V1 §9.4） */
void drawBackfillConfirm() {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認補登？", 20, COLOR_ACCENT_WARN);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s ×%u", shortLabel, backfillCount);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(buf, 90, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("成立後不可撤銷", 150, COLOR_TEXT_MUTED);

    drawCenteredText("主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 補登成功提示（2s 後自動消失，V1 §9.5） */
void drawBackfillSuccess() {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("補登成功", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s ×%u", shortLabel, backfillCount);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(buf, 90, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("已紀錄", 150, COLOR_TEXT_MUTED);
}

/** Amiodarone 兩段確認 */
void drawAmioConfirmPrompt() {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認 Amiodarone？", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    display.setTextSize(1);
    drawCenteredText("確認後將建立時間戳", 90, COLOR_TEXT_PRIMARY);
    drawCenteredText("不影響 EPI 倒數",   124, COLOR_TEXT_MUTED);

    if (showAmioArmedPrompt) {
        // 底部琥珀 bar overlay：再按一次主鍵確認
        const int16_t bar_h = 44;
        display.fillRect(0, SCREEN_H - bar_h, SCREEN_W, bar_h, COLOR_ACCENT_WARN);
        display.setFont(&fonts::efontTW_24);
        display.setTextSize(1.2f, 1.2f);
        display.setTextColor(COLOR_BG);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString("再按一次主鍵確認", SCREEN_W / 2, SCREEN_H - bar_h / 2);
    } else {
        drawCenteredText("主鍵確認　返回取消",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
    }
}

/** Timeline 子畫面（V1 §11.5） */
void drawTimeline() {
    // 標題
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("事件時間軸", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    if (eventCount == 0) {
        display.setTextSize(1);
        drawCenteredText("（無事件）", SCREEN_H / 2 - 12, COLOR_TEXT_MUTED);
        drawCenteredText("返回　總覽",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // 排序索引（純函式 lib）
    static uint16_t idx[MAX_EVENTS];
    caseSummary_buildTimeline(idx, events, eventCount);

    // 顯示最多 5 筆，row_h=28
    constexpr int16_t ROW_Y0  = 70;
    constexpr int16_t ROW_H   = 28;
    constexpr int16_t TIME_X  = 24;
    constexpr int16_t LABEL_X = 110;

    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    uint16_t shown = 0;
    for (uint16_t i = timelineScrollOffset; i < eventCount && shown < 5; i++, shown++) {
        const int16_t y = ROW_Y0 + shown * ROW_H;
        const ems_event_t& e = events[idx[i]];

        // 時間欄
        char timeBuf[16];
        const bool isSupp = isBackfillEvent(&e);
        if (isSupp) {
            snprintf(timeBuf, sizeof(timeBuf), "—");
            display.setTextColor(COLOR_TEXT_DIM);
        } else {
            const uint32_t sec = e.elapsed_ms / 1000;
            snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu",
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60));
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(TIME_X, y);
        display.print(timeBuf);

        // 類型欄
        const char* lbl =
            e.type == EVT_EPI_LOCAL          ? "EPI" :
            e.type == EVT_SHOCK_LOCAL        ? "電擊" :
            e.type == EVT_AMIODARONE         ? "Amiodarone" :
            e.type == EVT_EPI_PRE_HANDOVER   ? "接手前 EPI" :
            e.type == EVT_EPI_PURE_SUPP      ? "純補登 EPI" :
            e.type == EVT_SHOCK_PRE_HANDOVER ? "接手前電擊" :
            e.type == EVT_SHOCK_PURE_SUPP    ? "純補登電擊" : "?";
        display.setTextColor(isSupp ? COLOR_ACCENT_WARN : COLOR_TEXT_PRIMARY);
        display.setCursor(LABEL_X, y);
        display.print(lbl);
        if (e.count > 1) {
            display.print(" ×");
            display.print(e.count);
        }
    }

    drawCenteredText("返回　總覽",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

// ============================================================
//  Phase C 顯示函式
// ============================================================

/** 獨立 6 秒通氣節奏主畫面（V1 §13.6 執行中畫面 / §13.12 暫停畫面） */
/**
 * A8：VENT_PRE 預備畫面 — 進入 Vent 模式但尚未按主鍵開始
 * 對齊 demo VENT_PRE render
 */
void drawVentPre() {
    display.setFont(&fonts::efontTW_24);

    // 頂部標題
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("6 秒通氣節奏", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // 中央主訊息「按主鍵開始」（efontTW × 1.5 ≈ 36px）
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("按主鍵開始", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    // 副訊息兩行
    display.setTextSize(1);
    drawCenteredText("通氣音量可由 上/下 調整", SCREEN_H / 2 + 32, COLOR_TEXT_MUTED);
    drawCenteredText("主鍵暫停／繼續　長按 3 秒結束", SCREEN_H / 2 + 60, COLOR_TEXT_DIM);

    // 底部音量顯示
    char volBuf[32];
    snprintf(volBuf, sizeof(volBuf), "音量 %u/%u", ventVolume, VENT_VOLUME_MAX);
    drawCenteredText(volBuf, SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

void drawVentStandalone() {
    display.setFont(&fonts::efontTW_24);

    // 頂部標題 + 音量
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("6 秒通氣節奏", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    char volBuf[32];
    snprintf(volBuf, sizeof(volBuf), "音量 %u/%u", ventVolume, VENT_VOLUME_MAX);
    display.setTextSize(1);
    drawCenteredText(volBuf, 56, COLOR_TEXT_MUTED);

    if (ventPaused) {
        display.setTextSize(2);
        drawCenteredText("已暫停", SCREEN_H / 2 - 24, COLOR_ACCENT_WARN);
        display.setTextSize(1);
        drawCenteredText("主鍵繼續　返回結束",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // 中央大字單秒數（中心 y 略下移，避開頂部 Vol 行）
    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    char numBuf[4];
    snprintf(numBuf, sizeof(numBuf), "%u", num);
    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(numBuf, SCREEN_W / 2, SCREEN_H / 2 + 16);

    // 底部提示（橫條反色顯示結束 hint 或基本提示）
    display.setFont(&fonts::efontTW_24);
    if (ventBackHintShown) {
        const int16_t bar_h = 32;
        display.fillRect(0, SCREEN_H - bar_h, SCREEN_W, bar_h, COLOR_ACCENT_WARN);
        display.setTextSize(1);
        display.setTextColor(COLOR_BG);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString("如要結束　請長按主鍵",
                           SCREEN_W / 2, SCREEN_H - bar_h / 2);
    } else {
        display.setTextSize(1);
        drawCenteredText("主鍵暫停　長按 3 秒結束",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
    }
}

/** 獨立 vent 結束確認對話框（V1 §13.14 結束獨立 6 秒通氣節奏） */
void drawVentEndCheck() {
    display.setFont(&fonts::efontTW_24);

    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束通氣節奏？", OHCA_BADGE_Y, COLOR_ACCENT_WARN);

    display.setTextSize(2);
    drawCenteredText("確認結束？", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 快速功能選單（V1 §14.9 開啟、暫停、繼續與關閉 + §9 OHCA 中按返回鍵） */
void drawQuickMenu() {
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("快速功能", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // V1 §14.9：動態 3 / 4 項（B3：加「案件簡版總覽」對齊 demo）
    const char* labels[4];
    uint8_t count;
    if (!ohcaVentOverlayEnabled) {
        labels[0] = "開啟 6 秒給氣提示";
        labels[1] = "案件簡版總覽";
        labels[2] = "返回 OHCA";
        count = 3;
    } else {
        labels[0] = ohcaVentPaused ? "繼續 6 秒給氣" : "暫停 6 秒給氣";
        labels[1] = "關閉 6 秒給氣提示";
        labels[2] = "案件簡版總覽";
        labels[3] = "返回 OHCA";
        count = 4;
    }

    constexpr int16_t MENU_Y_START       = 78;
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    display.setFont(&fonts::efontTW_24);
    display.setTextSize(1);
    for (uint8_t i = 0; i < count; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    drawCenteredText("上下選擇　主鍵確認　返回關閉",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** OHCA 內 6 秒通氣輔助區塊（V1 §14.4 單秒數視窗 / §14.10 暫停狀態）
 *  右上角小 overlay：放在 OHCA badge 同 y 的右側，不撞中央大時間 / 標籤 / 計數
 *  y_top 參數保留 API 相容但忽略（新 layout 自決定位置）
 */
void drawOhcaVentOverlay(int /*y_top*/) {
    display.setFont(&fonts::efontTW_24);

    if (ohcaVentPaused) {
        // 暫停狀態：右上角「通氣暫停」
        display.setTextSize(1);
        display.setTextColor(COLOR_ACCENT_WARN);
        display.setTextDatum(textdatum_t::top_right);
        display.drawString("通氣暫停", SCREEN_W - 8, OHCA_BADGE_Y + 2);
        return;
    }

    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    // 右上角「通氣 N」
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::top_right);
    char buf[16];
    snprintf(buf, sizeof(buf), "通氣 %u", num);
    display.drawString(buf, SCREEN_W - 8, OHCA_BADGE_Y + 2);
}
