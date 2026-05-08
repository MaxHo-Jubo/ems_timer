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
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeMonoBold24pt7b.h>

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
 * Adafruit_ST7789 的薄包裝：補回 SH110X 風格的 clearDisplay() / display() 介面。
 * Step 1 用途：讓既有 1300+ 行 `display.xxx()` 呼叫點不需修改即可編譯通過。
 * Step 2 將以 `ems_display` 模組取代，本 adapter 屆時刪除。
 */
class TftAdapter : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;
    /** SH110X 相容：清空畫面（TFT 直接 fillScreen 黑） */
    void clearDisplay() { fillScreen(SH110X_BLACK); }
    /** SH110X 相容：buffer flush（TFT 立即寫入，no-op） */
    void display() { /* ST7789 writes immediately, nothing to flush */ }
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
/** 大時間視覺上偏 px（baseline 中心微調，留下方標籤空間） */
static const int16_t  OHCA_TIME_VISUAL_UP = 8;
/** 標籤距大時間 baseline 下方 px */
static const int16_t  OHCA_LABEL_GAP_PX   = 16;
/** 底部 EPI/Shock 計數行距底邊 px */
static const int16_t  OHCA_COUNTER_BOTTOM = 18;
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
    "OHCA Case",
    "6sec Vent",
    "Training",
    "History",
    "Settings",
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

/** OHCA 內 6 秒通氣輔助區塊開關（V1 §14.9 開啟、暫停、繼續與關閉） */
static bool ohcaVentOverlayEnabled = false;
/** V1 §14.10 / §14.11：OHCA 內 6 秒通氣暫停旗標（透過快速功能切換） */
static bool ohcaVentPaused         = false;

// ============================================================
// END_CHECK 與 SUMMARY 子狀態
// ============================================================

enum EndCheckCursor : uint8_t {
    END_CHECK_CURSOR_CONFIRM = 0,
    END_CHECK_CURSOR_CANCEL  = 1,
};
static EndCheckCursor endCheckCursor = END_CHECK_CURSOR_CANCEL;

static uint16_t summaryScrollOffset = 0;

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

