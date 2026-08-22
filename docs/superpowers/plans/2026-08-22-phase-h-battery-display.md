# Impl-Phase H 電量顯示 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 讓 MAX17043 燃料計的電量在主韌體中被讀取，並依 SoT V1 完成電量圖示、低電量警告與電池資訊畫面。

**Architecture:** 讀取層比照既有 `ems_rtc` 的 backend + Null 降級 pattern，把換算／趨勢／遲滯等純邏輯抽進 `fuel_gauge_logic` 以便 native test；顯示層先把 `main.cpp` 散落的 16 處 `pushSprite` 收斂成單一 `presentFrame()` 出口，電量圖示只需畫一次即可出現在所有畫面。

**Tech Stack:** C++17、PlatformIO、Arduino framework（ESP32-S3）、Wire（I2C）、LovyanGFX、Unity（native test）

**Spec:** `docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md`

## Global Constraints

以下數值來自 spec，逐字採用，每個 task 的需求隱含包含本節：

- I2C：`SDA = GPIO 42`、`SCL = GPIO 41`、燃料計位址 `0x36`、時脈 100kHz
- VCELL 換算：`mV = (raw >> 4) * 1.25`（MAX17043 為 1.25mV/LSB）
- SOC 換算：`percent = (raw >> 8) + (raw & 0xFF) / 256.0`
- 取樣間隔：**10 秒**；趨勢判定窗：**30 秒（3 個取樣點）**；趨勢死區：**±0.5%**
- 低電量門檻：**進入 20% ／ 解除 25%**（遲滯）
- `batteryPercent` 的 `255` = 燃料計不在線（**不可用 0**，0% 是合法讀數）
- 四格圖示分界：`0~24% = 1 格 / 25~49% = 2 格 / 50~74% = 3 格 / 75~100% = 4 格`
- 低電量閃爍：1Hz（500ms 翻轉）
- 低電量提示顯示 **3 秒**後自動消失，**不發聲**
- 「顯示一次」為 **per-boot，不寫 NVS**
- 程式碼風格：STEP 註解每個函式從 `STEP 01` 起算、變數與函式須有用途註解、`if` 一律加大括號、禁止 silent fallback
- 註解與文件用正體中文；commit message 用繁體中文，格式 `[PHASE-H] 類型: 說明`，不加 attribution 尾註

---

## File Structure

**新建**

| 檔案 | 責任 |
|---|---|
| `firmware/lib/ems_fuel_gauge/ems_fuel_gauge.h` | `FuelReading` struct、`ChargeState` enum、`FuelGaugeBackend` 純虛介面 |
| `firmware/lib/ems_fuel_gauge/null_backend.h` | 不在線時的降級實作，全部回無效值 |
| `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h` | 純邏輯宣告：換算、趨勢追蹤器、低電量遲滯 |
| `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.cpp` | 上述實作，不含任何 Arduino/Wire 相依 |
| `firmware/lib/ems_fuel_gauge/max17043_backend.h` | 硬體 backend 宣告 |
| `firmware/lib/ems_fuel_gauge/max17043_backend.cpp` | I2C 讀寫，唯一碰硬體的檔案 |
| `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp` | 純邏輯的 native test |

**修改**

| 檔案 | 改動 |
|---|---|
| `firmware/lib/ems_display_snapshot/ems_display_snapshot.h` | 新增 2 欄位 + 1 個 flag bit |
| `firmware/test/test_display_snapshot/test_display_snapshot.cpp` | 補新欄位與新 bit 的回歸測試 |
| `firmware/src/main.cpp` | 掛載 backend、10 秒輪詢、16 處 `pushSprite` → `presentFrame()`、snapshot 填值 |
| `firmware/src/ui_screens.cpp` | `drawBatteryIcon()`、電池資訊畫面 |
| `firmware/src/app_globals.h` | 圖示版面常數、全域狀態宣告 |

---

## Task 1：VCELL / SOC 換算純函式

**Files:**
- Create: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h`
- Create: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.cpp`
- Test: `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp`

**Interfaces:**
- Consumes: 無（第一個 task）
- Produces: `ems::vcell_raw_to_mv(uint16_t) -> uint16_t`、`ems::soc_raw_to_percent(uint16_t) -> uint8_t`

- [ ] **Step 1: 寫失敗測試**

建立 `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp`：

```cpp
// EMS Timer — Impl-Phase H：燃料計純邏輯測試
//
// 黃金值來自 2026-08-22 實機驗收（docs/power-module-purchase.md §10.8）：
//   VCELL raw=0xC030 → 3.844V、SOC raw=0x366A → 54.4%
#include <unity.h>
#include "fuel_gauge_logic.h"

void setUp()    {}
void tearDown() {}

static void test_vcell_golden_value_from_hardware_acceptance() {
    // 0xC030 >> 4 = 3075 counts；3075 * 1.25mV = 3843.75mV → 取整 3843
    TEST_ASSERT_EQUAL_UINT16(3843, ems::vcell_raw_to_mv(0xC030));
}

static void test_vcell_zero_is_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, ems::vcell_raw_to_mv(0x0000));
}

static void test_vcell_uses_high_12_bits_only() {
    // 低 4 bit 是無效位元，改變它們不可影響結果
    TEST_ASSERT_EQUAL_UINT16(ems::vcell_raw_to_mv(0xC030), ems::vcell_raw_to_mv(0xC03F));
}

static void test_soc_golden_value_from_hardware_acceptance() {
    // 0x36 = 54 整數；0x6A = 106 → 106/256 = 0.41 → 54.41% → 取整 54
    TEST_ASSERT_EQUAL_UINT8(54, ems::soc_raw_to_percent(0x366A));
}

static void test_soc_clamps_above_100() {
    // 燃料計剛充飽時可能回報 >100%，UI 不該顯示 103%
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0x6700));
}

static void test_soc_zero_is_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, ems::soc_raw_to_percent(0x0000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_vcell_golden_value_from_hardware_acceptance);
    RUN_TEST(test_vcell_zero_is_zero);
    RUN_TEST(test_vcell_uses_high_12_bits_only);
    RUN_TEST(test_soc_golden_value_from_hardware_acceptance);
    RUN_TEST(test_soc_clamps_above_100);
    RUN_TEST(test_soc_zero_is_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: 跑測試確認失敗**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 編譯失敗，找不到 `fuel_gauge_logic.h`

- [ ] **Step 3: 寫最小實作**

`firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h`：

```cpp
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

/** 電量百分比上限：燃料計剛充飽可能回報超過 100%，對外一律夾到 100 */
constexpr uint8_t SOC_PERCENT_MAX = 100;

/**
 * 將 VCELL 原始暫存器值換算為電池電壓
 * @param raw REG_VCELL 的 16-bit 原始值
 * @return 電池電壓，單位 mV（取整）
 */
uint16_t vcell_raw_to_mv(uint16_t raw);

/**
 * 將 SOC 原始暫存器值換算為電量百分比
 * @param raw REG_SOC 的 16-bit 原始值（高位元組整數 %、低位元組 1/256 %）
 * @return 電量百分比 0~100（超過 100 夾到 100）
 */
uint8_t soc_raw_to_percent(uint16_t raw);

}  // namespace ems
```

`firmware/lib/ems_fuel_gauge/fuel_gauge_logic.cpp`：

```cpp
#include "fuel_gauge_logic.h"

namespace ems {

uint16_t vcell_raw_to_mv(uint16_t raw) {
    // STEP 01: 取高 12 bit 的 A/D counts，乘上每 LSB 的電壓後取整
    const uint32_t counts = static_cast<uint32_t>(raw >> VCELL_SHIFT_BITS);
    return static_cast<uint16_t>(static_cast<float>(counts) * VCELL_LSB_MV);
}

uint8_t soc_raw_to_percent(uint16_t raw) {
    // STEP 01: 高位元組為整數部分，低位元組為 1/256 % 的小數部分
    const float whole    = static_cast<float>(raw >> 8);
    const float fraction = static_cast<float>(raw & 0xFF) * SOC_LSB_PERCENT;
    const float percent  = whole + fraction;

    // STEP 02: 充飽時可能超過 100%，夾到上限避免 UI 顯示 103%
    if (percent >= static_cast<float>(SOC_PERCENT_MAX)) {
        return SOC_PERCENT_MAX;
    }

    return static_cast<uint8_t>(percent);
}

}  // namespace ems
```

- [ ] **Step 4: 跑測試確認通過**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 6 tests PASS

- [ ] **Step 5: Commit**

```bash
cd firmware
git add lib/ems_fuel_gauge/fuel_gauge_logic.h lib/ems_fuel_gauge/fuel_gauge_logic.cpp \
        test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
git commit -m "[PHASE-H] feat: 燃料計 VCELL/SOC 換算純函式

換算公式沿用 2026-08-22 硬體驗收已核對過的版本（power-module-purchase.md §10.8），
測試直接用當時的實測值當黃金值：0xC030 → 3843mV、0x366A → 54%。

SOC 夾到 100：燃料計剛充飽會回報超過 100%，不夾會讓 UI 顯示 103%。
VCELL 低 4 bit 是無效位元，測試明確斷言改變它們不影響結果。"
```

---

## Task 2：充電狀態趨勢追蹤器

**Files:**
- Modify: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h`
- Modify: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.cpp`
- Test: `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp`

**Interfaces:**
- Consumes: Task 1 的 `soc_raw_to_percent`
- Produces: `ems::ChargeState` enum、`ems::ChargeTrendTracker` class（`push(uint8_t percent)`、`state() const -> ChargeState`、`reset()`）

- [ ] **Step 1: 寫失敗測試**

在 `test_fuel_gauge_logic.cpp` 的 `int main` 之前插入：

```cpp
// ============================================================
//  Group 2: 充電狀態趨勢推導
// ============================================================

