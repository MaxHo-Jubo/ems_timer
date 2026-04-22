/**
 * EMS Timer — Phase 2 韌體（2A~2E + 3A）
 *
 * 2026-04-22 升級：
 *   2A: 5 鍵重配置（主鍵/左上下/右電源/左上角錄音 noop）
 *   2B: 狀態機 IDLE / RUNNING / PAUSE / END
 *   2C: EmsEvent struct 擴充（event_id/source/mode/sync_flag/extra_data，MAX_EVENTS=100）
 *   2D: 4 種模式選單 + 給藥模式 240 秒倒數提醒
 *   2E: BLE JSON 格式更新（含所有新欄位）
 *   3A: LittleFS 持久儲存（任務結束寫 /sessions/<task_id>.json）
 *
 * 接線（ESP32-S3 GOOUUU 開發板）：
 *   BTN_PRIMARY（紅色大鍵）→ GPIO 4
 *   BTN_UP（左側上鍵）      → GPIO 5
 *   BTN_DOWN（左側下鍵）    → GPIO 6
 *   BTN_POWER（右側電源鍵） → GPIO 7
 *   BTN_RECORD（左上角鍵）  → GPIO 15
 *   蜂鳴器                  → GPIO 14
 *   OLED SDA/SCL            → GPIO 42/41
 *   INMP441 SCK/WS/SD       → GPIO 40/39/38（Phase 1.5，disabled）
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_sleep.h>

// 純邏輯函式（抽出至 lib/ems_logic 供 native 測試覆蓋）
#include "ems_time.h"

/** Phase 1.5：INMP441 麥克風，換新模組後改 1 */
#define ENABLE_MIC_MONITOR 0

#if ENABLE_MIC_MONITOR
#include <driver/i2s.h>
#endif

/** Phase 2D：震動馬達（1027 硬幣馬達），硬體到貨後改 1，確認腳位後設定 VIBRATION_PIN */
#define ENABLE_VIBRATION 0

// ============================================================
// 硬體常數
// ============================================================

/** OLED 解析度 */
static const uint8_t OLED_WIDTH     = 128;
static const uint8_t OLED_HEIGHT    = 64;

/** SSD1306 不需硬體 reset 設 -1 */
static const int8_t  OLED_RESET_PIN = -1;

/** SSD1306 預設 I2C 位址 */
static const uint8_t OLED_I2C_ADDR  = 0x3C;

/** I2C 腳位（S3 右側） */
static const uint8_t I2C_SDA_PIN    = 42;
static const uint8_t I2C_SCL_PIN    = 41;

/** 蜂鳴器 GPIO */
static const uint8_t BUZZER_PIN     = 14;

#if ENABLE_VIBRATION
/** 震動馬達 GPIO（S8050 NPN 基極，透過 1kΩ 接 GPIO） */
static const uint8_t  VIBRATION_PIN = 16;
/** 單次震動持續時間（ms） */
static const uint16_t VIBRATION_MS  = 500;
#endif

#if ENABLE_MIC_MONITOR
static const i2s_port_t I2S_PORT    = I2S_NUM_0;
static const uint8_t    I2S_WS_PIN  = 39;
static const uint8_t    I2S_SCK_PIN = 40;
static const uint8_t    I2S_SD_PIN  = 38;
#endif

// ============================================================
// 按鈕配置（5 鍵，2A）
// ============================================================

/** 主動按鈕數量（右 BTN6-8 GPIO 16/17/18 停用） */
static const uint8_t BTN_COUNT = 5;

/** 各按鈕 GPIO（INPUT_PULLUP，按下接 GND） */
static const uint8_t BTN_PINS[BTN_COUNT] = { 4, 5, 6, 7, 15 };

/** 按鈕索引語意 */
static const uint8_t BTN_PRIMARY = 0;  // 正下方紅色大鍵：短按記錄、長按任務控制
static const uint8_t BTN_UP      = 1;  // 左側上鍵：模式選單上一項
static const uint8_t BTN_DOWN    = 2;  // 左側下鍵：模式選單下一項
static const uint8_t BTN_POWER   = 3;  // 右側電源鍵：短按喚醒、長按關機（noop 佔位）
static const uint8_t BTN_RECORD  = 4;  // 左上角錄音鍵：noop 佔位（INMP441 到貨後啟用）

/** Debounce 時間（ms） */
static const uint16_t DEBOUNCE_MS = 80;

/** 短按上限：< 1500ms = 短按 */
static const uint16_t SHORT_PRESS_MAX_MS = 1500;

/** 長按下限：≥ 2000ms = 長按（在持續按住期間觸發，不等放開） */
static const uint16_t LONG_PRESS_MIN_MS  = 2000;

// ============================================================
// 狀態機（2B）
// ============================================================

enum DeviceState : uint8_t {
    STATE_IDLE    = 0,  // 待機：等待長按主鍵啟動任務
    STATE_RUNNING = 1,  // 執行中：計時 + 事件記錄
    STATE_PAUSE   = 2,  // 暫停：計時保留，等待繼續或結束
    STATE_END     = 3,  // 結束：儲存後自動回 IDLE
};

// ============================================================
// 操作模式（2D）
// ============================================================

enum OperationMode : uint8_t {
    MODE_MED      = 0,  // 給藥模式（預設）：240s 倒數提醒
    MODE_VENT     = 1,  // 通氣模式：記錄通氣事件
    MODE_CUSTOM   = 2,  // 自訂模式（Phase 3 設定功能）
    MODE_SETTINGS = 3,  // 設定模式（Phase 3 實作）
};

static const uint8_t MODE_COUNT = 4;

/** 模式顯示標籤 */
static const char* MODE_LABELS[MODE_COUNT] = { "MED", "VENT", "CUST", "SET" };

// ============================================================
// 事件資料結構（2C）
// ============================================================

/** 單次出勤可記錄的事件上限（從 30 擴充到 100，100 × ~36B = 3.6KB，S3 512KB SRAM 無壓力） */
static const uint16_t MAX_EVENTS = 100;

/** 事件類型 */
enum EventType : uint8_t {
    EVT_MEDICATION   = 0,  // 給藥（MODE_MED 主鍵短按）
    EVT_VENTILATION  = 1,  // 通氣（MODE_VENT 主鍵短按）
    EVT_CUSTOM       = 2,  // 自訂（其他模式主鍵短按）
    EVT_TASK_START   = 3,  // 任務開始（系統自動）
    EVT_TASK_PAUSE   = 4,  // 任務暫停（系統自動）
    EVT_TASK_RESUME  = 5,  // 任務繼續（系統自動）
    EVT_TASK_END     = 6,  // 任務結束（系統自動）
};

/** 事件來源 */
static const uint8_t SRC_BUTTON = 0;  // 使用者按鍵
static const uint8_t SRC_SYSTEM = 1;  // 系統自動

/**
 * 擴充後的事件紀錄（2C 規格）
 * 記憶體：36 bytes × 100 = 3.6KB
 */
struct EmsEvent {
    uint32_t event_id;        // 流水號，任務內從 1 開始自增
    uint64_t timestamp;       // epoch ms（絕對時間，需 BLE sync 校準）
    uint32_t elapsed_ms;      // 從任務開始的毫秒數（已扣除暫停時間）
    uint8_t  event_type;      // 事件類型（EventType）
    uint8_t  source;          // 來源（SRC_BUTTON / SRC_SYSTEM）
    uint8_t  mode;            // 當下操作模式（OperationMode）
    uint8_t  sync_flag;       // 0=未同步, 1=BLE 已同步
    char     extra_data[16];  // 備註（null-terminated string，縮短為 16 bytes 省記憶體）
};

// ============================================================
// BLE NUS 常數
// ============================================================

/** Nordic UART Service UUID */
static const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

/** 廣播名稱 */
static const char* BLE_DEVICE_NAME  = "EMS Timer";

/** 單次 Notify 最大 bytes */
static const size_t BLE_TX_BUF_SIZE = 256;

