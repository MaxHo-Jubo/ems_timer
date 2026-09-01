// EMS DoseSync Pro — Impl-Phase H：燃料計純邏輯
//
// 對應 spec：docs/superpowers/specs/2026-08-22-phase-h-battery-display-design.md §3
// 測試入口：firmware/test/test_fuel_gauge_logic/test_fuel_gauge_logic.cpp
//
// 本檔不得 include Arduino.h / Wire.h — 所有函式須能在 native 環境編譯與測試。
//
// 常數放置慣例：class 專屬常數收進該 class 為 `public static constexpr`；僅在需與外部共用或
// 與既有重複實作 dedupe 時才留在 namespace scope。

#pragma once

#include <cstdint>
#include "ems_fuel_gauge.h"

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

/** VCELL 合理性上界（mV）：單節 LiPo 充飽約 4.2V，留餘裕取 4400。
 *  超過此值代表讀值不可信（I2C 位元翻轉／EMI），而非電池真的有那麼高的電壓。
 *  下界不設：0mV 可能是「電池真的沒電」的合法讀數，區分兩者靠 FuelReading.valid */
constexpr uint16_t PLAUSIBLE_MAX_MV = 4400;

/** SOC 暫存器整數部分的合理性上界（%）：ModelGauge 剛充飽會短暫超衝過 100，
 *  但不可能高到這個程度。**此值為推導初值，需上機實測校正**（比照趨勢死區的處理方式）。
 *  注意檢查必須做在 raw 上：soc_raw_to_percent() 會把 >=100 夾到 100，
 *  垃圾值換算完就看不出來了 */
constexpr uint8_t PLAUSIBLE_SOC_WHOLE_MAX = 110;

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
 * VCELL 讀值是否落在物理上可能的範圍
 *
 * ⚠️ 本檢查僅為上界 range check，只能抓到明顯超界的垃圾值（例如 0xFFFF）；
 * 界內的位元翻轉（讀值仍落在合理範圍內但已失真）不在防禦範圍內。
 *
 * @param mv 換算後的電池電壓（mV）
 * @return true = 可信；false = 超出上界，應視為讀取異常
 */
bool is_plausible_vcell_mv(uint16_t mv);

/**
 * SOC 原始暫存器值是否落在物理上可能的範圍
 *
 * ⚠️ 本檢查僅為上界 range check，只能抓到明顯超界的垃圾值；界內的位元翻轉
 * （例如真實 SOC 15 翻成 79，兩者都 ≤110 會通過判定，而 79 足以誤清已鎖存的
 * 低電量警示）不在防禦範圍內。
 *
 * @param raw_soc SOC 暫存器（0x04）的 16-bit 原始值，高位元組為整數 %
 * @return true = 可信；false = 超出上界，應視為讀取異常
 */
bool is_plausible_soc_raw(uint16_t raw_soc);

/**
 * 把兩個原始暫存器值組成一筆讀取結果，含全部合理性判定。
 *
 * 這是「raw → FuelReading」的**唯一**決策點，刻意放在純邏輯層：I2C 取值是硬體相依的，
 * 但「取到之後怎麼判斷可不可信」是純值邏輯。放在 backend 裡就永遠不會被 native test
 * 涵蓋——實測把 SOC 守衛從 read() 刪掉，37 個測試全綠。
 *
 * @param raw_vcell VCELL 暫存器（0x02）的 16-bit 原始值
 * @param raw_soc   SOC 暫存器（0x04）的 16-bit 原始值
 * @return 兩項合理性判定都通過時 valid=true 並帶換算結果；任一未過則 valid=false
 */
FuelReading make_reading(uint16_t raw_vcell, uint16_t raw_soc);

/** 電量百分比的「不在線」哨兵。255 不是合法百分比；0 是（真的沒電），兩者不可混用。
 *  刻意與 to_display_percent() 放在同一個檔案——哨兵值與它唯一的產生點不該分家 */
constexpr uint8_t BATTERY_PERCENT_ABSENT = 255;

// 哨兵必須落在 0~100 合法電量範圍之外，否則 LowBatteryLatch::update() 的過濾會把它當成
// 合法電量放行（該函式的 guard 是 `percent > SOC_PERCENT_MAX`）。編譯期擋死，不需額外測試。
static_assert(BATTERY_PERCENT_ABSENT > SOC_PERCENT_MAX,
              "哨兵必須落在 0~100 合法電量範圍之外，否則 LowBatteryLatch::update() 的過濾失效");

