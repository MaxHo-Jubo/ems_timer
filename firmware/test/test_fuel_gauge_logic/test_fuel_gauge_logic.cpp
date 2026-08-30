// EMS DoseSync Pro — Impl-Phase H：燃料計純邏輯測試
//
// 黃金值來自 2026-08-22 實機驗收（docs/power-module-purchase.md §10.8）：
//   VCELL raw=0xC030 → 3843.75mV，函式回傳 uint16_t 取整（無條件捨去）為 3843
//   SOC   raw=0x366A → 54.41%，函式回傳 uint8_t 取整（無條件捨去）為 54
#include <unity.h>
#include "fuel_gauge_logic.h"
#include "null_fuel_gauge.h"

void setUp()    {}
void tearDown() {}

static void test_vcell_golden_value_from_hardware_acceptance() {
    // 驗證實機值：0xC030 轉換為 3843mV，對應實測 3.844V（補表與四捨五入差異）
    // 0xC030 >> 4 = 3075 counts；3075 * 1.25mV = 3843.75mV → 取整 3843
    TEST_ASSERT_EQUAL_UINT16(3843, ems::vcell_raw_to_mv(0xC030));
}

static void test_vcell_zero_is_zero() {
    // 邊界值驗證：零輸入應回傳零
    TEST_ASSERT_EQUAL_UINT16(0, ems::vcell_raw_to_mv(0x0000));
}

static void test_vcell_uses_high_12_bits_only() {
    // 位元應用驗證：低 4 bit 是無效位元，改變它們不可影響結果
    TEST_ASSERT_EQUAL_UINT16(ems::vcell_raw_to_mv(0xC030), ems::vcell_raw_to_mv(0xC03F));
}

static void test_vcell_max_raw_returns_unclamped_value() {
    // 極端值行為鎖定：0xFFFF >> 4 = 4095 counts × 1.25 = 5118.75 → 5118mV
    // 單節 LiPo 物理上不可能到此值，但本層不做合理性上界——「讀值不可信」判定屬 FuelReading.valid 層
    // 本測試鎖住當前行為，避免日後加上界時成為無聲 regression
    TEST_ASSERT_EQUAL_UINT16(5118, ems::vcell_raw_to_mv(0xFFFF));
}

static void test_soc_golden_value_from_hardware_acceptance() {
    // 驗證實機值：0x366A 轉換為 54%，對應實測 54.4%（補表差異）
    // 0x36 = 54 整數；0x6A = 106 → 106/256 = 0.41 → 54.41% → 取整 54
    TEST_ASSERT_EQUAL_UINT8(54, ems::soc_raw_to_percent(0x366A));
}

static void test_soc_clamps_above_100() {
    // 安全約束驗證：燃料計剛充飽時可能回報 >100%，UI 不該顯示 103%
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0x6700));
}

static void test_soc_zero_is_zero() {
    // 邊界值驗證：零輸入應回傳零
    TEST_ASSERT_EQUAL_UINT8(0, ems::soc_raw_to_percent(0x0000));
}

static void test_soc_max_raw_clamps_to_100() {
    // 極端值行為鎖定：垃圾讀值（0xFFFF = 255.99%）與正常超衝一樣夾到 100
    // 本層無法區分兩者，「讀值不可信」判定屬 FuelReading.valid 層
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0xFFFF));
}

static void test_soc_exactly_100_hits_clamp_boundary() {
    // 邊界測試：0x6400 = 整數 100、小數 0 —— 恰好落在 `>= SOC_PERCENT_MAX` 的邊界上
    TEST_ASSERT_EQUAL_UINT8(100, ems::soc_raw_to_percent(0x6400));
}

// ============================================================
//  Group 2: 充電狀態趨勢推導
// ============================================================

static void test_trend_before_window_full_is_unknown() {
    // 硬體無充電狀態訊號，開機後窗未滿時必須誠實回 Unknown，不可猜 Discharging
    ems::ChargeTrendTracker t;
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
}

static void test_trend_rising_beyond_deadband_is_charging() {
    // 窗內電壓上升超過死區判定為充電中（3810 - 3800 = +10mV > 3mV）
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);  // 窗滿：3810 - 3800 = +10mV > 死區
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
}

static void test_trend_falling_beyond_deadband_is_discharging() {
    // 窗內電壓下降超過死區判定為放電中（3800 - 3810 = -10mV < -3mV）
    ems::ChargeTrendTracker t;
    t.push(3810);
    t.push(3805);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_within_deadband_is_idle() {
    // 靜置時電壓會微幅抖動，死區內不可在充放電之間跳
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3800);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_slides_window_and_updates() {
    // 先判定充電，之後持續下降應翻成放電（窗會滑動，不是只看最初三筆）
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
    t.push(3805);
    t.push(3800);
    t.push(3795);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_reset_returns_to_unknown() {
    // reset() 清除取樣窗，狀態回到 Unknown；新資料應反映新趨勢，不受舊值殘留影響
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3805);
    t.push(3810);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
    t.reset();
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, t.state());
    // reset 後推入相反方向的值，應明確判定為下降（驗證樣本值不會殘留）
    t.push(3810);
    t.push(3805);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Discharging, t.state());
}

static void test_trend_ignores_middle_sample_only_endpoints_matter() {
    // 非單調數列：中間值飆高但頭尾相同 → 應為 Idle。
    // 若 newest 索引誤寫成 (head_+1)%N（讀到中間值），會誤判 Charging，本測試即失敗。
    // 現有的單調數列測試無法區分這兩種實作——中間樣本永遠站在正確方向上。
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3900);
    t.push(3800);
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_delta_equal_to_deadband_stays_idle() {
    // 死區邊界：變化剛好等於死區值（3mV）→ 不觸發，仍為 Idle
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3801);
    t.push(3803);   // delta = +3，等於死區，不應觸發
    TEST_ASSERT_EQUAL(ems::ChargeState::Idle, t.state());
}

static void test_trend_delta_just_over_deadband_charges() {
    // 死區邊界：變化剛好超過死區值 → 觸發 Charging。
    // 與上一個測試成對，鎖住死區常數——若 TREND_DEADBAND_MV 被誤改，兩者至少一個會 fail。
    ems::ChargeTrendTracker t;
    t.push(3800);
    t.push(3801);
    t.push(3804);   // delta = +4，超過死區
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, t.state());
}

// ============================================================
//  Group 3: 低電量遲滯與一次性提示
// ============================================================