/** dump 逐筆間隔（ms，避免 BLE stack 壅塞） */
static const uint16_t DUMP_ITEM_INTERVAL_MS = 10;

// ============================================================
// 提醒常數
// ============================================================

/** 給藥模式倒數時長（ms） */
static const uint32_t MED_COUNTDOWN_MS = 240UL * 1000;

/** 倒數到時後，提醒重複間隔（ms） */
static const uint32_t MED_REMINDER_REPEAT_MS = 30UL * 1000;

/** OLED 閃爍週期（ms） */
static const uint32_t FLASH_PERIOD_MS = 500;

/** OLED 整螢幕反色閃爍時長（ms，取代震動作為強提醒視覺回饋） */
static const uint16_t OLED_INVERT_FLASH_MS = 200;

/** END 狀態顯示時長：任務結束畫面停留多久後自動回 IDLE（ms） */
static const uint32_t END_DISPLAY_MS = 2000;

/** OLED 節流更新間隔（ms） */
static const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 250;

/** 倒數到時：嗶 3 聲 × 200ms on / 200ms off */
static const uint8_t  EXPIRE_BEEP_PULSES = 3;
static const uint16_t EXPIRE_BEEP_ON_MS  = 200;
static const uint16_t EXPIRE_BEEP_OFF_MS = 200;

/** 短確認音：嗶 1 聲 × 80ms */
static const uint8_t  SHORT_BEEP_PULSES = 1;
static const uint16_t SHORT_BEEP_ON_MS  = 80;

/** 倒數剩餘 1 分鐘中途警示（ms） */
static const uint32_t MED_1MIN_WARNING_MS = 60UL * 1000;

/** 1 分鐘警示：2 短嗶，比區間提醒強一點 */
static const uint8_t  MIN1_BEEP_PULSES = 2;
static const uint16_t MIN1_BEEP_ON_MS  = 150;
static const uint16_t MIN1_BEEP_OFF_MS = 100;

// --- 單純給藥（獨立時間戳）藥物選單 ---
/** 可記錄的藥物列表（extra_data 欄位使用，不影響 4 分鐘倒數） */
static const char* const DRUG_NAMES[] = {
    "Amiodarone", "TXA", "D50", "Atropine", "Adenosine", "Naloxone"
};
static const uint8_t DRUG_COUNT = 6;  // 同步 DRUG_NAMES 長度

/** 藥物選單無操作自動關閉（ms） */
static const uint32_t DRUG_MENU_TIMEOUT_MS = 8000;

// ============================================================
// 全域物件與狀態
// ============================================================

/** SSD1306 OLED 顯示器物件 */
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

// --- 按鈕狀態 ---
static uint8_t  lastBtnState[BTN_COUNT];
static uint32_t lastPressMs[BTN_COUNT]     = { 0 };  // 用於 debounce 的上次有效按下時間
static uint32_t btnPressStartMs[BTN_COUNT] = { 0 };  // 0 = 未按；> 0 = 按下時刻
static bool     btnLongFired[BTN_COUNT]    = { false };  // 本次按下是否已觸發長按

// --- 裝置狀態機 ---
static DeviceState   deviceState = STATE_IDLE;
static OperationMode currentMode = MODE_MED;

// --- 任務計時 ---
static uint32_t taskStartMs      = 0;   // 任務開始時的 millis()
static uint64_t taskId           = 0;   // 任務 ID（epoch ms，BLE sync 後準確）
static uint64_t sessionEpochOffset = 0; // epoch ms offset（BLE sync 校準）

// --- 暫停計時 ---
static uint32_t pauseStartMs  = 0;   // 當前暫停段的起點 millis()
static uint32_t totalPausedMs = 0;   // 本次任務累計暫停毫秒

// --- 事件陣列 ---
static EmsEvent events[MAX_EVENTS];
static uint16_t eventCount  = 0;
static uint32_t nextEventId = 1;  // 任務內流水號，從 1 開始

// --- 給藥倒數（MODE_MED）---
static uint32_t medCountdownStartMs       = 0;      // 倒數起點 millis()（0 = 尚未啟動）
static bool     medReminderActive         = false;  // 倒數到時後的強提醒狀態
static uint32_t lastReminderBeepMs        = 0;      // 上次重複提醒的 millis()
static bool     medOneMinWarningTriggered = false;  // 剩餘 1 分鐘中途警示是否已觸發

// --- 單純給藥選單（藥物獨立時間戳）---
static bool     drugMenuOpen   = false;  // 藥物子選單是否開啟
static uint8_t  drugMenuCursor = 0;      // 目前游標（DRUG_NAMES 索引）
static uint32_t drugMenuOpenMs = 0;      // 選單開啟時間（超時判斷）

// --- 蜂鳴器非 blocking SM ---
static uint8_t  beepPulsesRemaining = 0;
static uint32_t beepNextToggleMs    = 0;
static bool     beepActive          = false;
static uint16_t beepOnMs            = 0;
static uint16_t beepOffMs           = 0;

// --- OLED 整螢幕反色閃爍 SM（取代震動的強提醒視覺效果） ---
// 使用 start + duration 而非 end time 避免 millis() 接近 UINT32_MAX 時的 overflow
static bool     oledInverted        = false;  // 目前是否處於反色狀態
static uint32_t oledInvertStartMs   = 0;      // 反色起點（每次觸發更新）
static uint16_t oledInvertDurationMs = 0;     // 反色持續時長（ms）

// --- OLED 節流 ---
static uint32_t lastDisplayUpdateMs = 0;

// --- BLE ---
static BLEServer*         bleServer      = nullptr;
static BLECharacteristic* bleTxChar      = nullptr;
static BLECharacteristic* bleRxChar      = nullptr;
static bool               bleConnected   = false;
static bool               bleWasConnected = false;

/**
 * BLE RX pending 佇列（single slot）
 * onWrite callback 跑在 BLE GATT task，在 callback 內呼叫 notify / delay 會導致斷線。
 * 只 copy 進 buffer + 設 flag，main loop 再處理。
 */
static const size_t PENDING_CMD_BUF_SIZE = 256;
static char          pendingCmdBuf[PENDING_CMD_BUF_SIZE];
static size_t        pendingCmdLen   = 0;
static volatile bool pendingCmdReady = false;

/** LittleFS 可用旗標（mount 失敗時為 false，saveSession 跳過） */
static bool fsAvailable = false;

#if ENABLE_MIC_MONITOR
static int32_t  i2sBuffer[256];
static uint32_t lastMicPrintMs = 0;
#endif

// ============================================================
// 函式宣告
// ============================================================

void handleButtons();
void checkLongPresses();
void onShortPress(uint8_t btnIdx);
void onLongPress(uint8_t btnIdx);
void switchMode(int8_t delta);
void transitionState(DeviceState newState);
void recordEvent(uint8_t eventType, uint8_t source, const char* extra = "");
uint32_t getTaskElapsedMs();

void startMedCountdown();
void updateMedCountdown();

void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void updateBeepMachine();

void triggerOledFlash(uint16_t durationMs);
void updateOledFlashMachine();

#if ENABLE_VIBRATION
void triggerVibration();
#endif

void updateDisplay();
void drawIdleScreen();
void drawRunningScreen();
void drawPauseScreen();
void drawEndScreen();
void drawDrugMenuScreen();

void setupBLE();
void sendJson(JsonDocument& doc);
void sendHello();
void sendDump();
void handleRxCommand(const std::string& msg);
void updateBleAdvertising();

void setupLittleFS();
void saveSession();

#if ENABLE_MIC_MONITOR
void setupI2S();
#endif

// ============================================================
// BLE Callbacks
// ============================================================

/**
 * BLE 伺服器連線/斷線 callback
 */
class EmsServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        bleConnected = true;
        Serial.println("[BLE] client connected");
    }
    void onDisconnect(BLEServer* server) override {
        bleConnected = false;
        Serial.println("[BLE] client disconnected");
    }
};