/**
 * 把一次燃料計讀取結果轉成 UI 層要顯示的電量百分比。
 *
 * 這是 FuelReading（valid 旗標）與 batteryPercent（255 哨兵）之間**唯一**的轉譯出口。
 * 呼叫端不得自己寫 `reading.valid ? reading.percent : 255`：目的地是裸 uint8_t，
 * 漏掉 valid 判斷時 NullFuelGauge 回的 percent=0 會落在合法 0~100 範圍內，
 * 沒有任何邊界檢查攔得住，畫面會把硬體故障顯示成「電池耗盡」。
 *
 * @param reading 燃料計單次讀取結果
 * @return valid=true 時回 reading.percent（0~100）；valid=false 時回 BATTERY_PERCENT_ABSENT
 */
uint8_t to_display_percent(const FuelReading& reading);

/** 這個顯示百分比是否代表「燃料計不在線」。
 *  UI 端讀取電量前一律先呼叫本函式，不要在 render 程式碼裡各自比對 255 */
constexpr bool is_battery_absent(uint8_t display_percent) {
    return display_percent == BATTERY_PERCENT_ABSENT;
}

/** 四格電量圖示的格數上限：對應 spec §4.3 的四級分界，也是
 *  battery_segments_for_percent() 的回傳值上限。
 *
 *  app_globals.h 的版面常數 BATTERY_ICON_SEGMENTS（決定電量條要分成幾個繪製槽位）
 *  直接引用本常數，不是各自維護一份字面值 4——這部分是真的單一真相來源。
 *
 *  ⚠️ 但本常數**不是** battery_segments_for_percent() 格數映射的單一真相來源：
 *  該函式內的 `return 3; / return 2; / return 1;` 三個分支與三個門檻常數
 *  （BATTERY_SEGMENT_4/3/2_MIN_PERCENT）都是各自獨立寫死的資料，不是由本常數
 *  推導出來的。把本常數改成 5 不會讓格數真的變成 5 級，只會讓下面的
 *  static_assert 編譯失敗——這是刻意設計的「改格數需要同步改四個地方」提醒，
 *  不是自動同步機制。spec §4.3 把格數寫死為四格，不是可配置參數；為「改成
 *  5 格」這個目前不會發生的需求先做通用化（門檻收進 array、格數由 array 大小
 *  推導）屬過度設計，真的要改格數時再一併重構本檔與下面的 assert。 */
constexpr uint8_t BATTERY_ICON_SEGMENT_COUNT = 4;

static_assert(BATTERY_ICON_SEGMENT_COUNT == 4,
              "battery_segments_for_percent() 的 return 3/2/1 分支與三個 "
              "BATTERY_SEGMENT_*_MIN_PERCENT 門檻常數皆為寫死四級，不會因為改這個"
              "常數而自動跟著變；要改動格數必須同步重寫該函式與三個門檻常數，"
              "改完後才能把這個 assert 的比較值一併改掉");

/** 四格電量圖示的分界值（%，含下界）：對應 spec §4.3 的四級分界。
 *  由高到低比對，達到門檻即填滿對應格數，避免呼叫端各自猜測閾值 */
constexpr uint8_t BATTERY_SEGMENT_4_MIN_PERCENT = 75;
constexpr uint8_t BATTERY_SEGMENT_3_MIN_PERCENT = 50;
constexpr uint8_t BATTERY_SEGMENT_2_MIN_PERCENT = 25;

/**
 * 依電量百分比換算四格電量圖示應填滿的格數。
 *
 * 分界依 spec §4.3：0~24 / 25~49 / 50~74 / 75~100，四級對應填滿 1~BATTERY_ICON_SEGMENT_COUNT 格
 * （SOC 為 0 時仍回 1，這是 spec §4.3 最低一級分界本身如此——0% 是合法讀數，不是
 * 需要特殊處理的例外。「不在線」是完全不同的另一條路徑，由
 * ems::should_draw_battery_icon() 處理且連外框都不畫，不會走到本函式）。
 *
 * 抽進本檔（而非留在 UI 層的 static 函式）的理由：native 環境的 [env:native]
 * build_src_filter 排除整個 src/，UI 層函式沒有測試涵蓋；四格分界是 spec 明訂的
 * 全域約束，被改動不該無聲無息地漏測。
 *
 * ⚠️ 呼叫端必須先用 is_battery_absent() 擋掉哨兵值 255——本函式只負責合法電量
 * （0~100）到格數的換算，「在不在線」是另一個函式的職責，兩者不合併是因為
 * 呼叫端（drawBatteryIcon）本來就得先做「不在線就整個不畫」的早退判斷，
 * 這裡加一層哨兵防呆只會製造第二個判斷點，不會消除第一個。
 *
 * @param percent 電量百分比 0~100（呼叫端已排除哨兵值）
 * @return 填滿格數 1~BATTERY_ICON_SEGMENT_COUNT
 */
