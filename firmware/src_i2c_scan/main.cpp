// ============================================================
// I2C Scanner Smoke Test — DS3231 RTC 接線驗證
// ------------------------------------------------------------
// 用途：掃描 I2C bus，列出所有有回應的裝置位址
// 接線：依 docs/gpio-allocation.md §5.4
//   - SDA: GPIO 42
//   - SCL: GPIO 41
//   - VCC: 3V3（不要接 5V）
//   - GND: GND
// 預期：DS3231 出現在 0x68；模組上若有 AT24C32 EEPROM 還會多一個 0x57
// 燒錄：pio run -e i2c-scan -t upload
// 監看：pio device monitor -e i2c-scan
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// I2C bus 腳位（與 docs/gpio-allocation.md §5.4 一致，原 OLED bus 釋出後給 DS3231）
static constexpr int PIN_I2C_SDA = 42;
static constexpr int PIN_I2C_SCL = 41;

// I2C 時脈：DS3231 規格上限 400kHz；掃描階段先用標準 100kHz 求穩
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

// 兩輪掃描間隔（ms）
static constexpr uint32_t SCAN_INTERVAL_MS = 3000;

/**
 * 掃描 I2C bus 0x01~0x7E，列出所有 ACK 的位址
 * @return 偵測到的裝置數量
 */
static uint8_t scanI2cBus() {
    // STEP 01: 累計 ACK 的裝置數
    uint8_t deviceCount = 0;

    // STEP 02: 逐一對位址 0x01~0x7E 發送 START + ADDR，看是否 ACK
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        // STEP 02.01: 發起傳輸並結束（不送 data），endTransmission 回傳值 0 = ACK
        Wire.beginTransmission(addr);
        uint8_t result = Wire.endTransmission();

        // STEP 02.02: 有回應就印出位址與已知裝置註解
        if (result == 0) {
            Serial.printf("  [FOUND] 0x%02X", addr);
            if (addr == 0x68) {
                Serial.print("  <- DS3231 RTC (預期)");
            } else if (addr == 0x57) {
                Serial.print("  <- AT24C32 EEPROM (DS3231 模組附掛，可選)");
            }
            Serial.println();
            deviceCount++;
        }
    }

    return deviceCount;
}

void setup() {
    // STEP 01: 啟動 USB-CDC Serial，並等列舉完成
    //          （ESP32-S3 native USB 開機前 1-2 秒會吃掉 println，必須等 host attach）
    Serial.begin(115200);
    uint32_t waitStart = millis();
    while (!Serial && millis() - waitStart < 3000) {
        delay(10);
    }

    // STEP 02: 印 banner（attach 完成後才安全列印）
    Serial.println();
    Serial.println("============================================");
    Serial.println("I2C Scanner — DS3231 接線驗證");
    Serial.printf("  SDA = GPIO %d\n", PIN_I2C_SDA);
    Serial.printf("  SCL = GPIO %d\n", PIN_I2C_SCL);
    Serial.printf("  Clock = %lu Hz\n", (unsigned long)I2C_CLOCK_HZ);
    Serial.println("============================================");

    // STEP 03: 初始化 I2C master，指定 SDA/SCL 腳位與時脈
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    Serial.println("[READY] setup 完成，開始掃描...");
}

void loop() {
    // STEP 01: 印分隔列與時間戳
    Serial.println();
    Serial.printf("[SCAN @ %lu ms]\n", millis());

    // STEP 02: 跑一輪掃描，取得偵測到的裝置數
    uint8_t found = scanI2cBus();

    // STEP 03: 印結果摘要
    if (found == 0) {
        Serial.println("  (無裝置回應)");
        Serial.println("  排查：");
        Serial.println("    1. VCC/GND 接好？模組電源 LED 有亮？");
        Serial.println("    2. SDA/SCL 是否反接？(SDA=42, SCL=41)");
        Serial.println("    3. DS3231 電池有裝且 >2.8V？");
        Serial.println("    4. 杜邦線接觸不良？");
    } else {
        Serial.printf("  共 %u 個裝置回應\n", found);
    }

    // STEP 04: 隔 SCAN_INTERVAL_MS 再掃下一輪
    delay(SCAN_INTERVAL_MS);
}