/**
 * BLE RX characteristic Write callback：App 下發命令時觸發
 */
class EmsRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        // STEP 01: 取出命令，忽略空訊息或 buffer 尚未消化完的情形
        std::string value = c->getValue();
        if (value.empty() || pendingCmdReady) {
            return;
        }
        // STEP 02: 複製到 pending buffer（禁止在此 thread 呼叫 notify / delay）
        size_t len = value.length();
        if (len >= PENDING_CMD_BUF_SIZE) {
            len = PENDING_CMD_BUF_SIZE - 1;
        }
        memcpy(pendingCmdBuf, value.data(), len);
        pendingCmdBuf[len] = '\0';
        pendingCmdLen      = len;
        pendingCmdReady    = true;
    }
};

// ============================================================
// setup
// ============================================================

/**
 * 硬體初始化：GPIO、OLED、BLE、LittleFS
 */
void setup() {
    // STEP 01: 序列埠（等待 USB CDC 重枚舉）
    Serial.begin(115200);
    delay(3000);
    Serial.println("[EMS Timer] Phase 2 (2A~2E+3A) boot");

    // STEP 02: 蜂鳴器 GPIO
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

#if ENABLE_VIBRATION
    // STEP 02.01: 震動馬達 GPIO
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);
#endif

    // STEP 03: 按鈕初始化（INPUT_PULLUP，讀取開機初值避免誤觸）
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        lastBtnState[i] = digitalRead(BTN_PINS[i]);
    }

    // STEP 04: I2C bus
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // STEP 05: OLED 初始化
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[ERROR] OLED init failed. Check SDA=42, SCL=41.");
        while (true) {
            delay(1000);
        }
    }

    // STEP 06: 開機嗶 2 聲確認蜂鳴器可用
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(150);
        digitalWrite(BUZZER_PIN, LOW);
        delay(150);
    }

    // STEP 07: LittleFS（掛載 + 掃描未同步 session）
    setupLittleFS();

    // STEP 08: 顯示待機畫面
    drawIdleScreen();

    // STEP 09: BLE NUS 初始化 + 廣播
    setupBLE();

#if ENABLE_MIC_MONITOR
    // STEP 10: INMP441 I2S 麥克風（Phase 1.5）
    setupI2S();
#endif

    Serial.println("[EMS Timer] Ready. Long-press main button to start task.");
}

// ============================================================
// loop
// ============================================================

/**
 * 主迴圈：按鈕 → 長按檢查 → 給藥倒數 → 蜂鳴器 SM → BLE → OLED
 */
void loop() {
    // STEP 01: 偵測按鈕事件（短按/長按）
    handleButtons();
    checkLongPresses();

    // STEP 02: 給藥倒數（MODE_MED + STATE_RUNNING 才有效）
    updateMedCountdown();

    // STEP 02.01: 藥物選單超時自動關閉
    if (drugMenuOpen && (millis() - drugMenuOpenMs >= DRUG_MENU_TIMEOUT_MS)) {
        drugMenuOpen = false;
        Serial.println("[DRUG] menu timeout");
    }

    // STEP 03: 蜂鳴器非 blocking SM
    updateBeepMachine();

    // STEP 03.01: OLED 反色閃爍非 blocking SM
    updateOledFlashMachine();

    // STEP 04: BLE 廣播生命週期管理
    updateBleAdvertising();

    // STEP 05: 處理 App 下發的 pending 命令
    if (pendingCmdReady) {
        std::string cmd(pendingCmdBuf, pendingCmdLen);
        pendingCmdReady = false;
        handleRxCommand(cmd);
    }

    // STEP 06: END 狀態滿 2 秒後自動回 IDLE
    //   切換瞬間清除所有按鍵的持續按住狀態，避免 END→IDLE 邊界被誤觸長按
    //   即使使用者還按著 toggle，btnLongFired 會擋住新長按 fire，等放開後重置
    static uint32_t endEnteredMs = 0;
    if (deviceState == STATE_END) {
        if (endEnteredMs == 0) {
            endEnteredMs = millis();
        } else if (millis() - endEnteredMs >= END_DISPLAY_MS) {
            // STEP 06.01: 清除所有按鍵持續狀態（btnLongFired=true 等放開重置）
            for (uint8_t i = 0; i < BTN_COUNT; i++) {
                btnPressStartMs[i] = 0;
                btnLongFired[i]    = true;
            }
            endEnteredMs = 0;
            transitionState(STATE_IDLE);
        }
    } else {
        endEnteredMs = 0;
    }

    // STEP 07: 節流更新 OLED（每 250ms）
    uint32_t now = millis();
    if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
        lastDisplayUpdateMs = now;
        updateDisplay();
    }

#if ENABLE_MIC_MONITOR
    // STEP 08: Phase 1.5 麥克風峰值監控
    if (now - lastMicPrintMs >= 250) {
        lastMicPrintMs = now;
        size_t bytesRead = 0;
        i2s_read(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);
        int32_t peak = 0;
        uint16_t samplesRead = bytesRead / sizeof(int32_t);
        for (uint16_t j = 0; j < samplesRead; j++) {
            int32_t s = i2sBuffer[j] >> 8;
            if (s < 0) { s = -s; }
            if (s > peak) { peak = s; }
        }
        Serial.print("[MIC] peak=");
        Serial.println(peak);
    }
#endif
}

// ============================================================
// 按鈕處理（2A）
// ============================================================

/**
 * 輪詢 5 顆按鈕：偵測下降緣（記錄起點）和上升緣（判斷短按）
 */
void handleButtons() {
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        uint8_t  currentState = digitalRead(BTN_PINS[i]);
        uint32_t now          = millis();

        // STEP 01: 下降緣（HIGH → LOW，按下）— debounce 過濾
        if (lastBtnState[i] == HIGH && currentState == LOW) {
            if (now - lastPressMs[i] >= DEBOUNCE_MS) {
                lastPressMs[i]     = now;
                btnPressStartMs[i] = now;   // 記錄按下時刻
                btnLongFired[i]    = false;
                Serial.printf("[BTN %u] PRESS @ %ums\n", i, now);
            } else {
                Serial.printf("[BTN %u] debounce reject (%ums since last)\n",
                              i, (uint32_t)(now - lastPressMs[i]));
            }
        }

        // STEP 02: 上升緣（LOW → HIGH，放開）— 判斷是否為短按
        if (lastBtnState[i] == LOW && currentState == HIGH) {
            if (btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                uint32_t held = now - btnPressStartMs[i];
                if (held < SHORT_PRESS_MAX_MS) {
                    // END 狀態忽略短按語意，但仍印 log 方便 debug
                    if (deviceState == STATE_END) {
                        Serial.printf("[BTN %u] RELEASE held=%ums -> SHORT (END ignored)\n",
                                      i, held);
                    } else {
                        Serial.printf("[BTN %u] RELEASE held=%ums -> SHORT\n", i, held);
                        onShortPress(i);
                    }
                } else {
                    // 1500ms ~ 2000ms 灰色地帶：忽略
                    Serial.printf("[BTN %u] RELEASE held=%ums -> GRAY (ignored)\n",
                                  i, held);
                }
            } else if (btnLongFired[i]) {
                Serial.printf("[BTN %u] RELEASE (long already fired)\n", i);
            }
            // STEP 02.01: 重置按下狀態
            btnPressStartMs[i] = 0;
            btnLongFired[i]    = false;
        }

        lastBtnState[i] = currentState;
    }
}

/**
 * 主迴圈每次呼叫，確認是否有按鈕持續按住超過長按門檻
 */
