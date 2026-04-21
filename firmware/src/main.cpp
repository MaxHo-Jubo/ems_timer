/**
 * EMS Timer — Phase 2 韌體
 *
 * Phase 1（已完成 2026-04-17）：確認 8 按鈕 + OLED + 蜂鳴器 + I2S 麥克風硬體
 * Phase 2（本階段）：
 *   - OLED 計時顯示（當前模組名稱 + mm:ss 大字計時）
 *   - 事件陣列累積（EmsEvent × 30）
 *   - 混合計時模式：正數（up）+ 倒數（down）
 *   - 倒數搭配區間提醒（每 N 秒嗶一聲）與結束提醒（嗶 3 聲 + 畫面閃爍）
 *   - BLE NUS 通訊（Phase 2B 實作）
 *
 * 接線（ESP32-S3-DevKitC-1）：
 *   === 左側（按鈕 + SD 卡 + 蜂鳴器） ===
 *   BTN1 Epinephrine → GPIO 4   BTN2 Atropine → GPIO 5
 *   BTN3 CPR 開始    → GPIO 6   BTN4 CPR 結束 → GPIO 7
 *   BTN5 電擊        → GPIO 15  BTN6 插管     → GPIO 16
 *   BTN7 到院        → GPIO 17  BTN8 錄音     → GPIO 18
 *   SD CS/MOSI/CLK/MISO → GPIO 10/11/12/13（Phase 1 保留，未啟用）
 *   蜂鳴器 → GPIO 14
 *
 *   === 右側（OLED + 麥克風） ===
 *   OLED SDA/SCL → GPIO 42/41
 *   INMP441 SCK/WS/SD → GPIO 40/39/38
 *
 *   所有按鈕採 INPUT_PULLUP，按下拉低。
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
#include <esp_sleep.h>

/**
 * Phase 1 驗證 I2S 通訊通過，但 INMP441 靈敏度可疑，暫停麥克風偵測。
 * Phase 1.5 換新模組時把 0 改 1 即可重新啟用。
 */
#define ENABLE_MIC_MONITOR 0

#if ENABLE_MIC_MONITOR
#include <driver/i2s.h>
#endif

// ============================================================
// 硬體常數
// ============================================================

/** OLED 解析度 */
static const uint8_t OLED_WIDTH  = 128;
static const uint8_t OLED_HEIGHT = 64;

/** SSD1306 不需要硬體 reset 時設 -1 */
static const int8_t  OLED_RESET_PIN = -1;

/** SSD1306 預設 I2C 位址（7-bit） */
static const uint8_t OLED_I2C_ADDR = 0x3C;

/** I2C 腳位（S3 右側：SDA/SCL 相鄰 42/41） */
static const uint8_t I2C_SDA_PIN = 42;
static const uint8_t I2C_SCL_PIN = 41;

/** 蜂鳴器 GPIO（S3 左側） */
static const uint8_t BUZZER_PIN = 14;

#if ENABLE_MIC_MONITOR
/** I2S 麥克風腳位（INMP441） */
static const i2s_port_t I2S_PORT    = I2S_NUM_0;
static const uint8_t    I2S_WS_PIN  = 39;
static const uint8_t    I2S_SCK_PIN = 40;
static const uint8_t    I2S_SD_PIN  = 38;
#endif

/** SD 卡腳位（FSPI，Phase 1 尚未使用） */
static const uint8_t SD_CS_PIN   = 10;
static const uint8_t SD_MOSI_PIN = 11;
static const uint8_t SD_CLK_PIN  = 12;
static const uint8_t SD_MISO_PIN = 13;

// ============================================================
// 按鈕配置
// ============================================================

/** 按鈕數量 */
static const uint8_t BTN_COUNT = 8;

/** 按鈕 GPIO 腳位（INPUT_PULLUP，按下接 GND） */
static const uint8_t BTN_PINS[BTN_COUNT] = { 4, 5, 6, 7, 15, 16, 17, 18 };

/**
 * 藥物按鈕數量（BTN1~4 為藥物計時，BTN5~8 為系統功能）
 */
static const uint8_t MED_BTN_COUNT = 4;
static const uint8_t SYS_BTN_COUNT = 4;
static const uint8_t MED_GROUP_COUNT = 2;

/**
 * 藥物群組標籤（2 組 × 4 顆按鈕，透過選單切換）
 * Group 0（預設）：心肺復甦常用藥
 * Group 1：其他急救藥物
 */
static const char* MED_LABELS[MED_GROUP_COUNT][MED_BTN_COUNT] = {
    { "Epi", "Amio", "Atropine", "Adenosine" },   // Group 0
    { "Naloxone", "Nitro", "D50", "Morphine"  },   // Group 1
};

/** 系統按鈕標籤（BTN5~8，選單導航與電源） */
static const char* SYS_LABELS[SYS_BTN_COUNT] = {
    "Menu",   // BTN5 進入選單/確認（待做）
    "Next",   // BTN6 選單選下一個
    "Prev",   // BTN7 選單選上一個
    "Power",  // BTN8 開關機（待做）
};

/** 按鈕 debounce 時間（ms，救護現場防誤觸） */
static const uint16_t DEBOUNCE_MS = 200;

// ============================================================
// Phase 2：事件資料與計時配置
// ============================================================

/** 單次出勤可記錄的事件上限（Phase 2 設計決策 2026-04-18） */
static const uint16_t MAX_EVENTS = 30;

/**
 * 單筆事件紀錄
 * Phase 2 採軟體對時：timestamp = sessionEpochOffset + millis()
 * Phase 3 升級 DS3231 RTC 後改讀硬體時鐘
 */