static void test_low_battery_not_triggered_above_threshold() {
    // ENTER 門檻的寬鬆方向：21% 以上都不觸發（擋住門檻升到 21%）
    ems::LowBatteryLatch latch;
    latch.update(30);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_low_battery_triggers_at_or_below_20() {
    // ENTER 門檻的嚴格方向：20% 以下都觸發（擋住門檻降到 20%）
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
    // EXIT 門檻的嚴格方向：25% 以上才解除（擋住門檻升到 25%）
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
    // 先跌破 20% 觸發並消費首次事件，之後在 20~25% 之間來回抖動，不可重複觸發提示
    ems::LowBatteryLatch latch;
    latch.update(19);
    TEST_ASSERT_TRUE(latch.consume_first_entry());
    latch.update(23);
    latch.update(21);
    latch.update(24);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_first_entry_cleared_on_recovery_without_consume() {
    // 進入低電量後未消費即回升——解除時必須一併清掉待消費事件，
    // 否則 consume_first_entry() 會在非低電量狀態下回 true（違反契約）。
    // 這條清除線目前無任何測試守住，刪掉它 26 個測試全綠。
    ems::LowBatteryLatch latch;
    latch.update(18);   // 進入，掛起事件，但不消費
    latch.update(30);   // 回升解除
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_low_battery_not_triggered_at_21_percent() {
    // ENTER 門檻的寬鬆方向：21% 不應觸發。
    // 現有測試只驗 20% 會觸發（擋住門檻降到 19），沒有測試擋住門檻升到 21。
    ems::LowBatteryLatch latch;
    latch.update(21);
    TEST_ASSERT_FALSE(latch.is_low());
}

static void test_low_battery_does_not_clear_at_24_percent() {
    // EXIT 門檻的寬鬆方向：24% 不應解除。
    // 現有測試只驗 25% 會解除（擋住門檻升到 26），沒有測試擋住門檻降到 24。
    ems::LowBatteryLatch latch;
    latch.update(20);
    latch.update(24);
    TEST_ASSERT_TRUE(latch.is_low());
}

static void test_offline_sentinel_does_not_clear_low_battery() {
    // 哨兵值不得清除已鎖存的低電量狀態。
    // 255 = 燃料計不在線（spec §4.4）；若沒有契約防呆，255 >= 25 會誤判為電量回升。
    ems::LowBatteryLatch latch;
    latch.update(18);                          // 進入低電量
    latch.update(ems::BATTERY_PERCENT_ABSENT); // 感測器離線
    TEST_ASSERT_TRUE(latch.is_low());              // 鎖存不得被清
    TEST_ASSERT_TRUE(latch.consume_first_entry()); // 待消費事件也不得被清
}

// ============================================================
//  Group 4: Null backend 降級
// ============================================================

static void test_null_backend_not_present() {
    // NullFuelGauge 表示無實體硬體在線；上層無條件呼叫不需分支
    ems::NullFuelGauge nb;
    TEST_ASSERT_FALSE(nb.is_present());
}

static void test_null_backend_read_is_invalid() {
    // 不在線時 valid=false；caller 不可把 millivolts / percent 當合法讀數
    ems::NullFuelGauge nb;
    const ems::FuelReading r = nb.read();
    TEST_ASSERT_FALSE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(0, r.millivolts);  // 讀值無效時電壓須鎖住 0
    TEST_ASSERT_EQUAL_UINT8(0, r.percent);      // 讀值無效時百分比須鎖住 0
}

// ============================================================
//  Group：合理性判定（VCELL 上界與 SOC 原始值檢查）
// ============================================================

static void test_vcell_at_upper_bound_is_plausible() {
    // 4400 是界內，不是界外——邊界值本身必須可信，否則正常充飽會被判成異常
    TEST_ASSERT_TRUE(ems::is_plausible_vcell_mv(ems::PLAUSIBLE_MAX_MV));
}

static void test_vcell_above_upper_bound_is_implausible() {
    // 超過上界代表 I2C 位元翻轉或 EMI 干擾，讀值不可信
    TEST_ASSERT_FALSE(ems::is_plausible_vcell_mv(ems::PLAUSIBLE_MAX_MV + 1));
}

static void test_soc_raw_at_upper_bound_is_plausible() {
    // 高位元組 = PLAUSIBLE_SOC_WHOLE_MAX（界內）；低位元組帶非零值確認只看整數部分。
    // 不硬編字面值：PLAUSIBLE_SOC_WHOLE_MAX 註解明寫「需上機實測校正」，常數改動後
    // 測試須自動跟著新邊界走，否則會靜默測到已被校正掉的舊邊界
    const uint16_t raw = static_cast<uint16_t>(ems::PLAUSIBLE_SOC_WHOLE_MAX) << 8 | 0x80;
    TEST_ASSERT_TRUE(ems::is_plausible_soc_raw(raw));
}

static void test_soc_raw_above_upper_bound_is_implausible() {
    // 緊鄰上界外一格：高位元組 = PLAUSIBLE_SOC_WHOLE_MAX + 1，必須判為不可信。
    // 與 VCELL 側 test_vcell_above_upper_bound_is_implausible 成對，補上 SOC 側原本
    // 缺失的鑑別力——沒有這條，把 `<=` 鬆動一格（PLAUSIBLE_SOC_WHOLE_MAX + 1）
    // 不會被任何既有測試發現
    const uint16_t raw = static_cast<uint16_t>(ems::PLAUSIBLE_SOC_WHOLE_MAX + 1) << 8 | 0x80;
    TEST_ASSERT_FALSE(ems::is_plausible_soc_raw(raw));
}

static void test_soc_raw_garbage_is_implausible() {
    // 0xFFFF 是 I2C 匯流排故障的典型訊號：高位元組 255 遠超任何真實電量。
    // 若沒有這個檢查，soc_raw_to_percent() 會把它夾成 100 並標記 valid=true，
    // 把感測器故障偽裝成「滿電、一切正常」
    TEST_ASSERT_FALSE(ems::is_plausible_soc_raw(0xFFFF));
}

// ============================================================
//  Group：FuelReading constructor 映射正確性
// ============================================================

static void test_fuel_reading_constructor_maps_fields_correctly() {
    // 用三個互不相同的值，任何一組參數互換都會讓某條斷言變紅
    const ems::FuelReading r(true, 3843, 54);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(3843, r.millivolts);
    TEST_ASSERT_EQUAL_UINT8(54, r.percent);
}

// ============================================================
//  Group：make_reading() 統合判定測試
// ============================================================

static void test_make_reading_accepts_real_hardware_values() {
    // 2026-08-22 實機驗收值：VCELL raw=0xC030 → 3843mV、SOC raw=0x366A → 54%
    const ems::FuelReading r = ems::make_reading(0xC030, 0x366A);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT16(3843, r.millivolts);
    TEST_ASSERT_EQUAL_UINT8(54, r.percent);
}

static void test_make_reading_rejects_garbage_soc() {
    // VCELL 正常但 SOC 是垃圾：整筆必須無效。
    // 少了這條守衛，soc_raw_to_percent() 會把 0xFFFF 夾成 100 並標記 valid=true，
    // 把感測器故障偽裝成「滿電、一切正常」——低電量警示最不該有的失效方向
    const ems::FuelReading r = ems::make_reading(0xC030, 0xFFFF);
    TEST_ASSERT_FALSE(r.valid);
}

static void test_make_reading_rejects_implausible_vcell() {
    // SOC 正常但電壓超出單節 LiPo 的物理上界：整筆必須無效。
    // 0xFFF0 >> 4 = 4095 counts；4095 * 1.25 = 5118mV > 4400，超界
    const ems::FuelReading r = ems::make_reading(0xFFF0, 0x366A);
    TEST_ASSERT_FALSE(r.valid);
}

static void test_make_reading_zero_percent_is_valid() {
    // 0% 是合法讀數（電池真的沒電），不可被當成異常濾掉——
    // 這正是整個 valid 旗標設計要防的混淆
    // 0x9600 >> 4 = 2400 counts；2400 * 1.25 = 3000mV（界內）
    const ems::FuelReading r = ems::make_reading(0x9600, 0x0000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT8(0, r.percent);
}

// ============================================================
//  Group 5: FuelReading → 顯示百分比的轉譯
// ============================================================

static void test_display_percent_absent_when_invalid() {
    // 讀不到就回哨兵。注意 percent 欄位這裡刻意帶 77——轉譯只能看 valid，
    // 不可因為 percent 落在合法範圍內就把它當真
    const ems::FuelReading r{false, 3800, 77};
    TEST_ASSERT_EQUAL_UINT8(ems::BATTERY_PERCENT_ABSENT, ems::to_display_percent(r));
}

static void test_display_percent_zero_is_not_absent() {
    // 0% 是合法讀數（真的沒電），絕不可被轉成哨兵——這正是不拿 0 當哨兵的理由
    const ems::FuelReading r{true, 3000, 0};
    TEST_ASSERT_EQUAL_UINT8(0, ems::to_display_percent(r));
}

static void test_display_percent_passes_valid_value_through() {
    const ems::FuelReading r{true, 3844, 54};
    TEST_ASSERT_EQUAL_UINT8(54, ems::to_display_percent(r));
}

static void test_display_percent_full_charge_passes_through() {
    // 100 與哨兵 255 都是「邊界值」，確認滿電不會被誤判成不在線
    const ems::FuelReading r{true, 4200, 100};
    TEST_ASSERT_EQUAL_UINT8(100, ems::to_display_percent(r));
}

// ============================================================
//  Group 6: apply_fuel_reading() —— pollBattery() 決策邏輯抽出的純函式
//  （Fix Round 1 C1/I1：兩個方向都要鎖，鑑別力檢查見 task-6-report.md）
// ============================================================

static void test_apply_fuel_reading_failure_preserves_latched_low_battery() {
    // 方向一（既有）：讀取失敗不得清除已鎖存的低電量——先把 latch 餵到 low，
    // 再餵 invalid reading，鎖存狀態必須原封不動地透過 outcome 回傳。
    ems::ChargeTrendTracker trend;
    ems::LowBatteryLatch    latch;
    latch.update(18);  // 先進入低電量

    const ems::BatteryPollOutcome outcome =
        ems::apply_fuel_reading(ems::FuelReading(), trend, latch);  // invalid reading

    TEST_ASSERT_TRUE(outcome.low_battery);
}

static void test_apply_fuel_reading_failure_does_not_fabricate_low_battery() {
    // 方向二（C1 指出、目前完全沒防護的方向）：讀取失敗不得憑空觸發低電量。
    // latch 先餵一筆正常電量（非低電量），再餵 invalid reading，outcome.low_battery
    // 必須維持 false——若實作誤寫成 latch.update(reading.percent)，無效讀值的
    // percent 預設 0 會被判成跌破 20% 門檻，這條測試必須抓到。
    ems::ChargeTrendTracker trend;
    ems::LowBatteryLatch    latch;
    latch.update(50);  // 電量正常，非低電量

    const ems::BatteryPollOutcome outcome =
        ems::apply_fuel_reading(ems::FuelReading(), trend, latch);  // invalid reading

    TEST_ASSERT_FALSE(outcome.low_battery);
}

static void test_apply_fuel_reading_failure_returns_absent_defaults() {
    // 讀取失敗時三個欄位都要回到安全預設：percent 用哨兵、millivolts 歸零、
    // charge_state 回 Unknown（沒有可信讀值可判斷趨勢）。
    ems::ChargeTrendTracker trend;
    ems::LowBatteryLatch    latch;

    const ems::BatteryPollOutcome outcome =
        ems::apply_fuel_reading(ems::FuelReading(), trend, latch);

    TEST_ASSERT_EQUAL_UINT8(ems::BATTERY_PERCENT_ABSENT, outcome.percent);
    TEST_ASSERT_EQUAL_UINT16(0, outcome.millivolts);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, outcome.charge_state);
}

static void test_apply_fuel_reading_success_updates_all_fields() {
    // 讀取成功時三者都要正確更新：percent 透傳、趨勢推進（窗未滿仍是 Unknown，
    // 但樣本已確實被 push，見下一個測試驗證窗滿後會變化）、latch 依門檻判定。
    ems::ChargeTrendTracker trend;
    ems::LowBatteryLatch    latch;

    const ems::BatteryPollOutcome outcome =
        ems::apply_fuel_reading(ems::FuelReading(true, 3844, 18), trend, latch);

    TEST_ASSERT_EQUAL_UINT8(18, outcome.percent);
    TEST_ASSERT_EQUAL_UINT16(3844, outcome.millivolts);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, outcome.charge_state);  // 窗未滿（1/3）
    TEST_ASSERT_TRUE(outcome.low_battery);  // 18% <= LOW_BATTERY_ENTER_PERCENT(20)，應觸發
}

static void test_apply_fuel_reading_failure_resets_trend_window() {
    // 讀取失敗必須重置趨勢視窗，讓讀取恢復後從乾淨的窗重新累積，
    // 不可沿用失敗前殘留的樣本混算趨勢。
    ems::ChargeTrendTracker trend;
    ems::LowBatteryLatch    latch;

    // STEP 01: 連續餵 3 筆遞增電壓填滿窗（TREND_WINDOW_SAMPLES = 3），確認已判定 Charging
    ems::apply_fuel_reading(ems::FuelReading(true, 3800, 60), trend, latch);
    ems::apply_fuel_reading(ems::FuelReading(true, 3820, 60), trend, latch);
    const ems::BatteryPollOutcome filled =
        ems::apply_fuel_reading(ems::FuelReading(true, 3850, 60), trend, latch);
    TEST_ASSERT_EQUAL(ems::ChargeState::Charging, filled.charge_state);

    // STEP 02: 餵一筆讀取失敗，趨勢窗必須被清空
    ems::apply_fuel_reading(ems::FuelReading(), trend, latch);

    // STEP 03: 失敗後只餵 1 筆有效值——窗未滿（1/3），必須回 Unknown，
    //          不可沿用失敗前殘留的樣本判斷
    const ems::BatteryPollOutcome after_recover =
        ems::apply_fuel_reading(ems::FuelReading(true, 3900, 60), trend, latch);
    TEST_ASSERT_EQUAL(ems::ChargeState::Unknown, after_recover.charge_state);
}

// ============================================================
//  Group 7A: battery_segments_for_percent() —— 四格電量圖示格數換算
//  （Task 9 裁決 4：從 ui_screens.cpp 的 static 函式抽出，native 才測得到；
//    每個分界的兩側各一條，8 個邊界值 0/24/25/49/50/74/75/100）
// ============================================================

static void test_battery_segments_at_0_percent_is_1() {
    // 下界：SOC 為 0（真的沒電）仍回 1，外框本身已表示「有讀到值」
    TEST_ASSERT_EQUAL_UINT8(1, ems::battery_segments_for_percent(0));
}

static void test_battery_segments_at_24_percent_is_1() {
    // 第一個分界的寬鬆方向：24% 仍屬第 1 級（擋住門檻降到 24）
    TEST_ASSERT_EQUAL_UINT8(1, ems::battery_segments_for_percent(24));
}

static void test_battery_segments_at_25_percent_is_2() {
    // 第一個分界的嚴格方向：25% 起進第 2 級（擋住門檻升到 26）
    TEST_ASSERT_EQUAL_UINT8(2, ems::battery_segments_for_percent(25));
}

static void test_battery_segments_at_49_percent_is_2() {
    // 第二個分界的寬鬆方向：49% 仍屬第 2 級
    TEST_ASSERT_EQUAL_UINT8(2, ems::battery_segments_for_percent(49));
}

static void test_battery_segments_at_50_percent_is_3() {
    // 第二個分界的嚴格方向：50% 起進第 3 級
    TEST_ASSERT_EQUAL_UINT8(3, ems::battery_segments_for_percent(50));
}

static void test_battery_segments_at_74_percent_is_3() {
    // 第三個分界的寬鬆方向：74% 仍屬第 3 級
    TEST_ASSERT_EQUAL_UINT8(3, ems::battery_segments_for_percent(74));
}

static void test_battery_segments_at_75_percent_is_4() {
    // 第三個分界的嚴格方向：75% 起滿格
    TEST_ASSERT_EQUAL_UINT8(4, ems::battery_segments_for_percent(75));
}

static void test_battery_segments_at_100_percent_is_4() {
    // 上界：滿電仍是 4 格，不因合法範圍頂端而溢出
    TEST_ASSERT_EQUAL_UINT8(4, ems::battery_segments_for_percent(100));
}

// ============================================================
//  Group 7: compute_low_battery_blink_on() —— 低電量閃爍相位
//  （Fix Round 1 A：抽出前 captureDisplaySnapshot() 內這三行完全沒有測試保護，
//    燃料計不在線時未加守衛會造成永久性的每 500ms 無效全螢幕重繪）
// ============================================================

static void test_blink_offline_sentinel_forces_false_even_when_low() {
    // 核心修復：不在線（percent = 哨兵）時，即使 low 鎖存仍是 true，也必須回 false——
    // 圖示本來就不畫，讓相位繼續翻轉只會造成無意義的全螢幕重繪。沒這條等於沒修。
    TEST_ASSERT_FALSE(ems::compute_low_battery_blink_on(ems::BATTERY_PERCENT_ABSENT, true, 0));
}

static void test_blink_not_low_stays_false_while_online() {
    // 在線但非低電量：不閃爍
    TEST_ASSERT_FALSE(ems::compute_low_battery_blink_on(50, false, 0));
}

static void test_blink_phase_boundary_499ms_is_on() {
    // 499 / 500 = 0（整數除法）→ 0 % 2 == 0 → 亮相位
    TEST_ASSERT_TRUE(ems::compute_low_battery_blink_on(15, true, 499));
}

static void test_blink_phase_boundary_500ms_is_off() {
    // 500 / 500 = 1 → 1 % 2 == 0 為 false → 滅相位；跨過半週期邊界翻轉
    TEST_ASSERT_FALSE(ems::compute_low_battery_blink_on(15, true, 500));
}

static void test_blink_phase_boundary_999ms_is_off() {
    // 999 / 500 = 1 → 仍在第二個半週期內，滅相位
    TEST_ASSERT_FALSE(ems::compute_low_battery_blink_on(15, true, 999));
}

static void test_blink_phase_boundary_1000ms_is_on() {
    // 1000 / 500 = 2 → 2 % 2 == 0 → 翻回亮相位，完成一次 1Hz 全週期
    TEST_ASSERT_TRUE(ems::compute_low_battery_blink_on(15, true, 1000));
}

// ============================================================
//  Group 7B: should_draw_battery_icon() —— 電量圖示是否該畫的決策
//  （2026-08-23 controller 驗證抓到的 Critical：ui_screens.cpp 曾用
//    `if (!lowBlinkOn) return;` 單獨判斷是否跳過繪製，但 lowBlinkOn 在非低電量時
//    恆為 false，導致電量正常（絕大多數時間）時圖示完全不會出現。抽成純函式後
//    這四條測試直接鎖住三種情境，ui_screens.cpp 本身不在 native build 範圍內、
//    這個決策若留在 render 程式碼裡是測不到的）
// ============================================================

static void test_should_draw_absent_is_false_even_when_low_and_blink_on() {
    // 不在線一律不畫，即使低電量且正處於閃爍亮相位——連外框都不畫，
    // 避免「不在線」被讀成「已讀到電量、只是圖示恰好在暗相位」
    TEST_ASSERT_FALSE(ems::should_draw_battery_icon(ems::BATTERY_PERCENT_ABSENT, true, true));
}

static void test_should_draw_present_not_low_is_true_even_when_blink_off() {
    // 核心回歸測試：這正是實際會發生的路徑——電量正常時 low=false，
    // compute_low_battery_blink_on() 在這種情況下恆回 false，本測試鎖住
    // 「lowBlinkOn==false 但非低電量」時仍必須畫，不可被誤判成跳過繪製
    TEST_ASSERT_TRUE(ems::should_draw_battery_icon(50, false, false));
}

static void test_should_draw_present_low_and_blink_on_is_true() {
    // 低電量且處於閃爍亮相位 → 畫
    TEST_ASSERT_TRUE(ems::should_draw_battery_icon(15, true, true));
}

static void test_should_draw_present_low_and_blink_off_is_false() {
    // 低電量且處於閃爍暗相位 → 不畫（這才是 lowBlinkOn==false 真正該跳過繪製的情境）
    TEST_ASSERT_FALSE(ems::should_draw_battery_icon(15, true, false));
}

// ============================================================
//  Group 7C: is_low_battery_notice_visible() —— §13.16 低電量提示顯示期間判斷
//  （2026-08-23 handover §3-A3 重寫：此狀態必須是純函式而非留在 UI 層用 millis()
//    現算，否則不進 DisplaySnapshot、memcmp 判定不重繪，提示既不會出現也不會消失）
// ============================================================

/// 測試用的固定觸發時間戳（ms）：與 ems::LOW_BATTERY_NOTICE_MS 搭配推導邊界值，
/// 避免各測試各自硬編寫死的時間常數（2026-08-23 fix round 1 B8）
constexpr uint32_t NOTICE_TEST_START_MS = 1000;
/// 顯示視窗內最後一毫秒的經過時間，由 LOW_BATTERY_NOTICE_MS 推導——常數改動時邊界自動跟著走
constexpr uint32_t NOTICE_LAST_VISIBLE_MS = ems::LOW_BATTERY_NOTICE_MS - 1;
/// 溢位案例的起始時間戳：刻意選接近 uint32_t 上限的值，用於驗證溢位前後行為
constexpr uint32_t NOTICE_OVERFLOW_START_MS = 0xFFFFFF00;
/// 溢位「前」案例的實際經過時間（ms）：遠小於顯示視窗，是本設計唯一有鑑別力的案例（見 B7）
constexpr uint32_t NOTICE_OVERFLOW_ELAPSED_MS = 80;
/// 溢位「後」案例的實際經過時間（ms）：now_ms 已回繞成一個很小的值
constexpr uint32_t NOTICE_WRAPPED_ELAPSED_MS = 512;

/// SoT §13.16 明文鎖定的顯示時長（ms）。刻意獨立於 ems::LOW_BATTERY_NOTICE_MS、
/// 不從實作常數推導——理由見下方 test_notice_duration_matches_sot_spec()
constexpr uint32_t EXPECTED_SOT_LOW_BATTERY_NOTICE_MS = 3000;

static void test_notice_duration_matches_sot_spec() {
    // 鎖規格：SoT §13.16 明文要求顯示 3000ms，這是規格常數本身，不是實作細節。
    // 下面幾條邊界測試的期望值全部由 ems::LOW_BATTERY_NOTICE_MS 推導（2026-08-23
    // fix round 1 B8），常數改動時邊界會自動跟著走，但這也表示那些測試只證明了
    // 「函式用了自己的常數」，證明不了 §13.16 要求的具體數值——把
    // LOW_BATTERY_NOTICE_MS 從 3000 改成 5000，那些測試依然全過。
    // EXPECTED_SOT_LOW_BATTERY_NOTICE_MS 這個獨立常數就是刻意要鎖住規格本身，
    // 不能跟著實作常數一起漂移（2026-08-23 fix round 3 G3）。
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_SOT_LOW_BATTERY_NOTICE_MS, ems::LOW_BATTERY_NOTICE_MS);
}

static void test_notice_inactive_is_never_visible() {
    // 從未觸發過 → 一律不顯示，即使 now == start
    TEST_ASSERT_FALSE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(false, NOTICE_TEST_START_MS), NOTICE_TEST_START_MS));
}

