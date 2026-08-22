// EMS DoseSync Pro — Impl-Phase H：燃料計抽象介面
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3.1
// 測試入口：firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
//
// 用途：
//   同一份韌體支援有/無 MAX17043 兩種硬體配置。caller 拿 FuelGaugeBackend*
//   不分支處理，讀不到時 valid=false，UI 端據此完全不畫電量圖示。
//
// 兩種具體實作：
//   - Max17043Backend：實體燃料計（I2C 0x36，硬體相依，Task 5 實作完成）
//   - NullFuelGauge  ：無燃料計時的降級實作（本檔同 lib）

#pragma once

#include <cstdint>

namespace ems {

/**
 * 燃料計單次讀取結果。
 *
 * valid=false 時 millivolts 與 percent 均無意義，caller 不可拿來顯示。
 * 特別注意不可把 percent=0 當作「沒電」——0% 是合法讀數，「讀不到」要看 valid。
 */
struct FuelReading {
    bool     valid;       // 是否讀取成功
    uint16_t millivolts;  // 電池電壓（mV），valid=true 時有效
    uint8_t  percent;     // 電量百分比 0~100，valid=true 時有效

    /**
     * @param v  是否讀取成功；預設 false，讓忘記初始化的宣告收斂到「不可信」而非 UB
     * @param mv 電池電壓（mV）
     * @param p  電量百分比 0~100
     *
     * 用 constructor 而非 default member initializer：ESP32 目標以 gnu++11 編譯，
     * 帶 NSDMI 的 struct 在 C++11 不是 aggregate，`{false, 0, 0}` 會編譯失敗
     * （native 是 gnu++17 所以看不出來）。explicit 防止隱式轉換
     * （例如誤寫 `FuelReading r = some_bool()` 會靜默成立，這是本 struct 要防的那類錯誤）。
     */
    constexpr explicit FuelReading(bool v = false, uint16_t mv = 0, uint8_t p = 0)
        : valid(v), millivolts(mv), percent(p) {}
};

/**
 * 燃料計後端抽象介面。
 *
 * 介面契約：
 * - `is_present() == false` 時，`read()` 必須回 `valid == false`。
 * - `is_present() == true` 不保證 `read().valid == true`——在線但單次讀取仍可能失敗（如 I2C NACK、垃圾讀值）。
 *   Task 5 的 Max17043Backend 必須遵守此契約，而 NullFuelGauge 則永遠回 not-present + invalid。
 *
 * 多型刪除：解構子必須 virtual，否則指向子類物件的基類指標刪除時無法呼叫子類解構子。
 */
class FuelGaugeBackend {
public:
    /** 多型刪除，確保子類解構子被正確呼叫 */
    virtual ~FuelGaugeBackend() = default;

    /**
     * 是否實體燃料計在線。
     * @return true 表硬體在線且已初始化；false 表硬體缺失或不可用
     */
    virtual bool is_present() const = 0;

    /**
     * 讀取當前電壓與電量。
     *
     * I2C 讀取本質有副作用（狀態機推進、暫存器更新等），故不標 const；
     * 與 RtcBackend::now() const + mutable 相比更誠實表達意圖。
     *
     * @return FuelReading，valid=false 表讀取失敗或不在線
     */
    virtual FuelReading read() = 0;
};

}  // namespace ems