TftAdapter display(TFT_CS_PIN, TFT_DC_PIN, TFT_MOSI_PIN, TFT_SCLK_PIN, TFT_RST_PIN);

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
void drawOhcaEndCheck();
void drawOhcaLocked();
void drawOhcaSummary();
void drawTwoStepArmedOverlay(const char* what);
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

    // STEP 03: TFT 初始化（取代舊 SH1106 OLED）
    //   - init() 回傳 void，無 fail handler 介面（Adafruit_ST7789 API 限制）
    //   - invertDisplay(false)：蝦皮 ST7789 紅板 polarity 與 Adafruit 預設相反，必加（見 tft-migration-plan.md §3.5）
    display.init(TFT_WIDTH, TFT_HEIGHT);
    display.setRotation(1);  // 1 = 橫向 320x240
    display.invertDisplay(false);
    display.fillScreen(SH110X_BLACK);

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
                        globalState        = GLOBAL_VENT;
                        ventStartMs        = millis();
                        ventPrevSinceMs    = 0;
                        ventEndCheckShown  = false;
                        ventBackHintShown  = false;
                        ventPaused         = false;  // V1 §13.5 啟動規則：秒數從 1 開始
                        Serial.println("[VENT] enter standalone");
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

        // STEP 01.5: QUICK_MENU（Phase C，返回鍵入口；V1 §14.9 動態 2/3 項）
        if (ohcaSubState == SUBSTATE_QUICK_MENU) {
            // V1 §14.9 選項：
            //   未開啟 (overlay=false)：[0] Enable 6s vent  [1] Back to OHCA
            //   已開啟 (overlay=true) ：[0] Pause/Resume    [1] Disable 6s vent  [2] Back
            uint8_t cnt = ohcaVentOverlayEnabled ? 3 : 2;
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
                if (!ohcaVentOverlayEnabled) {
                    if (backfillCursor == 0) {
                        // STEP 01.5.1: Enable 6s vent（V1 §14.9）
                        ohcaVentOverlayEnabled = true;
                        ohcaVentPaused         = false;
                        ventStartMs            = millis();
                        ventPrevSinceMs        = 0;
                        Serial.println("[OHCA] vent overlay = ON");
                    }
                    // backfillCursor == 1 → Back（resetSubState 即可）
                } else {
                    if (backfillCursor == 0) {
                        // STEP 01.5.2: toggle Pause / Resume（V1 §14.10 暫停 / §14.11 繼續）
                        ohcaVentPaused = !ohcaVentPaused;
                        if (!ohcaVentPaused) {
                            // V1 §14.11：繼續時秒數重新從 1 開始
                            ventStartMs     = millis();
                            ventPrevSinceMs = 0;
                        } else {
                            stopBeep();
                        }
                        Serial.printf("[OHCA] vent paused = %d\n", ohcaVentPaused);
                    } else if (backfillCursor == 1) {
                        // STEP 01.5.3: Disable 6s vent（V1 §14.9）
                        ohcaVentOverlayEnabled = false;
                        ohcaVentPaused         = false;
                        stopBeep();
                        Serial.println("[OHCA] vent overlay = OFF");
                    }
                    // backfillCursor == 2 → Back
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
                if (endCheckCursor == END_CHECK_CURSOR_CONFIRM) {
                    dispatchOhcaEvent(OHCA_EVT_END_CONFIRM, 0);
                    Serial.println("[OHCA] case LOCKED");
                } else {
                    dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                }
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
            if (ohcaState == OHCA_STATE_END_CHECK) {
                endCheckCursor = (endCheckCursor == END_CHECK_CURSOR_CANCEL)
                               ? END_CHECK_CURSOR_CONFIRM : END_CHECK_CURSOR_CANCEL;
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                // Phase B: SUMMARY 上鍵 → 進 Timeline 子畫面
                ohcaSubState         = SUBSTATE_TIMELINE;
                timelineScrollOffset = 0;
            }
            break;

        case BTN_DOWN:
            if (ohcaState == OHCA_STATE_END_CHECK) {
                endCheckCursor = (endCheckCursor == END_CHECK_CURSOR_CANCEL)
                               ? END_CHECK_CURSOR_CONFIRM : END_CHECK_CURSOR_CANCEL;
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                ohcaSubState         = SUBSTATE_TIMELINE;
                timelineScrollOffset = 0;
            }
            break;

        case BTN_BACK:
            // STEP 01: SUMMARY 返回主功能表
            if (ohcaState == OHCA_STATE_SUMMARY) {
                exitOhcaCase();
                return;
            }
            // STEP 02: END_CHECK 返回鍵 = 取消
            if (ohcaState == OHCA_STATE_END_CHECK) {
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
            endCheckCursor = END_CHECK_CURSOR_CANCEL;
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
    display.invertDisplay(true);
}

void updateOledFlashMachine() {
    if (!oledInverted) return;
    if (millis() - oledInvertStartMs >= oledInvertDurationMs) {
        oledInverted = false;
        display.invertDisplay(false);
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
    uint8_t  flags;              /**< bit-packed prompt/overlay 狀態 */
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
    Serial.printf("[REDRAW] gs=%u os=%u sub=%u cur=%u cdSec=%lu vBeat=%u flags=0x%02x\n",
                  now.globalState, now.ohcaState, now.ohcaSubState, now.mainMenuCursor,
                  (unsigned long)now.countdownSec, now.ventBeat, now.flags);
    lastDisplaySnapshot = now;

    display.clearDisplay();

    if (globalState == GLOBAL_MAIN_MENU) {
        drawMainMenu();
    } else if (globalState == GLOBAL_OHCA) {
        // Phase B: sub-state 子流程畫面優先
        if (ohcaSubState == SUBSTATE_QUICK_MENU)        { drawQuickMenu();       display.display(); return; }
        if (ohcaSubState == SUBSTATE_DRUG_MENU)         { drawDrugMenu();        display.display(); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_TYPE)     { drawBackfillType();    display.display(); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_COUNT)    { drawBackfillCount();   display.display(); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_CONFIRM)  { drawBackfillConfirm(); display.display(); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS)  { drawBackfillSuccess(); display.display(); return; }
        if (ohcaSubState == SUBSTATE_AMIO_CONFIRM)      { drawAmioConfirmPrompt(); display.display(); return; }
        if (ohcaSubState == SUBSTATE_TIMELINE)          { drawTimeline();        display.display(); return; }

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
                    drawOhcaCountdownCommon(remain, COLOR_TEXT_PRIMARY, "Next dose",   false);
                } else if (ohcaState == OHCA_STATE_WARNING) {
                    drawOhcaCountdownCommon(remain, COLOR_ACCENT_WARN,  "Prepare EPI", false);
                } else if (ohcaState == OHCA_STATE_ALARMING) {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "GIVE EPI!",   alarmingFlashOn);
                } else {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "OVERTIME",    false);
                }
                break;
            }
            case OHCA_STATE_END_CHECK:
                drawOhcaEndCheck();
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

        // overlay：兩段確認 armed 提示
        if (showEpiArmedPrompt) drawTwoStepArmedOverlay("EPI? press again");
        else if (showShockArmedPrompt) drawTwoStepArmedOverlay("Shock? press again");

    } else if (globalState == GLOBAL_VENT) {
        if (ventEndCheckShown) drawVentEndCheck();
        else                    drawVentStandalone();
    } else if (globalState == GLOBAL_TRAINING_PLACEHOLDER) {
        drawPlaceholder("Training", "Phase D");
    } else if (globalState == GLOBAL_HISTORY_PLACEHOLDER) {
        drawPlaceholder("History", "Phase E");
    } else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
        drawPlaceholder("Settings", "Phase G");
    }

    display.display();
}

