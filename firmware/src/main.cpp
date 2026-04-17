/**
 * EMS Timer — Phase 1 韌體
 *
 * 目標：確認 8 顆按鈕 + SSD1306 OLED + 蜂鳴器 + INMP441 麥克風 全部正常
 *
 * 接線（ESP32-S3-DevKitC-1 — 2026-04-17）：
 *   板子：ESP32-S3-DevKitC-1（45 GPIO），左排 / 右排分區布線。
 *   燒錄 / 序列埠：使用 UART port（CH340/CP2102 橋接）。
 *
 *   === 左側（按鈕 + SD 卡 + 蜂鳴器） ===
 *   左側上半部連續 8 支 GPIO 做按鈕（跳過 GND/3V3/RST，GPIO 1/2 在右側不用）：
 *   BTN1 Epinephrine → GPIO 4  (另一端 → GND)
 *   BTN2 Atropine    → GPIO 5  (另一端 → GND)
 *   BTN3 CPR 開始    → GPIO 6  (另一端 → GND)
 *   BTN4 CPR 結束    → GPIO 7  (另一端 → GND)
 *   BTN5 電擊        → GPIO 15 (另一端 → GND)
 *   BTN6 插管        → GPIO 16 (另一端 → GND)
 *   BTN7 到院        → GPIO 17 (另一端 → GND)
 *   BTN8 錄音        → GPIO 18 (另一端 → GND)
 *
 *   SD 卡模組（FSPI 硬體 SPI）
 *     SD VCC   → 3.3V
 *     SD GND   → GND
 *     SD CS    → GPIO 10
 *     SD MOSI  → GPIO 11
 *     SD CLK   → GPIO 12
 *     SD MISO  → GPIO 13
 *
 *   蜂鳴器 正極(紅) → GPIO 14 (PWM 可輸出)
 *   蜂鳴器 負極(黑) → GND
 *
 *   === 右側（OLED + 麥克風） ===
 *   OLED VCC → 3.3V
 *   OLED GND → GND
 *   OLED SDA → GPIO 42
 *   OLED SCL → GPIO 41   // 與 SDA 相鄰
 *
 *   INMP441 VDD → 3.3V
 *   INMP441 GND → GND
 *   INMP441 SCK → GPIO 40   // I2S 三線相鄰
 *   INMP441 WS  → GPIO 39
 *   INMP441 SD  → GPIO 38
 *   INMP441 L/R → GND (左聲道)
 *
 *   所有按鈕採 INPUT_PULLUP，按下時將腳位拉低。
 *   避開腳位：0/3/45/46 (strap)、19/20 (USB D-/D+)、43/44 (UART0 TX/RX)、26-32 (in-package flash)。
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/**
 * Phase 1 驗收麥克風 I2S 通訊已通過，但 INMP441 模組靈敏度可疑（sleep/wake 循環），
 * 暫停麥克風偵測以免干擾 OLED 按鈕畫面。
 * Phase 1.5 換新模組時把 0 改 1，重新啟用 I2S 初始化 + loop 讀取 + OLED 音量條。
 */
#define ENABLE_MIC_MONITOR 0

#if ENABLE_MIC_MONITOR
#include <driver/i2s.h>
#endif

// --- 硬體常數 ---

/** OLED 解析度 */
static const uint8_t OLED_WIDTH  = 128;
static const uint8_t OLED_HEIGHT = 64;

/** I2C reset pin，SSD1306 不需要硬體 reset 時設 -1 */
static const int8_t OLED_RESET_PIN = -1;

/** SSD1306 預設 I2C 位址（7-bit = 0x3C） */
static const uint8_t OLED_I2C_ADDR = 0x3C;

/** I2C 腳位（S3 右側：SDA/SCL 相鄰 42/41） */
static const uint8_t I2C_SDA_PIN = 42;
static const uint8_t I2C_SCL_PIN = 41;

/** 蜂鳴器 GPIO（S3 左側） */
static const uint8_t BUZZER_PIN = 14;

#if ENABLE_MIC_MONITOR
/** I2S 麥克風腳位（INMP441；S3 右側：SCK/WS/SD 三線相鄰 40/39/38） */
static const i2s_port_t I2S_PORT    = I2S_NUM_0;
static const uint8_t    I2S_WS_PIN  = 39;  // Word Select (LRCK)
static const uint8_t    I2S_SCK_PIN = 40;  // Bit Clock
static const uint8_t    I2S_SD_PIN  = 38;  // Data
#endif

/** SD 卡腳位（FSPI 硬體 SPI，Phase 1 尚未使用，僅保留常數） */
static const uint8_t SD_CS_PIN   = 10;
static const uint8_t SD_MOSI_PIN = 11;
static const uint8_t SD_CLK_PIN  = 12;
static const uint8_t SD_MISO_PIN = 13;