void checkLongPresses() {
    // STEP 01: END 狀態期間忽略所有長按觸發（等待使用者放開 + 自動回 IDLE）
    if (deviceState == STATE_END) {
        return;
    }
    uint32_t now = millis();
    // STEP 02: 逐顆按鍵檢查是否到長按門檻
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 02.01: 已觸發或尚未按下 → 跳過
        if (btnPressStartMs[i] == 0 || btnLongFired[i]) {
            continue;
        }
        // STEP 02.02: 到達長按門檻 → 觸發一次
        if (now - btnPressStartMs[i] >= LONG_PRESS_MIN_MS) {
            btnLongFired[i] = true;
            Serial.printf("[BTN %u] LONG fired (held=%ums)\n",
                          i, (uint32_t)(now - btnPressStartMs[i]));
            onLongPress(i);
        }
    }
}

// ============================================================
// 按鍵語意（2A + 2B + 2D）
// ============================================================

/**
 * 切換操作模式 helper（消除 BTN_UP / BTN_DOWN 邏輯重複）
 * 含 MED countdown 進出場管理：切離 MED 清倒數狀態，切入 MED + RUNNING 重啟倒數
 * @param delta 方向：-1 = UP（反向循環），+1 = DOWN（正向循環）
 */
void switchMode(int8_t delta) {
    // STEP 01: 保存舊模式、計算新模式（+ MODE_COUNT 避免負數取模）
    OperationMode oldMode = currentMode;
    currentMode = (OperationMode)(((int)currentMode + MODE_COUNT + delta) % MODE_COUNT);

    // STEP 02: 回饋
    Serial.print("[MODE] → ");
    Serial.println(MODE_LABELS[currentMode]);
    triggerBeep(SHORT_BEEP_PULSES, 60, 0);

    // STEP 03: 切離 MED → 清除倒數狀態
    if (oldMode == MODE_MED && currentMode != MODE_MED) {
        medCountdownStartMs       = 0;
        medReminderActive         = false;
        medOneMinWarningTriggered = false;
    }
    // STEP 04: 切入 MED 且任務進行中 → 重啟倒數
    if (currentMode == MODE_MED && oldMode != MODE_MED &&
        deviceState == STATE_RUNNING) {
        startMedCountdown();
    }
}

/**
 * 短按事件處理（< 1500ms，放開時觸發）
 * @param btnIdx 按鈕索引（BTN_PRIMARY 等）
 */
void onShortPress(uint8_t btnIdx) {
    switch (btnIdx) {
        case BTN_PRIMARY:
            // STEP 01: 主鍵短按語意依狀態分派
            if (deviceState == STATE_RUNNING) {
                if (currentMode == MODE_MED) {
                    if (medReminderActive) {
                        // STEP 01.01: 強提醒中 → 給藥確認（EPINEPHRINE），重置倒數
                        medReminderActive = false;
                        recordEvent(EVT_MEDICATION, SRC_BUTTON, "epi");
                        startMedCountdown();
                        triggerBeep(SHORT_BEEP_PULSES, SHORT_BEEP_ON_MS, 0);
                    } else if (drugMenuOpen) {
                        // STEP 01.02: 藥物選單開啟中 → 確認記錄所選藥物（獨立時間戳，不重置倒數）
                        recordEvent(EVT_MEDICATION, SRC_BUTTON, DRUG_NAMES[drugMenuCursor]);
                        drugMenuOpen = false;
                        triggerBeep(SHORT_BEEP_PULSES, SHORT_BEEP_ON_MS, 0);
                        Serial.print("[DRUG] recorded: ");
                        Serial.println(DRUG_NAMES[drugMenuCursor]);
                    } else {
                        // STEP 01.03: 無提醒、無選單 → 開啟藥物子選單
                        drugMenuOpen   = true;
                        drugMenuCursor = 0;
                        drugMenuOpenMs = millis();
                        triggerBeep(SHORT_BEEP_PULSES, 60, 0);
                        Serial.println("[DRUG] menu opened");
                    }
                } else if (currentMode == MODE_VENT) {
                    // STEP 01.04: 通氣模式 → 記錄通氣事件
                    recordEvent(EVT_VENTILATION, SRC_BUTTON);
                    triggerBeep(SHORT_BEEP_PULSES, SHORT_BEEP_ON_MS, 0);
                } else {
                    // STEP 01.05: 其他模式 → 記錄自訂事件
                    recordEvent(EVT_CUSTOM, SRC_BUTTON);
                    triggerBeep(SHORT_BEEP_PULSES, SHORT_BEEP_ON_MS, 0);
                }
            } else if (deviceState == STATE_PAUSE) {
                // STEP 01.06: 暫停中短按 → 繼續任務
                transitionState(STATE_RUNNING);
            }
            // IDLE / END 狀態短按 noop
            break;

        case BTN_UP:
            // STEP 02: 上鍵 — 藥物選單開啟時導覽，否則切換模式
            if (drugMenuOpen) {
                // STEP 02.01: 藥物選單上一項
                drugMenuCursor = (drugMenuCursor + DRUG_COUNT - 1) % DRUG_COUNT;
                drugMenuOpenMs = millis();  // 重置超時
            } else if (deviceState == STATE_IDLE || deviceState == STATE_RUNNING) {
                // STEP 02.02: 模式切換 -1（MED → SET → CUST → VENT → MED 反向）
                switchMode(-1);
            }
            break;

        case BTN_DOWN:
            // STEP 03: 下鍵 — 藥物選單開啟時導覽，否則切換模式
            if (drugMenuOpen) {
                // STEP 03.01: 藥物選單下一項
                drugMenuCursor = (drugMenuCursor + 1) % DRUG_COUNT;
                drugMenuOpenMs = millis();
            } else if (deviceState == STATE_IDLE || deviceState == STATE_RUNNING) {
                // STEP 03.02: 模式切換 +1（MED → VENT → CUST → SET → MED 正向）
                switchMode(+1);
            }
            break;

        case BTN_POWER:
            // STEP 04: 電源鍵短按 → 螢幕喚醒（Phase 3 補強）
            Serial.println("[POWER] short press - screen wake (noop)");
            break;

        case BTN_RECORD:
            // STEP 05: 錄音鍵短按 → noop 佔位
            Serial.println("[RECORD] short press - noop (INMP441 pending)");
            break;

        default:
            break;
    }
}

/**
 * 長按事件處理（≥ 2000ms，持續按住時觸發）
 * @param btnIdx 按鈕索引
 */
void onLongPress(uint8_t btnIdx) {
    switch (btnIdx) {
        case BTN_PRIMARY:
            // STEP 01: 主鍵長按 → 任務狀態轉換
            if (deviceState == STATE_IDLE) {
                transitionState(STATE_RUNNING);
            } else if (deviceState == STATE_RUNNING) {
                transitionState(STATE_PAUSE);
            } else if (deviceState == STATE_PAUSE) {
                transitionState(STATE_END);
            }
            break;

        case BTN_POWER:
            // STEP 02: 電源鍵長按 → 關機（noop 佔位，避免 toggle switch 誤觸）
            Serial.println("[POWER] long press - shutdown (noop)");
            // esp_deep_sleep_start();  // 確認安全後啟用
            break;

        case BTN_RECORD:
            // STEP 03: 錄音鍵長按 → 開始/停止錄音（noop 佔位）
            Serial.println("[RECORD] long press - record toggle (noop)");
            break;

        default:
            break;
    }
}

// ============================================================
// 狀態機（2B）
// ============================================================

/**
 * 執行狀態轉換：記錄任務事件、更新計時、儲存資料
 * @param newState 目標狀態
 */
