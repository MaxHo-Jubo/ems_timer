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

/**
 * 充電狀態。硬體沒有充電訊號腳，本列舉由電壓趨勢推導而來。
 */
enum class ChargeState : uint8_t {
    Unknown     = 0,  // 開機或 reset 後，觀察窗未滿時的狀態；不可顯示充電符號，也不可預設為 Discharging（那是猜的）
    Charging    = 1,  // 電壓在 30 秒內上升超過死區，判定為充電中
    Discharging = 2,  // 電壓在 30 秒內下降超過死區，判定為放電中
    Idle        = 3,  // 電壓在 30 秒內變化在死區內，判定為靜置（與 Unknown 不同：已確認無充放電）
};

/**
 * 電池電壓趨勢追蹤器：吃連續的電壓取樣，推導充電狀態。
 *
 * 用固定長度的環形緩衝比較「最舊」與「最新」兩點，窗未滿前一律回 Unknown。
 *
 * ⚠️ 本層不對輸入做合理性檢查，單筆 I2C 垃圾讀值會被當成真實變化，可能造成假的充放電判定；
 * 不可信讀值的判定屬於後續 FuelReading.valid 層的職責。
 */
class ChargeTrendTracker {
public:
    /** 趨勢判定窗的取樣點數：3 點 × 10 秒取樣 = 30 秒觀察窗 */
    static constexpr uint8_t TREND_WINDOW_SAMPLES = 3;

    /** 趨勢死區（mV）：窗內變化絕對值低於此值視為靜置。
     *  下界由訊號決定——500mA 充電 30 秒窗僅變 5mV，死區須明顯小於它才偵測得到；
     *  上界由靜置雜訊決定，而該雜訊幅度目前無實測數據。
     *  ⚠️ 本值為推導初值，Task 6 主韌體整合後須收長時間靜置讀數校正（spec §10）。 */
    static constexpr uint16_t TREND_DEADBAND_MV = 3;

    /**
     * 推入一筆電池電壓取樣（mV）
     * @param millivolts 電池電壓，單位毫伏特
     *
     * ⚠️ 本類沒有時間概念，「30 秒觀察窗」的成立完全取決於呼叫端維持 10 秒輪詢。
     * 若呼叫端改變輪詢間隔，本處註解與死區參數須同步調整，否則觀察窗會靜默失效。
     */
    void push(uint16_t millivolts);

    /**
     * 取得當前推導出的充電狀態
     * @return 充電狀態（Unknown ⟹ 窗未滿；Charging ⟹ 上升；Discharging ⟹ 下降；Idle ⟹ 靜置）
     */
    ChargeState state() const;

    /**
     * 清空取樣窗，回到 Unknown（換電池或 backend 重新上線時呼叫）
     */
    void reset();

private:
    uint16_t samples_[TREND_WINDOW_SAMPLES] = {0};  // 環形緩衝
    uint8_t count_ = 0;                             // 已推入次數，飽和於 TREND_WINDOW_SAMPLES；僅用於判斷窗是否已滿，不可當總推入量統計
    uint8_t head_  = 0;                             // 下一次寫入位置
};

}  // namespace ems
