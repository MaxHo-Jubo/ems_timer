// 本檔只在 Arduino 環境編譯，native 測試環境排除
#ifdef ARDUINO

#include "max17043_backend.h"

#include "fuel_gauge_logic.h"

namespace ems {

bool Max17043Backend::begin(TwoWire& wire) {
    // STEP 01: 記住 bus，後續讀取都走它
    wire_ = &wire;

    // STEP 02: 讀 VERSION 當 probe，讀得到才視為在線
    uint16_t version = 0;  // 探測用：只看讀取是否成功，值本身不比對（未做型號校驗）
    present_ = read_register16(Max17043Backend::REG_VERSION, version);

    return present_;
}

FuelReading Max17043Backend::read() {
    // STEP 01: 不在線直接回無效，不可回 0 讓上層誤判為沒電
    if (!present_ || wire_ == nullptr) {
        return FuelReading();
    }

    // STEP 02: 讀電壓，失敗即整筆無效（不拿半筆資料當結果）
    uint16_t raw_vcell = 0;  // VCELL 暫存器原始值
    if (!read_register16(Max17043Backend::REG_VCELL, raw_vcell)) {
        return FuelReading();
    }

    // STEP 03: 讀電量，同樣不容忍部分失敗
    uint16_t raw_soc = 0;  // SOC 暫存器原始值
    if (!read_register16(Max17043Backend::REG_SOC, raw_soc)) {
        return FuelReading();
    }

    // STEP 04: 判定與換算交給純邏輯層——本檔在 native 編不到，
    //          合理性判定留在這裡就沒有任何測試守得住
    return make_reading(raw_vcell, raw_soc);
}

bool Max17043Backend::read_register16(uint8_t reg, uint16_t& out_value) {
    // STEP 01: 送出暫存器位址，以 repeated START 保持 bus
    wire_->beginTransmission(Max17043Backend::I2C_ADDR);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) {
        // endTransmission 非 0（無 ACK／匯流排逾時／其他通訊錯誤）視為失敗
        return false;
    }

    // STEP 02: 要求 2 bytes，數量不足代表讀取未完成
    if (wire_->requestFrom(Max17043Backend::I2C_ADDR, static_cast<uint8_t>(2)) != 2) {
        return false;
    }

    // STEP 03: MSB first 組成 16-bit
    const uint8_t msb = static_cast<uint8_t>(wire_->read());  // 高位元組
    const uint8_t lsb = static_cast<uint8_t>(wire_->read());  // 低位元組
    out_value = (static_cast<uint16_t>(msb) << 8) | lsb;

    return true;
}

}  // namespace ems

#endif  // ARDUINO