void transitionState(DeviceState newState) {
    Serial.print("[STATE] ");
    Serial.print(deviceState);
    Serial.print(" → ");
    Serial.println(newState);

    switch (newState) {
        case STATE_RUNNING:
            if (deviceState == STATE_IDLE) {
                // STEP 01: IDLE → RUNNING：建立新任務
                taskStartMs   = millis();
                totalPausedMs = 0;
                eventCount    = 0;
                nextEventId   = 1;
                taskId        = sessionEpochOffset + (uint64_t)taskStartMs;
                recordEvent(EVT_TASK_START, SRC_SYSTEM);
                // 給藥模式立即啟動 240s 倒數
                if (currentMode == MODE_MED) {
                    startMedCountdown();
                }
                triggerBeep(2, 100, 100);
                Serial.print("[TASK] started id=");
                Serial.println((unsigned long)(taskId / 1000UL));
            } else if (deviceState == STATE_PAUSE) {
                // STEP 02: PAUSE → RUNNING：繼續任務，補正暫停時間
                uint32_t pausedDuration = millis() - pauseStartMs;
                totalPausedMs += pausedDuration;
                // 補正給藥倒數起點（加回暫停時間）
                if (currentMode == MODE_MED && medCountdownStartMs > 0) {
                    medCountdownStartMs += pausedDuration;
                }
                recordEvent(EVT_TASK_RESUME, SRC_SYSTEM);
                triggerBeep(1, 100, 0);
            }
            break;

        case STATE_PAUSE:
            // STEP 03: RUNNING → PAUSE：記錄暫停時刻，關閉藥物選單
            pauseStartMs = millis();
            drugMenuOpen = false;
            recordEvent(EVT_TASK_PAUSE, SRC_SYSTEM);
            triggerBeep(2, 200, 100);
            break;

        case STATE_END:
            // STEP 04: PAUSE → END：結束任務並持久儲存
            totalPausedMs += millis() - pauseStartMs;
            drugMenuOpen        = false;
            recordEvent(EVT_TASK_END, SRC_SYSTEM);
            saveSession();
            medReminderActive   = false;
            medCountdownStartMs = 0;
            triggerBeep(EXPIRE_BEEP_PULSES, EXPIRE_BEEP_ON_MS, EXPIRE_BEEP_OFF_MS);
            Serial.println("[TASK] ended, session saved");
            break;

        case STATE_IDLE:
            // STEP 05: → IDLE：清空任務狀態（由 END 自動觸發）
            eventCount    = 0;
            nextEventId   = 1;
            taskId        = 0;
            taskStartMs   = 0;
            totalPausedMs = 0;
            break;

        default:
            break;
    }

    deviceState = newState;
}

// ============================================================
// 事件記錄（2C）
// ============================================================

/**
 * 記錄一筆事件到 events[] 陣列
 * @param eventType 事件類型（EventType）
 * @param source    來源（SRC_BUTTON / SRC_SYSTEM）
 * @param extra     備註字串（最多 15 字元，超過自動截斷）
 */
void recordEvent(uint8_t eventType, uint8_t source, const char* extra) {
    // STEP 01: 容量上限檢查
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[WARN] events[] full, drop");
        return;
    }

    // STEP 02: 計算時間
    uint32_t now     = millis();
    uint32_t elapsed = (taskStartMs > 0) ? (now - taskStartMs - totalPausedMs) : 0;
    uint64_t ts      = sessionEpochOffset + (uint64_t)now;

    // STEP 03: 填入欄位
    EmsEvent& e = events[eventCount];
    e.event_id  = nextEventId++;
    e.timestamp = ts;
    e.elapsed_ms = elapsed;
    e.event_type = eventType;
    e.source     = source;
    e.mode       = (uint8_t)currentMode;
    e.sync_flag  = 0;
    strncpy(e.extra_data, extra, sizeof(e.extra_data) - 1);
    e.extra_data[sizeof(e.extra_data) - 1] = '\0';
    eventCount++;

    Serial.print("[EVT] id=");
    Serial.print(e.event_id);
    Serial.print(" type=");
    Serial.print(eventType);
    Serial.print(" el=");
    Serial.print(elapsed);
    Serial.println("ms");
}

// ============================================================
// 計時輔助
// ============================================================

/**
 * 取得本次任務的有效已進行時間（已扣除所有暫停）
 * @return elapsed ms；尚未開始任務時回傳 0
 */
uint32_t getTaskElapsedMs() {
    // Thin wrapper：委派給 lib/ems_logic/ems_time.h 的純函式（便於 native 測試覆蓋）
    return ems::computeTaskElapsedMs(
        millis(),
        taskStartMs,
        pauseStartMs,
        totalPausedMs,
        (uint8_t)deviceState
    );
}

// ============================================================
// 給藥倒數（2D）
// ============================================================

/**
 * 啟動或重置 240 秒給藥倒數
 * 主鍵短按確認給藥後呼叫，以及 IDLE→RUNNING 時呼叫
 */
void startMedCountdown() {
    medCountdownStartMs       = millis();
    medReminderActive         = false;
    lastReminderBeepMs        = 0;
    medOneMinWarningTriggered = false;
    Serial.println("[MED] 240s countdown start/reset");
}

/**
 * 每個 loop 呼叫：處理倒數到時與重複提醒
 */
void updateMedCountdown() {
    // STEP 01: 只在 RUNNING + MED 模式 + 已啟動倒數時有效
    if (deviceState != STATE_RUNNING ||
        currentMode  != MODE_MED    ||
        medCountdownStartMs == 0) {
        return;
    }

    uint32_t now     = millis();
    uint32_t elapsed = now - medCountdownStartMs;

    // STEP 02: 剩餘 1 分鐘中途警示（僅觸發一次）
    if (!medReminderActive && !medOneMinWarningTriggered && elapsed > 0) {
        uint32_t remaining = (elapsed >= MED_COUNTDOWN_MS) ? 0 : (MED_COUNTDOWN_MS - elapsed);
        if (remaining <= MED_1MIN_WARNING_MS) {
            medOneMinWarningTriggered = true;
            triggerBeep(MIN1_BEEP_PULSES, MIN1_BEEP_ON_MS, MIN1_BEEP_OFF_MS);
            Serial.println("[MED] 1-min warning");
        }
    }

    // STEP 03: 倒數進行中且尚未到時 → 無動作
    if (elapsed < MED_COUNTDOWN_MS && !medReminderActive) {
        return;
    }

    // STEP 04: 首次到時 → 觸發強提醒
    if (!medReminderActive && elapsed >= MED_COUNTDOWN_MS) {
        medReminderActive  = true;
        lastReminderBeepMs = now;
        recordEvent(EVT_MEDICATION, SRC_SYSTEM, "reminder");
        triggerBeep(EXPIRE_BEEP_PULSES, EXPIRE_BEEP_ON_MS, EXPIRE_BEEP_OFF_MS);
        triggerOledFlash(OLED_INVERT_FLASH_MS);
#if ENABLE_VIBRATION
        triggerVibration();
#endif
        Serial.println("[MED] 240s expired! GIVE MED reminder");
        return;
    }

    // STEP 04: 提醒持續中 → 每 30 秒重複嗶聲
    if (medReminderActive && (now - lastReminderBeepMs >= MED_REMINDER_REPEAT_MS)) {
        lastReminderBeepMs = now;
        triggerBeep(EXPIRE_BEEP_PULSES, EXPIRE_BEEP_ON_MS, EXPIRE_BEEP_OFF_MS);
        triggerOledFlash(OLED_INVERT_FLASH_MS);
#if ENABLE_VIBRATION
        triggerVibration();
#endif
        Serial.println("[MED] reminder repeat");
    }
}

// ============================================================
// 蜂鳴器（非 blocking SM）
// ============================================================

/**
 * 觸發蜂鳴器脈衝序列（不阻塞 loop）
 * @param pulses 脈衝次數
 * @param onMs   on 時長（ms）
 * @param offMs  off 時長（ms）
 */
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
    // STEP 01: 每個脈衝 = on + off 各一步
    beepPulsesRemaining = pulses * 2;
    beepOnMs  = onMs;
    beepOffMs = offMs;
    beepActive        = false;
    beepNextToggleMs  = millis();
}

/**
 * 每個 loop cycle 驅動蜂鳴器 SM 前進一步
 */