struct EmsEvent {
    uint8_t  event_type;      // 0~7 對應按鈕索引
    uint64_t timestamp;       // epoch ms（絕對時間）
    uint32_t elapsed_ms;      // 從 session 啟動起的毫秒數（按下時）
    uint32_t elapsed_end_ms;  // 計時被中斷或結束時的 session elapsed ms（0 = 仍在計時）
};

/** 計時模式 */
enum TimerMode : uint8_t {
    TIMER_UP   = 0,  // 正數計時（mm:ss 往上加）
    TIMER_DOWN = 1,  // 倒數計時（從 duration 往下減，結束蜂鳴 + 閃爍）
};

/** 選單狀態機 */
enum MenuState : uint8_t {
    MENU_NONE = 0,  // 正常操作，無選單
    MENU_OPEN = 1,  // 選單顯示中，等待操作
};

/**
 * 事件計時配置
 * duration_s: 倒數總時長（秒，TIMER_UP 忽略）
 * interval_s: 區間提醒間隔（秒，0 = 不提醒）
 */
struct EventConfig {
    TimerMode mode;
    uint16_t  duration_s;
    uint16_t  interval_s;
};

/**
 * 藥物計時配置（2 組 × 4 顆按鈕）
 * Epi/Atropine 按 ACLS 規範倒數 5 分鐘，每 60 秒提醒
 * 其餘藥物正數計時（記錄給藥後經過時間）
 */
static const EventConfig MED_CFG[MED_GROUP_COUNT][MED_BTN_COUNT] = {
    {   // Group 0
        { TIMER_DOWN, 300, 60 },  // Epi：ACLS 每 3~5 分鐘，倒數 5 分鐘
        { TIMER_UP,   0,   0  },  // Amio：正數計時
        { TIMER_DOWN, 300, 60 },  // Atropine：每 3~5 分鐘，倒數 5 分鐘
        { TIMER_UP,   0,   0  },  // Adenosine：正數計時
    },
    {   // Group 1
        { TIMER_UP, 0, 0 },  // Naloxone
        { TIMER_UP, 0, 0 },  // Nitro
        { TIMER_UP, 0, 0 },  // D50
        { TIMER_UP, 0, 0 },  // Morphine
    },
};

/** 系統按鈕計時配置（BTN5~8，觸發不啟動計時） */
static const EventConfig SYS_CFG[SYS_BTN_COUNT] = {
    { TIMER_UP, 0, 0 },  // Menu
    { TIMER_UP, 0, 0 },  // Next
    { TIMER_UP, 0, 0 },  // Prev
    { TIMER_UP, 0, 0 },  // Power
};

/** OLED 重繪節流間隔（ms，避免過度重繪造成 I2C 壅塞） */
static const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 250;

/** 倒數結束閃爍週期（ms） */
static const uint32_t FLASH_PERIOD_MS = 500;

/** 區間提醒脈衝（短嗶 1 聲） */
static const uint8_t  INTERVAL_BEEP_PULSES = 1;
static const uint16_t INTERVAL_BEEP_ON_MS  = 100;
static const uint16_t INTERVAL_BEEP_OFF_MS = 0;

/** 倒數結束提醒脈衝（嗶 3 聲） */
static const uint8_t  EXPIRE_BEEP_PULSES = 3;
static const uint16_t EXPIRE_BEEP_ON_MS  = 200;
static const uint16_t EXPIRE_BEEP_OFF_MS = 200;

// ============================================================
// Phase 2B：BLE NUS 常數
// ============================================================

/** Nordic UART Service 標準 UUID */
static const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // App → Device (Write)
static const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Device → App (Notify)

/** BLE 廣播名稱（手機 scanner 顯示用） */
static const char* BLE_DEVICE_NAME = "EMS Timer";

/** Notify / serialize buffer 大小（單個訊息上限 bytes） */
static const size_t BLE_TX_BUF_SIZE = 256;

/** dump 逐筆間距（ms，避免 BLE stack 壅塞） */
static const uint16_t DUMP_ITEM_INTERVAL_MS = 10;

// ============================================================
// 選單系統常數
// ============================================================

/** 選單無操作自動關閉時間（ms） */
static const uint32_t MENU_TIMEOUT_MS = 5000;

// ============================================================
// 全域物件與狀態
// ============================================================

/** SSD1306 OLED 顯示器物件 */
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

/** 各按鈕上次有效按下的時間戳（debounce 用） */
static uint32_t lastPressMs[BTN_COUNT] = { 0 };

/** 各按鈕上一次讀到的狀態（INPUT_PULLUP：HIGH = 未按） */
static uint8_t  lastBtnState[BTN_COUNT];

/** 事件記錄陣列與計數 */
static EmsEvent events[MAX_EVENTS];
static uint16_t eventCount = 0;

/** Session 狀態 */
static uint32_t sessionId          = 0;      // 本次開機隨機產生
static uint32_t sessionStartMs     = 0;      // 首次按鈕按下時鎖定
static bool     sessionStarted     = false;  // 是否已啟動 session
static uint64_t sessionEpochOffset = 0;      // epoch ms offset（Phase 2B 由 BLE 下發）

/** 當前啟動中的事件（OLED 顯示主角） */
static int8_t   currentEventIdx     = -1;    // -1 = 尚無事件
static uint32_t currentEventStartMs = 0;     // 當前事件的 millis() 起點

/** 倒數提醒狀態 */
static uint16_t lastIntervalElapsed = 0;     // 上次觸發區間提醒時的秒數
static bool     countdownExpired    = false; // 倒數是否已結束（防止重複蜂鳴）