static void test_notice_at_start_is_visible() {
    // 觸發當下（經過時間 0ms）即應顯示
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(true, NOTICE_TEST_START_MS), NOTICE_TEST_START_MS));
}

static void test_notice_boundary_last_visible_ms_is_visible() {
    // 邊界的寬鬆方向：經過 LOW_BATTERY_NOTICE_MS - 1（< 顯示視窗）仍在顯示期間內
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(true, NOTICE_TEST_START_MS),
        NOTICE_TEST_START_MS + NOTICE_LAST_VISIBLE_MS));
}

static void test_notice_boundary_exact_duration_is_hidden() {
    // 邊界的嚴格方向：經過恰好 LOW_BATTERY_NOTICE_MS → 已過期，不再顯示
    TEST_ASSERT_FALSE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(true, NOTICE_TEST_START_MS),
        NOTICE_TEST_START_MS + ems::LOW_BATTERY_NOTICE_MS));
}

static void test_notice_boundary_before_millis_overflow_is_visible() {
    // 本設計唯一有鑑別力的溢位案例（2026-08-23 fix round 1 B7 修正）：
    // start=0xFFFFFF00、now=start+80（尚未回繞），實際經過 80ms（遠小於顯示視窗）。
    // 若誤寫成「截止時間 + 大小比較」（`now_ms > until_ms`）：until_ms = start_ms +
    // LOW_BATTERY_NOTICE_MS 已先回繞成一個很小的值（0x00000AB8），
    // `0xFFFFFF50 > 0x00000AB8` 為 true → 誤判成已過期 → 回 false，與正確答案 true
    // 不同——這條測試才真正證明「無號經過時間差」寫法的必要性；下一條溢位「後」的案例
    // 本身沒有這個鑑別力（見該測試註解）。
    const uint32_t now_ms = NOTICE_OVERFLOW_START_MS + NOTICE_OVERFLOW_ELAPSED_MS;
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(true, NOTICE_OVERFLOW_START_MS), now_ms));
}