void updateBeepMachine() {
    // STEP 01: 無待處理脈衝 → 跳過
    if (beepPulsesRemaining == 0) {
        return;
    }
    // STEP 02: 時間未到 → 跳過
    uint32_t now = millis();
    if (now < beepNextToggleMs) {
        return;
    }
    // STEP 03: 切換 GPIO
    beepActive = !beepActive;
    digitalWrite(BUZZER_PIN, beepActive ? HIGH : LOW);
    beepNextToggleMs = now + (beepActive ? beepOnMs : beepOffMs);
    beepPulsesRemaining--;
}

// ============================================================
// OLED 反色閃爍（非 blocking SM，取代震動的強提醒視覺效果）
// ============================================================

/**
 * 觸發 OLED 整螢幕反色閃爍一次
 * 非阻塞：反色指令透過 I2C 立即送出，到期由 updateOledFlashMachine() 還原
 * 重複呼叫會從呼叫當下重新計時 durationMs（覆蓋先前起點），適合連續提醒場景
 * 使用 elapsed 比較避免 millis() 接近 UINT32_MAX 時 end-time 加法 overflow
 * @param durationMs 反色持續時長（ms）
 */
void triggerOledFlash(uint16_t durationMs) {
    // STEP 01: 未反色 → 切到反色狀態
    if (!oledInverted) {
        display.invertDisplay(true);
        oledInverted = true;
    }
    // STEP 02: 記錄起點與時長（每次觸發重新計時）
    oledInvertStartMs    = millis();
    oledInvertDurationMs = durationMs;
}

/**
 * 每個 loop cycle 檢查反色是否到期，到期則恢復正常顯示
 * elapsed = millis() - oledInvertStartMs 對 UINT32_MAX 自動 wrap，免 overflow
 */
void updateOledFlashMachine() {
    // STEP 01: 未反色 → 跳過
    if (!oledInverted) {
        return;
    }
    // STEP 02: 到期 → 恢復正常顯示
    if ((uint32_t)(millis() - oledInvertStartMs) >= oledInvertDurationMs) {
        display.invertDisplay(false);
        oledInverted = false;
    }
}

#if ENABLE_VIBRATION
/**
 * 觸發震動馬達一次（blocking，VIBRATION_MS ms）
 * 使用 S8050 NPN 電晶體驅動 1027 扁平硬幣馬達
 */
void triggerVibration() {
    // STEP 01: 開啟 → 等待 → 關閉
    digitalWrite(VIBRATION_PIN, HIGH);
    delay(VIBRATION_MS);
    digitalWrite(VIBRATION_PIN, LOW);
}
#endif

// ============================================================
// OLED 顯示（2D + 2B）
// ============================================================

/**
 * 依裝置狀態選擇對應畫面
 */
void updateDisplay() {
    // STEP 01: 藥物子選單優先顯示（覆蓋主畫面）
    if (drugMenuOpen) {
        drawDrugMenuScreen();
        return;
    }
    // STEP 02: 依狀態選擇主畫面
    switch (deviceState) {
        case STATE_IDLE:    drawIdleScreen();    break;
        case STATE_RUNNING: drawRunningScreen(); break;
        case STATE_PAUSE:   drawPauseScreen();   break;
        case STATE_END:     drawEndScreen();     break;
        default:            break;
    }
}

/**
 * 待機畫面（STATE_IDLE）
 * 顯示當前模式 + BLE 狀態 + 操作提示
 */
void drawIdleScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 01: 標題列
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("EMS Timer   [IDLE]");

    // STEP 02: 分隔線
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 03: 操作提示
    display.setCursor(0, 15);
    display.println("Hold main button");
    display.setCursor(0, 25);
    display.println("2s to start task");

    // STEP 04: 目前模式
    display.setCursor(0, 40);
    display.print("Mode:[");
    display.print(MODE_LABELS[currentMode]);
    display.print("] UP/DN switch");

    // STEP 05: BLE 連線狀態
    display.setCursor(0, 54);
    display.print(bleConnected ? "BLE: connected" : "BLE: waiting...");

    display.display();
}

/**
 * 執行中畫面（STATE_RUNNING）
 * MED 模式：大字顯示 240s 倒數；其他模式：顯示任務已進行時間
 */
void drawRunningScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 01: 標題列（狀態 + 模式）
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("DoseSync [RUN][");
    display.print(MODE_LABELS[currentMode]);
    display.print("]");

    // STEP 02: 分隔線
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 03: 大字計時（MED: 倒數 or GIVE MED 閃爍；其他: 任務elapsed）
    if (currentMode == MODE_MED && medCountdownStartMs > 0) {
        if (medReminderActive) {
            // STEP 03.01: 強提醒閃爍「GIVE MED!」
            bool flashOn = ((millis() / FLASH_PERIOD_MS) % 2) == 0;
            if (flashOn) {
                display.setTextSize(2);
                display.setCursor(4, 14);
                display.print("GIVE MED!");
            }
        } else {
            // STEP 03.02: 倒數剩餘時間大字
            uint32_t elapsed = millis() - medCountdownStartMs;
            uint32_t remainMs = (elapsed >= MED_COUNTDOWN_MS) ? 0 : (MED_COUNTDOWN_MS - elapsed);
            uint32_t remSec   = remainMs / 1000;
            char timeStr[8];
            snprintf(timeStr, sizeof(timeStr), "%02u:%02u",
                     (uint16_t)(remSec / 60), (uint16_t)(remSec % 60));
            display.setTextSize(3);
            int16_t x1, y1;
            uint16_t w, h;
            display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
            display.setCursor((OLED_WIDTH - w) / 2, 14);
            display.print(timeStr);
        }
    } else {
        // STEP 03.03: 非 MED 模式：顯示任務已進行時間
        uint32_t taskMs  = getTaskElapsedMs();
        uint32_t taskSec = taskMs / 1000;
        char timeStr[8];
        snprintf(timeStr, sizeof(timeStr), "%02u:%02u",
                 (uint16_t)(taskSec / 60), (uint16_t)(taskSec % 60));
        display.setTextSize(3);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((OLED_WIDTH - w) / 2, 14);
        display.print(timeStr);
    }

    // STEP 04: 分隔線
    display.drawLine(0, 40, OLED_WIDTH - 1, 40, SSD1306_WHITE);

    // STEP 05: 下方左：任務已進行時間（MED 模式才顯示，其他模式顯示事件數）
    display.setTextSize(1);
    display.setCursor(0, 43);
    if (currentMode == MODE_MED && medCountdownStartMs > 0) {
        uint32_t taskMs  = getTaskElapsedMs();
        uint32_t taskSec = taskMs / 1000;
        char taskStr[16];
        snprintf(taskStr, sizeof(taskStr), "task %02u:%02u",
                 (uint16_t)(taskSec / 60), (uint16_t)(taskSec % 60));
        display.print(taskStr);
    } else {
        char cntStr[12];
        snprintf(cntStr, sizeof(cntStr), "evts: %u", eventCount);
        display.print(cntStr);
    }

    // STEP 06: 下方：最後一筆事件摘要
    display.setCursor(0, 54);
    if (eventCount > 0) {
        const EmsEvent& last = events[eventCount - 1];
        const char* typeName = "";
        switch (last.event_type) {
            case EVT_MEDICATION:   typeName = "Med";    break;
            case EVT_VENTILATION:  typeName = "Vent";   break;
            case EVT_CUSTOM:       typeName = "Cust";   break;
            case EVT_TASK_START:   typeName = "Start";  break;
            case EVT_TASK_PAUSE:   typeName = "Pause";  break;
            case EVT_TASK_RESUME:  typeName = "Resume"; break;
            case EVT_TASK_END:     typeName = "End";    break;
            default:               typeName = "?";      break;
        }
        uint32_t evtSec = last.elapsed_ms / 1000;
        char evtStr[22];
        snprintf(evtStr, sizeof(evtStr), "#%u %s %02u:%02u",
                 last.event_id, typeName,
                 (uint16_t)(evtSec / 60), (uint16_t)(evtSec % 60));
        display.print(evtStr);
    } else {
        display.print("No events yet");
    }

    display.display();
}

