// EMS DoseSync Pro — Impl-Phase H：燃料計純邏輯
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3
// 測試入口：firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
//
// 本檔不得 include Arduino.h / Wire.h — 所有函式須能在 native 環境編譯與測試。

#pragma once

#include <cstdint>

namespace ems {

/** VCELL 暫存器的有效資料位於 bits 15:4，取值需右移 4 位 */
constexpr uint8_t VCELL_SHIFT_BITS = 4;

/** MAX17043 的 VCELL 解析度：每 LSB 代表 1.25mV
 *  ⚠️ MAX17048/49 為 78.125µV，換型號此常數與下方換算須同步改，否則差 16 倍 */
constexpr float VCELL_LSB_MV = 1.25f;

/** SOC 暫存器低位元組為小數部分，每 LSB 代表 1/256 % */
constexpr float SOC_LSB_PERCENT = 1.0f / 256.0f;

/** 電量百分比上限：燃料計剛充飽可能回報超過 100%，對外一律夾到 100。
 *  注：clamp 目前不區分「晶片充飽超衝」與「垃圾讀值」，這是已知取捨；
 *  「讀值不可信」判定屬 FuelReading.valid 層的職責 */
constexpr uint8_t SOC_PERCENT_MAX = 100;

/**
 * 將 VCELL 原始暫存器值換算為電池電壓
 * @param raw VCELL 暫存器（位址 0x02）的 16-bit 原始值，有效資料位於 bits 15:4
 * @return 電池電壓，單位 mV（無條件捨去，非四捨五入）
 */
uint16_t vcell_raw_to_mv(uint16_t raw);

/**
 * 將 SOC 原始暫存器值換算為電量百分比
 * @param raw SOC 暫存器（位址 0x04）的 16-bit 原始值，高位元組為整數 %、低位元組為 1/256 %
 * @return 電量百分比 0~100（超過 100 夾到 100，無條件捨去，非四捨五入）
 */
uint8_t soc_raw_to_percent(uint16_t raw);

}  // namespace ems
