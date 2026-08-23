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