uint8_t battery_segments_for_percent(uint8_t percent);

/** 低電量閃爍半週期（ms）：500ms 翻轉一次 = 1Hz 閃爍
 *  與 compute_low_battery_blink_on() 同檔——常數與唯一使用它的邏輯不分處兩地 */
constexpr uint32_t BATTERY_BLINK_HALF_PERIOD_MS = 500;

/**
 * 低電量閃爍相位：是否處於「亮」的半週期。
 *
 * 燃料計不在線（percent 為哨兵）時一律回 false——此時圖示本來就不畫，
 * 若仍讓相位翻轉會使 snapshot 每半週期變化一次，造成永久性的無效全螢幕重繪。
 * 低電量鎖存狀態本身不受影響，維持既有語意。
 *
 * @param percent 目前電量百分比（255 = 燃料計不在線）
 * @param low     低電量鎖存狀態（含遲滯）
 * @param now_ms  目前時間戳（毫秒），呼叫端傳入 millis()
 * @return true = 亮相位；false = 滅相位、非低電量、或燃料計不在線
 */
bool compute_low_battery_blink_on(uint8_t percent, bool low, uint32_t now_ms);

/**
 * 電量圖示本次是否該畫內容（不含清背景——清背景由呼叫端無條件先執行，本函式
 * 只決定接下來要不要繼續畫外框/格子/閃電）。
 *
 * 三種情況：
 *   - 不在線（is_battery_absent(percent)）→ 不畫，連外框都不畫，避免被讀成沒電
 *   - 在線且非低電量 → 一律畫，不受 lowBlinkOn 影響
 *   - 在線且低電量 → 依 lowBlinkOn 相位決定（亮相位畫、暗相位不畫）
 *
 * 抽成本函式的理由：2026-08-23 controller 驗證時抓到 UI 層曾把「非低電量時
 * lowBlinkOn 恆為 false」誤讀成「lowBlinkOn==false 就該跳過繪製」（`if (!lowBlinkOn)
 * return;`），導致電量正常（絕大多數時間）時圖示完全不會出現，只有低電量亮相位
 * 才畫得出來——與 Task 9 的目的正好相反。三個布林的交互關係若散在 render 程式碼
 * 裡用連續 if 各自判斷，容易重蹈同一種耦合誤讀；抽成純函式後這個決策可以被
 * native test 鎖住（ui_screens.cpp 不在 [env:native] 的 build_src_filter 內，
 * UI 層程式碼本身完全測不到，這正是原本的 bug 能編譯通過、測試也不會抓到的原因）。
 *
 * @param percent    電量百分比（255 = 燃料計不在線）
 * @param low        低電量鎖存狀態（含遲滯）
 * @param lowBlinkOn 低電量閃爍目前相位；非低電量時呼叫端應恆傳 false
 *                   （compute_low_battery_blink_on() 的既有保證），但本函式不依賴
 *                   這個前提成立——非低電量分支直接回 true，不查看 lowBlinkOn
 * @return true = 該畫；false = 不畫（不在線，或低電量暗相位）
 */
bool should_draw_battery_icon(uint8_t percent, bool low, bool lowBlinkOn);

/** 低電量提示顯示時長（ms）：SoT §13.16 顯示一次後自動消失
 *  與 is_low_battery_notice_visible() 同檔——常數與唯一使用它的邏輯不分處兩地 */
constexpr uint32_t LOW_BATTERY_NOTICE_MS = 3000;

/**
 * §13.16 低電量提示的顯示計時器狀態。
 *
 * `active` 為 false 時 `start_ms` 無意義——不拿它兼作「未觸發」哨兵，理由同
 * is_low_battery_notice_visible() 的 doc：0 是 millis() 的合法回傳值。
 *
 * 收斂為單一 struct（而非呼叫端各自維護 active/start_ms 兩個獨立全域）：內部一律
 * 整包替換兩個欄位，不像兩個分開的 extern 全域那樣容易誤寫成只改其中一個而讓另一個
 * 沿用舊值。但這只是**約定**，不是型別保證——兩個欄位仍是 public（保持
 * aggregate-like 用法），任何人仍可寫 `g_low_battery_notice.active = true;` 單獨改
 * 一欄，繞過這個約定。
 *
 * 唯一的生產寫入點是 `low_battery_notice_tick()`——該函式吃 `LowBatteryNoticeState&`
 * 並原地改寫，呼叫端傳入 `g_low_battery_notice` 後函式回傳時它已是新狀態。不用回傳值
 * 交付新狀態的理由見該函式 JSDoc：回傳值需要呼叫端自己記得寫回，漏寫不會編譯錯誤，
 * 只會讓低電量閂鎖的一次性事件被消費掉卻沒有任何轉換生效。
 */
