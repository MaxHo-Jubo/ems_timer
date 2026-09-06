// ============================================================
// I2C Scanner Smoke Test — DS3231 RTC + MAX17043 燃料計接線驗證
// ------------------------------------------------------------
// 用途：掃描 I2C bus，列出所有有回應的裝置位址，並標示已知裝置的缺席狀況；
//       每輪另印 endTransmission 錯誤碼統計與掃描耗時，用來區分
//       「裝置不在（全部 NACK，整輪不到 0.1s）」與「bus 被拉住（出現逾時，每個位址卡 50ms）」；
//       開機時印 esp_reset_reason()，遇到重開迴圈可從這行分出 brownout / watchdog / panic
//       （2026-09-06 新增，動機見 tasks/todo.md T9：09-05 的鎖死症狀沒有任何錯誤碼與重開原因可查）
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
#include <esp_system.h>  // esp_reset_reason()

// I2C bus 腳位（與 docs/gpio-allocation.md §5.4 一致，原 OLED bus 釋出後給 DS3231 與 MAX17043 共用）
static constexpr int PIN_I2C_SDA = 42;
static constexpr int PIN_I2C_SCL = 41;

// I2C 時脈：DS3231 與 MAX17043 規格上限皆為 400kHz；掃描階段先用標準 100kHz 求穩
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

// 兩輪掃描間隔（ms）
static constexpr uint32_t SCAN_INTERVAL_MS = 3000;

// Wire.endTransmission() 回傳碼（arduino-esp32 Wire.cpp 註解）：
//   0=ACK、1=資料過長、2=位址 NACK、3=資料 NACK、4=其他錯誤、5=逾時
// 掃描只送位址不送資料，裝置缺席時一律回 2；回 5 代表 bus 被拉住
// （slave 抓著 SDA/SCL、地線參考跟 ESP32 不同、上拉不足），跟「裝置不在」是兩回事
static constexpr uint8_t I2C_RESULT_ACK       = 0;
static constexpr uint8_t I2C_RESULT_ADDR_NACK = 2;
static constexpr uint8_t I2C_RESULT_TIMEOUT   = 5;

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
    uint8_t  deviceCount;                       // 本輪 ACK 的裝置總數（含未登錄於對照表者）
    bool     knownPresent[KNOWN_DEVICE_COUNT];  // 各已知裝置是否在線，索引對應 KNOWN_DEVICES
    uint8_t  nackCount;                         // 回 2（位址 NACK）的位址數：bus 正常、該位址沒人
    uint8_t  timeoutCount;                      // 回 5（逾時）的位址數：bus 被拉住的直接證據
    uint8_t  otherErrorCount;                   // 回 1/3/4 的位址數：driver 回報的其他異常
    uint32_t durationMs;                        // 整輪掃描耗時（ms）；全 NACK 約 <100ms，逾時每位址 +50ms
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
 * 掃描 I2C bus 0x01~0x7E，逐一印出 ACK 的位址，並依 endTransmission 回傳碼分類統計
 * @return ScanResult：裝置總數、各已知裝置的在線旗標、錯誤碼統計與耗時
 */
static ScanResult scanI2cBus() {
    // STEP 01: 初始化結果（裝置數與各計數歸零、所有已知裝置預設視為不在線），記下起始時間
    ScanResult result = {};
    const uint32_t startMs = millis();  // 本輪掃描起點，供計算 durationMs

    // STEP 02: 逐一對位址 0x01~0x7E 發送 START + ADDR，依回傳碼分類
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        // STEP 02.01: 發起傳輸並結束（不送 data），取回 endTransmission 回傳碼
        Wire.beginTransmission(addr);
        const uint8_t code = Wire.endTransmission();  // 0=ACK、2=NACK、5=逾時、其他見常數註解

        // STEP 02.02: 非 ACK 的分三類計數；NACK 是常態不印，逾時與其他錯誤逐筆印出位址與回傳碼
        if (code == I2C_RESULT_ADDR_NACK) {
            // STEP 02.02.01: 位址 NACK = 該位址沒人，只計數
            result.nackCount++;
            continue;
        }
        if (code != I2C_RESULT_ACK) {
            // STEP 02.02.02: 逾時或其他錯誤碼，分別計數並印出位址供對照
            const bool timedOut = (code == I2C_RESULT_TIMEOUT);  // 5 = 逾時；其餘回傳碼視為 driver 其他錯誤
            if (timedOut) {
                result.timeoutCount++;
            } else {
                result.otherErrorCount++;
            }
            Serial.printf("  [%s] 0x%02X code=%u\n", timedOut ? "TIMEOUT" : "ERR", addr, code);
            continue;
        }

        // STEP 02.03: ACK：印出位址，對照表命中時附上裝置說明並標記在線
        Serial.printf("  [FOUND] 0x%02X", addr);
        size_t knownIndex = findKnownDevice(addr);
        if (knownIndex < KNOWN_DEVICE_COUNT) {
            Serial.printf("  <- %s", KNOWN_DEVICES[knownIndex].description);
            result.knownPresent[knownIndex] = true;
        }
        Serial.println();

        // STEP 02.04: 累計本輪回應總數
        result.deviceCount++;
    }

    // STEP 03: 記錄整輪耗時
    result.durationMs = millis() - startMs;

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