/** 目前正在計時的 events[] 索引（-1 = 無），用於記錄中斷/結束時間點 */
static int16_t activeEventRecordIdx = -1;

/** OLED 重繪節流 */
static uint32_t lastDisplayUpdateMs = 0;

/** 蜂鳴器非 blocking state machine */
static uint8_t  beepPulsesRemaining = 0;     // 剩餘階段數（每個脈衝 = on + off 兩階段）
static uint32_t beepNextToggleMs    = 0;     // 下次切換 GPIO 的時間
static bool     beepActive          = false; // 當前 GPIO 是否 HIGH
static uint16_t beepOnMs            = 0;     // on 階段時長
static uint16_t beepOffMs           = 0;     // off 階段時長

#if ENABLE_MIC_MONITOR
static int32_t  i2sBuffer[256];
static uint32_t lastMicPrintMs = 0;
#endif

/** BLE 狀態 */
static BLEServer*         bleServer       = nullptr;
static BLECharacteristic* bleTxChar       = nullptr;
static BLECharacteristic* bleRxChar       = nullptr;
static bool               bleConnected    = false;  // 當前是否有 central 連線
static bool               bleWasConnected = false;  // 上一輪狀態，用於偵測斷線後重啟廣播

/** 當前藥物群組（0 = Group 0，1 = Group 1，透過選單切換） */
static uint8_t currentMedGroup = 0;

/** 選單狀態機 */
static MenuState menuState  = MENU_NONE;  // 目前選單狀態
static uint8_t   menuCursor = 0;          // 目前游標位置（對應 MED_GROUP 索引）
static uint32_t  menuOpenMs = 0;          // 選單開啟時的 millis()，用於超時判斷

/**
 * BLE RX 命令 pending 佇列（single slot）
 *
 * 為什麼需要：onWrite callback 跑在 BLE stack 的 GATT task，若在裡面執行
 * notify（尤其 dump 這種多 packet 序列）或 delay，會阻塞 BLE 心跳導致 central
 * 主動斷線。因此 callback 只 copy 進 buffer + 設 flag，真正的處理交給 main loop。
 */
static const size_t PENDING_CMD_BUF_SIZE = 256;
static char          pendingCmdBuf[PENDING_CMD_BUF_SIZE];
static size_t        pendingCmdLen  = 0;
static volatile bool pendingCmdReady = false;

// ============================================================
// 函式宣告
// ============================================================

/**
 * 取得按鈕對應的顯示標籤
 * BTN 0~3 依 currentMedGroup 查藥物表，BTN 4~7 查系統按鈕表
 * @param eventType 按鈕索引（0-based）
 */
inline const char* getButtonLabel(uint8_t eventType) {
    if (eventType < MED_BTN_COUNT) {
        return MED_LABELS[currentMedGroup][eventType];
    }
    return SYS_LABELS[eventType - MED_BTN_COUNT];
}

/**
 * 取得按鈕對應的計時配置
 * @param eventType 按鈕索引（0-based）
 */
inline const EventConfig& getEventConfig(uint8_t eventType) {
    if (eventType < MED_BTN_COUNT) {
        return MED_CFG[currentMedGroup][eventType];
    }
    return SYS_CFG[eventType - MED_BTN_COUNT];
}

void handleButtons();
void handleSysButton(uint8_t sysIdx);
void recordEvent(uint8_t eventType);
void startEvent(uint8_t eventType);
void updateTimer();
void updateBeepMachine();
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void updateDisplay();
void drawIdleScreen();
void drawTimerScreen();
void drawMenuScreen();
uint32_t getCurrentElapsedMs();
uint32_t getCurrentRemainingMs();

void setupBLE();
void sendJson(JsonDocument& doc);
void sendHello();
void sendEvent(uint16_t idx);
void sendDump();
void handleRxCommand(const std::string& msg);
void updateBleAdvertising();

#if ENABLE_MIC_MONITOR
void setupI2S();
#endif

// ============================================================
// BLE Callbacks
// ============================================================

/**
 * BLE 伺服器連線/斷線 callback
 * 斷線後不直接重啟 advertising（BLE stack 需要時間清理），改由 loop 輪詢處理。
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
        // STEP 01: 取出 App 寫入的 bytes（ESP32 BLE 2.x 回傳 std::string）
        std::string value = c->getValue();
        if (value.empty()) {
            return;
        }

        // STEP 02: 若前一筆 pending 尚未被 loop 處理完 → 丟棄新訊息（避免 race）
        if (pendingCmdReady) {
            return;
        }

        // STEP 03: 複製到 buffer（切勿在此 callback thread 呼叫 notify / delay，
        //          會阻塞 BLE stack 心跳並導致斷線）
        size_t len = value.length();
        if (len >= PENDING_CMD_BUF_SIZE) {
            len = PENDING_CMD_BUF_SIZE - 1;
        }
        memcpy(pendingCmdBuf, value.data(), len);
        pendingCmdBuf[len] = '\0';
        pendingCmdLen = len;

        // STEP 04: 最後才設 flag（memory ordering：確保 loop 讀到 flag 時 buffer 已 ready）
        pendingCmdReady = true;
    }
};

// ============================================================
// setup
// ============================================================

/**
 * 初始化 I2C、OLED、按鈕、蜂鳴器與 session
 */