static void test_notice_survives_millis_overflow() {
    // 溢位「後」：now 已回繞到一個很小的值，實際經過時間仍是 NOTICE_WRAPPED_ELAPSED_MS
    // （< 顯示視窗）。這個案例本身沒有鑑別力——`now_ms > until_ms` 的錯誤寫法在此剛好
    // 也算出「未過期」，兩種實作都會通過（2026-08-23 fix round 1 B7：原本這是本設計
    // 唯一的溢位測試，但它證明不了任何事；鑑別力由上一條案例提供），保留做為
    // 「回繞前後都要正確」的完整性檢查。
    const uint32_t now_ms = NOTICE_OVERFLOW_START_MS + NOTICE_WRAPPED_ELAPSED_MS;
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_visible(
        ems::LowBatteryNoticeState(true, NOTICE_OVERFLOW_START_MS), now_ms));
}

// ============================================================
//  Group 7D: is_low_battery_notice_context() —— §13.16 提示適用情境判斷
//  （2026-08-23 fix round 1 A4：抽出前留在 main.cpp pollBattery() 內用裸 if 判斷，
//    ui_screens.cpp 不進 native build，main.cpp 同樣不進 native build，
//    這個決策原本完全沒有測試涵蓋）
// ============================================================

static void test_notice_context_ohca_is_applicable() {
    // OHCA 案件進行中（含 Training：呼叫端傳入的 in_ohca 已涵蓋）一律適用
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_context(true, false, false));
}

