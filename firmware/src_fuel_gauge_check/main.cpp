// ============================================================
// MAX17043 燃料計 Quick Check — 讀數合理性驗證
// ------------------------------------------------------------
// 用途：讀出 MAX17043 的電池電壓與電量百分比，與電表實測值比對，
//       確認晶片 VDD 吃到的是真實電池電壓，而非誤接的穩壓 3.3V
// 前提：I2C 接線已由 src_i2c_scan 驗證通過（位址 0x36 有回應）
// 接線：依 docs/power-module-purchase.md §10.7
//   - SDA: GPIO 42 / SCL: GPIO 41 / GND 與 ESP32 共地
//   - VDD 由 LiPo 電池直接供電，板上 VCC 排針不接
// 判讀：電壓應落在 3.0~4.2V，且與電表量到的電池電壓相近（差距 < 0.05V）
// 燒錄：pio run -e fuel-gauge-check -t upload
// 監看：pio device monitor -e fuel-gauge-check
// ============================================================

#include <Arduino.h>
#include <Wire.h>

// I2C bus 腳位（與 docs/gpio-allocation.md §5.4 一致）
static constexpr int PIN_I2C_SDA = 42;
static constexpr int PIN_I2C_SCL = 41;

// I2C 時脈：MAX17043 規格上限 400kHz；驗證階段沿用 i2c-scan 的 100kHz 求穩
static constexpr uint32_t I2C_CLOCK_HZ = 100000;

// MAX17043 I2C 7-bit 位址（docs/gpio-allocation.md §5.4）
static constexpr uint8_t MAX17043_I2C_ADDR = 0x36;

// 暫存器位址（MAX17043 datasheet Table 1. Register Summary）
static constexpr uint8_t REG_VCELL   = 0x02;  // 電池電壓 A/D 讀值
static constexpr uint8_t REG_SOC     = 0x04;  // 電量百分比（State Of Charge）
static constexpr uint8_t REG_VERSION = 0x08;  // 晶片產品版本，用於確認通訊正常

// VCELL 為 12-bit A/D 結果，資料位於 bits 15:4，取值需右移 4 位
static constexpr uint8_t VCELL_SHIFT_BITS = 4;

// MAX17043 的 VCELL 解析度：每 LSB 代表 1.25mV
// ⚠️ 換成 MAX17048/49 時此值為 78.125µV，公式必須同步改
static constexpr float VCELL_LSB_MV = 1.25f;

// SOC 暫存器低位元組為小數部分，每 LSB 代表 1/256 %
static constexpr float SOC_LSB_PERCENT = 1.0f / 256.0f;

// LiPo 單節合理電壓範圍（mV）：低於下限視為過放或 VDD 未吃到電池，高於上限視為異常
static constexpr float LIPO_MIN_MV = 3000.0f;
static constexpr float LIPO_MAX_MV = 4200.0f;

// 兩輪讀取間隔（ms）
static constexpr uint32_t CHECK_INTERVAL_MS = 2000;

/**
 * 讀取 MAX17043 的一個 16-bit 暫存器（MSB first）
 * @param regAddr  暫存器位址（REG_VCELL / REG_SOC / REG_VERSION）
 * @param outValue 讀取成功時寫入該暫存器的 16-bit 原始值
 * @return true = 讀取成功；false = I2C 無 ACK 或回傳位元組數不足
 */
static bool readRegister16(uint8_t regAddr, uint16_t& outValue) {
    // STEP 01: 送出要讀的暫存器位址，以 repeated START 保持 bus 不放開
    Wire.beginTransmission(MAX17043_I2C_ADDR);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    // STEP 02: 要求 2 bytes，數量不足代表讀取未完成，不可拿半筆資料當結果
    if (Wire.requestFrom(MAX17043_I2C_ADDR, static_cast<uint8_t>(2)) != 2) {
        return false;
    }

    // STEP 03: 依 MSB first 組成 16-bit 值
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    outValue = (static_cast<uint16_t>(msb) << 8) | lsb;

    return true;
}

/**
 * 將 VCELL 原始暫存器值換算為電池電壓（mV）
 * @param rawVcell REG_VCELL 的 16-bit 原始值
 * @return 電池電壓，單位 mV
 */
static float vcellToMillivolts(uint16_t rawVcell) {
    // STEP 01: 取出高 12 bit 的 A/D counts，再乘上每 LSB 代表的電壓
    return static_cast<float>(rawVcell >> VCELL_SHIFT_BITS) * VCELL_LSB_MV;
}