struct LowBatteryNoticeState {
    bool     active;
    uint32_t start_ms;

    /**
     * 預設建構：從未觸發過的初始狀態。
     *
     * 與下方雙參數 constructor 分開宣告，不共用一個「兩個參數皆有預設值」的
     * constructor——`LowBatteryNoticeState(bool a = false, uint32_t ms = 0)` 會讓
     * `LowBatteryNoticeState(true)` 變成合法寫法，得到 `active=true, start_ms=0`
     * 的狀態：裝置開機超過 LOW_BATTERY_NOTICE_MS 後 is_low_battery_notice_visible()
     * 會立刻判它已過期，提示完全不出現且沒有任何錯誤訊號——這是把「漏傳觸發時間」
     * 靜默轉成合法狀態的 silent fallback，違反 CLAUDE.md no-fallback-after-root-cause
     * （2026-08-23 fix round 4 H1，CRITICAL）。拆成兩個 constructor 後
     * `LowBatteryNoticeState(true)` 這種單參數呼叫直接編譯失敗，不會再有機會產生
     * 這個非法狀態。現行所有呼叫點皆為零參數（`{}`）或雙參數，拆分前已逐一核對過。
     */
    constexpr LowBatteryNoticeState() : active(false), start_ms(0) {}

    /**
     * @param a  提示計時器目前是否有效
     * @param ms 觸發當下的時間戳（毫秒），僅在 a 為 true 時有意義
     *
     * 用 constructor 而非 default member initializer：比照同 lib 的 `FuelReading`
     * 既有教訓（`FuelReading` 實際定義在 `lib/ems_fuel_gauge/ems_fuel_gauge.h:26`，
     * 與本檔同一個 lib 但不同 header）——ESP32 目標以 gnu++11 編譯，
     * 帶 NSDMI 的 struct 在 C++11 不是 aggregate，`{false, 0}` 這種列表初始化會
     * 編譯失敗（native 是 gnu++17 所以看不出來，2026-08-23 fix round 2 上機驗證前
     * 才在 esp32-s3-devkitc-1 環境編譯時抓到）。
     */
    constexpr LowBatteryNoticeState(bool a, uint32_t ms)
        : active(a), start_ms(ms) {}
};

/**
 * §13.16 低電量提示目前是否應該顯示。
 *
 * 以「起始時間 + 經過時長」判斷而非「截止時間 + 大小比較」：絕對截止時間（`until_ms
 * = state.start_ms + LOW_BATTERY_NOTICE_MS`）本身也會 wrap，一旦 `until_ms` 先回繞
 * 成一個很小的值、而 `now_ms` 尚未回繞，`now_ms > until_ms` 這種大小比較就會把
 * 「其實還在顯示視窗內」的時間點誤判成已過期——`until_ms` 與 `now_ms` 各自 wrap
 * 的時機不同步，兩者已經不能用普通大小比較可靠排序。無號經過時間差
 * `now_ms - state.start_ms` 不受這個影響：只要顯示視窗（3 秒）遠小於 `uint32_t`
 * 的計數週期（約 49.7 天），這個差值在跨越回繞邊界前後都恆正確，與 pollBattery()
 * 的節流寫法同一個慣例。
 *
 * @param state  提示顯示計時器目前狀態。`state.active` 為 false 有兩種來源——
 *               「從未觸發」或「曾經顯示過但已復歸（逾期，或離開適用情境）」，
 *               本函式不區分兩者，對呼叫端而言意義相同：一律不顯示（2026-08-23
 *               fix round 2 E4：三處舊註解曾誤寫成「是否已被觸發過」，未涵蓋
 *               復歸後的 false；fix round 3 G4：改吃單一 state 而非分開的
 *               active/start_ms 兩個參數，避免呼叫端各自維護兩個全域造成不同步）
 * @param now_ms 目前時間戳（毫秒），呼叫端傳入 millis()
 * @return true = 仍在 LOW_BATTERY_NOTICE_MS 的顯示期間內
 */
bool is_low_battery_notice_visible(const LowBatteryNoticeState& state, uint32_t now_ms);