static void test_notice_context_vent_without_pre_is_applicable() {
    // VENT 進行中且已離開 VENT_PRE 準備畫面 → 適用
    TEST_ASSERT_TRUE(ems::is_low_battery_notice_context(false, true, false));
}

static void test_notice_context_vent_with_pre_is_not_applicable() {
    // 通氣尚未真正開始（VENT_PRE 準備畫面）不算「案件進行中」（A3 裁決）
    TEST_ASSERT_FALSE(ems::is_low_battery_notice_context(false, true, true));
}

static void test_notice_context_neither_ohca_nor_vent_is_not_applicable() {
    // 純選單瀏覽等情境：兩者皆非 → 不適用
    TEST_ASSERT_FALSE(ems::is_low_battery_notice_context(false, false, false));
}

// ============================================================
//  Group 7E: low_battery_notice_tick() —— §13.16 提示狀態機（2026-08-23 fix round 2
//  E1 CRITICAL/E2/E3：守衛、觸發、逾期復歸、離開情境復歸原本分散在 main.cpp 的
//  tryStartLowBatteryNotice() 與呼叫端的短路求值裡，main.cpp 不進 native build，
//  這些狀態轉換原本完全沒有真實序列測試涵蓋——舊測試
//  test_notice_hidden_after_reset_ignores_wrapped_window 只是把 active=false 傳進
//  is_low_battery_notice_visible()，驗證的是該純函式既有契約，刪掉 main.cpp 裡的
//  復歸邏輯它照樣通過，現已移除並由本組真正驗證狀態轉換的測試取代。
// ============================================================

/// tick 測試共用的低電量觸發百分比：只要低於 ems::LowBatteryLatch::LOW_BATTERY_ENTER_PERCENT
/// 即可掛起 pending 事件，具體差距不影響測試意圖，由門檻常數推導（2026-08-23 fix round 3 G6）
constexpr uint8_t TICK_TEST_LOW_PERCENT = ems::LowBatteryLatch::LOW_BATTERY_ENTER_PERCENT - 2;
/// tick 測試共用的「觸發後、遠未逾期」時間偏移（ms），用於驗證離開情境立即復歸。
/// 由 LOW_BATTERY_NOTICE_MS 推導（顯示視窗的 1/6）而非硬編字面值——硬編值若未來
/// 剛好不再遠小於顯示視窗，本該測「離開情境復歸」的測試會改成測到「逾期復歸」，
/// 依然通過但鑑別力悄悄流失
constexpr uint32_t TICK_TEST_SHORT_OFFSET_MS = ems::LOW_BATTERY_NOTICE_MS / 6;
/// tick 測試共用的重複 tick 間隔（ms），用於驗證多輪 tick 不重複觸發
constexpr uint32_t TICK_TEST_REPEAT_STEP_MS = 100;

static void test_tick_out_of_context_does_not_consume_pending_event() {
    // CRITICAL E1：守衛必須收斂在 tick 內——非適用情境時即使 latch 有 pending 事件
    // 也不可消費。若守衛只靠呼叫端記得檢查，任何未來 caller 漏掉就會不可逆地把
    // pending 事件清掉且沒有任何錯誤訊號。
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);  // 進入低電量，掛起 pending 事件

    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(
        state, latch,
        /*in_ohca=*/false, /*in_vent=*/false, /*vent_pre_shown=*/false,
        /*now_ms=*/NOTICE_TEST_START_MS);

    TEST_ASSERT_FALSE(state.active);
    // 事件必須仍然 pending——tick 不得在非適用情境下把它消費掉
    TEST_ASSERT_TRUE(latch.consume_first_entry());
}

static void test_tick_entering_ohca_consumes_and_activates() {
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);

    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(
        state, latch,
        /*in_ohca=*/true, /*in_vent=*/false, /*vent_pre_shown=*/false,
        /*now_ms=*/NOTICE_TEST_START_MS);

    TEST_ASSERT_TRUE(state.active);
    TEST_ASSERT_EQUAL_UINT32(NOTICE_TEST_START_MS, state.start_ms);
    // 事件已被消費，之後同一輪不會再觸發第二次
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

