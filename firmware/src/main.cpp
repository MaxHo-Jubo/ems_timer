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

/** 按鈕對應事件名稱（顯示用，OLED 中字最多約 10 字元） */
static const char* BTN_LABELS[BTN_COUNT] = {
    "Epi",        // BTN1 Epinephrine
    "Atropine",   // BTN2 Atropine
    "CPR Start",  // BTN3 CPR 開始
    "CPR End",    // BTN4 CPR 結束
    "Shock",      // BTN5 電擊
    "Intubate",   // BTN6 插管
    "Arrive",     // BTN7 到院
    "Record",     // BTN8 錄音
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
    uint8_t  event_type;   // 0~7 對應按鈕索引
    uint64_t timestamp;    // epoch ms（絕對時間）
    uint32_t elapsed_ms;   // 從 session 啟動起的毫秒數
};

/** 計時模式 */
enum TimerMode : uint8_t {
    TIMER_UP   = 0,  // 正數計時（mm:ss 往上加）
    TIMER_DOWN = 1,  // 倒數計時（從 duration 往下減，結束蜂鳴 + 閃爍）
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
 * 每個 event_type 的計時配置（索引對應 BTN 順序）
 * Phase 2 驗證用預設：Epi/Atropine 倒數 3 分鐘，其餘正數計時
 * 後續要改倒數時長或新增倒數事件，直接改這張表即可
 */
static const EventConfig EVENT_CFG[BTN_COUNT] = {
    { TIMER_DOWN, 180, 60 },  // BTN1 Epi：倒數 3 分鐘，每 60 秒嗶
    { TIMER_DOWN, 180, 60 },  // BTN2 Atropine：倒數 3 分鐘，每 60 秒嗶
    { TIMER_UP,   0,   0  },  // BTN3 CPR Start：正數
    { TIMER_UP,   0,   0  },  // BTN4 CPR End：正數
    { TIMER_UP,   0,   0  },  // BTN5 Shock：正數
    { TIMER_UP,   0,   0  },  // BTN6 Intubate：正數
    { TIMER_UP,   0,   0  },  // BTN7 Arrive：正數
    { TIMER_UP,   0,   0  },  // BTN8 Record：正數
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

void handleButtons();
void recordEvent(uint8_t eventType);
void startEvent(uint8_t eventType);
void updateTimer();
void updateBeepMachine();
void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void updateDisplay();
void drawIdleScreen();
void drawTimerScreen();
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
    Serial.println("[EMS Timer] Phase 2 boot");

    // STEP 02: 初始化蜂鳴器 GPIO
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // STEP 03: 初始化所有按鈕（INPUT_PULLUP 不需外部電阻）
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 03.01: 設定腳位模式
        pinMode(BTN_PINS[i], INPUT_PULLUP);

        // STEP 03.02: 初始化按鈕狀態快取
        lastBtnState[i] = HIGH;
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
                // STEP 01.02.01.01: 記錄時間戳並觸發事件
                lastPressMs[i] = now;
                recordEvent(i);
                startEvent(i);

                Serial.print("[BTN");
                Serial.print(i + 1);
                Serial.print("] ");
                Serial.print(BTN_LABELS[i]);
                Serial.print(" recorded, total=");
                Serial.println(eventCount);
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
    events[eventCount].event_type = eventType;
    events[eventCount].timestamp  = ts;
    events[eventCount].elapsed_ms = elapsed;
    eventCount++;

    // STEP 06: 已連線 App 則即時 Notify 推送新事件
    sendEvent(eventCount - 1);
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
    const EventConfig& cfg = EVENT_CFG[currentEventIdx];

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
        triggerBeep(EXPIRE_BEEP_PULSES, EXPIRE_BEEP_ON_MS, EXPIRE_BEEP_OFF_MS);
        Serial.print("[TIMER] ");
        Serial.print(BTN_LABELS[currentEventIdx]);
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
    const EventConfig& cfg = EVENT_CFG[currentEventIdx];
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
 * 依 session 狀態繪製 OLED：待機或計時畫面
 */
void updateDisplay() {
    if (currentEventIdx < 0) {
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
    display.print(BTN_LABELS[currentEventIdx]);

    // STEP 03: 頂部右：已記錄事件數（靠右對齊）
    char countStr[12];
    snprintf(countStr, sizeof(countStr), "#%u", eventCount);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(OLED_WIDTH - w, 0);
    display.print(countStr);

    // STEP 04: 決定顯示時間（正數：elapsed；倒數：remaining；倒數結束：0 + 閃爍）
    const EventConfig& cfg = EVENT_CFG[currentEventIdx];
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
    doc["label"] = BTN_LABELS[e.event_type];
    doc["ts"]    = e.timestamp;
    doc["el"]    = e.elapsed_ms;
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
        doc["label"] = BTN_LABELS[e.event_type];
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
        // 給 App 一點時間完成 service discovery + CCCD 訂閱再送 hello
        delay(100);
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