void drawMainMenu() {
    // 螢幕已由 updateDisplay() 的 clearDisplay() 清為黑底，這裡不重複 fillScreen 避免雙閃。

    // STEP 01: 上方標題列 — "EMS DOSESYNC PRO" 灰字 size 2，y=8
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setCursor(16, 12);
    display.print("EMS DOSESYNC PRO");

    // STEP 02: 標題下分隔線 y=36，灰色橫貫
    display.drawLine(16, 36, SCREEN_W - 16, 36, COLOR_TEXT_DIM);

    // STEP 03: 5 個選單項，y=58 起每 36px 一行（總高 180px，剩 50/240 padding 上下分配）
    //   - cursor 項：白底黑字（demo cursor highlight）
    //   - 非 cursor：黑底白字
    //   - text size 3（每字 18×24 px），左 padding 24
    constexpr int16_t MENU_Y_START   = 58;
    constexpr int16_t MENU_ROW_H     = 36;
    constexpr int16_t MENU_TEXT_PAD  = 24;
    constexpr int16_t MENU_TEXT_SIZE = 3;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 8;  // text 在 row 內垂直置中的偏移

    display.setTextSize(MENU_TEXT_SIZE);
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

void drawOhcaStartFlash() {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 24);
    display.println("Start OHCA");
}

void drawOhcaWaitFirstEpi() {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("OHCA Case");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 24);
    display.println("Press EPI");
    display.setTextSize(1);
    display.setCursor(8, 50);
    display.println("(2-step confirm)");
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

    int16_t bx, by;       // getTextBounds 回填 bounding box 起點 offset
    uint16_t bw, bh;      // getTextBounds 回填 bounding box 寬高

    // STEP 02: 頂部 mode badge "OHCA"（綠）
    display.setFont();  // default 5x7
    display.setTextSize(2);
    display.setTextColor(COLOR_ACCENT_OK);
    display.getTextBounds("OHCA", 0, 0, &bx, &by, &bw, &bh);
    display.setCursor((SCREEN_W - (int16_t)bw) / 2, OHCA_BADGE_Y);
    display.print("OHCA");

    // STEP 03: 中央大時間（FreeMonoBold24pt7b，monospace 確保 mm:ss 數字 tick 不左右抖動）
    char timeStr[8];
    const uint32_t total_sec = time_ms / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)(total_sec / 60),
             (unsigned long)(total_sec % 60));

    display.setFont(&FreeMonoBold24pt7b);
    display.setTextSize(1);
    display.setTextColor(timeColor);
    display.getTextBounds(timeStr, 0, 0, &bx, &by, &bw, &bh);
    // 自訂字型 cursor.y 是 baseline；OHCA_TIME_VISUAL_UP 補上偏給 STEP 04 標籤留空間
    const int16_t time_x = (SCREEN_W - (int16_t)bw) / 2 - bx;
    const int16_t time_y = SCREEN_H / 2 + (int16_t)bh / 2 - OHCA_TIME_VISUAL_UP;
    display.setCursor(time_x, time_y);
    display.print(timeStr);

    // STEP 04: 時間下方標籤
    display.setFont();
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
    display.setCursor((SCREEN_W - (int16_t)bw) / 2, time_y + OHCA_LABEL_GAP_PX);
    display.print(label);

    // STEP 05: 累加 EPI / Shock 事件次數（含補登 count）
    uint16_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if      (isEpiEvent(&events[i]))   epiN   += events[i].count;
        else if (isShockEvent(&events[i])) shockN += events[i].count;
    }

    // STEP 06: 底部計數行 渲染
    char counter[24];     // "EPI 65535  Shock 65535\0" = 23 byte
    snprintf(counter, sizeof(counter), "EPI %u  Shock %u", epiN, shockN);
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_DIM);
    display.getTextBounds(counter, 0, 0, &bx, &by, &bw, &bh);
    display.setCursor((SCREEN_W - (int16_t)bw) / 2, SCREEN_H - OHCA_COUNTER_BOTTOM);
    display.print(counter);
}