void setup() {
    // STEP 01: 初始化序列埠
    Serial.begin(115200);
    delay(5000); // 等 USB CDC 重枚舉後 Serial Monitor 重連
    Serial.println("[EMS Timer] Phase 2 boot");

    // STEP 02: 初始化蜂鳴器 GPIO
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // STEP 03: 初始化所有按鈕（INPUT_PULLUP 不需外部電阻）
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 03.01: 設定腳位模式
        pinMode(BTN_PINS[i], INPUT_PULLUP);

        // STEP 03.02: 讀取實際腳位狀態作為初始值（toggle switch 開機可能已接低）
        lastBtnState[i] = digitalRead(BTN_PINS[i]);
    }

    // STEP 04: 初始化 I2C bus
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // STEP 05: 初始化 OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        // STEP 05.01: OLED 初始化失敗 → 停在這裡輸出錯誤
        Serial.println("[ERROR] OLED init failed. Check wiring (SDA=42, SCL=41).");
        while (true) {
            delay(1000);
        }
    }

    // STEP 06: 產生 session_id（Phase 2B BLE 連線時 App 可覆寫）
    sessionId = esp_random();
    Serial.print("[EMS Timer] Session ID: 0x");
    Serial.println(sessionId, HEX);

    // STEP 07: 顯示開機待機畫面
    drawIdleScreen();

    Serial.println("[EMS Timer] OLED OK. Waiting for first button press...");

    // STEP 08: 蜂鳴器開機測試 — 嗶兩聲確認可響
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
    }

    // STEP 09: 初始化 BLE NUS service 並開始廣播
    setupBLE();

#if ENABLE_MIC_MONITOR
    // STEP 10: Phase 1.5 麥克風初始化
    setupI2S();
#endif
}

// ============================================================
// loop
// ============================================================

/**
 * 主迴圈：按鈕輪詢 → 計時狀態檢查 → 蜂鳴器 SM → 節流更新 OLED
 */
void loop() {
    // STEP 01: 輪詢按鈕偵測下降緣
    handleButtons();

    // STEP 02: 更新計時狀態（倒數結束、區間提醒）
    updateTimer();

    // STEP 03: 驅動蜂鳴器非 blocking state machine
    updateBeepMachine();

    // STEP 04: 處理 BLE 連線/斷線邊緣事件（送 hello、重啟廣播）
    updateBleAdvertising();

    // STEP 04.01: 選單無操作超時自動關閉
    if (menuState == MENU_OPEN && (millis() - menuOpenMs >= MENU_TIMEOUT_MS)) {
        menuState = MENU_NONE;
        Serial.println("[SYS] Menu timeout, closed");
    }

    // STEP 05: 處理 App 下發的 pending 命令（onWrite callback 只 queue，實際處理放這）
    if (pendingCmdReady) {
        // STEP 05.01: copy 出 buffer 並立即清 flag，讓 callback 可接下一筆
        std::string cmd(pendingCmdBuf, pendingCmdLen);
        pendingCmdReady = false;

        // STEP 05.02: 在 main task context 執行命令處理（含 notify + delay）
        handleRxCommand(cmd);
    }

    // STEP 06: 節流更新 OLED（250ms 一次）
    uint32_t now = millis();
    if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
        lastDisplayUpdateMs = now;
        updateDisplay();
    }

#if ENABLE_MIC_MONITOR
    // STEP 05: Phase 1.5 麥克風讀取
    if (now - lastMicPrintMs >= 250) {
        // STEP 05.01: 讀取 I2S 樣本
        lastMicPrintMs = now;
        size_t bytesRead = 0;
        i2s_read(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);

        // STEP 05.02: 計算峰值
        int32_t peak = 0;
        uint16_t samplesRead = bytesRead / sizeof(int32_t);
        for (uint16_t i = 0; i < samplesRead; i++) {
            int32_t sample = i2sBuffer[i] >> 8;
            if (sample < 0) { sample = -sample; }
            if (sample > peak) { peak = sample; }
        }
        Serial.print("[MIC] peak=");
        Serial.println(peak);
    }
#endif
}

// ============================================================
// 按鈕處理
// ============================================================

/**
 * 輪詢所有按鈕，偵測下降緣（按下）事件並觸發 recordEvent + startEvent
 */
void handleButtons() {
    // STEP 01: 走訪所有按鈕
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 01.01: 讀取當前狀態
        uint8_t currentState = digitalRead(BTN_PINS[i]);

        // STEP 01.02: 偵測下降緣（HIGH → LOW）
        if (lastBtnState[i] == HIGH && currentState == LOW) {
            // STEP 01.02.01: debounce 檢查
            uint32_t now = millis();
            if (now - lastPressMs[i] >= DEBOUNCE_MS) {
                lastPressMs[i] = now;

                if (i < MED_BTN_COUNT) {
                    // STEP 01.02.01.01: 藥物按鈕 → 先封存前一計時器的中斷時間點
                    if (activeEventRecordIdx >= 0) {
                        events[activeEventRecordIdx].elapsed_end_ms =
                            sessionStarted ? (now - sessionStartMs) : 0;
                        Serial.print("[TIMER] interrupted at ");
                        Serial.print(events[activeEventRecordIdx].elapsed_end_ms);
                        Serial.println("ms");
                    }
                    recordEvent(i);
                    startEvent(i);
                    Serial.print("[BTN");
                    Serial.print(i + 1);
                    Serial.print("] ");
                    Serial.print(getButtonLabel(i));
                    Serial.print(" recorded, total=");
                    Serial.println(eventCount);
                } else {
                    // STEP 01.02.01.02: 系統按鈕 → 不記錄事件，進入系統處理
                    handleSysButton(i - MED_BTN_COUNT);
                }
            }
        }

        // STEP 01.03: 儲存本次狀態供下一輪比對
        lastBtnState[i] = currentState;
    }
}

/**
 * 記錄一筆事件到 events 陣列
 * @param eventType 事件類型（對應按鈕索引 0~7）
 */

