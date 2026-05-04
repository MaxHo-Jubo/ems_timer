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
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "ems_ohca_state.h"
#include "ems_ohca_countdown.h"
#include "ems_two_step_confirm.h"

using namespace ems;

// ============================================================
// 硬體常數（gpio-allocation.md）
// ============================================================

/** OLED 解析度 */
static const uint8_t OLED_WIDTH     = 128;
static const uint8_t OLED_HEIGHT    = 64;
static const int8_t  OLED_RESET_PIN = -1;
static const uint8_t OLED_I2C_ADDR  = 0x3C;
static const uint8_t I2C_SDA_PIN    = 42;
static const uint8_t I2C_SCL_PIN    = 41;

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
static const uint16_t LONG_3S_PRESS_MS    = 3000;  // OHCA OVERTIME 結束前檢查觸發

// ============================================================
// 全域狀態機（pm-dev-spec §2）
// ============================================================

enum GlobalState : uint8_t {
    GLOBAL_MAIN_MENU            = 0,
    GLOBAL_OHCA                 = 1,
    GLOBAL_VENT_PLACEHOLDER     = 2,  // Phase C 待實作
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

/** 兩段確認 instances */
static two_step_confirm_t epiConfirm;
static two_step_confirm_t shockConfirm;

/** 兩段確認 armed 提示視窗 */
static bool     showEpiArmedPrompt   = false;
static uint32_t epiArmedPromptStart  = 0;
static bool     showShockArmedPrompt = false;
static uint32_t shockArmedPromptStart = 0;
static const uint32_t ARMED_PROMPT_MS = 5000;

// ============================================================
// 事件紀錄（簡單陣列，Phase E 才做持久化）
// ============================================================

struct OhcaEvent {
    uint8_t  type;        // 1=EPI, 2=Shock
    uint32_t elapsed_ms;  // 自 case 開始
};
static const uint16_t MAX_EVENTS = 50;
static OhcaEvent events[MAX_EVENTS];
static uint16_t  eventCount = 0;

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
static bool     btnLong3sFired[BTN_COUNT]   = { false };

// ============================================================
// OLED + 蜂鳴 / 反色 SM
// ============================================================

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

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
void onLong3sPress(uint8_t btnIdx);
void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms);
void updateOhcaTick();
void recordEvent(uint8_t type);
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
void drawOhcaCountdownCommon(const char* label, uint32_t remaining_ms, bool show_overtime);
void drawOhcaEndCheck();
void drawOhcaLocked();
void drawOhcaSummary();
void drawTwoStepArmedOverlay(const char* what);
void drawPlaceholder(const char* title, const char* phase);