void drawOhcaEndCheck() {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("End Check");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
    display.setCursor(4, 18);
    display.println("Confirm end of case?");

    // 兩個選項
    int y_confirm = 36, y_cancel = 50;
    if (endCheckCursor == END_CHECK_CURSOR_CONFIRM) {
        display.fillRect(0, y_confirm - 1, OLED_WIDTH, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
    }
    display.setCursor(4, y_confirm);
    display.print("> Confirm end");

    display.setTextColor(SH110X_WHITE);
    if (endCheckCursor == END_CHECK_CURSOR_CANCEL) {
        display.fillRect(0, y_cancel - 1, OLED_WIDTH, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
    }
    display.setCursor(4, y_cancel);
    display.print("> Back to case");
}

void drawOhcaLocked() {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("LOCKED");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 22);
    display.println("Case End");
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("Events: ");
    display.print(eventCount);
    display.setCursor(0, 58);
    display.print("[Main] -> Summary");
}

void drawOhcaSummary() {
    // 用 caseSummary 聚合（V1 §11）
    ohca_case_summary_t s;
    caseSummary_build(&s, events, eventCount, /*case_start*/ 0, /*case_end*/ 0);

    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Summary | OHCA");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    display.setCursor(0, 14);
    display.print("EPI Total: ");
    display.print(s.epi_total);
    display.setCursor(0, 24);
    display.print(" L:"); display.print(s.epi_local);
    display.print(" PH:"); display.print(s.epi_pre_handover);
    display.print(" PS:"); display.print(s.epi_pure_supp);

    display.setCursor(0, 34);
    display.print("Shock Total: ");
    display.print(s.shock_total);
    display.setCursor(0, 44);
    display.print(" L:"); display.print(s.shock_local);
    display.print(" PH:"); display.print(s.shock_pre_handover);
    display.print(" PS:"); display.print(s.shock_pure_supp);

    display.setCursor(72, 14);
    display.print("Amio:");
    display.print(s.amio_total);

    display.setCursor(0, 56);
    display.print("[Up]Time [Bk]Menu");

    display.setCursor(0, 56);
    display.print("[Back] -> Menu");
}

void drawTwoStepArmedOverlay(const char* what) {
    // 在底部畫一條反色提示條
    display.fillRect(0, OLED_HEIGHT - 12, OLED_WIDTH, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setTextSize(1);
    display.setCursor(2, OLED_HEIGHT - 10);
    display.print(what);
}

void drawPlaceholder(const char* title, const char* phase) {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(title);
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 22);
    display.print(phase);
    display.setTextSize(1);
    display.setCursor(0, 44);
    display.println("Not implemented");
    display.setCursor(0, 56);
    display.println("[Back] -> Menu");
}

// ============================================================
//  Phase B 顯示函式
// ============================================================

/** 藥物選單（V1 §9.2） */
void drawDrugMenu() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Drug Menu");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    const char* labels[2] = { "Backfill EPI", "Amiodarone" };
    for (uint8_t i = 0; i < 2; i++) {
        int y = 18 + i * 12;
        if (i == backfillCursor) {
            display.fillRect(0, y - 1, OLED_WIDTH, 11, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
        } else {
            display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print("> ");
        display.print(labels[i]);
    }
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 56);
    display.println("[Back] cancel");
}

/** 補登類型選擇（接手前 / 純補登） */
void drawBackfillType() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(backfillCategory == BACKFILL_CAT_EPI ? "Backfill EPI"
                                                         : "Backfill Shock");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    const char* labels[2] = { "Pre-handover", "Pure backfill" };
    for (uint8_t i = 0; i < 2; i++) {
        int y = 18 + i * 12;
        if (i == backfillCursor) {
            display.fillRect(0, y - 1, OLED_WIDTH, 11, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
        } else {
            display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print("> ");
        display.print(labels[i]);
    }
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 56);
    display.println("[Back] cancel");
}

/** 補登次數選擇（V1 §9.6） */
void drawBackfillCount() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    const char* typeLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "PreHandover EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "Pure Supp EPI"   :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "PreHandover Shk" :
                                                             "Pure Supp Shk";
    display.println(typeLabel);
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    display.setCursor(0, 18);
    display.print("Count: ");
    uint8_t maxN = suppCountMax(backfillSuppType);
    display.print("(1~");
    display.print(maxN);
    display.print(")");

    display.setTextSize(3);
    display.setCursor(48, 30);
    display.print(backfillCount);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.println("[Up/Dn] [Main]OK [Bk]");
}