/**
 * §13.16 低電量提示的適用情境判斷：目前是否算「案件進行中」。
 *
 * 刻意不接收 app 層的 `GlobalState` enum 當參數——`ems_fuel_gauge` 對 app 層狀態機
 * 一無所知，呼叫端（main.cpp）把三個布林先轉譯好再傳入，維持 lib 純度（2026-08-23
 * fix round 1 A4 裁決：reviewer 原提議把 enum 搬進 lib，未採納）。
 *
 * VENT 模式排除 `vent_pre_shown`（通氣尚未真正開始的準備畫面）：spec §5 只排除
 * 「純選單瀏覽」，但 VENT_PRE 連通氣都還沒開始，不該算「案件進行中」。OHCA 的
 * END_CHECK／LOCKED／SUMMARY 等子狀態刻意不比照排除——案件總覽與結束檢查仍在案件
 * 流程內，救護員同樣需要知道電量（同一裁決）。
 *
 * @param in_ohca        呼叫端傳入 `globalState == GLOBAL_OHCA`（含 Training：
 *                       Training 開案後 globalState 同為 GLOBAL_OHCA，此參數已涵蓋）
 * @param in_vent        呼叫端傳入 `globalState == GLOBAL_VENT`
 * @param vent_pre_shown 呼叫端傳入 `ventPreShown`（VENT 模式下通氣尚未真正開始）
 * @return true = 適用情境，呼叫端可消費低電量閂鎖的首次進入事件
 */
bool is_low_battery_notice_context(bool in_ohca, bool in_vent, bool vent_pre_shown);

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

    /** 趨勢死區（mV）：窗內變化絕對值小於等於此值視為靜置。
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
     * 清空取樣窗，回到 Unknown。
     *
     * 呼叫時機不只「換電池或 backend 重新上線」：讀取暫時性失敗時也要 reset
     * （見 apply_fuel_reading() 的失敗分支，每輪失敗都會呼叫），確保讀取恢復後從
     * 乾淨的窗重新累積，避免拿失敗前後不連續的樣本混算趨勢。本函式冪等，
     * 連續失敗期間重複呼叫沒有副作用。
     */
    void reset();

private:
    uint16_t samples_[TREND_WINDOW_SAMPLES] = {0};  // 環形緩衝
    uint8_t count_ = 0;                             // 已推入次數，飽和於 TREND_WINDOW_SAMPLES；僅用於判斷窗是否已滿，不可當總推入量統計
    uint8_t head_  = 0;                             // 下一次寫入位置
};

/**
 * 低電量狀態閂鎖：帶遲滯的低電量判定 + 一次性提示的消費旗標。
 *
 * 對應 SoT §13.16（執行中只顯示一次提示）與 §20.3（低電量仍可開案但需警告）。
 *
 * ⚠️ 本類期望呼叫端先透過 `FuelReading.valid` 過濾無效讀值；不可信的百分比（例如 255 = 燃料計不在線）
 * 會被直接忽略，不更新任何狀態。這樣才能保護已鎖存的低電量警示不被哨兵值誤清。
 *
 * 設計備註：`entry_pending_` 旗標僅存在 RAM、不寫 NVS——充完電拔掉再掉到門檻要視為新事件重新提醒；
 * 寫 NVS 會讓裝置一輩子只提醒一次。
 */
class LowBatteryLatch {
public:
    /** 低電量進入門檻（%）：SOC 小於等於此值視為低電量 */
    static constexpr uint8_t LOW_BATTERY_ENTER_PERCENT = 20;

    /** 低電量解除門檻（%）：SOC 大於等於此值才解除低電量。
     *  與進入門檻拉開形成遲滯，否則 SOC 在 20% 附近抖動會反覆觸發提示與閃爍 */
    static constexpr uint8_t LOW_BATTERY_EXIT_PERCENT = 25;

    /**
     * 餵入一筆電量取樣，更新低電量狀態。
     * 超出 0~100 的值一律視為不可信讀值，直接略過不更新狀態。
     *
     * @param percent 電量百分比 0~100；超出此範圍視為哨兵值（例如 255 = 不在線）並略過
     */
    void update(uint8_t percent);

    /**
     * 當前是否處於低電量（含遲滯）
     * @return 是否在遲滯的低電量狀態（進入門檻 20% ≤ SOC ≤ 解除門檻 25%）
     */
    bool is_low() const { return is_low_; }

    /**
     * 消費「首次進入低電量」事件。
     * @return 自建立（或上次解除低電量）以來，低電量狀態第一次被進入時回 true；其餘回 false
     */
    bool consume_first_entry();

private:
    bool is_low_          = false;  // 當前低電量狀態（帶遲滯）
    bool entry_pending_   = false;  // 尚未被消費的「首次進入」事件
};

