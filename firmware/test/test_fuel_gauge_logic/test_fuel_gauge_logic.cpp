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
    return UNITY_END();
}