/** 補登確認對話框（V1 §9.4） */
void drawBackfillConfirm() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Confirm backfill?");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "Pre-EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "Pure-EPI"   :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "Pre-Shk" :
                                                             "Pure-Shk";

    display.setTextSize(2);
    display.setCursor(8, 16);
    display.print(shortLabel);
    display.print(" x");
    display.print(backfillCount);

    display.setTextSize(1);
    display.setCursor(0, 38);
    display.println("Not undoable");
    display.setCursor(0, 56);
    display.println("[Main]OK [Bk]cancel");
}

/** 補登成功提示（2s 後自動消失，V1 §9.5） */
void drawBackfillSuccess() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Backfill OK");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "Pre-EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "Pure-EPI"   :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "Pre-Shk" :
                                                             "Pure-Shk";

    display.setTextSize(2);
    display.setCursor(8, 22);
    display.print(shortLabel);
    display.print(" x");
    display.print(backfillCount);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.println("Recorded");
}

/** Amiodarone 兩段確認 */
void drawAmioConfirmPrompt() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Amiodarone");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    display.setTextSize(2);
    display.setCursor(8, 22);
    display.println("Confirm?");

    display.setTextSize(1);
    if (showAmioArmedPrompt) {
        display.fillRect(0, OLED_HEIGHT - 12, OLED_WIDTH, 12, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(2, OLED_HEIGHT - 10);
        display.print("Press [Main] again");
    } else {
        display.setCursor(0, 56);
        display.println("[Main]confirm [Bk]cancel");
    }
}

/** Timeline 子畫面（V1 §11.5） */
void drawTimeline() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Timeline");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    if (eventCount == 0) {
        display.setCursor(0, 24);
        display.println("(no events)");
        display.setCursor(0, 56);
        display.println("[Back] -> Summary");
        return;
    }

    // 排序索引（純函式 lib）
    static uint16_t idx[MAX_EVENTS];
    caseSummary_buildTimeline(idx, events, eventCount);

    // 顯示最多 4 筆
    uint16_t shown = 0;
    for (uint16_t i = timelineScrollOffset; i < eventCount && shown < 4; i++, shown++) {
        int y = 14 + shown * 10;
        display.setCursor(0, y);
        const ems_event_t& e = events[idx[i]];

        // 時間欄：本機顯示 mm:ss；補登顯示「-」
        if (isBackfillEvent(&e)) {
            display.print("- ");
        } else {
            uint32_t sec = e.elapsed_ms / 1000;
            if (sec / 60 < 10) display.print("0");
            display.print(sec / 60);
            display.print(":");
            if (sec % 60 < 10) display.print("0");
            display.print(sec % 60);
            display.print(" ");
        }

        // 類型 + count（補登 ×N）
        const char* lbl =
            e.type == EVT_EPI_LOCAL          ? "EPI" :
            e.type == EVT_SHOCK_LOCAL        ? "Shk" :
            e.type == EVT_AMIODARONE         ? "Amio" :
            e.type == EVT_EPI_PRE_HANDOVER   ? "PreEPI" :
            e.type == EVT_EPI_PURE_SUPP      ? "PurEPI" :
            e.type == EVT_SHOCK_PRE_HANDOVER ? "PreShk" :
            e.type == EVT_SHOCK_PURE_SUPP    ? "PurShk" : "?";
        display.print(lbl);
        if (e.count > 1) {
            display.print("x");
            display.print(e.count);
        }
    }
    display.setCursor(0, 56);
    display.println("[Back] -> Summary");
}