/**
 * 處理系統按鈕（BTN5~8）動作
 * BTN5 Menu：開啟/確認選單；BTN6 Next / BTN7 Prev：移動游標；BTN8 Power：deep sleep
 * @param sysIdx 0=Menu 1=Next 2=Prev 3=Power
 */
void handleSysButton(uint8_t sysIdx) {
    // STEP 01: 依系統按鈕分派動作
    switch (sysIdx) {
        case 0:  // BTN5 Menu / Confirm
            if (menuState == MENU_NONE) {
                // STEP 01.01: 開啟選單，游標定位到目前群組
                menuState  = MENU_OPEN;
                menuCursor = currentMedGroup;
                menuOpenMs = millis();
                Serial.println("[SYS] Menu opened");
            } else {
                // STEP 01.02: 選單開啟中 → 確認選擇並關閉
                currentMedGroup = menuCursor;
                menuState = MENU_NONE;
                triggerBeep(1, 100, 0);  // 確認音效
                Serial.print("[SYS] Group selected: ");
                Serial.println(currentMedGroup);
            }
            break;

        case 1:  // BTN6 Next — 選單游標向下移動
            if (menuState == MENU_OPEN) {
                menuCursor = (menuCursor + 1) % MED_GROUP_COUNT;
                menuOpenMs = millis();  // 重置超時計時
                Serial.print("[SYS] Menu cursor: ");
                Serial.println(menuCursor);
            }
            break;

        case 2:  // BTN7 Prev — 選單游標向上移動
            if (menuState == MENU_OPEN) {
                menuCursor = (menuCursor == 0) ? (MED_GROUP_COUNT - 1) : (menuCursor - 1);
                menuOpenMs = millis();  // 重置超時計時
                Serial.print("[SYS] Menu cursor: ");
                Serial.println(menuCursor);
            }
            break;

        case 3:  // BTN8 Power — 待實作（長按偵測機制確認後再啟用 deep sleep）
            // esp_deep_sleep_start() 暫時停用，toggle switch 會誤觸
            Serial.println("[SYS] Power - not implemented");
            break;

        default:
            break;
    }
}

void recordEvent(uint8_t eventType) {
    // STEP 01: 首次事件 → 鎖定 session 起算點
    if (!sessionStarted) {
        sessionStartMs = millis();
        sessionStarted = true;
    }

    // STEP 02: 超過容量上限 → 丟棄新事件（保留最早紀錄，符合法醫優先原則）
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[WARN] events[] full, dropping new event");
        return;
    }

    // STEP 03: 計算相對經過時間（從 session 啟動起）
    uint32_t now = millis();
    uint32_t elapsed = now - sessionStartMs;

    // STEP 04: 計算絕對時間戳（Phase 2：軟體對時 offset + millis）
    uint64_t ts = sessionEpochOffset + (uint64_t)now;

    // STEP 05: 寫入陣列
    events[eventCount].event_type     = eventType;
    events[eventCount].timestamp      = ts;
    events[eventCount].elapsed_ms     = elapsed;
    events[eventCount].elapsed_end_ms = 0;  // 0 = 計時中，中斷/結束時填入
    activeEventRecordIdx = (int16_t)eventCount;
    eventCount++;

    // STEP 06: 即時 Notify 推送（暫時停用，待確認 Phase 3 是否需要）
    // sendEvent(eventCount - 1);
}

/**
 * 啟動指定事件的計時顯示（切換 OLED 主畫面到此事件）
 * @param eventType 事件類型
 */
void startEvent(uint8_t eventType) {
    // STEP 01: 切換當前事件並重置計時起點
    currentEventIdx = eventType;
    currentEventStartMs = millis();

    // STEP 02: 重置倒數相關狀態
    lastIntervalElapsed = 0;
    countdownExpired = false;
}

// ============================================================
// 計時狀態機
// ============================================================

/**
 * 檢查當前事件的計時進度，觸發區間提醒與倒數結束提醒
 */
void updateTimer() {
    // STEP 01: 尚無當前事件 → 跳過
    if (currentEventIdx < 0) {
        return;
    }

    // STEP 02: 取得當前事件配置
    const EventConfig& cfg = getEventConfig(currentEventIdx);

    // STEP 03: 正數模式沒有提醒邏輯，直接回傳
    if (cfg.mode != TIMER_DOWN) {
        return;
    }

    // STEP 04: 倒數模式處理
    uint32_t elapsedMs = getCurrentElapsedMs();
    uint16_t elapsedSec = (uint16_t)(elapsedMs / 1000);

    // STEP 05: 倒數結束 → 嗶 3 聲（僅觸發一次）
    if (!countdownExpired && elapsedSec >= cfg.duration_s) {
        countdownExpired = true;
        if (activeEventRecordIdx >= 0) {
            events[activeEventRecordIdx].elapsed_end_ms = millis() - sessionStartMs;
        }
        triggerBeep(EXPIRE_BEEP_PULSES, EXPIRE_BEEP_ON_MS, EXPIRE_BEEP_OFF_MS);
        Serial.print("[TIMER] ");
        Serial.print(getButtonLabel(currentEventIdx));
        Serial.println(" countdown expired");
        return;
    }

    // STEP 06: 區間提醒（每 interval_s 秒嗶一聲）
    if (cfg.interval_s > 0 && !countdownExpired) {
        // STEP 06.01: 計算目前落在第幾個區間
        uint16_t intervalCount = elapsedSec / cfg.interval_s;

        // STEP 06.02: 進入新區間時觸發提醒
        uint16_t thisIntervalMark = intervalCount * cfg.interval_s;
        if (intervalCount > 0 && thisIntervalMark != lastIntervalElapsed) {
            lastIntervalElapsed = thisIntervalMark;
            triggerBeep(INTERVAL_BEEP_PULSES, INTERVAL_BEEP_ON_MS, INTERVAL_BEEP_OFF_MS);
            Serial.print("[TIMER] interval beep at ");
            Serial.print(thisIntervalMark);
            Serial.println("s");
        }
    }
}