// ============================================================
// setup() / loop()
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("[BOOT] EMS Timer Phase A");

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

    // STEP 03: OLED 初始化
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[FAIL] SSD1306 not found");
    }
    display.clearDisplay();
    display.display();

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

    uint32_t now = millis();
    twoStepConfirm_tick(&epiConfirm,   now);
    twoStepConfirm_tick(&shockConfirm, now);

    // STEP 01: armed 提示 5s 自動消失
    if (showEpiArmedPrompt && now - epiArmedPromptStart >= ARMED_PROMPT_MS) {
        showEpiArmedPrompt = false;
    }
    if (showShockArmedPrompt && now - shockArmedPromptStart >= ARMED_PROMPT_MS) {
        showShockArmedPrompt = false;
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
            if (cur == LOW) {
                // STEP 01.01: 按下（press start）
                if (now - lastPressMs[i] >= DEBOUNCE_MS) {
                    btnPressStartMs[i] = now;
                    btnLong3sFired[i]  = false;
                    lastPressMs[i]     = now;
                }
            } else {
                // STEP 01.02: 放開（release）
                if (btnPressStartMs[i] > 0 && !btnLong3sFired[i]) {
                    uint32_t held = now - btnPressStartMs[i];
                    if (held < SHORT_PRESS_MAX_MS) {
                        onShortPress(i);
                    }
                }
                btnPressStartMs[i] = 0;
            }
            lastBtnState[i] = cur;
        } else {
            // STEP 02: 按住中 — 檢查長按 3s 觸發（fire on threshold）
            if (cur == LOW && btnPressStartMs[i] > 0 && !btnLong3sFired[i]) {
                if (now - btnPressStartMs[i] >= LONG_3S_PRESS_MS) {
                    btnLong3sFired[i] = true;
                    onLong3sPress(i);
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
                        ohcaLastEpiMs     = 0;
                        ohcaPrevSinceMs   = 0;
                        alarmMuted        = false;
                        Serial.println("[OHCA] Case start (START_FLASH)");
                        break;
                    case 1: globalState = GLOBAL_VENT_PLACEHOLDER;     break;
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

    // ===== Phase X 佔位畫面：返回鍵回主功能表 =====
    if (globalState >= GLOBAL_VENT_PLACEHOLDER && globalState <= GLOBAL_SETTINGS_PLACEHOLDER) {
        if (btnIdx == BTN_BACK) {
            enterMainMenu();
        }
        return;
    }

    // ===== OHCA 模式 =====
    if (globalState != GLOBAL_OHCA) return;

    uint32_t now = millis();

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
            } else if (ohcaState == OHCA_STATE_SUMMARY && summaryScrollOffset > 0) {
                summaryScrollOffset--;
            }
            break;

        case BTN_DOWN:
            if (ohcaState == OHCA_STATE_END_CHECK) {
                endCheckCursor = (endCheckCursor == END_CHECK_CURSOR_CANCEL)
                               ? END_CHECK_CURSOR_CONFIRM : END_CHECK_CURSOR_CANCEL;
            } else if (ohcaState == OHCA_STATE_SUMMARY && summaryScrollOffset + 4 < eventCount) {
                summaryScrollOffset++;
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
                recordEvent(1);
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
                recordEvent(2);
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

void onLong3sPress(uint8_t btnIdx) {
    Serial.printf("[BTN] long3s %u (state=%u/%u)\n", btnIdx, globalState, ohcaState);

    if (btnIdx != BTN_PRIMARY) return;
    if (globalState != GLOBAL_OHCA) return;
    if (ohcaState != OHCA_STATE_OVERTIME) return;

    dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_LONG_3S, 0);
    endCheckCursor = END_CHECK_CURSOR_CANCEL;  // 預設停在「返回案件」避免誤確認
    stopBeep();
    Serial.println("[OHCA] enter END_CHECK");
}

// ============================================================
// OHCA event dispatcher（包裝 nextOhcaState）
// ============================================================

void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms) {
    ohca_state_t prev = ohcaState;
    ohcaState = nextOhcaState(prev, event, since_ms);
    if (ohcaState != prev) {
        Serial.printf("[OHCA] %u -> %u (evt=%u)\n", prev, ohcaState, event);
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

void recordEvent(uint8_t type) {
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[EVT] full, drop");
        return;
    }
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (millis() - caseStartMs);
    events[eventCount].type       = type;
    events[eventCount].elapsed_ms = elapsed;
    eventCount++;
}

// ============================================================
// 退出 OHCA case（從 SUMMARY 返回主功能表）
// ============================================================

void enterMainMenu() {
    globalState = GLOBAL_MAIN_MENU;
}

void exitOhcaCase() {
    enterMainMenu();
    ohcaState           = OHCA_STATE_MAIN_MENU;
    ohcaLastEpiMs       = 0;
    ohcaPrevSinceMs     = 0;
    eventCount          = 0;
    summaryScrollOffset = 0;
    alarmMuted          = false;
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

void updateDisplay() {
    display.clearDisplay();

    if (globalState == GLOBAL_MAIN_MENU) {
        drawMainMenu();
    } else if (globalState == GLOBAL_OHCA) {
        switch (ohcaState) {
            case OHCA_STATE_START_FLASH:
                drawOhcaStartFlash();
                break;
            case OHCA_STATE_WAIT_FIRST_EPI:
                drawOhcaWaitFirstEpi();
                break;
            case OHCA_STATE_COUNTDOWN: {
                uint32_t now = millis();
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
                drawOhcaCountdownCommon("EPI Countdown", remain, false);
                break;
            }
            case OHCA_STATE_WARNING: {
                uint32_t now = millis();
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
                drawOhcaCountdownCommon("Prepare EPI", remain, false);
                break;
            }
            case OHCA_STATE_ALARMING: {
                uint32_t now = millis();
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                uint32_t past = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;
                drawOhcaCountdownCommon("GIVE EPI!", past, true);
                break;
            }
            case OHCA_STATE_OVERTIME: {
                uint32_t now = millis();
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                uint32_t past = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;
                drawOhcaCountdownCommon("OVERTIME", past, true);
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

        // overlay：兩段確認 armed 提示
        if (showEpiArmedPrompt) drawTwoStepArmedOverlay("EPI? press again");
        else if (showShockArmedPrompt) drawTwoStepArmedOverlay("Shock? press again");

    } else if (globalState == GLOBAL_VENT_PLACEHOLDER) {
        drawPlaceholder("6sec Vent", "Phase C");
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
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("EMS DoseSync Pro");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        int y = 14 + i * 10;
        if (i == mainMenuCursor) {
            display.fillRect(0, y - 1, OLED_WIDTH, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, y);
        display.print(MAIN_MENU_LABELS[i]);
    }
}

void drawOhcaStartFlash() {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 24);
    display.println("Start OHCA");
}

void drawOhcaWaitFirstEpi() {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("OHCA Case");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 24);
    display.println("Press EPI");
    display.setTextSize(1);
    display.setCursor(8, 50);
    display.println("(2-step confirm)");
}

void drawOhcaCountdownCommon(const char* label, uint32_t time_ms, bool show_overtime) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(label);
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // 時間 mm:ss 大字顯示
    uint32_t total_sec = time_ms / 1000;
    uint32_t mm = total_sec / 60;
    uint32_t ss = total_sec % 60;

    display.setTextSize(3);
    display.setCursor(12, 22);
    if (mm < 10) display.print("0");
    display.print(mm);
    display.print(":");
    if (ss < 10) display.print("0");
    display.print(ss);

    // 事件數提示
    display.setTextSize(1);
    display.setCursor(0, 54);
    display.print("EPI:");
    uint8_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if (events[i].type == 1) epiN++;
        else if (events[i].type == 2) shockN++;
    }
    display.print(epiN);
    display.print("  Shk:");
    display.print(shockN);
    if (show_overtime) {
        display.setCursor(96, 54);
        display.print("OVT");
    }
}

void drawOhcaEndCheck() {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("End Check");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);
    display.setCursor(4, 18);
    display.println("Confirm end of case?");

    // 兩個選項
    int y_confirm = 36, y_cancel = 50;
    if (endCheckCursor == END_CHECK_CURSOR_CONFIRM) {
        display.fillRect(0, y_confirm - 1, OLED_WIDTH, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(4, y_confirm);
    display.print("> Confirm end");

    display.setTextColor(SSD1306_WHITE);
    if (endCheckCursor == END_CHECK_CURSOR_CANCEL) {
        display.fillRect(0, y_cancel - 1, OLED_WIDTH, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(4, y_cancel);
    display.print("> Back to case");
}

void drawOhcaLocked() {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("LOCKED");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);
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
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Summary  ");
    display.print(eventCount);
    display.println(" evts");
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // 顯示最多 4 筆，從 summaryScrollOffset 起
    uint16_t shown = 0;
    for (uint16_t i = summaryScrollOffset; i < eventCount && shown < 4; i++, shown++) {
        int y = 14 + shown * 10;
        display.setCursor(0, y);
        display.print((i + 1));
        display.print(". ");
        display.print(events[i].type == 1 ? "EPI" : (events[i].type == 2 ? "Shk" : "?"));
        display.print(" @");
        uint32_t sec = events[i].elapsed_ms / 1000;
        if (sec / 60 < 10) display.print("0");
        display.print(sec / 60);
        display.print(":");
        if (sec % 60 < 10) display.print("0");
        display.print(sec % 60);
    }

    display.setCursor(0, 56);
    display.print("[Back] -> Menu");
}

void drawTwoStepArmedOverlay(const char* what) {
    // 在底部畫一條反色提示條
    display.fillRect(0, OLED_HEIGHT - 12, OLED_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(2, OLED_HEIGHT - 10);
    display.print(what);
}

void drawPlaceholder(const char* title, const char* phase) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(title);
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 22);
    display.print(phase);
    display.setTextSize(1);
    display.setCursor(0, 44);
    display.println("Not implemented");
    display.setCursor(0, 56);
    display.println("[Back] -> Menu");
}