static void test_tick_vent_pre_shown_does_not_consume() {
    // VENT_PRE 準備畫面不算「案件進行中」（A3 裁決），守衛仍要擋住消費
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);

    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(
        state, latch,
        /*in_ohca=*/false, /*in_vent=*/true, /*vent_pre_shown=*/true,
        /*now_ms=*/NOTICE_TEST_START_MS);

    TEST_ASSERT_FALSE(state.active);
    TEST_ASSERT_TRUE(latch.consume_first_entry());  // 事件未被消費
}

static void test_tick_leaving_context_deactivates_immediately() {
    // E2：提示顯示期間（遠未逾期）離開適用情境 → 下一 tick 立即復歸，不等 3 秒到期，
    // 否則不透明 panel 會整塊蓋在已切換的新頁面上（例如通氣結束回主選單）。
    // 鑑別力自檢：刪掉 tick 內「離開情境復歸」那段（STEP 01）會讓本測試變紅——
    // 少了 STEP 01，流程會落到 STEP 02（未逾期，TICK_TEST_SHORT_OFFSET_MS <
    // LOW_BATTERY_NOTICE_MS，跳過）與 STEP 03（latch 事件已消費，
    // consume_first_entry() 回 false），最終 STEP 04 對 state 不做任何寫入
    // （state.active 仍是 true），與本測試的期望 false 不同。
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(state, latch, true, false, false, NOTICE_TEST_START_MS);
    TEST_ASSERT_TRUE(state.active);

    // TICK_TEST_SHORT_OFFSET_MS 後離開情境（遠在 LOW_BATTERY_NOTICE_MS 之前）
    ems::low_battery_notice_tick(
        state, latch, false, false, false,
        NOTICE_TEST_START_MS + TICK_TEST_SHORT_OFFSET_MS);
    TEST_ASSERT_FALSE(state.active);
}

static void test_tick_expiry_deactivates() {
    // 鑑別力自檢：刪掉 tick 內「逾期復歸」那段（STEP 02）會讓本測試變紅——少了
    // STEP 02，流程會落到 STEP 03（latch 事件已消費，consume_first_entry() 回
    // false），最終 STEP 04 對 state 不做任何寫入（state.active 仍是 true），
    // 與本測試的期望 false 不同。與上一條測試互斥情境（本條仍在情境內），確認
    // 兩段復歸邏輯各自獨立、缺一不可。
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(state, latch, true, false, false, NOTICE_TEST_START_MS);
    TEST_ASSERT_TRUE(state.active);

    // 仍在情境內，但已過顯示視窗（恰好 LOW_BATTERY_NOTICE_MS）
    ems::low_battery_notice_tick(
        state, latch, true, false, false,
        NOTICE_TEST_START_MS + ems::LOW_BATTERY_NOTICE_MS);
    TEST_ASSERT_FALSE(state.active);
}

static void test_tick_recovery_survives_millis_wraparound() {
    // 逾期復歸後，即使後續 tick 傳入「數值上看起來像落回顯示視窗內」的 now_ms
    // （模擬 millis() 完整繞回一輪後湊巧落在舊 start_ms 附近），也必須維持隱藏——
    // 一旦 active 復歸為 false 且 latch 沒有新的 pending 事件，後續 tick 無論
    // now_ms 為何都會維持 inactive，不需要額外的溢位判斷。
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(state, latch, true, false, false, NOTICE_OVERFLOW_START_MS);
    TEST_ASSERT_TRUE(state.active);

    // 逾期，復歸為 inactive
    ems::low_battery_notice_tick(
        state, latch, true, false, false,
        NOTICE_OVERFLOW_START_MS + ems::LOW_BATTERY_NOTICE_MS);
    TEST_ASSERT_FALSE(state.active);

    // 模擬 millis() 完整繞回後又落回「看起來像視窗內」的時間點——維持隱藏
    ems::low_battery_notice_tick(
        state, latch, true, false, false,
        NOTICE_OVERFLOW_START_MS + NOTICE_WRAPPED_ELAPSED_MS);
    TEST_ASSERT_FALSE(state.active);
}

static void test_tick_repeated_ticks_do_not_retrigger() {
    // 觸發一次後，後續多輪 tick（仍在情境內、仍在顯示視窗內）不應重新消費事件或
    // 改變 start_ms——latch 冪等（事件已於第一次 tick 消費，之後 consume_first_entry()
    // 恆回 false），重複呼叫是安全的。
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    ems::LowBatteryNoticeState state{};
    ems::low_battery_notice_tick(state, latch, true, false, false, NOTICE_TEST_START_MS);
    TEST_ASSERT_TRUE(state.active);
    TEST_ASSERT_EQUAL_UINT32(NOTICE_TEST_START_MS, state.start_ms);

    ems::low_battery_notice_tick(
        state, latch, true, false, false,
        NOTICE_TEST_START_MS + TICK_TEST_REPEAT_STEP_MS);
    TEST_ASSERT_TRUE(state.active);
    TEST_ASSERT_EQUAL_UINT32(NOTICE_TEST_START_MS, state.start_ms);

    ems::low_battery_notice_tick(
        state, latch, true, false, false,
        NOTICE_TEST_START_MS + TICK_TEST_REPEAT_STEP_MS * 2);
    TEST_ASSERT_TRUE(state.active);
    TEST_ASSERT_EQUAL_UINT32(NOTICE_TEST_START_MS, state.start_ms);
}

// ============================================================
//  Group 7F: low_battery_confirm_decide() —— §20.3 低電量開案確認框按鍵決策
//  （2026-08-30 Task 11 fix round 1：純函式決策核心，窮舉
//  {None, Ohca, Vent, Training} × {Primary, Back, Other} 全部 12 種組合，
//  確保三個目標（OHCA/VENT/Training）互不串線，不能只挑代表值讓覆蓋率看似齊全
//  實則有洞。⚠️ 本組只鎖住 low_battery_confirm_decide() 這個純函式自身的真值表，
//  不涵蓋 input_handler.cpp onShortPress() 的整合接線（那段活在 src/，native
//  環境不編譯）——整合層的覆蓋率缺口記在 handover §8 殘餘風險⑥（fix round 3 T 段）。
// ============================================================