// ============================================================
//  Phase C 顯示函式
// ============================================================

/** 獨立 6 秒通氣節奏主畫面（V1 §13.6 執行中畫面 / §13.12 暫停畫面） */
void drawVentStandalone() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("6sec Vent  Vol:");
    display.print(ventVolume);
    display.print("/");
    display.print(VENT_VOLUME_MAX);
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    if (ventPaused) {
        // V1 §13.12 暫停畫面：保留節奏標題，秒數窗位置改顯示 PAUSED
        display.setTextSize(2);
        display.setCursor(20, 24);
        display.print("PAUSED");
        display.setTextSize(1);
        display.setCursor(0, 56);
        display.print("[Main] resume [Bk]end");
        return;
    }

    // 大字顯示當前秒數
    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    display.setTextSize(5);
    display.setCursor(48, 18);
    display.print(num);

    // 底部提示
    display.setTextSize(1);
    if (ventBackHintShown) {
        display.fillRect(0, OLED_HEIGHT - 12, OLED_WIDTH, 12, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(2, OLED_HEIGHT - 10);
        display.print("Long-press [Main] to end");
    } else {
        display.setCursor(0, 56);
        display.print("[M]pause [M]3s=end");
    }
}

/** 獨立 vent 結束確認對話框（V1 §13.14 結束獨立 6 秒通氣節奏） */
void drawVentEndCheck() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("End vent rhythm?");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    display.setTextSize(2);
    display.setCursor(8, 22);
    display.println("Confirm?");

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.println("[Main]OK [Bk]cancel");
}

/** 快速功能選單（V1 §14.9 開啟、暫停、繼續與關閉 + §9 OHCA 中按返回鍵） */
void drawQuickMenu() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Quick Menu");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);

    // V1 §14.9：動態 2 / 3 項
    const char* labels[3];
    uint8_t count;
    if (!ohcaVentOverlayEnabled) {
        labels[0] = "Enable 6s vent";
        labels[1] = "Back to OHCA";
        count = 2;
    } else {
        labels[0] = ohcaVentPaused ? "Resume 6s vent" : "Pause 6s vent";
        labels[1] = "Disable 6s vent";
        labels[2] = "Back to OHCA";
        count = 3;
    }
    for (uint8_t i = 0; i < count; i++) {
        int y = 18 + i * 12;
        if (i == backfillCursor) {
            display.fillRect(0, y - 1, OLED_WIDTH, 11, SH110X_WHITE);
            display.setTextColor(SH110X_BLACK);
        } else {
            display.setTextColor(SH110X_WHITE);
        }
        display.setCursor(4, y);
        display.print("> ");
        display.print(labels[i]);
    }
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 56);
    display.println("[Main]OK [Bk]close");
}

/** OHCA 內 6 秒通氣輔助區塊（V1 §14.4 單秒數視窗 / §14.10 暫停狀態） */
void drawOhcaVentOverlay(int y_top) {
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, y_top);

    if (ohcaVentPaused) {
        // V1 §14.10：暫停狀態畫面「6秒給氣｜已暫停 / 快速功能可繼續」
        display.print("6s vent PAUSED");
        display.setCursor(0, y_top + 8);
        display.print("(QuickMenu Resume)");
        return;
    }

    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    // 標籤 + 單秒數
    display.print("6s vent ON  ");

    // 大字單秒數（往右排）
    display.setTextSize(2);
    display.setCursor(80, y_top - 2);
    display.print(num);
}