/**
 * 取得當前事件已經過的毫秒數
 * @return elapsed ms；無當前事件時回傳 0
 */
uint32_t getCurrentElapsedMs() {
    if (currentEventIdx < 0) {
        return 0;
    }
    return millis() - currentEventStartMs;
}

/**
 * 取得倒數剩餘毫秒數（僅 TIMER_DOWN 模式有意義）
 * @return remaining ms；非倒數模式或已結束時回傳 0
 */
uint32_t getCurrentRemainingMs() {
    if (currentEventIdx < 0) {
        return 0;
    }
    const EventConfig& cfg = getEventConfig(currentEventIdx);
    if (cfg.mode != TIMER_DOWN) {
        return 0;
    }
    uint32_t totalMs = (uint32_t)cfg.duration_s * 1000;
    uint32_t elapsed = getCurrentElapsedMs();
    if (elapsed >= totalMs) {
        return 0;
    }
    return totalMs - elapsed;
}

// ============================================================
// 蜂鳴器（非 blocking state machine）
// ============================================================

/**
 * 觸發蜂鳴器脈衝序列（不阻塞 loop）
 * 每個脈衝包含 on + off 兩階段；連續 pulses 次。
 * @param pulses 脈衝次數
 * @param onMs   每次開啟時長（ms）
 * @param offMs  每次關閉時長（ms，最後一次保留以確保 GPIO 被拉低）
 */
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
    // STEP 01: 設定剩餘階段（on/off 各算一階）
    beepPulsesRemaining = pulses * 2;
    beepOnMs = onMs;
    beepOffMs = offMs;

    // STEP 02: 初始關閉，第一次 toggle 會開啟
    beepActive = false;
    beepNextToggleMs = millis();
}

/**
 * 蜂鳴器 state machine 更新（每個 loop cycle 呼叫一次）
 */
void updateBeepMachine() {
    // STEP 01: 沒有待處理的脈衝 → 跳過
    if (beepPulsesRemaining == 0) {
        return;
    }

    // STEP 02: 時間未到 → 跳過
    uint32_t now = millis();
    if (now < beepNextToggleMs) {
        return;
    }

    // STEP 03: 切換 GPIO 狀態
    beepActive = !beepActive;
    digitalWrite(BUZZER_PIN, beepActive ? HIGH : LOW);

    // STEP 04: 排定下次切換時間
    beepNextToggleMs = now + (beepActive ? beepOnMs : beepOffMs);
    beepPulsesRemaining--;
}

// ============================================================
// OLED 顯示
// ============================================================

/**
 * 依 session 狀態繪製 OLED：選單 > 計時 > 待機
 */
void updateDisplay() {
    if (menuState == MENU_OPEN) {
        drawMenuScreen();
    } else if (currentEventIdx < 0) {
        drawIdleScreen();
    } else {
        drawTimerScreen();
    }
}

/**
 * 待機畫面：尚未按過任何按鈕時顯示
 */
void drawIdleScreen() {
    // STEP 01: 清除畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 02: 標題
    display.setTextSize(2);
    display.setCursor(10, 8);
    display.println("EMS Timer");

    // STEP 03: Phase 標示
    display.setTextSize(1);
    display.setCursor(20, 38);
    display.println("Phase 2 Ready");

    // STEP 04: 操作提示
    display.setCursor(10, 52);
    display.println("Press any button");

    // STEP 05: 送出畫面
    display.display();
}

/**
 * 計時畫面：頂部事件名稱 + 事件計數、中間大字 mm:ss、底部模式標示
 */
void drawTimerScreen() {
    // STEP 01: 清除畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 02: 頂部左：當前事件名稱
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(getButtonLabel(currentEventIdx));

    // STEP 03: 頂部右：已記錄事件數（靠右對齊）
    char countStr[12];
    snprintf(countStr, sizeof(countStr), "#%u", eventCount);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(OLED_WIDTH - w, 0);
    display.print(countStr);

    // STEP 04: 決定顯示時間（正數：elapsed；倒數：remaining；倒數結束：0 + 閃爍）
    const EventConfig& cfg = getEventConfig(currentEventIdx);
    uint32_t displayMs;
    bool isCountdown = (cfg.mode == TIMER_DOWN);
    bool expired = false;

    if (isCountdown) {
        // STEP 04.01: 倒數模式
        if (countdownExpired) {
            displayMs = 0;
            expired = true;
        } else {
            displayMs = getCurrentRemainingMs();
        }
    } else {
        // STEP 04.02: 正數模式
        displayMs = getCurrentElapsedMs();
    }

    // STEP 05: 格式化為 mm:ss（超過 99 分鐘仍以 mm:ss 顯示，不做 hh:mm:ss）
    uint32_t totalSec = displayMs / 1000;
    uint16_t mm = (uint16_t)(totalSec / 60);
    uint16_t ss = (uint16_t)(totalSec % 60);
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u", mm, ss);

    // STEP 06: 倒數結束閃爍控制
    bool flashVisible = true;
    if (expired) {
        flashVisible = ((millis() / FLASH_PERIOD_MS) % 2) == 0;
    }

    // STEP 07: 大字居中顯示計時（text size 3 寬度約 18px/char）
    if (flashVisible) {
        display.setTextSize(3);
        display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        int16_t cx = (int16_t)((OLED_WIDTH - w) / 2);
        display.setCursor(cx, 18);
        display.print(timeStr);
    }

    // STEP 08: 底部模式標示
    display.setTextSize(1);
    display.setCursor(0, 54);
    if (isCountdown) {
        if (expired) {
            display.print("DONE!");
        } else {
            display.print("Countdown");
        }
    } else {
        display.print("Elapsed");
    }

    // STEP 09: 送出畫面
    display.display();
}