/**
 * 暫停畫面（STATE_PAUSE）
 * 閃爍 PAUSED + 任務時間 + 按鍵提示
 */
void drawPauseScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 01: 標題
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("DoseSync  [PAUSE]");

    // STEP 02: 分隔線
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 03: PAUSED 閃爍文字
    bool flashOn = ((millis() / 600) % 2) == 0;
    if (flashOn) {
        display.setTextSize(2);
        display.setCursor(16, 14);
        display.print("PAUSED");
    }

    // STEP 04: 分隔線
    display.drawLine(0, 40, OLED_WIDTH - 1, 40, SSD1306_WHITE);

    // STEP 05: 任務資訊與按鍵提示
    display.setTextSize(1);
    display.setCursor(0, 43);
    uint32_t taskMs  = getTaskElapsedMs();
    uint32_t taskSec = taskMs / 1000;
    char infoStr[24];
    snprintf(infoStr, sizeof(infoStr), "task %02u:%02u | #%u evts",
             (uint16_t)(taskSec / 60), (uint16_t)(taskSec % 60), eventCount);
    display.print(infoStr);

    display.setCursor(0, 54);
    display.print("Sht=Resume Lng=End");

    display.display();
}

/**
 * 結束畫面（STATE_END）
 * 顯示已儲存訊息，2 秒後自動回 IDLE
 */
void drawEndScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 01: 標題
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("DoseSync   [END]");

    // STEP 02: 分隔線
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 03: 完成訊息
    display.setTextSize(2);
    display.setCursor(16, 14);
    display.print("Saved!");

    // STEP 04: 分隔線
    display.drawLine(0, 40, OLED_WIDTH - 1, 40, SSD1306_WHITE);

    // STEP 05: 事件數量
    display.setTextSize(1);
    display.setCursor(0, 43);
    char info[20];
    snprintf(info, sizeof(info), "%u events recorded", eventCount);
    display.print(info);

    display.setCursor(0, 54);
    display.print("Back to IDLE...");

    display.display();
}

/**
 * 藥物子選單畫面（單純給藥 - 獨立時間戳）
 *
 * 視窗顯示 3 筆藥物（游標置中），選中項目反白。
 * 主鍵短按：確認記錄；上/下鍵：導覽；8 秒無操作：自動關閉
 *
 * Layout（128×64）：
 *   y=0  : "[單純給藥]"
 *   y=10 : 分隔線
 *   y=13 : item[start]
 *   y=25 : item[start+1]  ← 游標項目
 *   y=37 : item[start+2]
 *   y=50 : 分隔線
 *   y=54 : "OK=主鍵  8s超時"
 */
void drawDrugMenuScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 01: 標題列
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("[Single Drug Rec]");

    // STEP 02: 分隔線
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 03: 計算 3 項視窗的起始索引（游標盡量置中）
    uint8_t viewSize  = 3;
    uint8_t startIdx;
    if (DRUG_COUNT <= viewSize) {
        startIdx = 0;
    } else if (drugMenuCursor == 0) {
        startIdx = 0;
    } else if (drugMenuCursor >= DRUG_COUNT - 1) {
        startIdx = DRUG_COUNT - viewSize;
    } else {
        startIdx = drugMenuCursor - 1;  // 游標在中間那列
    }

    // STEP 04: 繪製 3 項（每項高 12px，從 y=13 開始）
    for (uint8_t i = 0; i < viewSize && (startIdx + i) < DRUG_COUNT; i++) {
        uint8_t itemIdx = startIdx + i;
        int16_t itemY   = 13 + (int16_t)(i * 12);

        if (itemIdx == drugMenuCursor) {
            // STEP 04.01: 選中項：反白背景 + 黑色文字
            display.fillRect(0, itemY - 1, OLED_WIDTH, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(2, itemY);
        display.print(itemIdx == drugMenuCursor ? ">" : " ");
        display.print(" ");
        display.print(DRUG_NAMES[itemIdx]);
    }

    // STEP 05: 分隔線 + 底部提示
    display.setTextColor(SSD1306_WHITE);
    display.drawLine(0, 50, OLED_WIDTH - 1, 50, SSD1306_WHITE);
    display.setCursor(0, 54);
    display.print("OK=main  8s=cancel");

    display.display();
}

// ============================================================
// BLE NUS 通訊（2E）
// ============================================================

/**
 * 初始化 BLE NUS service 並開始廣播
 */
void setupBLE() {
    // STEP 01: 初始化 BLE 裝置並提高 MTU
    BLEDevice::init(BLE_DEVICE_NAME);
    BLEDevice::setMTU(517);

    // STEP 02: 建立 GATT Server
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new EmsServerCallbacks());

    // STEP 03: 建立 NUS service
    BLEService* service = bleServer->createService(NUS_SERVICE_UUID);

    // STEP 04: TX characteristic（Device → App，Notify）
    bleTxChar = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    bleTxChar->addDescriptor(new BLE2902());

    // STEP 05: RX characteristic（App → Device，Write）
    bleRxChar = service->createCharacteristic(NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
    bleRxChar->setCallbacks(new EmsRxCallbacks());

    // STEP 06: 啟動 service + 廣播
    service->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.print("[BLE] NUS ready, advertising as '");
    Serial.print(BLE_DEVICE_NAME);
    Serial.println("'");
}

/**
 * 序列化 JSON 並透過 TX Notify 推送給 App
 * @param doc 已填好欄位的 JsonDocument
 */
void sendJson(JsonDocument& doc) {
    // STEP 01: 未連線 → 略過
    if (!bleConnected || bleTxChar == nullptr) {
        return;
    }
    // STEP 02: 序列化（末尾加 '\n' 方便 App 切行解析）
    char buf[BLE_TX_BUF_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf) - 2);
    if (len == 0 || len >= sizeof(buf) - 2) {
        Serial.println("[BLE] serialize overflow, drop");
        return;
    }
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    bleTxChar->setValue((uint8_t*)buf, len + 1);
    bleTxChar->notify();
}

/**
 * 連線時送出 hello 訊息（含裝置狀態與任務資訊）
 */
void sendHello() {
    JsonDocument doc;
    doc["t"]        = "hello";
    doc["ver"]      = "phase2c";
    doc["state"]    = (uint8_t)deviceState;
    doc["mode"]     = (uint8_t)currentMode;
    doc["count"]    = eventCount;
    // task_id 以 epoch 秒表示（uint32_t，到 2106 年不溢位）
    doc["task_id"]  = (uint32_t)(taskId / 1000UL);
    sendJson(doc);
}

/**
 * App 請求時批次回傳所有事件（dump_start → dump_item × N → dump_end）
 * 2E：JSON 欄位包含所有新增欄位，傳完後標記 sync_flag = 1
 */
void sendDump() {
    // STEP 01: dump_start
    {
        JsonDocument doc;
        doc["t"]     = "dump_start";
        doc["count"] = eventCount;
        sendJson(doc);
    }

    // STEP 02: 逐筆送出（含 2C 所有新欄位）
    for (uint16_t i = 0; i < eventCount; i++) {
        EmsEvent& e = events[i];
        JsonDocument doc;
        doc["t"]     = "dump_item";
        doc["idx"]   = i;
        doc["id"]    = e.event_id;
        // timestamp 以 epoch 秒 + ms fraction 分開，避免 64-bit 精度問題
        doc["ts"]    = (uint32_t)(e.timestamp / 1000UL);
        doc["ts_ms"] = (uint16_t)(e.timestamp % 1000UL);
        doc["el"]    = e.elapsed_ms;
        doc["type"]  = e.event_type;
        doc["src"]   = e.source;
        doc["mode"]  = e.mode;
        doc["sync"]  = e.sync_flag;
        doc["extra"] = e.extra_data;
        sendJson(doc);
        // 傳完後標記已同步
        e.sync_flag = 1;
        delay(DUMP_ITEM_INTERVAL_MS);
    }

    // STEP 03: dump_end
    {
        JsonDocument doc;
        doc["t"] = "dump_end";
        sendJson(doc);
    }
}