/**
 * current=None, action=Primary → 確認框未顯示時任何按鍵都無意義，主鍵也不例外，
 * 不應誤判成「有東西可以啟動」。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_none_primary_stays_none_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::None,
                                         ems::ConfirmDialogAction::Primary);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=None, action=Back → 同上，維持 None 且不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_none_back_stays_none_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::None,
                                         ems::ConfirmDialogAction::Back);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=None, action=Other → 同上，維持 None 且不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_none_other_stays_none_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::None,
                                         ems::ConfirmDialogAction::Other);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Ohca, action=Primary → 主鍵確認：關閉確認框（next_target=None）且要求
 * 呼叫端啟動 Ohca。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_ohca_primary_closes_and_proceeds() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Ohca,
                                         ems::ConfirmDialogAction::Primary);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_TRUE(d.proceed);
}

/**
 * current=Ohca, action=Back → 返回鍵取消：關閉確認框，不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_ohca_back_closes_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Ohca,
                                         ems::ConfirmDialogAction::Back);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Ohca, action=Other → 其餘按鍵忽略：確認框維持顯示（next_target 仍是
 * Ohca），不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_ohca_other_keeps_ohca_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Ohca,
                                         ems::ConfirmDialogAction::Other);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Ohca, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Vent, action=Primary → 同 Ohca，關閉確認框並要求啟動 Vent。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_vent_primary_closes_and_proceeds() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Vent,
                                         ems::ConfirmDialogAction::Primary);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_TRUE(d.proceed);
}

/**
 * current=Vent, action=Back → 返回鍵取消：關閉確認框，不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_vent_back_closes_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Vent,
                                         ems::ConfirmDialogAction::Back);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Vent, action=Other → Vent 情境下維持顯示必須回 Vent，不可誤回其他
 * 目標——鎖住三個目標互不串線。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_vent_other_keeps_vent_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Vent,
                                         ems::ConfirmDialogAction::Other);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Vent, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Training, action=Primary → 同 Ohca/Vent，關閉確認框並要求啟動 Training。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_training_primary_closes_and_proceeds() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Training,
                                         ems::ConfirmDialogAction::Primary);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_TRUE(d.proceed);
}

/**
 * current=Training, action=Back → 返回鍵取消：關閉確認框，不啟動。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_training_back_closes_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Training,
                                         ems::ConfirmDialogAction::Back);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::None, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

/**
 * current=Training, action=Other → Training 情境下維持顯示必須回 Training，
 * 不可誤回其他目標——鎖住三個目標互不串線。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_confirm_decide_training_other_keeps_training_no_proceed() {
    // STEP 01: 呼叫決策函式，取得本次決策結果
    const ems::LowBatteryConfirmDecision d =
        ems::low_battery_confirm_decide(ems::LowBatteryConfirmTarget::Training,
                                         ems::ConfirmDialogAction::Other);
    // STEP 02: 斷言 d.next_target／d.proceed 兩個欄位皆符合真值表
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Training, d.next_target);
    TEST_ASSERT_FALSE(d.proceed);
}

// ============================================================
//  Group 7G: try_request_low_battery_start_confirm() —— §20.3 確認請求核心進場判斷
//  （2026-08-30 Task 11 fix round 3 P：取代 round 2 的 apply_low_battery_start_confirm_
//  request()——舊版無條件執行，「是否真的低電量」的守衛仍留在呼叫端；這輪把守衛也收
//  進本函式，唯一權威是 latch.is_low()。本組鎖住：低電量時攔截並正確設定/消費、非低
//  電量時完全不攔截（不動 target_out、不消費 latch）、三個 target 互不串線；不窮舉
//  （不像 Group 7F 的按鍵決策真值表），只需覆蓋代表情境。
// ============================================================

/**
 * latch.is_low() 為真（含 pending 事件）時呼叫 → 回傳 true，target_out 等於傳入的
 * target，且 pending 事件已被消費（呼叫端接著呼叫 consume_first_entry() 應回 false）。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_try_request_confirm_when_low_with_pending_returns_true_and_consumes() {
    // STEP 01: 準備一個剛跌破門檻、is_low()=true 且有 pending 低電量事件的 latch
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);  // 進入低電量，is_low()=true 且掛起 pending 事件

    // STEP 02: 呼叫核心進場判斷
    ems::LowBatteryConfirmTarget target_out = ems::LowBatteryConfirmTarget::None;
    const bool intercepted = ems::try_request_low_battery_start_confirm(
        target_out, latch, ems::LowBatteryStartTarget::Ohca);

    // STEP 03: 斷言已攔截、target_out 正確設定，且 pending 事件已被消費
    TEST_ASSERT_TRUE(intercepted);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Ohca, target_out);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

/**
 * latch.is_low() 為真但 pending 事件已被別處消費過（例如 §13.16 提示已先觸發）時呼叫
 * → 守衛只看 is_low()，不看 pending 事件，仍應回傳 true 並正確設定 target_out。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_try_request_confirm_when_low_without_pending_still_returns_true() {
    // STEP 01: 準備一個 is_low()=true 但 pending 事件已被消費過的 latch
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    TEST_ASSERT_TRUE(latch.consume_first_entry());  // 模擬 pending 事件已被別處（§13.16）消費

    // STEP 02: 呼叫核心進場判斷
    ems::LowBatteryConfirmTarget target_out = ems::LowBatteryConfirmTarget::None;
    const bool intercepted = ems::try_request_low_battery_start_confirm(
        target_out, latch, ems::LowBatteryStartTarget::Vent);

    // STEP 03: 斷言仍已攔截、target_out 正確設定，consume_first_entry() 呼叫後依然 false
    TEST_ASSERT_TRUE(intercepted);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Vent, target_out);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

/**
 * latch.is_low() 為假（非低電量）時呼叫 → 回傳 false，且完全不攔截：不動 target_out、
 * 不消費 latch。這是本輪要鎖住的核心契約——round 2 的測試完全沒驗到這件事。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_try_request_confirm_when_not_low_returns_false_and_untouched() {
    // STEP 01: 準備一個從未進入低電量的乾淨 latch，target_out 先設一個可辨識的哨兵值
    ems::LowBatteryLatch latch;
    ems::LowBatteryConfirmTarget target_out = ems::LowBatteryConfirmTarget::Training;  // 哨兵值

    // STEP 02: 呼叫核心進場判斷
    const bool intercepted = ems::try_request_low_battery_start_confirm(
        target_out, latch, ems::LowBatteryStartTarget::Ohca);

    // STEP 03: 斷言未攔截、target_out 維持哨兵值不變、latch 完全沒被消費
    TEST_ASSERT_FALSE(intercepted);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Training, target_out);
    TEST_ASSERT_FALSE(latch.consume_first_entry());
}

/**
 * 三個 target 值各測一次，確認 target_out 沒有互相串線（例如誤把 Training 寫成 Ohca）。
 * @param  無參數
 * @return void（斷言，無回傳值）
 */
