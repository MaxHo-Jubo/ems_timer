// ============================================================
// DS3231 RTC Demo — 設時間、讀時間、讀溫度
// ------------------------------------------------------------
// 用途：
//   1. 首次上電（或電池失憶）自動用編譯時間初始化 RTC
//   2. 每秒讀一次當前時間 + 板載溫度感測器
//   3. 驗證電池備援：拔 USB → 等幾秒 → 重新上電，時間應持續走（不歸零）
//
// 接線：依 docs/gpio-allocation.md §5.4
//   - SDA: GPIO 42 / SCL: GPIO 41 / VCC: 3V3 / GND: GND
//
// 燒錄：pio run -e rtc-demo -t upload
// 監看：pio device monitor -e rtc-demo
//
// ⚠️ 強制重設時間：
//   按下「強制設時鈕」（GPIO 0，板載 BOOT 鍵）開機 → setup 內偵測到 LOW
//   就會用編譯時間覆寫 RTC。一般使用時不要按 BOOT。
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

// I2C bus 腳位（與 docs/gpio-allocation.md §5.4 一致）
static constexpr int PIN_I2C_SDA = 42;
static constexpr int PIN_I2C_SCL = 41;

// 強制重設時間用的板載 BOOT 鍵（開機時按住 = 用編譯時間覆寫 RTC）
static constexpr int PIN_FORCE_SET_TIME = 0;

// 主迴圈印一次的間隔（ms）
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;

// 全域 RTC 物件（RTClib 內部走 Wire）
RTC_DS3231 rtc;

/**
 * 用編譯時間（__DATE__ / __TIME__）設定 RTC
 * 注意：此時間是「燒錄主機編譯當下」的時間，與實際燒錄/上電時間有 1~30 秒誤差
 *       Dev-Phase 3 整合後改由 App 連線下發 epoch ms，這裡只是 demo 階段的方便手段
 */
static void setRtcFromCompileTime() {
    DateTime compileTime(F(__DATE__), F(__TIME__));
    rtc.adjust(compileTime);
    Serial.print("[RTC] 已用編譯時間覆寫: ");
    Serial.print(compileTime.timestamp(DateTime::TIMESTAMP_FULL));
    Serial.println();
}

/**
 * 印一筆時間 + 溫度
 */
static void printRtcSnapshot() {
    // STEP 01: 讀當下時間
    DateTime now = rtc.now();

    // STEP 02: 讀 DS3231 內建溫度感測器（解析度 0.25°C，用於補償晶振，但也可外讀）
    float tempC = rtc.getTemperature();

    // STEP 03: 印 ISO 風格時間戳 + 溫度 + uptime
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    Serial.printf("[RTC] %s  temp=%.2f°C  uptime=%lus\n",
                  buf, tempC, (unsigned long)(millis() / 1000));
}

void setup() {
    // STEP 01: 啟動 USB-CDC Serial，等列舉（ESP32-S3 native USB 前 1-2 秒會吃掉 println）
    Serial.begin(115200);
    uint32_t waitStart = millis();
    while (!Serial && millis() - waitStart < 3000) {
        delay(10);
    }

    Serial.println();
    Serial.println("============================================");
    Serial.println("DS3231 RTC Demo");
    Serial.printf("  SDA = GPIO %d / SCL = GPIO %d\n", PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.println("============================================");

    // STEP 02: 設定 BOOT 鍵為輸入（用於強制重設時間判斷）
    pinMode(PIN_FORCE_SET_TIME, INPUT_PULLUP);

    // STEP 03: 啟動 I2C bus 並初始化 RTC
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    if (!rtc.begin()) {
        Serial.println("[FATAL] DS3231 找不到（0x68 無回應）");
        Serial.println("        檢查接線：SDA=42, SCL=41, VCC=3V3, GND");
        while (true) {
            delay(1000);  // STEP 03.01: 卡住等使用者修
        }
    }
    Serial.println("[OK] DS3231 初始化成功");

    // STEP 04: 判斷是否需要覆寫時間
    bool forceSet = (digitalRead(PIN_FORCE_SET_TIME) == LOW);
    bool lostPower = rtc.lostPower();

    if (forceSet) {
        // STEP 04.01: 使用者開機時按住 BOOT，強制用編譯時間覆寫
        Serial.println("[RTC] 偵測到 BOOT 鍵按住 → 強制重設時間");
        setRtcFromCompileTime();
    } else if (lostPower) {
        // STEP 04.02: RTC 電池失憶（首次上電 / 電池沒電 / 電池沒裝），自動初始化
        Serial.println("[RTC] 偵測到電池失憶 → 用編譯時間初始化");
        setRtcFromCompileTime();
    } else {
        // STEP 04.03: 正常情況，沿用 RTC 內既有時間（驗證電池備援）
        Serial.println("[RTC] 電池備援正常，沿用既有時間");
    }

    Serial.println("[READY] 開始每秒列印時間...");
    Serial.println();
}

void loop() {
    // STEP 01: 每秒印一次當下 RTC 狀態
    printRtcSnapshot();

    // STEP 02: 等下一輪
    delay(PRINT_INTERVAL_MS);
}