/**
 * 選單畫面：顯示藥物群組選擇列表，游標反白選中項目
 * 頂部標題 → 橫線分隔 → 群組列表（游標反白）→ 底部現行群組提示
 */
void drawMenuScreen() {
    // STEP 01: 清除畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 02: 標題列
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("[ Drug Group ]");

    // STEP 03: 分隔線（y=10）
    display.drawLine(0, 10, OLED_WIDTH - 1, 10, SSD1306_WHITE);

    // STEP 04: 逐一繪製群組選項（每列高 12px，從 y=13 開始）
    for (uint8_t i = 0; i < MED_GROUP_COUNT; i++) {
        // STEP 04.01: 計算此列 Y 座標
        int16_t itemY = 13 + (int16_t)i * 12;

        if (menuCursor == i) {
            // STEP 04.02: 選中項目：反白背景 + 黑色文字
            display.fillRect(0, itemY - 1, OLED_WIDTH, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        // STEP 04.03: 游標符號 + 群組標籤（顯示前兩個藥名預覽）
        display.setCursor(0, itemY);
        display.print(menuCursor == i ? ">" : " ");
        display.print("G");
        display.print(i);
        display.print(":");
        display.print(MED_LABELS[i][0]);
        display.print("/");
        display.print(MED_LABELS[i][1]);
    }

    // STEP 05: 底部提示（目前群組 + 確認鍵說明）
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 54);
    display.print("Now:G");
    display.print(currentMedGroup);
    display.print(" OK=BTN5");

    // STEP 06: 送出畫面
    display.display();
}

// ============================================================
// BLE NUS 通訊
// ============================================================

/**
 * 初始化 BLE NUS service、characteristic 與 advertising
 */
void setupBLE() {
    // STEP 01: 初始化 BLE 裝置
    BLEDevice::init(BLE_DEVICE_NAME);

    // STEP 02: 提高 ATT MTU，讓單次 Notify 可承載更大 JSON
    BLEDevice::setMTU(517);

    // STEP 03: 建立 GATT Server 並註冊連線 callback
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new EmsServerCallbacks());

    // STEP 04: 建立 NUS service
    BLEService* service = bleServer->createService(NUS_SERVICE_UUID);

    // STEP 05: 建立 TX characteristic（Device → App，Notify）
    bleTxChar = service->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );

    // STEP 05.01: 加上 CCCD（0x2902）讓 App 可訂閱 Notify
    bleTxChar->addDescriptor(new BLE2902());

    // STEP 06: 建立 RX characteristic（App → Device，Write）
    bleRxChar = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    bleRxChar->setCallbacks(new EmsRxCallbacks());

    // STEP 07: 啟動 service
    service->start();

    // STEP 08: 設定 advertising
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);  // iPhone 連線相容性參考值
    adv->setMinPreferred(0x12);

    // STEP 09: 開始廣播
    BLEDevice::startAdvertising();

    Serial.print("[BLE] NUS ready, advertising as '");
    Serial.print(BLE_DEVICE_NAME);
    Serial.println("'");
}

/**
 * 序列化 JSON 文件並透過 TX characteristic 推送給 App
 * @param doc 已填入欄位的 JsonDocument
 */
void sendJson(JsonDocument& doc) {
    // STEP 01: 未連線 → 不送（省資源，不阻塞裝置端）
    if (!bleConnected || bleTxChar == nullptr) {
        return;
    }

    // STEP 02: 序列化到固定 buffer（訊息尾加 '\n' 方便 App 切行）
    char buf[BLE_TX_BUF_SIZE];
    size_t len = serializeJson(doc, buf, sizeof(buf) - 2);
    if (len == 0 || len >= sizeof(buf) - 2) {
        Serial.println("[BLE] serialize overflow, drop");
        return;
    }
    buf[len]     = '\n';
    buf[len + 1] = '\0';

    // STEP 03: 設定 characteristic 值並 notify
    bleTxChar->setValue((uint8_t*)buf, len + 1);
    bleTxChar->notify();
}

/**
 * 連線建立時送 hello 訊息（含 session_id 與目前事件數）
 */
void sendHello() {
    JsonDocument doc;
    doc["t"]     = "hello";
    doc["ver"]   = "phase2";
    char sidStr[12];
    snprintf(sidStr, sizeof(sidStr), "0x%08X", (unsigned int)sessionId);
    doc["sid"]   = sidStr;
    doc["count"] = eventCount;
    sendJson(doc);
}

/**
 * 推送單筆事件給 App（按鈕按下時即時觸發）
 * @param idx events[] 的索引
 */
void sendEvent(uint16_t idx) {
    if (idx >= eventCount) {
        return;
    }
    const EmsEvent& e = events[idx];

    JsonDocument doc;
    doc["t"]     = "evt";
    doc["idx"]   = idx;
    doc["type"]  = e.event_type;
    doc["label"] = getButtonLabel(e.event_type);
    doc["ts"]    = e.timestamp;
    doc["el"]    = e.elapsed_ms;
    doc["end"]   = e.elapsed_end_ms;  // 0 = 計時中，>0 = 中斷/結束時的 session elapsed ms
    sendJson(doc);
}