/**
 * §13.16 低電量提示狀態機一次 tick：判斷是否該啟動、是否該復歸，並消費低電量閂鎖的
 * 首次進入事件。
 *
 * 守衛（是否適用情境）收斂在本函式內，不留給呼叫端。`LowBatteryLatch::consume_first_entry()`
 * 是公開 API，任何未來呼叫端若忘記在非適用情境下跳過呼叫，會**不可逆地**把 pending
 * 事件消費掉且沒有任何錯誤訊號——之後真正進案件時 `consume_first_entry()` 只會一直
 * 回 false，提示靜默消失，不會有人發現（2026-08-23 fix round 2 E1，CRITICAL；比照
 * CLAUDE.md guard-placement 原則：守衛加在共用層，不是加在呼叫端的記憶力上）。
 * `consume_first_entry()` 本身維持 public——Task 3 既有測試直接測 latch 自身的契約
 * （rearm 語意、失敗不清警示）。
 *
 * ⚠️ **`consume_first_entry()` 現在有兩個合法生產呼叫點**（2026-08-30 Task 11 fix
 * round 1 之後不再只有本函式一個入口）：本函式（§13.16 執行中一次性提示）與
 * `try_request_low_battery_start_confirm()`（§20.3 低電量開案確認框核心進場判斷，
 * fuel_gauge_logic.h/.cpp；`main.cpp` 的 `requestLowBatteryStartConfirm()` 是它在
 * app 層唯一的呼叫端，本身不直接碰 latch）。兩者不會搶同一次事件，理由是確認框顯示
 * 期間三個目標（OHCA／VENT／Training）在本函式的守衛判斷下都必然落在「不適用情境」：
 *   - OHCA：確認框顯示全程 `globalState` 停在 `GLOBAL_MAIN_MENU`，`in_ohca` 為 false。
 *   - VENT：確認框顯示時 `globalState == GLOBAL_VENT` 但 `ventPreShown == true`，
 *     `is_low_battery_notice_context()` 的 `vent_pre_shown` 參數本來就會把這個情境
 *     判為不適用，`in_vent && !vent_pre_shown` 為 false。
 *   - Training：確認框顯示於 `GLOBAL_TRAINING_SETUP` 設定畫面內，案件尚未開始，
 *     `globalState` 還沒切到 `GLOBAL_OHCA`，`in_ohca` 同樣為 false。
 * 三種情況下本函式的 STEP 01 都會判定不適用情境並提前 return，不會走到 STEP 03 去
 * 呼叫 `latch.consume_first_entry()`，因此與 `try_request_low_battery_start_confirm()`
 * 在確認框顯示當下的消費不會搶同一次事件。
 *
 * ⚠️ **本函式不是純函式，帶副作用**：適用情境內且有待消費事件時，會呼叫
 * `latch.consume_first_entry()` **原地修改**呼叫端傳入的 `latch`（2026-08-23
 * fix round 4 H3：舊版 JSDoc 完全沒提這件事，回傳新狀態的外觀加上隱藏副作用是
 * 會誤導人的組合）。這是刻意的取捨，不是疏漏：守衛（是否該消費事件）必須放在
 * 本函式內才能防住上方說的 CRITICAL，若改成讓本函式維持純函式、吃一個外部算好的
 * `bool first_entry_consumed` 參數，等於把「要不要消費」的決策推回呼叫端，
 * fix round 2 E1 那個 CRITICAL 會原樣回來。
 *
 * `state` 參數同樣原地改寫，不再以回傳值交付新狀態。舊簽名回傳
 * `LowBatteryNoticeState`，呼叫端必須自己把回傳值寫回全域；這個寫回動作本身可以
 * 被漏接——編譯照樣過，但 `latch` 的一次性事件已經在函式內被消費掉，往後每輪 tick
 * 都拿不到新事件，提示永久靜默消失，且沒有任何錯誤訊號可循。改成與 `latch` 同樣
 * 原地修改後，呼叫端沒有機會漏接這個轉換：只要傳了同一個 `state` 變數進來，函式
 * 回傳時它已經是新狀態，不存在「事件已消費、但轉換沒有生效」的中間態。
 *
 * 內部依序判斷：
 *   1. 離開適用情境 → 立即復歸為 inactive，不等顯示視窗到期（fix round 2 E2：
 *      提示期間結束通氣回主選單、或切到 GLOBAL_SYNC，不透明 panel 不該蓋在新頁面上）
 *   2. 仍在情境內但已逾期 → 復歸為 inactive 並結束本次 tick（fix round 1 A5，
 *      現收斂進本函式）。逾期復歸這條分支會直接 return，本次 tick 不會走到 STEP 3；
 *      若 latch 此時剛好也有待消費事件，要等下一輪 tick 才會被消費——延遲一個
 *      loop 週期，可忽略。
 *   3. 仍在情境內、未逾期，且 latch 有待消費事件 → 消費並啟動
 *   4. 以上皆非 → 維持現狀，不修改 `state`（可能是「已啟動且仍在顯示視窗內」，
 *      也可能是「inactive 且無新事件」）
 *
 * @param state          [in/out] 傳入時為上一次 tick 後的狀態，函式內會被原地改寫為
 *                       本次 tick 後的新狀態——呼叫端不需要、也不應該另外把回傳值
 *                       寫回全域，因為沒有回傳值可寫，寫回動作已在函式內完成
 * @param latch          低電量閂鎖，本函式可能呼叫其 consume_first_entry()（僅在
 *                       適用情境內才會呼叫，見上方守衛說明）
 * @param in_ohca        呼叫端傳入 `globalState == GLOBAL_OHCA`
 * @param in_vent        呼叫端傳入 `globalState == GLOBAL_VENT`
 * @param vent_pre_shown 呼叫端傳入 `ventPreShown`
 * @param now_ms         目前時間戳（毫秒），呼叫端傳入 millis()
 */