#if ENABLE_MIC_MONITOR
/** I2S 讀取緩衝區大小（樣本數） */
static const uint16_t I2S_BUFFER_LEN = 256;
#endif

/** 按鈕數量 */
static const uint8_t BTN_COUNT = 8;

/** 按鈕 GPIO 腳位（INPUT_PULLUP，按下接 GND）
 *  GOOUUU S3 左側上半部連續 8 支：BTN1~8 對應 GPIO 4/5/6/7/15/16/17/18
 *  （GPIO 1/2 在板子右側，跟 OLED 同側，不採用） */
static const uint8_t BTN_PINS[BTN_COUNT] = { 4, 5, 6, 7, 15, 16, 17, 18 };

/** 按鈕對應事件名稱（顯示用，最多 10 字元） */
static const char* BTN_LABELS[BTN_COUNT] = {
    "Epi",       // BTN1 Epinephrine
    "Atropine",  // BTN2 Atropine
    "CPR Start", // BTN3 CPR 開始
    "CPR End",   // BTN4 CPR 結束
    "Shock",     // BTN5 電擊
    "Intubate",  // BTN6 插管
    "Arrive",    // BTN7 到院
    "Record",    // BTN8 錄音
};

/** 按鈕 debounce 時間（ms），救護現場防誤觸 */
static const uint16_t DEBOUNCE_MS = 200;

// --- 全域物件與狀態 ---

/** SSD1306 OLED 顯示器物件 */
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

/** 各按鈕上次有效按下的時間戳（用於 debounce） */
static uint32_t lastPressMs[BTN_COUNT] = { 0 };

/** 各按鈕上一次讀到的狀態（INPUT_PULLUP：HIGH = 未按，LOW = 按下） */
static uint8_t lastBtnState[BTN_COUNT];

/** 最後一次被按下的按鈕索引（-1 = 尚未按過） */
static int8_t lastPressedIdx = -1;

/** 各按鈕按下次數（debug 用） */
static uint32_t pressCounts[BTN_COUNT] = { 0 };

#if ENABLE_MIC_MONITOR
/** I2S 讀取緩衝區 */
static int32_t i2sBuffer[256];

/** 上次印出麥克風數值的時間戳 */
static uint32_t lastMicPrintMs = 0;
#endif

// --- 函式宣告 ---
void updateDisplay();
#if ENABLE_MIC_MONITOR
void updateMicDisplay(int32_t peak);
void setupI2S();
#endif

/**
 * 初始化 I2C、OLED 與所有按鈕
 */
void setup() {
    // STEP 01: 初始化序列埠
    Serial.begin(115200);
    Serial.println("[EMS Timer] Phase 1 boot");

    // STEP 02: 初始化蜂鳴器
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // STEP 03: 初始化所有按鈕，INPUT_PULLUP 不需外部電阻
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 02.01: 設定腳位模式
        pinMode(BTN_PINS[i], INPUT_PULLUP);

        // STEP 02.02: 初始化按鈕狀態快取
        lastBtnState[i] = HIGH;
    }

    // STEP 03: 初始化 I2C，指定 SDA/SCL 腳位
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // STEP 04: 初始化 OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        // STEP 04.01: 初始化失敗 → 停在這裡並持續輸出錯誤
        Serial.println("[ERROR] OLED init failed. Check wiring (SDA=42, SCL=41).");
        while (true) {
            delay(1000);
        }
    }

    // STEP 05: 顯示開機畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(10, 8);
    display.println("EMS Timer");

    display.setTextSize(1);
    display.setCursor(20, 38);
    display.println("Phase 1 Ready");

    display.setCursor(10, 52);
    display.println("Press any button");

    display.display();

    Serial.println("[EMS Timer] OLED OK. Waiting for button press...");

    // STEP 08: 蜂鳴器開機測試 — 嗶兩聲確認有聲音
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
    }

#if ENABLE_MIC_MONITOR
    // STEP 09: 初始化 I2S 麥克風（Phase 1.5 啟用）
    setupI2S();
#endif
}

/**
 * 主迴圈：輪詢 8 顆按鈕，偵測按下事件
 */
void loop() {
    // STEP 01: 輪詢所有按鈕
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        // STEP 01.01: 讀取當前按鈕狀態
        uint8_t currentState = digitalRead(BTN_PINS[i]);

        // STEP 01.02: 偵測下降緣（HIGH → LOW = 按下）
        if (lastBtnState[i] == HIGH && currentState == LOW) {
            // STEP 01.02.01: 計算距上次有效按下的時間
            uint32_t now = millis();

            if (now - lastPressMs[i] >= DEBOUNCE_MS) {
                // STEP 01.02.01.01: 有效按下 → 更新計數與時間戳
                pressCounts[i]++;
                lastPressMs[i] = now;
                lastPressedIdx = i;

                Serial.print("[BTN");
                Serial.print(i + 1);
                Serial.print("] ");
                Serial.print(BTN_LABELS[i]);
                Serial.print(" #");
                Serial.println(pressCounts[i]);

                // STEP 01.02.01.02: 更新 OLED 顯示
                updateDisplay();
            }
        }

        // STEP 01.03: 儲存本次狀態供下一輪比對
        lastBtnState[i] = currentState;
    }

