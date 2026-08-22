// EMS DoseSync Pro — Impl-Phase H：MAX17043 燃料計 I2C backend
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3
// 接線依據：docs/power-module-purchase.md §10.7（VDD 由電池供電，VCC 排針不接）
// 硬體驗收：docs/power-module-purchase.md §10.8
//
// 本檔是本 lib 唯一碰硬體的檔案，換算邏輯一律呼叫 fuel_gauge_logic。
// 整個 class 用 #ifdef ARDUINO 守護：native 編譯環境可安全 #include 本檔（宣告被忽略），
// 避免 Wire.h 找不到直接失敗。

#pragma once

#include "ems_fuel_gauge.h"

#ifdef ARDUINO

#include <Wire.h>

namespace ems {

/**
 * MAX17043 燃料計 I2C backend。
 *
 * 介面契約：
 * - `begin()` 非執行緒安全，只能在單一 task 呼叫（`wire_` 與 `present_` 分開賦值）
 * - `present_` 是 `begin()` 當下的快取：硬體事後物理斷線時 `is_present()` 會繼續回 `true`
 *   （但 `read()` 仍會正確回 invalid，未違反基類契約；呼叫端不該拿 `is_present()` 決定 UI）
 */
class Max17043Backend : public FuelGaugeBackend {
public:
    /** MAX17043 I2C 7-bit 位址（docs/gpio-allocation.md §5.4） */
    static constexpr uint8_t I2C_ADDR = 0x36;

    /** 暫存器位址（MAX17043 datasheet Table 1） */
    static constexpr uint8_t REG_VCELL   = 0x02;  // 電池電壓 A/D 讀值
    static constexpr uint8_t REG_SOC     = 0x04;  // 電量百分比
    static constexpr uint8_t REG_VERSION = 0x08;  // 晶片版本，用於 probe

    /**
     * 探測燃料計是否在線（讀 VERSION 暫存器）
     * @param wire 已 begin() 過的 I2C bus
     * @return true = 偵測到並可讀；false = 無回應或讀取失敗
     */
    bool begin(TwoWire& wire);

    /**
     * 是否實體燃料計在線（快取自 `begin()` 當下的 probe 結果）
     * @return true = `begin()` 時偵測到燃料計；false = 無燃料計或 `begin()` 未呼叫
     */
    bool is_present() const override { return present_; }

    /**
     * 讀取當前電壓與電量
     * @return FuelReading，valid=false 表 I2C 讀取失敗、讀值超出合理範圍或硬體不在線
     */
    FuelReading read() override;

private:
    /**
     * 讀取一個 16-bit 暫存器（MSB first）
     * @param reg       暫存器位址
     * @param out_value 成功時寫入讀到的值
     * @return true = 讀取成功；false = I2C 通訊失敗或回傳位元組數不足
     */
    bool read_register16(uint8_t reg, uint16_t& out_value);

    TwoWire* wire_   = nullptr;  // 由 begin() 注入，未 begin 前為 nullptr
    bool     present_ = false;   // probe 結果（begin() 當下的快取）
};

}  // namespace ems

#endif  // ARDUINO