void low_battery_notice_tick(LowBatteryNoticeState& state,
                             LowBatteryLatch& latch,
                             bool in_ohca, bool in_vent,
                             bool vent_pre_shown,
                             uint32_t now_ms);

/** §20.3 低電量開案的可啟動目標——不含哨兵值，requestLowBatteryStartConfirm() 只接受
 *  這個型別，誤傳「未顯示確認框」狀態在編譯期就不可能發生（2026-08-30 fix round 2 G，
 *  CRITICAL：原本 requestLowBatteryStartConfirm() 吃 LowBatteryConfirmTarget，該型別含
 *  None，介面上仍「能」被誤傳 None——一旦誤傳，函式會讓確認框保持關閉，卻仍不可逆消費
 *  latch，這正是本函式原本要避免的那個 bug，只是換了個位置）。數值刻意與
 *  LowBatteryConfirmTarget 對齊（Ohca=1／Vent=2／Training=3），兩者互轉不會失真。 */
enum class LowBatteryStartTarget : uint8_t {
    Ohca     = 1,
    Vent     = 2,
    Training = 3,
};

/** §20.3 低電量開案確認框目前的顯示狀態。None = 確認框未顯示。
 *  只用於 UI 狀態追蹤（g_lowBatteryConfirmTarget、LowBatteryConfirmDecision.next_target）。
 *  不作為 §20.3 開啟確認框請求的輸入型別（那是 LowBatteryStartTarget 的職責，見上方），
 *  但仍是 low_battery_confirm_decide() 的目前 UI 狀態輸入（`current` 參數）。 */
enum class LowBatteryConfirmTarget : uint8_t {
    None     = 0,
    Ohca     = 1,
    Vent     = 2,
    Training = 3,
};

/** 確認框收到的按鍵語意——呼叫端已把硬體按鍵索引（BTN_PRIMARY/BTN_BACK/其他）映射成這三種之一，
 *  純函式不依賴 app_globals.h 的硬體常數。 */
enum class ConfirmDialogAction : uint8_t { Primary, Back, Other };

/** low_battery_confirm_decide() 的決策結果。 */
struct LowBatteryConfirmDecision {
    LowBatteryConfirmTarget next_target;  // 決策後的新狀態；None = 確認框應關閉
    bool                    proceed;      // true = 呼叫端現在應執行「current 對應目標」的啟動動作
};

/**
 * §20.3 低電量開案確認框的按鍵決策：純函式，不碰任何全域、不觸發任何副作用。
 *
 * 真值表：
 *   current=None            → 恆回 {None, false}（確認框未顯示，任何按鍵都無意義；呼叫端不應該在
 *                              這個狀態下呼叫本函式，但呼叫仍是安全的 no-op）
 *   current!=None, Primary  → {None, true}——呼叫端接下來要執行「current 對應目標」的啟動動作
 *                              （本函式只回報決策，不知道也不需要知道啟動動作長什麼樣）
 *   current!=None, Back     → {None, false}——取消，不啟動
 *   current!=None, Other    → {current, false}——忽略，維持原確認框顯示，不啟動
 *
 * @param current 目前待確認的目標
 * @param action  觸發的按鍵語意
 * @return 決策結果
 */