/**
 * 將 SOC 原始暫存器值換算為電量百分比
 * @param rawSoc REG_SOC 的 16-bit 原始值（高位元組為整數 %，低位元組為 1/256 %）
 * @return 電量百分比
 */
static float socToPercent(uint16_t rawSoc) {
    // STEP 01: 高位元組為整數部分
    float wholePercent = static_cast<float>(rawSoc >> 8);

    // STEP 02: 低位元組為小數部分，每 LSB 為 1/256 %
    float fractionPercent = static_cast<float>(rawSoc & 0xFF) * SOC_LSB_PERCENT;

    return wholePercent + fractionPercent;
}

/**
 * 依電壓值印出合理性判讀與對應的排查方向
 * @param millivolts vcellToMillivolts() 換算出的電池電壓（mV）
 */
static void printVoltageVerdict(float millivolts) {
    // STEP 01: 低於 LiPo 下限——電池過放，或 VDD 根本沒吃到電池電壓
    if (millivolts < LIPO_MIN_MV) {
        Serial.println("  [WARN] 電壓低於 3.0V：電池過放，或 VDD 沒接到電池正極");
        return;
    }

    // STEP 02: 高於 LiPo 上限——超出單節 LiPo 充飽電壓，接錯電源的機率高
    if (millivolts > LIPO_MAX_MV) {
        Serial.println("  [WARN] 電壓高於 4.2V：超出單節 LiPo 範圍，確認 VDD 沒接到升壓後的 5V");
        return;
    }

    // STEP 03: 落在合理範圍，但仍需與電表比對才能確認吃到的是電池而非穩壓電源
    Serial.println("  [OK] 電壓落在 3.0~4.2V 合理範圍");
    Serial.println("       請拿電表量電池 +/-，兩者差距應 < 0.05V；");
    Serial.println("       若讀數恆定在 3.3V 附近不隨電池變動，代表 VDD 誤接穩壓電源");
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
    Serial.println("MAX17043 燃料計 Quick Check");
    Serial.printf("  SDA = GPIO %d\n", PIN_I2C_SDA);
    Serial.printf("  SCL = GPIO %d\n", PIN_I2C_SCL);
    Serial.printf("  位址 = 0x%02X\n", MAX17043_I2C_ADDR);
    Serial.println("============================================");

    // STEP 03: 初始化 I2C master，指定 SDA/SCL 腳位與時脈
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);

    // STEP 04: 讀晶片版本，確認位址 0x36 真的有回應再進入輪詢
    uint16_t rawVersion = 0;
    if (readRegister16(REG_VERSION, rawVersion)) {
        Serial.printf("[READY] VERSION = 0x%04X，開始讀取\n", rawVersion);
    } else {
        Serial.println("[ERROR] 讀不到 VERSION 暫存器——先用 i2c-scan 確認 0x36 是否在線");
    }
}

void loop() {
    // STEP 01: 印分隔列與時間戳
    Serial.println();
    Serial.printf("[CHECK @ %lu ms]\n", millis());

    // STEP 02: 讀電池電壓，讀失敗就直接回報並跳過本輪判讀
    uint16_t rawVcell = 0;
    if (!readRegister16(REG_VCELL, rawVcell)) {
        Serial.println("  [ERROR] VCELL 讀取失敗（I2C 無回應）");
        delay(CHECK_INTERVAL_MS);
        return;
    }

    // STEP 03: 讀電量百分比，同樣不容忍讀失敗
    uint16_t rawSoc = 0;
    if (!readRegister16(REG_SOC, rawSoc)) {
        Serial.println("  [ERROR] SOC 讀取失敗（I2C 無回應）");
        delay(CHECK_INTERVAL_MS);
        return;
    }

    // STEP 04: 印原始值與換算結果（附 raw 供人工核對換算係數是否對得上）
    float millivolts = vcellToMillivolts(rawVcell);
    float percent    = socToPercent(rawSoc);
    Serial.printf("  VCELL  raw=0x%04X  ->  %.3f V\n", rawVcell, millivolts / 1000.0f);
    Serial.printf("  SOC    raw=0x%04X  ->  %.1f %%\n", rawSoc, percent);

    // STEP 05: 依電壓值印出合理性判讀
    printVoltageVerdict(millivolts);

    // STEP 06: 隔 CHECK_INTERVAL_MS 再讀下一輪
    delay(CHECK_INTERVAL_MS);
}