/**
 * 印出本輪錯誤碼統計與耗時，並依 bus 狀態選擇要印的判讀與排查清單。
 * 這是唯一判定 bus 狀態的地方：loop() 只印有回應的裝置，不另行推斷，
 * 否則「bus 被拉住」時會先看到接線排查清單、再看到「不是裝置缺席」，自相矛盾。
 * @param result 本輪掃描結果
 */
static void printScanDiagnostics(const ScanResult& result) {
    // STEP 01: 印統計與耗時（人看判讀，也給事後比對 log 用）
    Serial.printf("  耗時 %lu ms；NACK %u、逾時 %u、其他錯誤 %u\n",
                  (unsigned long)result.durationMs,
                  result.nackCount, result.timeoutCount, result.otherErrorCount);

    // STEP 02: 有逾時就是 bus 被拉住，直接指向量測步驟；不印接線排查清單（那是缺席用的）
    if (result.timeoutCount > 0) {
        Serial.println("  判讀：bus 被拉住（逾時），不是裝置缺席。");
        Serial.println("        量 SDA/SCL 對 ESP32 GND 的閒置電位、ESP32 GND 對電池負極的偏移，");
        Serial.println("        再依序拔掉單一裝置重掃（步驟見 tasks/todo.md T9）");
        return;
    }

    // STEP 03: 沒逾時但有其他錯誤碼：driver 層異常，先看 code 值再說
    if (result.otherErrorCount > 0) {
        Serial.println("  判讀：driver 回報非 NACK/逾時的錯誤碼，bus 狀態異常，對照上方 [ERR] 列");
        return;
    }

    // STEP 04: 全部 NACK：bus 本身正常，純粹沒裝置回應——這時才輪到接線排查清單
    if (result.deviceCount == 0) {
        Serial.println("  判讀：bus 正常運作（全部 NACK、掃描快速），只是沒有裝置回應");
        Serial.println("  排查：");
        Serial.println("    1. SDA/SCL 是否反接？(SDA=42, SCL=41)");
        Serial.println("    2. 杜邦線接觸不良？");
        Serial.println("    3. DS3231：VCC 接 3V3（不可 5V）、GND 共地、模組電源 LED 有亮？");
        Serial.println("    4. MAX17043：VDD 需為真實電池電壓 3.0~4.2V、GND 與 ESP32 共地；");
        Serial.println("       板上 VCC 排針不可接 3V3（見 docs/power-module-purchase.md §10.7）");
    }
}

/**
 * 把 esp_reset_reason() 轉成可讀名稱，供開機 banner 列印
 * @param reason esp_reset_reason() 的回傳值
 * @return 對應的英文名稱字串；未列舉的值回 "OTHER"
 */
static const char* resetReasonName(esp_reset_reason_t reason) {
    // STEP 01: 逐一對應 IDF 4.4 esp_system.h 的列舉值（只列本工具關心的診斷類別）
    switch (reason) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_EXT:      return "EXT";
        case ESP_RST_SW:       return "SW";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default:               return "UNKNOWN";  // 含 ESP_RST_UNKNOWN 與未列舉值，原始數字另有印出
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
    // 重開迴圈時這行是唯一能分出 brownout / watchdog / panic 的證據（ROM 的 rst:0x.. 分不出）
    const esp_reset_reason_t resetReason = esp_reset_reason();  // 本次開機的重置原因
    Serial.printf("  Reset reason = %s (%d)\n", resetReasonName(resetReason), (int)resetReason);
    Serial.println("============================================");

    // STEP 03: 初始化 I2C master，指定 SDA/SCL 腳位與時脈
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    Serial.println("[READY] setup 完成，開始掃描...");
}

void loop() {
    // STEP 01: 印分隔列與時間戳
    Serial.println();
    Serial.printf("[SCAN @ %lu ms]\n", millis());

    // STEP 02: 跑一輪掃描，取得裝置總數、各已知裝置的在線狀況與錯誤碼統計
    ScanResult result = scanI2cBus();

    // STEP 03: 有裝置回應時印總數與缺席的已知裝置；沒回應的情況不在這裡下判斷
    if (result.deviceCount > 0) {
        Serial.printf("  共 %u 個裝置回應\n", result.deviceCount);
        printMissingKnownDevices(result);
    }

    // STEP 04: 錯誤碼統計與 bus 狀態判讀（唯一判定點，缺席時的排查清單也由它印）
    printScanDiagnostics(result);

    // STEP 05: 隔 SCAN_INTERVAL_MS 再掃下一輪
    delay(SCAN_INTERVAL_MS);
}