#if ENABLE_MIC_MONITOR
    // STEP 02: 每 250ms 讀一次麥克風並印出峰值（Phase 1.5 啟用；搭配 dma_buf_count=2 穩定版）
    uint32_t now = millis();
    if (now - lastMicPrintMs >= 250) {
        lastMicPrintMs = now;

        // STEP 02.01: 從 I2S 讀取樣本
        size_t bytesRead = 0;
        i2s_read(I2S_PORT, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);

        // STEP 02.02: 找出最大絕對值（峰值振幅）
        int32_t peak = 0;
        uint16_t samplesRead = bytesRead / sizeof(int32_t);
        for (uint16_t i = 0; i < samplesRead; i++) {
            // STEP 02.02.01: INMP441 資料在高 24 bit，右移 8 位取有效值
            int32_t sample = i2sBuffer[i] >> 8;
            if (sample < 0) { sample = -sample; }
            if (sample > peak) { peak = sample; }
        }

        Serial.print("[MIC] peak=");
        Serial.println(peak);

        // STEP 02.03: 更新 OLED 顯示麥克風音量
        updateMicDisplay(peak);
    }
#endif
}

/**
 * 更新 OLED：顯示最後按下的按鈕名稱與按下次數
 */
void updateDisplay() {
    // STEP 01: 清除畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 02: 第一行標題
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("EMS Timer - Phase 1");

    if (lastPressedIdx < 0) {
        // STEP 03: 尚未按過任何按鈕
        display.setCursor(10, 30);
        display.println("Press any button");
    } else {
        // STEP 04: 大字顯示按鈕編號
        display.setTextSize(4);
        display.setCursor(20, 18);
        display.print("BTN");
        display.println(lastPressedIdx + 1);

        // STEP 05: 顯示 GPIO 與按下次數
        display.setTextSize(1);
        display.setCursor(0, 54);
        display.print("GPIO ");
        display.print(BTN_PINS[lastPressedIdx]);
        display.print("  #");
        display.println(pressCounts[lastPressedIdx]);
    }

    // STEP 06: 送出畫面緩衝區到 OLED
    display.display();
}

#if ENABLE_MIC_MONITOR
/**
 * 更新 OLED：顯示麥克風音量 bar
 *
 * @param peak 本次讀取的峰值振幅（0 ~ 8388608）
 */
void updateMicDisplay(int32_t peak) {
    /** INMP441 24-bit 最大值，用於換算 bar 寬度 */
    static const int32_t MIC_MAX = 8388608;

    // STEP 01: 清除畫面
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // STEP 02: 標題
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("MIC LEVEL");

    // STEP 03: 將 peak 對應到 bar 寬度（0 ~ 128 pixel）
    int16_t barWidth = (int16_t)((int64_t)peak * 128 / MIC_MAX);
    if (barWidth > 128) { barWidth = 128; }

    // STEP 04: 畫外框
    display.drawRect(0, 20, 128, 20, SSD1306_WHITE);

    // STEP 05: 填入音量 bar
    if (barWidth > 0) {
        display.fillRect(0, 20, barWidth, 20, SSD1306_WHITE);
    }

    // STEP 06: 顯示數值
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("peak: ");
    display.println(peak);

    // STEP 07: 送出畫面緩衝區
    display.display();
}

/**
 * 初始化 I2S 介面，設定為 INMP441 麥克風輸入模式
 */
void setupI2S() {
    // STEP 01: 設定 I2S 驅動參數
    i2s_config_t i2sConfig = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        // 開高取樣率到 96k，BCLK = 96k × 32 × 2 = 6.144 MHz，遠超 INMP441 normal mode 門檻，減少 BCLK jitter 邊緣問題
        .sample_rate          = 96000,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        // ESP32-S3 legacy driver 的 slot 順序跟 ESP32 顛倒：L/R 接 GND 時要用 ONLY_RIGHT 才讀到 INMP441 資料
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        // DMA ring 只保留 2 塊避免舊資料堆積（讀到的永遠接近當下），128 sample × 96k = 1.3ms/塊
        .dma_buf_count        = 2,
        .dma_buf_len          = 128,
        // 開 APLL 降低 BCLK jitter（S3 APLL 對 audio rate 仍有效）
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

    // STEP 05: 清空 DMA buffer 的初始化殘留
    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("[EMS Timer] I2S mic OK (WS=39, SCK=40, SD=38) @ 96kHz APLL");
}
#endif  // ENABLE_MIC_MONITOR