LowBatteryConfirmDecision low_battery_confirm_decide(LowBatteryConfirmTarget current,
                                                       ConfirmDialogAction action);

/**
 * §20.3 低電量開案確認框的核心進場判斷：只有 latch.is_low() 為真時才設定待啟動目標
 * 並消費 latch 的 pending 事件；否則什麼都不做、回傳 false。守衛完全收斂在這裡，
 * 呼叫端不需要、也不應該自己先判斷是否低電量——這樣任何未來呼叫點都不可能誤呼叫
 * 進而不可逆消費事件（2026-08-30 fix round 3 P：round 1/2 都把「設 target+消費 latch」
 * 綁成一次呼叫，但「要不要攔截」的判斷仍留在呼叫端，這次收尾）。
 *
 * 取代 round 2 的 `apply_low_battery_start_confirm_request()`（無條件執行版本，已整個
 * 移除，不留舊名字並存造成混淆）。原本 native 環境不編譯 `src/`，這個核心契約完全沒有
 * 測試鎖住；抽進 lib 才能被 native test 涵蓋，比照本 repo 既有的 apply_fuel_reading() 抽法。
 * 不是純函式（`latch` 會被原地修改），但不碰 Arduino/全域，可 native test。
 *
 * @param target_out [out]    需要攔截時寫入待啟動目標；不需要攔截時不動
 * @param latch      [in/out] 低電量閂鎖。讀 `latch.is_low()` 決定是否攔截；攔截時會呼叫
 *                            `latch.consume_first_entry()` **原地消費**它的 pending 事件
 *                            （§13.16 的一次性提示），這是不可逆的副作用，不是唯讀輸入
 * @param target     使用者原本想啟動的目標
 * @return 是否已攔截（true=已設定確認框，呼叫端不要啟動；false=非低電量，呼叫端照常啟動）
 */
bool try_request_low_battery_start_confirm(LowBatteryConfirmTarget& target_out,
                                             LowBatteryLatch& latch,
                                             LowBatteryStartTarget target);

/**
 * 一次輪詢的結果。呼叫端（main.cpp 的 pollBattery()）把這四個值寫進對應全域即可，
 * 不需要自己重複決策邏輯。
 */
struct BatteryPollOutcome {
    uint8_t     percent;       // 已轉譯的顯示百分比（含 BATTERY_PERCENT_ABSENT 哨兵）
    uint16_t    millivolts;    // 電池電壓；讀取失敗時為 0
    ChargeState charge_state;  // 充電狀態；讀取失敗時為 Unknown
    bool        low_battery;   // 低電量（含遲滯）；讀取失敗時保留既有鎖存
};

/**
 * 把一次燃料計讀取結果套用到趨勢追蹤器與低電量閂鎖，回傳要寫進全域的值。
 *
 * main.cpp 的 pollBattery() STEP 03/04 決策部分的純函式版本——比照本 repo
 * ems_rtc_glue 的先例（把原本內嵌在 main.cpp 的膠合邏輯抽成純函式），對齊 memory
 * feedback_extract_testable_pure_logic。main.cpp 只留節流、read() 呼叫、寫全域、
 * Serial log；決策邏輯搬進這裡才能被 native test 涵蓋到（原本 pollBattery() 是
 * main.cpp 裡的 static 函式，在任何測試環境都不會被連結進去，是真正的 0 覆蓋率）。
 *
 * 讀取失敗時**完全不呼叫** `latch.update()`——不是為了「不推進狀態機」這個籠統理由，
 * 而是因為傳未轉譯的 `reading.percent` 進去是危險的：無效讀值時它是建構子預設值 0，
 * 而 `0 <= SOC_PERCENT_MAX` 會穿透 `LowBatteryLatch::update()` 的 guard。若當下電量
 * 正常（未鎖存低電量），一次暫時性 I2C 失敗就會被誤判成跌破 20% 門檻，在電量正常時
 * 憑空觸發低電量警示——這與「失敗把既有警示清掉」是相反方向的錯誤，兩者都必須防。
 *
 * @param reading 本輪讀取結果
 * @param trend   趨勢追蹤器（讀取失敗時會被 reset；成功時 push 一筆新樣本）
 * @param latch   低電量閂鎖（讀取失敗時只讀 is_low()，絕不呼叫 update()）
 * @return 要寫進 g_battery_percent / millivolts / charge_state / low 四個全域的值
 */
BatteryPollOutcome apply_fuel_reading(const FuelReading& reading,
                                      ChargeTrendTracker& trend,
                                      LowBatteryLatch& latch);

}  // namespace ems