/**
 * App 請求時批次回傳所有歷史事件
 * 協定：dump_start → dump_item × N → dump_end
 * 逐筆 Notify 避免單訊息超過 MTU，且每筆間隔 10ms 讓 BLE stack 消化
 */
void sendDump() {
    // STEP 01: dump_start 告知總數
    {
        JsonDocument doc;
        doc["t"]     = "dump_start";
        doc["count"] = eventCount;
        sendJson(doc);
    }

    // STEP 02: 逐筆送 dump_item
    for (uint16_t i = 0; i < eventCount; i++) {
        const EmsEvent& e = events[i];
        JsonDocument doc;
        doc["t"]     = "dump_item";
        doc["idx"]   = i;
        doc["type"]  = e.event_type;
        doc["label"] = getButtonLabel(e.event_type);
        doc["ts"]    = e.timestamp;
        doc["el"]    = e.elapsed_ms;
        sendJson(doc);
        delay(DUMP_ITEM_INTERVAL_MS);
    }

    // STEP 03: dump_end 告知結束
    {
        JsonDocument doc;
        doc["t"] = "dump_end";
        sendJson(doc);
    }
}

/**
 * 處理 App 下發的 JSON 命令
 * 支援：sync（對時）、dump（批次讀取）、clear（清空事件）
 * @param msg App 寫入的原始字串
 */
void handleRxCommand(const std::string& msg) {
    // STEP 01: 印出原始訊息供 debug
    Serial.print("[BLE] RX: ");
    Serial.println(msg.c_str());

    // STEP 02: 解析 JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (err) {
        Serial.print("[BLE] parse err: ");
        Serial.println(err.c_str());
        return;
    }

    // STEP 03: 取得命令字串
    const char* cmd = doc["cmd"];
    if (cmd == nullptr) {
        Serial.println("[BLE] missing 'cmd' field");
        return;
    }

    // STEP 04: 分派到對應處理
    if (strcmp(cmd, "sync") == 0) {
        // STEP 04.01: 時間同步 — 反推 offset 讓 timestamp = offset + millis()
        uint64_t ts = doc["ts"] | (uint64_t)0;
        if (ts == 0) {
            Serial.println("[BLE] sync missing ts");
            return;
        }
        uint32_t now = millis();
        sessionEpochOffset = ts - (uint64_t)now;
        Serial.print("[BLE] time synced, epoch_ms=");
        Serial.println((unsigned long)(ts / 1000ULL));

        // STEP 04.01.01: 回 ack
        JsonDocument ack;
        ack["t"]      = "ack";
        ack["cmd"]    = "sync";
        ack["offset"] = sessionEpochOffset;
        sendJson(ack);
    }
    else if (strcmp(cmd, "dump") == 0) {
        // STEP 04.02: 批次回傳歷史事件
        sendDump();
    }
    else if (strcmp(cmd, "clear") == 0) {
        // STEP 04.03: 清空事件陣列與 session 狀態
        eventCount = 0;
        currentEventIdx = -1;
        sessionStarted = false;
        countdownExpired = false;
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
 * 管理 BLE advertising 生命週期
 *   - 首次連線 → 送 hello
 *   - 斷線後 → 延遲重啟 advertising（等 BLE stack 清理完畢）
 */
void updateBleAdvertising() {
    // STEP 01: 偵測「剛連線」邊緣事件 → 送 hello
    if (bleConnected && !bleWasConnected) {
        bleWasConnected = true;
        // 給 App 足夠時間完成 service discovery + CCCD 訂閱再送 hello
        delay(3000);
        sendHello();
        return;
    }

    // STEP 02: 偵測「剛斷線」邊緣事件 → 重啟 advertising
    if (!bleConnected && bleWasConnected) {
        bleWasConnected = false;
        delay(100);  // 等 BLE stack 清理
        BLEDevice::startAdvertising();
        Serial.println("[BLE] advertising restarted");
    }
}

#if ENABLE_MIC_MONITOR
// ============================================================
// I2S 麥克風（Phase 1.5 保留）
// ============================================================

/**
 * 初始化 I2S 介面，設定為 INMP441 麥克風輸入模式
 */
void setupI2S() {
    // STEP 01: 設定 I2S 驅動參數
    i2s_config_t i2sConfig = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        // 96k × 32 × 2 = 6.144 MHz BCLK，遠超 INMP441 normal mode 門檻，減少 BCLK jitter
        .sample_rate          = 96000,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        // ESP32-S3 legacy driver slot 順序跟 ESP32 顛倒：L/R 接 GND 時要用 ONLY_RIGHT
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        // DMA ring 僅保留 2 塊避免舊資料堆積
        .dma_buf_count        = 2,
        .dma_buf_len          = 128,
        // APLL 降低 BCLK jitter
        .use_apll             = true,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    // STEP 02: 設定 I2S 腳位
    i2s_pin_config_t pinConfig = {
        .bck_io_num   = I2S_SCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD_PIN,
    };

    // STEP 03: 安裝 I2S 驅動
    esp_err_t err = i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);
    if (err != ESP_OK) {
        Serial.print("[ERROR] I2S driver install failed: ");
        Serial.println(err);
        return;
    }

    // STEP 04: 套用腳位設定
    err = i2s_set_pin(I2S_PORT, &pinConfig);
    if (err != ESP_OK) {
        Serial.print("[ERROR] I2S set pin failed: ");
        Serial.println(err);
        return;
    }

    // STEP 05: 清空 DMA buffer 殘留
    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("[EMS Timer] I2S mic OK (WS=39, SCK=40, SD=38) @ 96kHz APLL");
}
#endif  // ENABLE_MIC_MONITOR