static void test_try_request_confirm_three_targets_do_not_cross_wire() {
    // STEP 01: 準備一個 is_low()=true 的 latch，逐一呼叫三個 target，各自用一個獨立的
    //          target_out 變數承接
    ems::LowBatteryLatch latch;
    latch.update(TICK_TEST_LOW_PERCENT);
    ems::LowBatteryConfirmTarget ohca_out     = ems::LowBatteryConfirmTarget::None;
    ems::LowBatteryConfirmTarget vent_out     = ems::LowBatteryConfirmTarget::None;
    ems::LowBatteryConfirmTarget training_out = ems::LowBatteryConfirmTarget::None;
    const bool ohca_intercepted =
        ems::try_request_low_battery_start_confirm(ohca_out, latch, ems::LowBatteryStartTarget::Ohca);
    const bool vent_intercepted =
        ems::try_request_low_battery_start_confirm(vent_out, latch, ems::LowBatteryStartTarget::Vent);
    const bool training_intercepted =
        ems::try_request_low_battery_start_confirm(training_out, latch, ems::LowBatteryStartTarget::Training);

    // STEP 02: 逐一斷言，確認三次呼叫皆已攔截且互不影響彼此的 target_out
    TEST_ASSERT_TRUE(ohca_intercepted);
    TEST_ASSERT_TRUE(vent_intercepted);
    TEST_ASSERT_TRUE(training_intercepted);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Ohca, ohca_out);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Vent, vent_out);
    TEST_ASSERT_EQUAL(ems::LowBatteryConfirmTarget::Training, training_out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_vcell_golden_value_from_hardware_acceptance);
    RUN_TEST(test_vcell_zero_is_zero);
    RUN_TEST(test_vcell_uses_high_12_bits_only);
    RUN_TEST(test_vcell_max_raw_returns_unclamped_value);
    RUN_TEST(test_soc_golden_value_from_hardware_acceptance);
    RUN_TEST(test_soc_clamps_above_100);
    RUN_TEST(test_soc_zero_is_zero);
    RUN_TEST(test_soc_max_raw_clamps_to_100);
    RUN_TEST(test_soc_exactly_100_hits_clamp_boundary);
    RUN_TEST(test_trend_before_window_full_is_unknown);
    RUN_TEST(test_trend_rising_beyond_deadband_is_charging);
    RUN_TEST(test_trend_falling_beyond_deadband_is_discharging);
    RUN_TEST(test_trend_within_deadband_is_idle);
    RUN_TEST(test_trend_slides_window_and_updates);
    RUN_TEST(test_trend_reset_returns_to_unknown);
    RUN_TEST(test_trend_ignores_middle_sample_only_endpoints_matter);
    RUN_TEST(test_trend_delta_equal_to_deadband_stays_idle);
    RUN_TEST(test_trend_delta_just_over_deadband_charges);
    RUN_TEST(test_low_battery_not_triggered_above_threshold);
    RUN_TEST(test_low_battery_triggers_at_or_below_20);
    RUN_TEST(test_low_battery_stays_low_between_thresholds);
    RUN_TEST(test_low_battery_clears_at_or_above_25);
    RUN_TEST(test_first_entry_consumed_only_once);
    RUN_TEST(test_first_entry_rearms_after_recovery);
    RUN_TEST(test_boot_already_low_counts_as_entry);
    RUN_TEST(test_hysteresis_does_not_retrigger_while_oscillating);
    RUN_TEST(test_first_entry_cleared_on_recovery_without_consume);
    RUN_TEST(test_low_battery_not_triggered_at_21_percent);
    RUN_TEST(test_low_battery_does_not_clear_at_24_percent);
    RUN_TEST(test_offline_sentinel_does_not_clear_low_battery);
    RUN_TEST(test_null_backend_not_present);
    RUN_TEST(test_null_backend_read_is_invalid);
    RUN_TEST(test_vcell_at_upper_bound_is_plausible);
    RUN_TEST(test_vcell_above_upper_bound_is_implausible);
    RUN_TEST(test_soc_raw_at_upper_bound_is_plausible);
    RUN_TEST(test_soc_raw_above_upper_bound_is_implausible);
    RUN_TEST(test_soc_raw_garbage_is_implausible);
    RUN_TEST(test_fuel_reading_constructor_maps_fields_correctly);
    RUN_TEST(test_make_reading_accepts_real_hardware_values);
    RUN_TEST(test_make_reading_rejects_garbage_soc);
    RUN_TEST(test_make_reading_rejects_implausible_vcell);
    RUN_TEST(test_make_reading_zero_percent_is_valid);
    RUN_TEST(test_display_percent_absent_when_invalid);
    RUN_TEST(test_display_percent_zero_is_not_absent);
    RUN_TEST(test_display_percent_passes_valid_value_through);
    RUN_TEST(test_display_percent_full_charge_passes_through);
    RUN_TEST(test_battery_segments_at_0_percent_is_1);
    RUN_TEST(test_battery_segments_at_24_percent_is_1);
    RUN_TEST(test_battery_segments_at_25_percent_is_2);
    RUN_TEST(test_battery_segments_at_49_percent_is_2);
    RUN_TEST(test_battery_segments_at_50_percent_is_3);
    RUN_TEST(test_battery_segments_at_74_percent_is_3);
    RUN_TEST(test_battery_segments_at_75_percent_is_4);
    RUN_TEST(test_battery_segments_at_100_percent_is_4);
    RUN_TEST(test_apply_fuel_reading_failure_preserves_latched_low_battery);
    RUN_TEST(test_apply_fuel_reading_failure_does_not_fabricate_low_battery);
    RUN_TEST(test_apply_fuel_reading_failure_returns_absent_defaults);
    RUN_TEST(test_apply_fuel_reading_success_updates_all_fields);
    RUN_TEST(test_apply_fuel_reading_failure_resets_trend_window);
    RUN_TEST(test_blink_offline_sentinel_forces_false_even_when_low);
    RUN_TEST(test_blink_not_low_stays_false_while_online);
    RUN_TEST(test_blink_phase_boundary_499ms_is_on);
    RUN_TEST(test_blink_phase_boundary_500ms_is_off);
    RUN_TEST(test_blink_phase_boundary_999ms_is_off);
    RUN_TEST(test_blink_phase_boundary_1000ms_is_on);
    RUN_TEST(test_should_draw_absent_is_false_even_when_low_and_blink_on);
    RUN_TEST(test_should_draw_present_not_low_is_true_even_when_blink_off);
    RUN_TEST(test_should_draw_present_low_and_blink_on_is_true);
    RUN_TEST(test_should_draw_present_low_and_blink_off_is_false);
    RUN_TEST(test_notice_duration_matches_sot_spec);
    RUN_TEST(test_notice_inactive_is_never_visible);
    RUN_TEST(test_notice_at_start_is_visible);
    RUN_TEST(test_notice_boundary_last_visible_ms_is_visible);
    RUN_TEST(test_notice_boundary_exact_duration_is_hidden);
    RUN_TEST(test_notice_boundary_before_millis_overflow_is_visible);
    RUN_TEST(test_notice_survives_millis_overflow);
    RUN_TEST(test_notice_context_ohca_is_applicable);
    RUN_TEST(test_notice_context_vent_without_pre_is_applicable);
    RUN_TEST(test_notice_context_vent_with_pre_is_not_applicable);
    RUN_TEST(test_notice_context_neither_ohca_nor_vent_is_not_applicable);
    RUN_TEST(test_tick_out_of_context_does_not_consume_pending_event);
    RUN_TEST(test_tick_entering_ohca_consumes_and_activates);
    RUN_TEST(test_tick_vent_pre_shown_does_not_consume);
    RUN_TEST(test_tick_leaving_context_deactivates_immediately);
    RUN_TEST(test_tick_expiry_deactivates);
    RUN_TEST(test_tick_recovery_survives_millis_wraparound);
    RUN_TEST(test_tick_repeated_ticks_do_not_retrigger);
    RUN_TEST(test_confirm_decide_none_primary_stays_none_no_proceed);
    RUN_TEST(test_confirm_decide_none_back_stays_none_no_proceed);
    RUN_TEST(test_confirm_decide_none_other_stays_none_no_proceed);
    RUN_TEST(test_confirm_decide_ohca_primary_closes_and_proceeds);
    RUN_TEST(test_confirm_decide_ohca_back_closes_no_proceed);
    RUN_TEST(test_confirm_decide_ohca_other_keeps_ohca_no_proceed);
    RUN_TEST(test_confirm_decide_vent_primary_closes_and_proceeds);
    RUN_TEST(test_confirm_decide_vent_back_closes_no_proceed);
    RUN_TEST(test_confirm_decide_vent_other_keeps_vent_no_proceed);
    RUN_TEST(test_confirm_decide_training_primary_closes_and_proceeds);
    RUN_TEST(test_confirm_decide_training_back_closes_no_proceed);
    RUN_TEST(test_confirm_decide_training_other_keeps_training_no_proceed);
    RUN_TEST(test_try_request_confirm_when_low_with_pending_returns_true_and_consumes);
    RUN_TEST(test_try_request_confirm_when_low_without_pending_still_returns_true);
    RUN_TEST(test_try_request_confirm_when_not_low_returns_false_and_untouched);
    RUN_TEST(test_try_request_confirm_three_targets_do_not_cross_wire);
    return UNITY_END();
}
