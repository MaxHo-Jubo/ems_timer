// ============================================================
// I2C Scanner Smoke Test — DS3231 RTC + MAX17043 燃料計接線驗證
// ------------------------------------------------------------
// 用途：掃描 I2C bus，列出所有有回應的裝置位址，並標示已知裝置的缺席狀況
// 接線：依 docs/gpio-allocation.md §5.4
//   - SDA: GPIO 42（兩顆裝置共用）
//   - SCL: GPIO 41（兩顆裝置共用）
//   - GND: 共地
//   - DS3231 VCC: 3V3（不要接 5V）
//   - MAX17043 VDD: 由 LiPo 電池直接供電，板上 VCC 排針不接
//     （該排針是 VDD 外露節點而非電源輸入，詳見 docs/power-module-purchase.md §10.7）
// 預期：DS3231 出現在 0x68、MAX17043 出現在 0x36；
//       DS3231 模組上若附掛 AT24C32 EEPROM 還會多一個 0x57
// 燒錄：pio run -e i2c-scan -t upload
// 監看：pio device monitor -e i2c-scan
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// I2C bus 腳位（與 docs/gpio-allocation.md §5.4 一致，原 OLED bus 釋出後給 DS3231 與 MAX17043 共用）
static constexpr int PIN_I2C_SDA = 42;
static constexpr int PIN_I2C_SCL = 41;

// I2C 時脈：DS3231 與 MAX17043 規格上限皆為 400kHz；掃描階段先用標準 100kHz 求穩
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

// 兩輪掃描間隔（ms）
static constexpr uint32_t SCAN_INTERVAL_MS = 3000;

/**
 * 已知 I2C 裝置對照表項目：掃到時附註裝置名稱，未掃到時列入缺席清單
 */
struct KnownI2cDevice {
    uint8_t     address;      // I2C 7-bit 位址
    const char* description;  // 裝置說明，供掃描輸出與缺席清單顯示
};

// 本專案掛在 41/42 bus 上的已知裝置（位址依 docs/gpio-allocation.md §5.4）
static constexpr KnownI2cDevice KNOWN_DEVICES[] = {
    {0x36, "MAX17043 燃料計"},
    {0x57, "AT24C32 EEPROM (DS3231 模組附掛)"},
    {0x68, "DS3231 RTC"},
};

// 對照表筆數，同時作為 ScanResult 在線旗標陣列的長度與 findKnownDevice 的 not-found 哨兵
static constexpr size_t KNOWN_DEVICE_COUNT = sizeof(KNOWN_DEVICES) / sizeof(KNOWN_DEVICES[0]);

/**
 * 單輪掃描的結果
 */
struct ScanResult {
    uint8_t deviceCount;                       // 本輪 ACK 的裝置總數（含未登錄於對照表者）
    bool    knownPresent[KNOWN_DEVICE_COUNT];  // 各已知裝置是否在線，索引對應 KNOWN_DEVICES
};

/**
 * 在已知裝置對照表中查找位址
 * @param addr I2C 7-bit 位址
 * @return 對照表索引；未登錄則回傳 KNOWN_DEVICE_COUNT 作為 not-found 哨兵
 */
static size_t findKnownDevice(uint8_t addr) {
    // STEP 01: 逐筆比對位址，命中即回傳索引
    for (size_t i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        if (KNOWN_DEVICES[i].address == addr) {
            return i;
        }
    }

    // STEP 02: 對照表沒登錄這個位址
    return KNOWN_DEVICE_COUNT;
}

/**
 * 掃描 I2C bus 0x01~0x7E，逐一印出 ACK 的位址
 * @return ScanResult：裝置總數與各已知裝置的在線旗標
 */
static ScanResult scanI2cBus() {
    // STEP 01: 初始化結果（裝置數歸零、所有已知裝置預設視為不在線）
    ScanResult result = {};

    // STEP 02: 逐一對位址 0x01~0x7E 發送 START + ADDR，看是否 ACK
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        // STEP 02.01: 發起傳輸並結束（不送 data），endTransmission 回傳值 0 = ACK
        Wire.beginTransmission(addr);
        uint8_t ack = Wire.endTransmission();
        if (ack != 0) {
            continue;
        }

        // STEP 02.02: 印出位址，對照表命中時附上裝置說明並標記在線
        Serial.printf("  [FOUND] 0x%02X", addr);
        size_t knownIndex = findKnownDevice(addr);
        if (knownIndex < KNOWN_DEVICE_COUNT) {
            Serial.printf("  <- %s", KNOWN_DEVICES[knownIndex].description);
            result.knownPresent[knownIndex] = true;
        }
        Serial.println();

        // STEP 02.03: 累計本輪回應總數
        result.deviceCount++;
    }

    return result;
}

/**
 * 印出對照表中本輪沒有回應的已知裝置，用於區分「整條 bus 不通」與「單一裝置沒接上」
 * @param result 本輪掃描結果
 */
static void printMissingKnownDevices(const ScanResult& result) {
    // STEP 01: 逐筆檢查已知裝置的在線旗標
    bool headerPrinted = false;
    for (size_t i = 0; i < KNOWN_DEVICE_COUNT; i++) {
        if (result.knownPresent[i]) {
            continue;
        }

        // STEP 01.01: 首次遇到缺席者才印標題，避免全部在線時留下空段落
        if (!headerPrinted) {
            Serial.println("  已知裝置未回應（本次未接該裝置則屬正常）：");
            headerPrinted = true;
        }

        // STEP 01.02: 列出缺席裝置的位址與說明
        Serial.printf("    - 0x%02X %s\n", KNOWN_DEVICES[i].address, KNOWN_DEVICES[i].description);
    }
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
    Serial.println("I2C Scanner — DS3231 + MAX17043 接線驗證");
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

    // STEP 02: 跑一輪掃描，取得裝置總數與各已知裝置的在線狀況
    ScanResult result = scanI2cBus();

    // STEP 03: 印結果摘要
    if (result.deviceCount == 0) {
        // STEP 03.01: 整條 bus 都沒回應，列出共通排查項與各裝置的專屬檢查點
        Serial.println("  (無裝置回應)");
        Serial.println("  排查：");
        Serial.println("    1. SDA/SCL 是否反接？(SDA=42, SCL=41)");
        Serial.println("    2. 杜邦線接觸不良？");
        Serial.println("    3. DS3231：VCC 接 3V3（不可 5V）、GND 共地、模組電源 LED 有亮？");
        Serial.println("    4. MAX17043：VDD 需為真實電池電壓 3.0~4.2V、GND 與 ESP32 共地；");
        Serial.println("       板上 VCC 排針不可接 3V3（見 docs/power-module-purchase.md §10.7）");
    } else {
        // STEP 03.02: bus 有裝置回應，補列缺席的已知裝置以區分「bus 不通」與「單顆沒接上」
        Serial.printf("  共 %u 個裝置回應\n", result.deviceCount);
        printMissingKnownDevices(result);
    }

    // STEP 04: 隔 SCAN_INTERVAL_MS 再掃下一輪
    delay(SCAN_INTERVAL_MS);
}