static void test_trend_before_window_full_is_unknown() {
    // 硬體無充電狀態訊號，開機後窗未滿時必須誠實回 Unknown，不可猜 Discharging
    ems::ChargeTrendTracker t;
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(50);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(50);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
}

static void test_trend_rising_beyond_deadband_is_charging() {
    ems::ChargeTrendTracker t;
    t.push(50);
    t.push(51);
    t.push(53);  // 窗滿：53 - 50 = +3 > 死區
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
}

static void test_trend_falling_beyond_deadband_is_discharging() {
    ems::ChargeTrendTracker t;
    t.push(53);
    t.push(52);
    t.push(50);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_within_deadband_is_idle() {
    // 靜置時 SOC 會微幅抖動，死區內不可在充放電之間跳
    ems::ChargeTrendTracker t;
    t.push(50);
    t.push(50);
    t.push(50);
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_slides_window_and_updates() {
    // 先判定充電，之後持續下降應翻成放電（窗會滑動，不是只看最初三筆）
    ems::ChargeTrendTracker t;
    t.push(50);
    t.push(52);
    t.push(54);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
    t.push(52);
    t.push(50);
    t.push(48);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_reset_returns_to_unknown() {
    ems::ChargeTrendTracker t;
    t.push(50);
    t.push(52);
    t.push(54);
    t.reset();
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
}
```

並在 `main()` 的 `UNITY_BEGIN()` 之後補上對應的 `RUN_TEST`：

```cpp
    RUN_TEST(test_trend_before_window_full_is_unknown);
    RUN_TEST(test_trend_rising_beyond_deadband_is_charging);
    RUN_TEST(test_trend_falling_beyond_deadband_is_discharging);
    RUN_TEST(test_trend_within_deadband_is_idle);
    RUN_TEST(test_trend_slides_window_and_updates);
    RUN_TEST(test_trend_reset_returns_to_unknown);
```

- [ ] **Step 2: 跑測試確認失敗**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 編譯失敗，`ChargeTrendTracker` 未定義

- [ ] **Step 3: 寫最小實作**

在 `fuel_gauge_logic.h` 的 `}  // namespace ems` 之前加入：

```cpp
/** 趨勢判定窗的取樣點數：3 點 × 10 秒取樣 = 30 秒觀察窗 */
constexpr uint8_t TREND_WINDOW_SAMPLES = 3;

/** 趨勢死區（百分點）：窗內變化絕對值低於此值視為靜置，避免雜訊讓狀態跳動 */
constexpr float TREND_DEADBAND_PERCENT = 0.5f;

/**
 * 充電狀態。硬體沒有充電訊號腳，本列舉由 SOC 趨勢推導而來。
 *
 * Unknown 是誠實的初始態：開機後趨勢窗未滿前無從判斷，此時不可顯示充電符號，
 * 也不可預設為 Discharging——那是猜的。
 */
enum class ChargeState : uint8_t {
    Unknown     = 0,
    Charging    = 1,
    Discharging = 2,
    Idle        = 3,
};

/**
 * SOC 趨勢追蹤器：吃連續的電量取樣，推導充電狀態。
 *
 * 用固定長度的環形緩衝比較「最舊」與「最新」兩點，窗未滿前一律回 Unknown。
 */
class ChargeTrendTracker {
public:
    /** 推入一筆電量取樣（0~100） */
    void push(uint8_t percent);

    /** 取得當前推導出的充電狀態 */
    ChargeState state() const;

    /** 清空取樣窗，回到 Unknown（換電池或 backend 重新上線時呼叫） */
    void reset();

private:
    uint8_t samples_[TREND_WINDOW_SAMPLES] = {0};  // 環形緩衝
    uint8_t count_ = 0;                            // 已推入的總筆數（用於判斷窗是否已滿）
    uint8_t head_  = 0;                            // 下一次寫入位置
};
```

在 `fuel_gauge_logic.cpp` 的 `}  // namespace ems` 之前加入：

```cpp
void ChargeTrendTracker::push(uint8_t percent) {
    // STEP 01: 寫入環形緩衝並前進 head
    samples_[head_] = percent;
    head_ = static_cast<uint8_t>((head_ + 1) % TREND_WINDOW_SAMPLES);

    // STEP 02: count_ 只累加到窗滿為止，避免長時間執行後溢位
    if (count_ < TREND_WINDOW_SAMPLES) {
        count_++;
    }
}

ChargeState ChargeTrendTracker::state() const {
    // STEP 01: 窗未滿時無從判斷趨勢，誠實回 Unknown
    if (count_ < TREND_WINDOW_SAMPLES) {
        return ChargeState::Unknown;
    }

    // STEP 02: head_ 指向下一次寫入位置，也就是環形緩衝中最舊的那筆
    const uint8_t oldest = samples_[head_];
    const uint8_t newest = samples_[(head_ + TREND_WINDOW_SAMPLES - 1) % TREND_WINDOW_SAMPLES];
    const float   delta  = static_cast<float>(newest) - static_cast<float>(oldest);

    // STEP 03: 死區內視為靜置
    if (delta > TREND_DEADBAND_PERCENT) {
        return ChargeState::Charging;
    }
    if (delta < -TREND_DEADBAND_PERCENT) {
        return ChargeState::Discharging;
    }

    return ChargeState::Idle;
}

void ChargeTrendTracker::reset() {
    // STEP 01: 清掉計數與位置即可讓 state() 回到 Unknown，樣本值不需清零
    count_ = 0;
    head_  = 0;
}
```

- [ ] **Step 4: 跑測試確認通過**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 12 tests PASS

- [ ] **Step 5: Commit**

```bash
cd firmware
git add lib/ems_fuel_gauge/fuel_gauge_logic.h lib/ems_fuel_gauge/fuel_gauge_logic.cpp \
        test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
git commit -m "[PHASE-H] feat: 充電狀態趨勢追蹤器

MAX17043 沒有充電狀態暫存器、聯騰板也沒拉 CHRG 腳，充電與否只能從 SOC 趨勢推導。
3 點環形緩衝 × 10 秒取樣 = 30 秒觀察窗，比較最舊與最新兩點，±0.5 個百分點內視為
靜置（沒有死區的話，靜置時的微幅抖動會讓狀態在充電/放電之間反覆跳）。

ChargeState::Unknown 不是佔位值而是有意義的狀態：窗未滿前無從判斷，此時不顯示
充電符號。預設成 Discharging 會是在猜。"
```

---

## Task 3：低電量遲滯判定

**Files:**
- Modify: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.h`
- Modify: `firmware/lib/ems_fuel_gauge/fuel_gauge_logic.cpp`
- Test: `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp`

**Interfaces:**
- Consumes: 無
- Produces: `ems::LowBatteryLatch` class（`update(uint8_t percent) -> bool`、`is_low() const -> bool`、`consume_first_entry() -> bool`）

`consume_first_entry()` 是 §13.16「執行中只顯示一次」的實作：第一次跨進低電量時回 `true`，之後回 `false`，直到解除低電量後再次跨入才會重新回 `true`。

- [ ] **Step 1: 寫失敗測試**

在 `test_fuel_gauge_logic.cpp` 的 `int main` 之前插入：

```cpp
// ============================================================
//  Group 3: 低電量遲滯與一次性提示
// ============================================================

static void test_low_battery_not_triggered_above_threshold() {
    ems::LowBatteryLatch latch;
    latch.update(30);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_low_battery_triggers_at_or_below_20() {
    ems::LowBatteryLatch latch;
    latch.update(20);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_low_battery_stays_low_between_thresholds() {
    // 遲滯：進入 20% 後，回升到 22% 仍算低電量（未達 25% 解除門檻）
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(22);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_low_battery_clears_at_or_above_25() {
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(25);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_first_entry_consumed_only_once() {
    // §13.16：執行中只顯示一次提示
    ems::LowBatteryLatch latch;
    latch.update(18);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_first_entry_rearms_after_recovery() {
    // 充完電拔掉再掉到 20%，應該重新提醒一次
    ems::LowBatteryLatch latch;
    latch.update(18);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    latch.update(30);  // 充電回升，解除
    latch.update(18);  // 再次掉落
    TEST_ASSERT_TRUE(latch.consume_first_entry());
}

static void test_boot_already_low_counts_as_entry() {
    // 開機當下就低於 20%，沒有「上一次在門檻上」的紀錄，仍必須算一次跨越
    ems::LowBatteryLatch latch;
    latch.update(15);
    TEST_ASSERT_TRUE(latch.is_low());
    TEST_ASSERT_TRUE(latch.consume_first_entry());
}

static void test_hysteresis_does_not_retrigger_while_oscillating() {
    // 在 20~25% 之間來回抖動，不可重複觸發提示
    ems::LowBatteryLatch latch;
    latch.update(19);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    latch.update(23);
    latch.update(21);
    latch.update(24);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}
```

並在 `main()` 補上對應的 `RUN_TEST`：

```cpp
    RUN_TEST(test_low_battery_not_triggered_above_threshold);
    RUN_TEST(test_low_battery_triggers_at_or_below_20);
    RUN_TEST(test_low_battery_stays_low_between_thresholds);
    RUN_TEST(test_low_battery_clears_at_or_above_25);
    RUN_TEST(test_first_entry_consumed_only_once);
    RUN_TEST(test_first_entry_rearms_after_recovery);
    RUN_TEST(test_boot_already_low_counts_as_entry);
    RUN_TEST(test_hysteresis_does_not_retrigger_while_oscillating);
```

- [ ] **Step 2: 跑測試確認失敗**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 編譯失敗，`LowBatteryLatch` 未定義

- [ ] **Step 3: 寫最小實作**

在 `fuel_gauge_logic.h` 的 `}  // namespace ems` 之前加入：

```cpp
/** 低電量進入門檻（%）：SOC 小於等於此值視為低電量 */
constexpr uint8_t LOW_BATTERY_ENTER_PERCENT = 20;

/** 低電量解除門檻（%）：SOC 大於等於此值才解除低電量
 *  與進入門檻拉開形成遲滯，否則 SOC 在 20% 附近抖動會反覆觸發提示與閃爍 */
constexpr uint8_t LOW_BATTERY_EXIT_PERCENT = 25;

/**
 * 低電量狀態閂鎖：帶遲滯的低電量判定 + 一次性提示的消費旗標。
 *
 * 對應 SoT §13.16（執行中只顯示一次提示）與 §20.3（低電量仍可開案但需警告）。
 */
class LowBatteryLatch {
public:
    /**
     * 餵入一筆電量取樣，更新低電量狀態
     * @param percent 電量百分比 0~100
     */
    void update(uint8_t percent);

    /** 當前是否處於低電量（含遲滯） */
    bool is_low() const { return is_low_; }

    /**
     * 消費「首次進入低電量」事件。
     * @return 自上次解除低電量以來第一次呼叫且當前為低電量時回 true，其餘回 false
     */
    bool consume_first_entry();

private:
    bool is_low_          = false;  // 當前低電量狀態（帶遲滯）
    bool entry_pending_   = false;  // 尚未被消費的「首次進入」事件
};
```

在 `fuel_gauge_logic.cpp` 的 `}  // namespace ems` 之前加入：

```cpp
void LowBatteryLatch::update(uint8_t percent) {
    // STEP 01: 已在低電量：只有回升到解除門檻才脫離，形成遲滯
    if (is_low_) {
        if (percent >= LOW_BATTERY_EXIT_PERCENT) {
            is_low_        = false;
            entry_pending_ = false;
        }
        return;
    }

    // STEP 02: 未在低電量：跌到進入門檻即觸發，並掛上一次性提示事件
    //          開機首次取樣就低於門檻也走這條，不會漏觸發
    if (percent <= LOW_BATTERY_ENTER_PERCENT) {
        is_low_        = true;
        entry_pending_ = true;
    }
}

bool LowBatteryLatch::consume_first_entry() {
    // STEP 01: 沒有待消費事件就回 false（提示只顯示一次）
    if (!entry_pending_) {
        return false;
    }

    // STEP 02: 消費掉事件，下次呼叫回 false，直到解除後再次跨入才重新掛起
    entry_pending_ = false;
    return true;
}
```

- [ ] **Step 4: 跑測試確認通過**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 20 tests PASS

- [ ] **Step 5: Commit**

```bash
cd firmware
git add lib/ems_fuel_gauge/fuel_gauge_logic.h lib/ems_fuel_gauge/fuel_gauge_logic.cpp \
        test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
git commit -m "[PHASE-H] feat: 低電量遲滯判定與一次性提示閂鎖

進入 20%、解除 25%。沒有遲滯的話 SOC 在門檻附近抖動會讓提示與閃爍反覆觸發。

consume_first_entry() 實作 SoT §13.16「執行中只顯示一次」：解除低電量後會重新掛起
事件，所以充完電拔掉再掉下來會重新提醒——這是刻意的，寫進 NVS 會讓它一輩子只提醒
一次。測試涵蓋開機當下就低於門檻的情形（沒有「上一次在門檻上」的紀錄仍須算一次
跨越）與 20~25% 之間來回抖動不重複觸發。"
```

---

## Task 4：Backend 介面與 Null 降級實作

**Files:**
- Create: `firmware/lib/ems_fuel_gauge/ems_fuel_gauge.h`
- Create: `firmware/lib/ems_fuel_gauge/null_backend.h`
- Test: `firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp`

**Interfaces:**
- Consumes: 無
- Produces: `ems::FuelReading` struct（`bool valid; uint16_t millivolts; uint8_t percent;`）、`ems::FuelGaugeBackend` 純虛介面（`is_present()`、`read()`）、`ems::NullFuelGauge`

- [ ] **Step 1: 寫失敗測試**

在 `test_fuel_gauge_logic.cpp` 頂端的 include 之後補上：

```cpp
#include "null_backend.h"
```

在 `int main` 之前插入：

```cpp
// ============================================================
//  Group 4: Null backend 降級
// ============================================================

static void test_null_backend_not_present() {
    ems::NullFuelGauge nb;
    TEST_ASSERT_FALSE(nb.is_present());
}

static void test_null_backend_read_is_invalid() {
    // 不在線時 valid=false；caller 不可把 percent 當合法讀數
    ems::NullFuelGauge nb;
    const ems::FuelReading r = nb.read();
    TEST_ASSERT_FALSE(r.valid);
}
```

並在 `main()` 補上：

```cpp
    RUN_TEST(test_null_backend_not_present);
    RUN_TEST(test_null_backend_read_is_invalid);
```

- [ ] **Step 2: 跑測試確認失敗**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 編譯失敗，找不到 `null_backend.h`

- [ ] **Step 3: 寫最小實作**

`firmware/lib/ems_fuel_gauge/ems_fuel_gauge.h`：

```cpp
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
//   - Max17043Backend：實體燃料計（I2C 0x36，硬體相依）
//   - NullFuelGauge  ：無燃料計時的降級實作

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
};

class FuelGaugeBackend {
public:
    virtual ~FuelGaugeBackend() = default;

    /** 是否實體燃料計在線（NullFuelGauge 永遠 false） */
    virtual bool is_present() const = 0;

    /**
     * 讀取當前電壓與電量。
     * @return FuelReading，valid=false 表讀取失敗或不在線
     */
    virtual FuelReading read() = 0;
};

}  // namespace ems
```

`firmware/lib/ems_fuel_gauge/null_backend.h`：

```cpp
// EMS DoseSync Pro — Impl-Phase H：燃料計無硬體降級實作
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3.2
//
// 用途：
//   I2C probe 沒偵測到 0x36 時掛上 NullFuelGauge，讓上層無條件呼叫不需分支。
//     is_present() → false
//     read()       → {valid=false, 0, 0}（UI 端據此不畫圖示）

#pragma once

#include "ems_fuel_gauge.h"

namespace ems {

class NullFuelGauge : public FuelGaugeBackend {
public:
    bool is_present() const override { return false; }
    FuelReading read() override { return {false, 0, 0}; }
};

}  // namespace ems
```

- [ ] **Step 4: 跑測試確認通過**

Run: `cd firmware && pio test -e native -f test_fuel_gauge_logic`
Expected: 22 tests PASS

- [ ] **Step 5: Commit**

```bash
cd firmware
git add lib/ems_fuel_gauge/ems_fuel_gauge.h lib/ems_fuel_gauge/null_backend.h \
        test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
git commit -m "[PHASE-H] feat: 燃料計 backend 介面與 Null 降級實作

比照 ems_rtc 的 backend + Null pattern：偵測不到 0x36 時掛 NullFuelGauge，上層不需
到處分支，缺硬體也不會 crash。

FuelReading 用 valid 旗標而非拿 percent=0 兼作哨兵：0% 是合法讀數（真的沒電），
共用會讓硬體故障偽裝成電池耗盡。"
```

---

## Task 5：MAX17043 硬體 backend

**Files:**
- Create: `firmware/lib/ems_fuel_gauge/max17043_backend.h`
- Create: `firmware/lib/ems_fuel_gauge/max17043_backend.cpp`

**Interfaces:**
- Consumes: Task 1 的 `vcell_raw_to_mv` / `soc_raw_to_percent`、Task 4 的 `FuelGaugeBackend`
- Produces: `ems::Max17043Backend`（`begin(TwoWire&) -> bool`、`is_present()`、`read()`）

本 task 碰硬體，無 native test；正確性由 Task 1 的換算測試 + on-target 驗證覆蓋。

- [ ] **Step 1: 寫實作**

`firmware/lib/ems_fuel_gauge/max17043_backend.h`：

```cpp
// EMS DoseSync Pro — Impl-Phase H：MAX17043 燃料計 I2C backend
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3
// 接線依據：docs/power-module-purchase.md §10.7（VDD 由電池供電，VCC 排針不接）
// 硬體驗收：docs/power-module-purchase.md §10.8
//
// 本檔是本 lib 唯一碰硬體的檔案，換算邏輯一律呼叫 fuel_gauge_logic。

#pragma once

#include <Wire.h>

#include "ems_fuel_gauge.h"

namespace ems {

/** MAX17043 I2C 7-bit 位址（docs/gpio-allocation.md §5.4） */
constexpr uint8_t MAX17043_I2C_ADDR = 0x36;

/** 暫存器位址（MAX17043 datasheet Table 1） */
constexpr uint8_t MAX17043_REG_VCELL   = 0x02;  // 電池電壓 A/D 讀值
constexpr uint8_t MAX17043_REG_SOC     = 0x04;  // 電量百分比
constexpr uint8_t MAX17043_REG_VERSION = 0x08;  // 晶片版本，用於 probe

class Max17043Backend : public FuelGaugeBackend {
public:
    /**
     * 探測燃料計是否在線（讀 VERSION 暫存器）
     * @param wire 已 begin() 過的 I2C bus
     * @return true = 偵測到並可讀
     */
    bool begin(TwoWire& wire);

    bool is_present() const override { return present_; }
    FuelReading read() override;

private:
    /**
     * 讀取一個 16-bit 暫存器（MSB first）
     * @param reg      暫存器位址
     * @param out_value 成功時寫入讀到的值
     * @return true = 讀取成功；false = 無 ACK 或位元組數不足
     */
    bool read_register16(uint8_t reg, uint16_t& out_value);

    TwoWire* wire_   = nullptr;  // 由 begin() 注入，未 begin 前為 nullptr
    bool     present_ = false;   // probe 結果
};

}  // namespace ems
```

`firmware/lib/ems_fuel_gauge/max17043_backend.cpp`：

```cpp
#include "max17043_backend.h"

#include "fuel_gauge_logic.h"

namespace ems {

bool Max17043Backend::begin(TwoWire& wire) {
    // STEP 01: 記住 bus，後續讀取都走它
    wire_ = &wire;

    // STEP 02: 讀 VERSION 當 probe，讀得到才視為在線
    uint16_t version = 0;
    present_ = read_register16(MAX17043_REG_VERSION, version);

    return present_;
}

FuelReading Max17043Backend::read() {
    // STEP 01: 不在線直接回無效，不可回 0 讓上層誤判為沒電
    if (!present_ || wire_ == nullptr) {
        return {false, 0, 0};
    }

    // STEP 02: 讀電壓，失敗即整筆無效（不拿半筆資料當結果）
    uint16_t raw_vcell = 0;
    if (!read_register16(MAX17043_REG_VCELL, raw_vcell)) {
        return {false, 0, 0};
    }

    // STEP 03: 讀電量，同樣不容忍部分失敗
    uint16_t raw_soc = 0;
    if (!read_register16(MAX17043_REG_SOC, raw_soc)) {
        return {false, 0, 0};
    }

    // STEP 04: 換算交給純邏輯層（native 已測過）
    return {true, vcell_raw_to_mv(raw_vcell), soc_raw_to_percent(raw_soc)};
}

bool Max17043Backend::read_register16(uint8_t reg, uint16_t& out_value) {
    // STEP 01: 送出暫存器位址，以 repeated START 保持 bus
    wire_->beginTransmission(MAX17043_I2C_ADDR);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0) {
        return false;
    }

    // STEP 02: 要求 2 bytes，數量不足代表讀取未完成
    if (wire_->requestFrom(MAX17043_I2C_ADDR, static_cast<uint8_t>(2)) != 2) {
        return false;
    }

    // STEP 03: MSB first 組成 16-bit
    const uint8_t msb = static_cast<uint8_t>(wire_->read());
    const uint8_t lsb = static_cast<uint8_t>(wire_->read());
    out_value = (static_cast<uint16_t>(msb) << 8) | lsb;

    return true;
}

}  // namespace ems
```

- [ ] **Step 2: 確認韌體編譯通過**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS（此時 backend 尚未被 main.cpp 使用，只驗證能編譯）

- [ ] **Step 3: 確認 native test 仍全綠**

Run: `cd firmware && pio test -e native`
Expected: 全部通過（新 lib 的硬體檔不進 native build）

- [ ] **Step 4: Commit**

```bash
cd firmware
git add lib/ems_fuel_gauge/max17043_backend.h lib/ems_fuel_gauge/max17043_backend.cpp
git commit -m "[PHASE-H] feat: MAX17043 I2C backend

唯一碰硬體的檔案，換算一律轉呼叫 fuel_gauge_logic（那層已有 native test 用實機
黃金值覆蓋）。

read() 對 endTransmission 失敗與 requestFrom 位元組數不足都回 valid=false，不拿
半筆資料當結果，也不 fallback 成 0。probe 走 VERSION 暫存器。"
```

---

## Task 6：main.cpp 掛載與 10 秒輪詢

**Files:**
- Modify: `firmware/src/main.cpp`（`STEP 06.5` RTC 區塊之後、主迴圈）
- Modify: `firmware/src/app_globals.h`

**Interfaces:**
- Consumes: Task 2 的 `ChargeTrendTracker`、Task 3 的 `LowBatteryLatch`、Task 4/5 的 backend
- Produces: 全域 `g_fuel_gauge`（`ems::FuelGaugeBackend*`）、`g_battery_percent`（`uint8_t`，255=不在線）、`g_battery_charge_state`（`ems::ChargeState`）、`g_battery_low`（`bool`）

- [ ] **Step 1: 加入全域宣告**

在 `firmware/src/app_globals.h` 的 GPIO 常數區之後加入：

```cpp
// ── Impl-Phase H：電池狀態（由 main.cpp 每 10 秒輪詢更新）──

/** 電量輪詢間隔（ms）：電量變化極慢，10 秒對 UI 已綽綽有餘 */
constexpr uint32_t BATTERY_POLL_INTERVAL_MS = 10000;

/** 電量百分比。255 = 燃料計不在線；0 是合法讀數（真的沒電），兩者不可混用 */
constexpr uint8_t BATTERY_PERCENT_ABSENT = 255;
```

- [ ] **Step 2: 在 main.cpp setup 掛載 backend**

在 `firmware/src/main.cpp` 的 `STEP 06.5` RTC 區塊結束之後插入 `STEP 06.6`（若既有編號已被佔用則接續順號，並依 STEP-COMMENT-INSERT 規則重排後續編號）：

```cpp
    // STEP 06.6: Impl-Phase H — 燃料計初始化（runtime 偵測 I2C 0x36）
    //   Wire.begin 已於 STEP 06.5 呼叫過，此處直接沿用同一條 bus
    //   對齊 spec §3.2：偵測到掛 Max17043Backend，否則掛 NullFuelGauge，缺硬體不阻擋開機
    static ems::Max17043Backend fuel_be;
    static ems::NullFuelGauge   fuel_null_be;
    if (fuel_be.begin(Wire)) {
        g_fuel_gauge = &fuel_be;
        Serial.println("[FUEL] MAX17043 detected at 0x36");
    } else {
        g_fuel_gauge = &fuel_null_be;
        Serial.println("[FUEL] MAX17043 not present, 電量顯示停用");
    }
```

並在 main.cpp 全域區加入定義：

```cpp
// Impl-Phase H：電池狀態全域（由 pollBattery() 每 10 秒更新，UI 端唯讀）
ems::FuelGaugeBackend*  g_fuel_gauge           = nullptr;
uint8_t                 g_battery_percent      = BATTERY_PERCENT_ABSENT;
uint16_t                g_battery_millivolts   = 0;  // 僅在 g_battery_percent != 255 時有意義
ems::ChargeState        g_battery_charge_state = ems::ChargeState::Unknown;
bool                    g_battery_low          = false;
static ems::ChargeTrendTracker g_battery_trend;
static ems::LowBatteryLatch    g_battery_latch;
```

並在 `app_globals.h` 加上對應的 extern 宣告，供 `ui_screens.cpp` 使用：

```cpp
extern uint8_t          g_battery_percent;       // Phase H：電量 0~100，255 = 不在線
extern uint16_t         g_battery_millivolts;    // Phase H：電壓 mV
extern ems::ChargeState g_battery_charge_state;  // Phase H：充電狀態
extern bool             g_battery_low;           // Phase H：低電量（含遲滯）
```

`app_globals.h` 需先 `#include "ems_fuel_gauge.h"` 才認得 `ems::ChargeState`。

- [ ] **Step 3: 加入輪詢函式並在主迴圈呼叫**

在 main.cpp 加入：

```cpp
/**
 * 每 BATTERY_POLL_INTERVAL_MS 讀一次燃料計，更新電量／充電狀態／低電量旗標。
 * 讀取失敗或不在線時 g_battery_percent 設為 BATTERY_PERCENT_ABSENT，UI 端據此不畫圖示。
 */
static void pollBattery() {
    // STEP 01: 未到取樣時間就跳過
    static uint32_t last_poll_ms = 0;
    const uint32_t now_ms = millis();
    if (last_poll_ms != 0 && (now_ms - last_poll_ms) < BATTERY_POLL_INTERVAL_MS) {
        return;
    }
    last_poll_ms = now_ms;

    // STEP 02: 讀取；失敗一律標成不在線，不沿用上一筆舊值假裝還讀得到
    const ems::FuelReading reading = g_fuel_gauge->read();
    if (!reading.valid) {
        g_battery_percent      = BATTERY_PERCENT_ABSENT;
        g_battery_millivolts   = 0;
        g_battery_charge_state = ems::ChargeState::Unknown;
        g_battery_low          = false;
        g_battery_trend.reset();
        return;
    }

    // STEP 03: 更新趨勢與低電量閂鎖
    g_battery_percent    = reading.percent;
    g_battery_millivolts = reading.millivolts;
    g_battery_trend.push(reading.percent);
    g_battery_charge_state = g_battery_trend.state();
    g_battery_latch.update(reading.percent);
    g_battery_low = g_battery_latch.is_low();

    Serial.printf("[FUEL] %u%% %umV state=%u low=%d\n",
                  reading.percent, reading.millivolts,
                  static_cast<unsigned>(g_battery_charge_state), g_battery_low ? 1 : 0);
}
```

在 `loop()` 內、既有 `updateDisplay()` 呼叫之前加入 `pollBattery();`。

- [ ] **Step 4: 編譯並上機驗證**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1 -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`

接著讀 serial（port 名稱每次都要用 `ls /dev/cu.*` 重新確認，macOS 會漂移）：

```bash
~/.platformio/penv/bin/python -c "
import serial, time
s = serial.Serial()
s.port = '$(ls /dev/cu.usbmodem* | head -1)'; s.baudrate = 115200; s.timeout = 1
s.dtr = False; s.rts = False
s.open(); time.sleep(0.2)
s.setDTR(False); s.setRTS(True); time.sleep(0.15); s.setRTS(False)
end = time.time() + 40
while time.time() < end:
    line = s.readline()
    if line: print(line.decode('utf-8','replace'), end='')
s.close()"
```

Expected: 開機看到 `[FUEL] MAX17043 detected at 0x36`，之後每 10 秒一筆 `[FUEL] 54% 3844mV state=... low=0`

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/main.cpp src/app_globals.h
git commit -m "[PHASE-H] feat: 主韌體掛載燃料計並每 10 秒輪詢

W1 收尾：主韌體開機 probe 0x36，偵測到掛 Max17043Backend、否則掛 NullFuelGauge，
缺硬體不阻擋開機。輪詢結果餵進趨勢追蹤器與低電量閂鎖，寫進三個全域供 UI 唯讀。

讀取失敗時把 g_battery_percent 設回 255（不在線）而不是沿用上一筆舊值——沿用會讓
拔掉燃料計後畫面繼續顯示最後那個電量，看起來一切正常。"
```

---

## Task 7：DisplaySnapshot 新增電池欄位

**Files:**
- Modify: `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`
- Test: `firmware/test/test_display_snapshot/test_display_snapshot.cpp`

**Interfaces:**
- Consumes: Task 6 的全域 `g_battery_percent` / `g_battery_charge_state`
- Produces: `DisplaySnapshot.batteryPercent`、`DisplaySnapshot.batteryChargeState`、`SNAP_FLAG_BATTERY_LOW_BLINK = 0x00040000`

⚠️ DisplaySnapshot 漏欄位已連踩 4 次。本 task 必須同時改到四處：`DisplaySnapshot` struct、`DisplaySnapshotInputs` struct、`captureSnapshot()` 的欄位拷貝、以及 main.cpp 的 `captureDisplaySnapshot()` 填值（後者在 Task 8 一併處理）。

- [ ] **Step 1: 寫失敗測試**

在 `firmware/test/test_display_snapshot/test_display_snapshot.cpp` 的 `int main` 之前插入：

```cpp
// ============================================================
//  Group 5: Impl-Phase H 電池欄位
// ============================================================

static void test_battery_percent_change_triggers_redraw() {
    // 待機畫面電量從 55% 掉到 54% 也必須觸發重繪，否則圖示會停格
    DisplaySnapshotInputs a;
    a.batteryPercent = 55;
    DisplaySnapshotInputs b;
    b.batteryPercent = 54;
    TEST_ASSERT_FALSE(snapshotsEqual(captureSnapshot(a), captureSnapshot(b)));
}

static void test_battery_charge_state_change_triggers_redraw() {
    DisplaySnapshotInputs a;
    a.batteryChargeState = 1;  // Charging
    DisplaySnapshotInputs b;
    b.batteryChargeState = 2;  // Discharging
    TEST_ASSERT_FALSE(snapshotsEqual(captureSnapshot(a), captureSnapshot(b)));
}

static void test_battery_absent_differs_from_zero_percent() {
    // 255（不在線）與 0%（真的沒電）必須是不同狀態
    DisplaySnapshotInputs absent;
    absent.batteryPercent = 255;
    DisplaySnapshotInputs empty;
    empty.batteryPercent = 0;
    TEST_ASSERT_FALSE(snapshotsEqual(captureSnapshot(absent), captureSnapshot(empty)));
}

static void test_flag_battery_low_blink_sets_bit_0x40000() {
    DisplaySnapshotInputs in;
    in.batteryLowBlinkOn = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_BATTERY_LOW_BLINK, captureSnapshot(in).flags);
}
```

並在 `main()` 補上：

```cpp
    RUN_TEST(test_battery_percent_change_triggers_redraw);
    RUN_TEST(test_battery_charge_state_change_triggers_redraw);
    RUN_TEST(test_battery_absent_differs_from_zero_percent);
    RUN_TEST(test_flag_battery_low_blink_sets_bit_0x40000);
```

同時把既有的 `test_all_flags_bit_masks_are_unique` 納入新 bit——找到該測試中列出所有 mask 的陣列，加入 `SNAP_FLAG_BATTERY_LOW_BLINK`。

- [ ] **Step 2: 跑測試確認失敗**

Run: `cd firmware && pio test -e native -f test_display_snapshot`
Expected: 編譯失敗，`batteryPercent` 不是 `DisplaySnapshotInputs` 的成員

- [ ] **Step 3: 寫最小實作**

在 `ems_display_snapshot.h` 的 `DisplaySnapshot` struct 內，`settingsCursor` 之後、`flags` 之前加入：

```cpp
    uint8_t  batteryPercent;     ///< Phase H：電量 0~100；255 = 燃料計不在線（0 是合法讀數，不可共用）
    uint8_t  batteryChargeState; ///< Phase H：ems::ChargeState 列舉值（0=Unknown 1=Charging 2=Discharging 3=Idle）
```

在 `DisplaySnapshotFlag` enum 的最後加入：

```cpp
    SNAP_FLAG_BATTERY_LOW_BLINK = 0x00040000,  // Phase H：低電量閃爍相位（1Hz 翻轉）
```

在 `DisplaySnapshotInputs` struct 的最後加入：

```cpp
    uint8_t  batteryPercent     = 255;    // Phase H：預設 255 = 不在線
    uint8_t  batteryChargeState = 0;      // Phase H：預設 Unknown
    bool     batteryLowBlinkOn  = false;  // Phase H：低電量閃爍當前相位
```

在 `captureSnapshot()` 的 STEP 01 欄位拷貝區最後加入：

```cpp
    s.batteryPercent      = in.batteryPercent;      // Phase H
    s.batteryChargeState  = in.batteryChargeState;  // Phase H
```

在 STEP 02 的 flag 區最後加入：

```cpp
    if (in.batteryLowBlinkOn)      s.flags |= SNAP_FLAG_BATTERY_LOW_BLINK;
```

- [ ] **Step 4: 跑測試確認通過**

Run: `cd firmware && pio test -e native -f test_display_snapshot`
Expected: 全部通過（含既有測試）

- [ ] **Step 5: Commit**

```bash
cd firmware
git add lib/ems_display_snapshot/ems_display_snapshot.h test/test_display_snapshot/test_display_snapshot.cpp
git commit -m "[PHASE-H] feat: DisplaySnapshot 新增電池欄位與低電量閃爍 bit

同型 bug 已連踩 4 次（historyCursor / summarySubmenuCursor / endCheckCursor /
Phase G 設定選單），這次四處一起改：struct、Inputs、captureSnapshot 拷貝、flag bit。

測試明確涵蓋 255（不在線）與 0%（真的沒電）必須是不同 snapshot——共用哨兵會讓
硬體故障偽裝成電池耗盡，也會讓拔掉燃料計後畫面不重繪。"
```

---

## Task 8：presentFrame() 統一重繪出口

**Files:**
- Modify: `firmware/src/main.cpp`（16 處 `pushSprite` 呼叫點 + `captureDisplaySnapshot()`）

**Interfaces:**
- Consumes: Task 7 的 snapshot 欄位、Task 6 的電池全域
- Produces: `presentFrame()`（無參數、無回傳），取代所有直接的 `display.pushSprite(0, 0)`

- [ ] **Step 1: 新增 presentFrame 並填 snapshot**

在 main.cpp 加入（`drawBatteryIcon()` 於 Task 9 實作，本 task 先留呼叫點並提供空實作以便獨立驗收）：

```cpp
/**
 * 統一重繪出口：畫全域 overlay 後把 sprite 推到實體 TFT。
 *
 * 所有畫面都必須經由本函式推送，不可直接呼叫 display.pushSprite()——
 * 電量圖示等全域 overlay 只在這裡畫一次，新畫面才不會漏。
 */
static void presentFrame() {
    // STEP 01: 全域 overlay（電量圖示；不在線時本身不畫任何東西）
    drawBatteryIcon();

    // STEP 02: DMA 整片推送
    display.pushSprite(0, 0);
}
```

在 `captureDisplaySnapshot()` 內補上電池欄位填值：

```cpp
    in.batteryPercent     = g_battery_percent;
    in.batteryChargeState = static_cast<uint8_t>(g_battery_charge_state);
    in.batteryLowBlinkOn  = g_battery_low && ((millis() / BATTERY_BLINK_HALF_PERIOD_MS) % 2 == 0);
```

並在 `app_globals.h` 加入：

```cpp
/** 低電量閃爍半週期（ms）：500ms 翻轉一次 = 1Hz 閃爍 */
constexpr uint32_t BATTERY_BLINK_HALF_PERIOD_MS = 500;
```

- [ ] **Step 2: 替換全部 16 處呼叫點**

把 main.cpp 中所有 `display.pushSprite(0, 0);` 改為 `presentFrame();`。以下為完整清單（行號為改動前狀態，實際以檔案內容為準）：

```
836, 853, 863, 864, 865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 985
```

其中 863~874 為單行形式，例如：

```cpp
        if (ohcaSubState == SUBSTATE_QUICK_MENU)        { drawQuickMenu();       presentFrame(); return; }
```

改完後驗證沒有漏網之魚：

```bash
cd firmware && grep -n "display.pushSprite" src/main.cpp
```

Expected: 只剩 `presentFrame()` 內部那一處，以及第 454 行與第 834 行的**註解**（註解不是呼叫點，但文字提到 pushSprite 時應同步改為描述 presentFrame，避免註解與程式不一致）。

- [ ] **Step 3: 加入 drawBatteryIcon 空實作**

在 `firmware/src/ui_screens.cpp` 末端加入（Task 9 才填內容，本 task 先讓它可編譯）：

```cpp
/**
 * 右上角電量圖示（Impl-Phase H）。
 * 於 presentFrame() 內對所有畫面統一繪製；燃料計不在線時完全不畫。
 */
void drawBatteryIcon() {
    // Task 9 實作繪製內容
}
```

宣告加在 `firmware/src/app_globals.h` 的 UI 函式宣告區（`void drawMainMenu();` 所在處，約第 605 行）：

```cpp
void drawBatteryIcon();  // Phase H：右上角電量圖示，由 presentFrame() 統一呼叫
```

- [ ] **Step 4: 編譯並上機確認畫面行為未變**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1 -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`

實機操作：主選單上下移動、進 OHCA、開通氣、進歷史列表、進設定。
Expected: 所有畫面與改動前完全一致（此時圖示尚未實作，只驗證收斂沒有破壞既有重繪）

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/main.cpp src/ui_screens.cpp src/app_globals.h
git commit -m "[PHASE-H] refactor: pushSprite 收斂為 presentFrame() 統一出口

main.cpp 原本有 16 處 drawXxx(); display.pushSprite(0,0); return; 各自為政，沒有
統一出口。要讓電量圖示出現在所有畫面，散彈式在每個 draw 後補一行是行不通的——
第 17 個畫面的作者不會知道要加。

收斂後全域 overlay 只寫在 presentFrame() 一處。本 commit 不含圖示繪製內容
（drawBatteryIcon 先留空實作），純驗證收斂沒有破壞既有重繪行為。"
```

---

## Task 9：電量圖示繪製與閃爍

**Files:**
- Modify: `firmware/src/ui_screens.cpp`（`drawBatteryIcon()`）
- Modify: `firmware/src/app_globals.h`（版面常數）

**Interfaces:**
- Consumes: Task 6 的 `g_battery_percent` / `g_battery_charge_state` / `g_battery_low`、Task 8 的 `presentFrame()`
- Produces: 完整的 `drawBatteryIcon()`

- [ ] **Step 1: 加入版面常數**

在 `app_globals.h` 的版面常數區加入：

```cpp
// ── Impl-Phase H：右上角電量圖示版面（獨立頂行，不與 OHCA_BADGE_Y 那行爭位）──

/** 圖示右緣距螢幕右邊界（px），與既有 overlay 的 8px 邊距一致 */
constexpr int16_t BATTERY_ICON_RIGHT_MARGIN = 8;

/** 圖示頂緣 y 座標（px）：位於 OHCA_BADGE_Y=14 那行之上的空白頂行 */
constexpr int16_t BATTERY_ICON_Y = 2;

/** 電池外框寬度（px，不含正極頭） */
constexpr int16_t BATTERY_ICON_BODY_W = 20;

/** 電池外框高度（px），受限於頂行僅 14px 可用 */
constexpr int16_t BATTERY_ICON_BODY_H = 10;

/** 正極頭寬度（px） */
constexpr int16_t BATTERY_ICON_TIP_W = 2;

/** 正極頭高度（px） */
constexpr int16_t BATTERY_ICON_TIP_H = 4;

/** 四格電量條的格數 */
constexpr uint8_t BATTERY_ICON_SEGMENTS = 4;
```

- [ ] **Step 2: 實作繪製**

`ui_screens.cpp` 需先 include 才認得 `ems::ChargeState`（若 `app_globals.h` 已依 Task 6 加入該 include，此處可省）：

```cpp
#include "ems_fuel_gauge.h"
```

把 Task 8 留下的空實作替換為：

```cpp
/**
 * 依電量百分比換算應填滿的格數
 * @param percent 電量百分比 0~100
 * @return 填滿格數 1~4（SOC 為 0 時仍回 1，外框本身表示「有讀到值」）
 */
static uint8_t batterySegmentsForPercent(uint8_t percent) {
    // STEP 01: 分界依 spec §4.3：0~24 / 25~49 / 50~74 / 75~100
    if (percent >= 75) {
        return 4;
    }
    if (percent >= 50) {
        return 3;
    }
    if (percent >= 25) {
        return 2;
    }
    return 1;
}

/**
 * 右上角電量圖示（Impl-Phase H）。
 * 於 presentFrame() 內對所有畫面統一繪製；燃料計不在線時完全不畫。
 */
void drawBatteryIcon() {
    // STEP 01: 不在線時完全不畫——畫空電池會被讀成沒電
    if (g_battery_percent == BATTERY_PERCENT_ABSENT) {
        return;
    }

    // STEP 02: 低電量閃爍的暗相位時整個圖示不畫（1Hz 翻轉）
    const bool blink_off = g_battery_low
                        && ((millis() / BATTERY_BLINK_HALF_PERIOD_MS) % 2 != 0);
    if (blink_off) {
        return;
    }

    // STEP 03: 計算外框位置（右緣對齊，往左展開）
    const int16_t body_x = SCREEN_W - BATTERY_ICON_RIGHT_MARGIN
                         - BATTERY_ICON_TIP_W - BATTERY_ICON_BODY_W;
    const int16_t body_y = BATTERY_ICON_Y;

    // STEP 04: 低電量用警示色，其餘用一般前景色
    const uint16_t color = g_battery_low ? COLOR_ACCENT_ALERT : COLOR_TEXT_MUTED;

    // STEP 05: 畫外框與正極頭
    display.drawRect(body_x, body_y, BATTERY_ICON_BODY_W, BATTERY_ICON_BODY_H, color);
    display.fillRect(body_x + BATTERY_ICON_BODY_W,
                     body_y + (BATTERY_ICON_BODY_H - BATTERY_ICON_TIP_H) / 2,
                     BATTERY_ICON_TIP_W, BATTERY_ICON_TIP_H, color);

    // STEP 06: 依電量填格（格與格之間留 1px 間隙）
    const uint8_t segments = batterySegmentsForPercent(g_battery_percent);
    const int16_t seg_w = (BATTERY_ICON_BODY_W - 4) / BATTERY_ICON_SEGMENTS;
    for (uint8_t i = 0; i < segments; i++) {
        display.fillRect(body_x + 2 + i * seg_w, body_y + 2,
                         seg_w - 1, BATTERY_ICON_BODY_H - 4, color);
    }

    // STEP 07: 充電中在外框上疊一個閃電符號（SoT §13.17 的 🔋⚡）
    if (g_battery_charge_state == ems::ChargeState::Charging) {
        useZhFont();
        display.setTextSize(1);
        display.setTextColor(COLOR_ACCENT_OK);
        display.setTextDatum(textdatum_t::top_right);
        display.drawString("⚡", body_x - 2, body_y);
    }
}
```

- [ ] **Step 3: 編譯上機驗證**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1 -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`

實機檢查：
1. 每個畫面右上角都看得到電池圖示（主選單／OHCA／通氣／歷史／設定）
2. 圖示不與 OHCA 的「通氣 N」或 Training 浮水印重疊
3. 插上 USB 充電，約 30 秒後出現閃電符號

Expected: 三項全部符合

- [ ] **Step 4: 跑完整 native test 確認無回歸**

Run: `cd firmware && pio test -e native`
Expected: 全部通過

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/ui_screens.cpp src/app_globals.h
git commit -m "[PHASE-H] feat: 右上角四格電量圖示與低電量閃爍

圖示放 y=2 的獨立頂行，不進 OHCA_BADGE_Y=14 那行——那行的右側已被通氣 overlay
與 Training 浮水印佔用，擠進去要動三處已驗收的座標。獨立頂行是零向後相容風險的
選擇，代價是圖示只有 10px 高。

不在線時完全不畫（連外框都不畫）：畫空電池會被讀成沒電。四格分界 0~24/25~49/
50~74/75~100 為本專案設計決定，SoT §13.17 只寫「電量圖示」未規定格數。"
```

---

## Task 10：§13.16 執行中低電量一次性提示

**Files:**
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/ui_screens.cpp`
- Modify: `firmware/src/app_globals.h`

**Interfaces:**
- Consumes: Task 3 的 `LowBatteryLatch::consume_first_entry()`、Task 6 的輪詢
- Produces: `g_low_battery_notice_until_ms`（`uint32_t`）、`drawLowBatteryNotice()`

- [ ] **Step 1: 加入常數與全域**

`app_globals.h`：

```cpp
/** 低電量提示顯示時長（ms）：SoT §13.16 顯示一次後自動消失 */
constexpr uint32_t LOW_BATTERY_NOTICE_MS = 3000;
```

main.cpp 全域區：

```cpp
// Impl-Phase H：低電量提示的顯示截止時間（0 = 未顯示）
uint32_t g_low_battery_notice_until_ms = 0;
```

- [ ] **Step 2: 在 pollBattery 觸發提示**

在 Task 6 的 `pollBattery()` STEP 03 末尾加入：

```cpp
    // STEP 04: §13.16 — 案件進行中首次跨進低電量顯示一次提示，不發聲
    //          Training 一併適用（同樣耗電，練習時也需要知道電池狀況）
    const bool in_active_case = (globalState == GLOBAL_OHCA)
                             || (globalState == GLOBAL_VENT);
    if (in_active_case && g_battery_latch.consume_first_entry()) {
        g_low_battery_notice_until_ms = now_ms + LOW_BATTERY_NOTICE_MS;
        Serial.println("[FUEL] 低電量提示觸發（§13.16，不發聲）");
    }
```

- [ ] **Step 3: 繪製提示並接進 presentFrame**

`ui_screens.cpp`：

```cpp
/**
 * §13.16 低電量提示：案件進行中首次跨進低電量時顯示 3 秒。
 * 依規格不發聲；時間到後由 presentFrame 自動停止繪製。
 */
void drawLowBatteryNotice() {
    // STEP 01: 未在顯示期間就不畫
    if (g_low_battery_notice_until_ms == 0 || millis() > g_low_battery_notice_until_ms) {
        return;
    }

    // STEP 02: 置中兩行提示，蓋在當前畫面之上
    useZhFont();
    display.setTextSize(1);
    display.setTextColor(COLOR_ACCENT_WARN);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString("低電量", SCREEN_W / 2, SCREEN_H / 2 - 10);
    display.drawString("建議接上行動電源", SCREEN_W / 2, SCREEN_H / 2 + 10);
}
```

在 `presentFrame()` 的 STEP 01 之後、STEP 02 之前插入（並把原 STEP 02 重編為 STEP 03）：

```cpp
    // STEP 02: §13.16 低電量提示（顯示期間蓋在畫面最上層）
    drawLowBatteryNotice();
```

- [ ] **Step 4: 上機驗證**

因為要驗低電量，需讓電量降到 20% 以下。若電池電量充足不便實測，暫時把 `LOW_BATTERY_ENTER_PERCENT` 改成高於當前電量的值（例如 90）燒錄驗證，驗完改回 20 重新燒錄。

Run: `cd firmware && pio run -e esp32-s3-devkitc-1 -t upload --upload-port $(ls /dev/cu.usbmodem* | head -1)`

Expected: 進 OHCA 後出現一次「低電量／建議接上行動電源」，3 秒後消失、**不發聲**，之後電量圖示持續閃爍；離開再進 OHCA 不再重複顯示

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/main.cpp src/ui_screens.cpp src/app_globals.h
git commit -m "[PHASE-H] feat: 執行中低電量一次性提示（SoT §13.16）

案件進行中首次跨進 20% 顯示一次「低電量／建議接上行動電源」，3 秒自動消，之後只
閃圖示。依規格不發聲——這是 SoT 明文，不是疏漏。

「案件進行中」含 Training：練習同樣耗電，救護員需要知道電池狀況。純選單瀏覽不觸發，
避免待機時跳提示。"
```

---

## Task 11：§20.3 低電量開案確認框

**Files:**
- Modify: `firmware/src/input_handler.cpp`（OHCA 入口的按鍵處理）
- Modify: `firmware/src/main.cpp`（新 sub-state 分支）
- Modify: `firmware/src/ui_screens.cpp`

**Interfaces:**
- Consumes: Task 6 的 `g_battery_low`、既有 `drawConfirmDialog()`
- Produces: `SUBSTATE_LOW_BATTERY_CONFIRM` 列舉值、`drawLowBatteryStartConfirm()`

- [ ] **Step 1: 新增 sub-state 列舉**

在 `app_globals.h` 既有 `SUBSTATE_*` 列舉的最後加入（沿用既有編號慣例，接續最大值）：

```cpp
    /** Impl-Phase H：低電量下開案的確認對話框（SoT §20.3） */
    SUBSTATE_LOW_BATTERY_CONFIRM,
```

- [ ] **Step 2: 先把建案流程抽成共用 helper**

建案目前是 `input_handler.cpp` 主選單分派裡 `case 0:` 的 12 行 inline 程式碼（`globalState = GLOBAL_OHCA;` 起算到 `Serial.println("[OHCA] Case start (START_FLASH)");`）。確認框確認後需要走完全相同的流程，這是第 2 個呼叫點——依 EXTRACT-SHARED-HELPER 應該抽出，不可複製一份。

在 `input_handler.cpp` 加入：

```cpp
/**
 * 建立並啟動一筆 OHCA 案件。
 * 由主選單直接進案與 §20.3 低電量確認後進案兩處共用，避免兩份初始化邏輯分歧。
 */
static void startOhcaCase() {
    // STEP 01: 切換全域狀態與案件模式（訓練後不殘留 TRAINING）
    globalState = GLOBAL_OHCA;
    g_case_mode = CASE_MODE_OHCA;
    dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, 0);

    // STEP 02: 案件時間基準
    startFlashStartMs = millis();
    caseStartMs       = millis();
    caseStartEpochMs  = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 對時前 = 0

    // STEP 03: 事件與計數歸零
    eventCount      = 0;
    nextEventId     = 1;
    ohcaLastEpiMs   = 0;
    ohcaPrevSinceMs = 0;
    alarmMuted      = false;

    // STEP 04: SoT §16.6 新 case 起始為「App未同步」
    g_ohca_live_synced_at_ms = 0;
    resetSubState();
    Serial.println("[OHCA] Case start (START_FLASH)");
}
```

把主選單 `case 0:` 的那 12 行替換為低電量攔截 + 呼叫 helper：

```cpp
                    case 0:  // OHCA Case
                        // STEP 01.01: §20.3 — 低電量下開案需先確認，確認前不建立案件
                        if (g_battery_low) {
                            globalState  = GLOBAL_OHCA;
                            ohcaSubState = SUBSTATE_LOW_BATTERY_CONFIRM;
                            break;
                        }
                        startOhcaCase();
                        break;
```

> 註：攔截時先把 `globalState` 設成 `GLOBAL_OHCA` 才進得了 OHCA 的 sub-state 分派，但**尚未呼叫 `startOhcaCase()`**，所以案件還沒建立——選「否」時只要退回主選單即可，不會留下空案件。

- [ ] **Step 3: 繪製與按鍵處理**

`ui_screens.cpp`：

```cpp
/**
 * §20.3 低電量開案確認：低電量仍允許開始 OHCA，但需明確確認。
 * 選「是」進案、選「否」回主選單且不建立案件。
 */
void drawLowBatteryStartConfirm() {
    drawConfirmDialog("低電量", "建議接上行動電源\n是否開始？");
}
```

在 main.cpp 的 OHCA sub-state 分支清單（Task 8 已改為 `presentFrame()` 的那一區）加入：

```cpp
        if (ohcaSubState == SUBSTATE_LOW_BATTERY_CONFIRM) { drawLowBatteryStartConfirm(); presentFrame(); return; }
```

在 `input_handler.cpp` 的 OHCA sub-state 按鍵分派區（既有 `SUBSTATE_AMIO_CONFIRM` 那串 if 的相鄰位置）加入：

```cpp
        // STEP XX: LOW_BATTERY_CONFIRM：主鍵確認開案；返回鍵取消回主選單且不建案
        if (ohcaSubState == SUBSTATE_LOW_BATTERY_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                ohcaSubState = 0;
                globalState  = GLOBAL_MAIN_MENU;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                ohcaSubState = 0;
                startOhcaCase();  // Step 2 抽出的共用 helper
                return;
            }
            return;
        }
```

> 實作時把 `STEP XX` 換成該函式內的實際順號，並依 STEP-COMMENT-INSERT 規則把後續 STEP 編號往後重排。

- [ ] **Step 4: 上機驗證**

同 Task 10，必要時暫時調高 `LOW_BATTERY_ENTER_PERCENT` 以便觸發。

Expected:
1. 低電量下從主選單開 OHCA → 出現「低電量／建議接上行動電源／是否開始？」
2. 按主鍵 → 正常進入 OHCA 並開始計時
3. 按返回鍵 → 回主選單，**未建立案件**（進歷史列表確認沒有多出一筆）

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/input_handler.cpp src/main.cpp src/ui_screens.cpp src/app_globals.h
git commit -m "[PHASE-H] feat: 低電量開案確認框（SoT §20.3）

低電量仍允許開案（規格明文：不強制關機、只提醒），但要求明確確認。選否回主選單
且不建立案件——驗收時要進歷史列表確認沒有多出一筆空案件。

複用既有 drawConfirmDialog 與 SUBSTATE_AMIO_CONFIRM 的分派 pattern，不另造輪子。"
```

---

## Task 12：系統設定選單新增「電池資訊」

**Files:**
- Modify: `firmware/lib/ui_settings/ui_settings.h`（游標常數）
- Modify: `firmware/lib/ui_settings/ui_settings.cpp`（項目 Y 座標與繪製）
- Modify: `firmware/src/input_handler.cpp:13`（`SETTINGS_MENU_COUNT`）
- Test: `firmware/test/test_settings_ui/test_main.cpp`（既有「顯示 4 項目」測試需同步）

**Interfaces:**
- Consumes: 無
- Produces: `SETTINGS_CURSOR_BATTERY_INFO`（值 4）、`SETTINGS_MENU_COUNT` 由 4 增為 5

⚠️ 這是本計畫唯一動到 Phase G 已驗收功能的地方。改完必須跑 `test_settings_ui` 與 `test_settings` 確認沒有回歸。

- [ ] **Step 1: 先跑既有測試建立基準**

Run: `cd firmware && pio test -e native -f test_settings_ui -f test_settings`
Expected: 全部通過（記下通過數，改完要比對）

- [ ] **Step 2: 修改選單項目與邊界**

`firmware/lib/ui_settings/ui_settings.h` —— 在既有游標常數（`SETTINGS_CURSOR_DEVICE_NAME 0` ~ `SETTINGS_CURSOR_VENT_VOL 3`）之後加入：

```cpp
#define SETTINGS_CURSOR_BATTERY_INFO 4   // Phase H：電池資訊（導覽項，按主鍵進子畫面）
```

`firmware/lib/ui_settings/ui_settings.cpp` —— 在既有 `SETTINGS_ITEM4_Y 150` 之後加入（沿用 40px 等間距）：

```cpp
#define SETTINGS_ITEM5_Y        190   // 電池資訊
```

並在 `drawSettingsMenu()` 的 STEP 04 查表迴圈之後、STEP 05 確認對話框之前插入（後續 STEP 編號依 STEP-COMMENT-INSERT 規則往後重排）：

```cpp
     // STEP 05: 電池資訊 — 導覽項，只有標籤沒有可調值，不進 kSettingsAdjustableItems 查表
     if (cursor == SETTINGS_CURSOR_BATTERY_INFO) {
         disp.fill_rect(SETTINGS_MENU_X, SETTINGS_ITEM5_Y, SETTINGS_CURSOR_WIDTH, SETTINGS_CURSOR_HEIGHT, SETTINGS_COLOR_WHITE);
     }
     disp.text("電池資訊", SETTINGS_MENU_X, SETTINGS_ITEM5_Y, SETTINGS_FONT_SIZE, SETTINGS_COLOR_WHITE);
```

> 不加進 `kSettingsAdjustableItems` 的理由：那張表是給「上下鍵調數值」的項目用的，電池資訊是按主鍵進子畫面的導覽項，語意不同。混進去會讓調值邏輯誤把它當可調項。

`firmware/src/input_handler.cpp:13` —— UP/DOWN wrap-around 用的項目數：

```cpp
#define SETTINGS_MENU_COUNT    5   // 裝置名稱 / 亮度 / 系統音量 / 通氣音量 / 電池資訊
```

- [ ] **Step 3: 跑測試確認沒有回歸**

Run: `cd firmware && pio test -e native -f test_settings_ui -f test_settings`

既有 `test_settings_ui/test_main.cpp` 的 G1.1 明確宣稱「drawSettingsMenu 顯示 4 項目」並逐項查表斷言，加第 5 項後必須同步：把該測試的項目清單加上「電池資訊」，並更新註解中的「4 項目」字樣。

Expected: 全部通過，且通過數應比 Step 1 的基準多（新增的第 5 項斷言）

- [ ] **Step 4: 上機驗證**

Expected: 設定選單出現第 5 項「電池資訊」，上下捲動在 5 項之間正確 wrap-around，其餘 4 項功能不變

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/ui_screens.cpp src/input_handler.cpp test/test_settings_ui test/test_settings
git commit -m "[PHASE-H] feat: 系統設定選單新增「電池資訊」項

SoT §19.1 的設定選單列表含此項，Phase G 當時只做了前 4 項。

這是 Phase H 電量功能唯一動到 Phase G 已驗收範圍的地方：項目數 4 → 5，游標
wrap-around 邊界同步調整，既有 settings 測試一併確認無回歸。"
```

---

## Task 13：電池資訊畫面

**Files:**
- Modify: `firmware/src/ui_screens.cpp`（`drawBatteryInfo()`）
- Modify: `firmware/src/main.cpp`（畫面分派 + snapshot 填值）
- Modify: `firmware/src/input_handler.cpp`（進出畫面的按鍵處理）
- Modify: `firmware/src/app_globals.h`（`settingsBatteryInfoMode` 宣告）
- Modify: `firmware/lib/ems_display_snapshot/ems_display_snapshot.h`（新 flag bit）
- Test: `firmware/test/test_display_snapshot/test_display_snapshot.cpp`

**Interfaces:**
- Consumes: Task 6 的電池全域、Task 12 的 `SETTINGS_CURSOR_BATTERY_INFO`
- Produces: `drawBatteryInfo()`、`settingsBatteryInfoMode`、`SNAP_FLAG_SETTINGS_BATTERY_INFO = 0x00080000`

- [ ] **Step 1: 實作畫面**

`ui_screens.cpp`：

```cpp
/**
 * 電池資訊畫面（SoT §19.1 設定選單第 5 項）。
 * 顯示電量百分比、電壓與充電狀態；燃料計不在線時明確標示，不顯示假數值。
 */
void drawBatteryInfo() {
    useZhFont();
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::top_left);

    // STEP 01: 標題
    drawCenteredText("電池資訊", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // STEP 02: 不在線時只顯示狀態，不編造數值
    if (g_battery_percent == BATTERY_PERCENT_ABSENT) {
        drawCenteredText("燃料計未偵測到", SCREEN_H / 2, COLOR_TEXT_MUTED);
        return;
    }

    // STEP 03: 電量與電壓
    char buf[32];
    snprintf(buf, sizeof(buf), "電量：%u%%", g_battery_percent);
    display.drawString(buf, 24, 70);

    snprintf(buf, sizeof(buf), "電壓：%u.%03u V",
             g_battery_millivolts / 1000, g_battery_millivolts % 1000);
    display.drawString(buf, 24, 100);

    // STEP 04: 充電狀態；Unknown 顯示「判斷中」而非猜一個狀態
    const char* state_text = "判斷中";
    if (g_battery_charge_state == ems::ChargeState::Charging) {
        state_text = "充電中";
    } else if (g_battery_charge_state == ems::ChargeState::Discharging) {
        state_text = "放電中";
    } else if (g_battery_charge_state == ems::ChargeState::Idle) {
        state_text = "靜置";
    }
    snprintf(buf, sizeof(buf), "充電狀態：%s", state_text);
    display.drawString(buf, 24, 130);
}
```

- [ ] **Step 2: 新增畫面狀態旗標（含 snapshot 同步）**

比照既有 `settingsEditorMode`（`app_globals.h:456`）的做法，新增一個 bool 表示「正在電池資訊畫面」：

`app_globals.h`：

```cpp
extern bool    settingsBatteryInfoMode;  // Phase H：true = 電池資訊子畫面顯示中
```

`input_handler.cpp` 與 `main.cpp` 各自加上對應的 `extern` / 定義（比照 `settingsEditorMode` 既有寫法）。

⚠️ **這個旗標必須同步進 DisplaySnapshot，否則進出電池資訊畫面不會重繪**（這正是連踩 4 次的同型 bug）。在 `ems_display_snapshot.h`：

```cpp
    SNAP_FLAG_SETTINGS_BATTERY_INFO = 0x00080000,  // Phase H：電池資訊子畫面顯示中
```

`DisplaySnapshotInputs` 加 `bool settingsBatteryInfo = false;`，`captureSnapshot()` 的 flag 區加：

```cpp
    if (in.settingsBatteryInfo)    s.flags |= SNAP_FLAG_SETTINGS_BATTERY_INFO;
```

main.cpp 的 `captureDisplaySnapshot()` 加 `in.settingsBatteryInfo = settingsBatteryInfoMode;`。

並在 `test_display_snapshot.cpp` 補一個對應測試（比照 Task 7 的 flag 測試寫法）：

```cpp
static void test_flag_settings_battery_info_sets_bit_0x80000() {
    DisplaySnapshotInputs in;
    in.settingsBatteryInfo = true;
    TEST_ASSERT_EQUAL_UINT32(SNAP_FLAG_SETTINGS_BATTERY_INFO, captureSnapshot(in).flags);
}
```

記得同步加進 `main()` 的 `RUN_TEST` 與既有的 `test_all_flags_bit_masks_are_unique` mask 清單。

- [ ] **Step 3: 接上按鍵與畫面分派**

`input_handler.cpp` 的設定選單主鍵處理中，游標在 `SETTINGS_CURSOR_BATTERY_INFO` 時設 `settingsBatteryInfoMode = true;`；該畫面內按返回鍵設回 `false`（比照既有設定子畫面的返回處理）。

main.cpp 的設定畫面分派中，在既有 `drawSettingsMenu(...)` 之前加入：

```cpp
            if (settingsBatteryInfoMode) {
                drawBatteryInfo();
                presentFrame();
                return;
            }
```

- [ ] **Step 4: 上機驗證**

Expected: 設定 → 電池資訊，顯示的電量與電壓和 Task 6 的 `[FUEL]` serial log 一致；進出畫面都有正確重繪；拔掉燃料計重開機後顯示「燃料計未偵測到」而非 0%

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/ui_screens.cpp src/main.cpp src/input_handler.cpp src/app_globals.h \
        lib/ems_display_snapshot/ems_display_snapshot.h \
        test/test_display_snapshot/test_display_snapshot.cpp
git commit -m "[PHASE-H] feat: 電池資訊畫面

顯示電量、電壓與充電狀態。兩個刻意的選擇：
- 燃料計不在線時顯示「燃料計未偵測到」，不顯示 0%（那會被讀成沒電）
- 充電狀態 Unknown 顯示「判斷中」而非猜一個——趨勢窗未滿時本來就不知道

settingsBatteryInfoMode 同步進 DisplaySnapshot（新 flag bit 0x80000）：不同步的話
進出這個子畫面不會觸發重繪，正是連踩 4 次的同型 bug。"
```

---

## Task 14：裝置資訊畫面接上真實電池資料

**Files:**
- Modify: `firmware/src/ui_screens.cpp`（裝置資訊畫面）

**Interfaces:**
- Consumes: Task 6 / Task 13 的電池全域
- Produces: 無新介面

- [ ] **Step 1: 定位並替換**

搜尋裝置資訊畫面中「電池」與「充電狀態」兩列目前的資料來源：

```bash
cd firmware && grep -n "電池\|充電狀態" src/ui_screens.cpp
```

若為寫死字串或留白，替換為與 Task 13 相同的取值邏輯（電量顯示 `%`、不在線顯示 `—`）。

- [ ] **Step 2: 上機驗證**

Expected: 設定 → 裝置資訊，電池欄位與電池資訊畫面顯示一致

- [ ] **Step 3: 跑完整測試**

Run: `cd firmware && pio test -e native`
Expected: 全部通過

- [ ] **Step 4: 韌體編譯確認**

Run: `cd firmware && pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS

- [ ] **Step 5: Commit**

```bash
cd firmware
git add src/ui_screens.cpp
git commit -m "[PHASE-H] feat: 裝置資訊畫面接上真實電池資料

SoT §19.7 的「電池：86%」「充電狀態」原為靜態內容，接上燃料計實際讀數。
不在線時顯示「—」而非 0%，與電池資訊畫面的處理一致。"
```

---

## 完成後的收尾

全部 14 個 task 完成後：

- [ ] 更新 `docs/pm-dev-spec.md §四 Phase H`，標記「低電量警告」已完成，其餘三項（螢幕常亮、邊充邊用、Type-C 插拔）仍未做
- [ ] 更新 `docs/power-module-purchase.md §10.6` 的 checklist：「韌體讀取邏輯尚未實作」該項改為已完成
- [ ] 在 `docs/progress.md` / `progress.html` 追加一筆進度（走 `pm-html-report` skill 的累加模式）

## 未涵蓋事項（來自 spec §9）

以下**不在**本計畫範圍，完成後仍為待辦：

- Impl-Phase H 其餘三項：螢幕常亮（§13.18）、邊充邊用測試、Type-C 插拔不中斷案件
- 通氣 overlay 與 Training 浮水印互撞的既有 bug
- 低電量區（接近 3.0V）的燃料計精度驗證——**這會影響 Task 3 的門檻準確度**，spec §10 已記錄
- 局部重繪優化（低電量閃爍期間每 500ms 全片重繪）