/**
 * 處理 App 下發的 JSON 命令
 * 支援：sync（對時）、dump（批次讀取）、clear（清空，僅 IDLE 狀態）
 * @param msg App 寫入的原始字串
 */
void handleRxCommand(const std::string& msg) {
    // STEP 01: 解析 JSON
    Serial.print("[BLE] RX: ");
    Serial.println(msg.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, msg)) {
        Serial.println("[BLE] parse error");
        return;
    }

    // STEP 02: 取命令
    const char* cmd = doc["cmd"];
    if (cmd == nullptr) {
        return;
    }

    // STEP 03: 分派
    if (strcmp(cmd, "sync") == 0) {
        // STEP 03.01: 時間同步 — 計算 epoch offset
        uint64_t ts = doc["ts"] | (uint64_t)0;
        if (ts == 0) {
            Serial.println("[BLE] sync missing ts");
            return;
        }
        sessionEpochOffset = ts - (uint64_t)millis();
        Serial.print("[BLE] synced epoch_sec=");
        Serial.println((unsigned long)(ts / 1000UL));

        JsonDocument ack;
        ack["t"]   = "ack";
        ack["cmd"] = "sync";
        sendJson(ack);
    }
    else if (strcmp(cmd, "dump") == 0) {
        // STEP 03.02: 批次讀取所有事件
        sendDump();
    }
    else if (strcmp(cmd, "clear") == 0) {
        // STEP 03.03: 清空事件陣列（僅 IDLE 狀態允許）
        if (deviceState != STATE_IDLE) {
            JsonDocument nack;
            nack["t"]   = "nack";
            nack["cmd"] = "clear";
            nack["err"] = "not idle";
            sendJson(nack);
            return;
        }
        eventCount  = 0;
        nextEventId = 1;
        Serial.println("[BLE] events cleared");

        JsonDocument ack;
        ack["t"]   = "ack";
        ack["cmd"] = "clear";
        sendJson(ack);
    }
    else {
        Serial.print("[BLE] unknown cmd: ");
        Serial.println(cmd);
    }
}

/**
 * 管理 BLE advertising 生命週期：
 *   連線後送 hello；斷線後重啟廣播
 */
void updateBleAdvertising() {
    // STEP 01: 剛連線 → 等 service discovery 完成再送 hello
    if (bleConnected && !bleWasConnected) {
        bleWasConnected = true;
        delay(3000);
        sendHello();
        return;
    }
    // STEP 02: 剛斷線 → 重啟廣播
    if (!bleConnected && bleWasConnected) {
        bleWasConnected = false;
        delay(100);
        BLEDevice::startAdvertising();
        Serial.println("[BLE] advertising restarted");
    }
}

// ============================================================
// LittleFS 持久儲存（3A）
// ============================================================

/**
 * 掛載 LittleFS 並建立 /sessions 目錄；列出未同步 session 檔案
 */
void setupLittleFS() {
    // STEP 01: 掛載（format_on_fail=false，保留既有資料）
    if (!LittleFS.begin(false)) {
        Serial.println("[FS] mount failed, formatting...");
        if (!LittleFS.begin(true)) {
            Serial.println("[FS] format failed, LittleFS disabled");
            fsAvailable = false;
            return;
        }
    }
    fsAvailable = true;
    Serial.println("[FS] LittleFS mounted");

    // STEP 02: 確保 /sessions 目錄存在
    if (!LittleFS.exists("/sessions")) {
        LittleFS.mkdir("/sessions");
    }

    // STEP 03: 掃描並列出未同步 session
    File dir = LittleFS.open("/sessions");
    if (!dir || !dir.isDirectory()) {
        return;
    }
    uint8_t count = 0;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            Serial.print("[FS] unsynced: ");
            Serial.println(f.name());
            count++;
        }
        f = dir.openNextFile();
    }
    if (count > 0) {
        Serial.print("[FS] ");
        Serial.print(count);
        Serial.println(" unsynced session(s)");
    }
}

/**
 * 任務結束時序列化事件陣列並寫入 /sessions/<epoch_sec>.json
 * 使用 f.print() 串接 JSON，避免大型 buffer 分配問題
 */
void saveSession() {
    // STEP 01: LittleFS 不可用或無事件 → 跳過
    if (!fsAvailable) {
        Serial.println("[FS] disabled, skip save");
        return;
    }
    if (eventCount == 0) {
        Serial.println("[FS] no events, skip save");
        return;
    }

    // STEP 02: 建立檔名（/sessions/<epoch_sec>.json）
    uint32_t epochSec = (taskId > 0) ? (uint32_t)(taskId / 1000UL) : (uint32_t)(millis() / 1000UL);
    char filename[32];
    snprintf(filename, sizeof(filename), "/sessions/%u.json", epochSec);

    // STEP 03: 開檔（覆寫）
    File f = LittleFS.open(filename, "w");
    if (!f) {
        Serial.print("[FS] open failed: ");
        Serial.println(filename);
        return;
    }

    // STEP 04: 寫入 JSON header
    f.print("{\"task_id\":");
    f.print(epochSec);           // epoch 秒（uint32_t）
    f.print(",\"event_count\":");
    f.print(eventCount);
    f.print(",\"events\":[");

    // STEP 05: 逐筆事件串接
    for (uint16_t i = 0; i < eventCount; i++) {
        const EmsEvent& e = events[i];
        if (i > 0) {
            f.print(",");
        }
        f.print("{\"id\":");
        f.print((unsigned long)e.event_id);
        f.print(",\"ts\":");
        f.print((unsigned long)(e.timestamp / 1000UL));  // epoch 秒
        f.print(",\"ts_ms\":");
        f.print((unsigned int)(e.timestamp % 1000UL));   // ms 小數
        f.print(",\"el\":");
        f.print((unsigned long)e.elapsed_ms);
        f.print(",\"type\":");
        f.print((unsigned int)e.event_type);
        f.print(",\"src\":");
        f.print((unsigned int)e.source);
        f.print(",\"mode\":");
        f.print((unsigned int)e.mode);
        f.print(",\"sync\":");
        f.print((unsigned int)e.sync_flag);
        f.print(",\"extra\":\"");
        f.print(e.extra_data);
        f.print("\"}");
    }

    // STEP 06: 關閉 JSON
    f.print("]}");
    f.close();

    Serial.print("[FS] saved: ");
    Serial.println(filename);
}

// ============================================================
// I2S 麥克風（Phase 1.5 保留）
// ============================================================

#if ENABLE_MIC_MONITOR
/**
 * 初始化 INMP441 I2S 麥克風
 * ESP32-S3 legacy driver 的 slot 順序與 ESP32 顛倒：L/R=GND 時用 ONLY_RIGHT
 */
void setupI2S() {
    // STEP 01: I2S 設定
    i2s_config_t i2sConfig = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = 96000,          // 96k 降低 BCLK jitter
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,  // S3 反直覺設定
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 2,
        .dma_buf_len          = 128,
        .use_apll             = true,           // APLL 降低 jitter
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    // STEP 02: 腳位設定
    i2s_pin_config_t pinConfig = {
        .bck_io_num   = I2S_SCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD_PIN,
    };

    // STEP 03: 安裝驅動
    if (i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL) != ESP_OK) {
        Serial.println("[ERROR] I2S install failed");
        return;
    }

    // STEP 04: 套用腳位
    if (i2s_set_pin(I2S_PORT, &pinConfig) != ESP_OK) {
        Serial.println("[ERROR] I2S set pin failed");
        return;
    }

    // STEP 05: 清空 DMA buffer 殘留
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[EMS Timer] I2S mic OK @ 96kHz APLL");
}
#endif  // ENABLE_MIC_MONITOR
