// EMS DoseSync Pro — Impl-Phase H：燃料計無硬體降級實作
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3.1（檔案定義）、§3.2（掛載時機）
// 測試入口：firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
//
// 用途：
//   I2C probe 沒偵測到 0x36 時掛上 NullFuelGauge，讓上層無條件呼叫不需分支。
//     is_present() → false
//     read()       → {valid=false, 0, 0}（UI 端據此不畫圖示）

#pragma once

#include "ems_fuel_gauge.h"

namespace ems {

/**
 * 無硬體時的燃料計降級實作。所有方法回降級值。
 */
class NullFuelGauge : public FuelGaugeBackend {
public:
    /**
     * 是否有實體燃料計在線。NullFuelGauge 永遠不在線。
     * @return false，表示燃料計不可用
     */
    bool is_present() const override { return false; }

    /**
     * 讀取當前電壓與電量。NullFuelGauge 回無效讀值。
     * @return {valid=false, 0, 0}，告訴 caller 讀取失敗，不可使用 millivolts / percent
     */
    FuelReading read() override { return {false, 0, 0}; }
};

}  // namespace ems
