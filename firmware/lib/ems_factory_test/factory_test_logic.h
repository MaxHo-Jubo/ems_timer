// EMS DoseSync Pro — 產測（factory-test）純邏輯
//
// 用途：src_factory_test/main.cpp 的判定層。硬體端只負責讀 I2C／按鍵／RTC 暫存器，把觀測結果
//       交給本檔的純函式產生新的 FactoryTestState；PASS／FAIL／WAITING 與失敗原因全部由本檔決定，
//       讓判定規則能在 native 環境測試（feedback_extract_testable_pure_logic）。
// 測試：firmware/test/test_factory_test_logic/test_factory_test_logic.cpp
// 對外判讀說明：docs/vendor-assembly-brief.html §7.4／§7.5——本檔回傳的字串會直接上 TFT，
//       兩邊必須一字不差；TFT 只用內建 ASCII 字型，所以本檔所有字串限 ASCII。
//
// 設計原則（2026-09-09 codex Tier 3 review 三個 CRITICAL 的修正）：
//   1. 按鍵「依序」驗收：只接受下一顆預期按鍵，接反或短路都會 FAIL，不能靠「都按過」矇混
//   2. RTC 走時判定只吃通過 BCD／範圍驗證的讀值，讀失敗或亂資料 → FAIL，不能靠亂數變化騙過
//   3. RTC 失憶（OSF）不再靜默覆蓋：第一次只 seed 並要求斷電驗證，斷電後 OSF 再亮 → FAIL
//   所有失敗狀態都是黏性的：一旦出現就鎖到 RST 重開，避免「閃一下 FAIL 又回 WAITING」被看漏
#pragma once

#include <cstdint>

namespace ems {

/** 產測涵蓋的按鍵數，與主韌體 app_globals.h 的 BTN_COUNT 相同（8 鍵封版） */
constexpr uint8_t FT_BUTTON_COUNT = 8;

/**
 * RTC 走時判定逾時（ms）：秒數連續這麼久沒變就視為晶振停擺。
 * 硬體端 1 秒輪詢一次，3 秒足以排除「剛好取樣在同一秒」的邊界。
 */
constexpr uint32_t FT_RTC_TICK_TIMEOUT_MS = 3000;

/** DS3231 時間暫存器 0x00~0x06 的長度（秒、分、時、星期、日、月、年） */
constexpr uint8_t FT_RTC_RAW_LEN = 7;

/**
 * 判定「真的斷過電」的最短離線秒數。
 * 離線秒數 = 開機時 RTC 讀值 − NVS 最後記錄的 RTC 讀值 − ESP 開機至今秒數；
 * NVS 每 2 秒記一次，按 RST 只會量到 ≤ 2 秒，廠商文件要求斷電 ≥ 10 秒，5 秒在兩者中間
 */
constexpr int32_t FT_POWER_CYCLE_MIN_OFF_S = 5;

// Wire.endTransmission() 回傳碼（arduino-esp32 Wire.cpp）：0=ACK、1=資料過長、2=位址 NACK、
// 3=資料 NACK、4=其他錯誤、5=逾時。掃描只送位址，缺席一律回 2；回 5 代表 bus 被拉住；
// 其餘都是 driver 層異常，不可當成「沒人在家」放過
constexpr uint8_t FT_I2C_CODE_ACK       = 0;
constexpr uint8_t FT_I2C_CODE_ADDR_NACK = 2;
constexpr uint8_t FT_I2C_CODE_TIMEOUT   = 5;

/** 單一位址的探測結果 */
enum class FtProbe : uint8_t {
    Present,   // ACK
    Absent,    // 位址 NACK：bus 正常，該位址沒裝置
    BusStuck,  // 逾時：SDA/SCL 被拉住
    BusError,  // 其他回傳碼：driver 層異常
};

/** 單輪輪詢（約 1 秒）內允許的最大前進秒數；超過或倒退都是亂資料，不是走時 */
constexpr uint32_t FT_RTC_MAX_STEP_S = 10;

/** RTC 走時狀態 */
enum class FtRtcTick : uint8_t {
    Unknown,  // 觀察時間還不夠，不能下結論
    Ok,       // 秒數有在變，且是合理的向前變化
    Stuck,    // 達到 FT_RTC_TICK_TIMEOUT_MS 秒數仍沒變
    Invalid,  // 倒退或一輪跳超過 FT_RTC_MAX_STEP_S：合法 BCD 但不合理，視同 I/O 錯誤
};

/** RTC 備援電池（CR2032）驗證狀態；值會存進 NVS，順序不可改 */
enum class FtRtcBackup : uint8_t {
    Unverified,  // 尚未證明：RTC 失憶剛 seed 過，或還沒觀察到一次「板子斷電 ≥ 5 秒而 RTC 仍在走」
    Ok,          // 已證明：板子斷電期間 RTC 靠電池繼續走時
    Lost,        // 之前已 seed 過，這次上電又失憶：電池沒裝或沒電
};

/** 按鍵驗收錯誤 */
enum class FtButtonError : uint8_t {
    None,
    WrongOrder,  // 按到的不是下一顆預期按鍵（接反、接錯 GPIO）
    Multiple,    // 同一輪掃描兩顆以上同時觸發（兩條按鍵線短路）
};

/** 總判定 */
enum class FtVerdict : uint8_t {
    Pending,  // 還在等：見 ft_pending_reason()
    Pass,     // 全部必要項通過
    Fail,     // 有必要項失敗：見 ft_fail_reason()
};

/**
 * 失敗種類。優先序（由上而下）唯一定義在 ft_fail_kind()；顯示字串（ft_fail_reason_for）與
 * 硬體端的細節格式都依這個 enum 分派，避免兩處各自維護一份優先序而分歧
 */
enum class FtFailKind : uint8_t {
    None,
    BusStuck,     // I2C 逾時
    BusError,     // I2C 其他錯誤碼
    NvsError,     // 主控 NVS 讀寫失敗（備援電池驗證的證據存不下來）
    RtcMissing,   // 0x68 曾經或目前沒有 ACK
    RtcIo,        // 暫存器讀寫失敗或讀值非法
    RtcStuck,     // 走時停擺
    RtcBattery,   // 斷電後失憶
    ButtonOrder,  // 按鍵錯序
    ButtonShort,  // 按鍵同時觸發
};

/** 一輪 I2C 掃描三個已知位址各自的 endTransmission 回傳碼 */
struct FtI2cScan {
    uint8_t rtc_code;     // 0x68 DS3231
    uint8_t eeprom_code;  // 0x57 AT24C32（DS3231 模組附掛，選配）
    uint8_t gauge_code;   // 0x36 電量計排針（選配，本次不焊）

    /** 預設全部視為位址 NACK（沒裝置），與 FactoryTestState 同理用 ctor 而非 NSDMI */
    constexpr FtI2cScan(uint8_t rtc = FT_I2C_CODE_ADDR_NACK,
                        uint8_t eeprom = FT_I2C_CODE_ADDR_NACK,
                        uint8_t gauge = FT_I2C_CODE_ADDR_NACK)
        : rtc_code(rtc), eeprom_code(eeprom), gauge_code(gauge) {}
};

/** DS3231 時間暫存器解碼後的欄位（24 小時制） */
struct FtRtcTime {
    uint8_t second;  // 0~59
    uint8_t minute;  // 0~59
    uint8_t hour;    // 0~23
    uint8_t day;     // 1~31
    uint8_t month;   // 1~12
    uint8_t year;    // 0~99（2000 起算）

    constexpr FtRtcTime() : second(0), minute(0), hour(0), day(1), month(1), year(0) {}
};

/**
 * 硬體端每輪填入的觀測結果，判定只看這個 struct。
 * 必要項：bus 正常、RTC 在線／可讀／走時／電池有效、8 鍵依序按過且無錯接。
 * 選配項：EEPROM（DS3231 模組附掛）、電量計（本次留排針不焊），在不在都不影響判定。
 * 所有 *_error／*_stuck／Lost／button_error 一旦成立就不再被清除（黏性），直到 RST。
 */
struct FactoryTestState {
    // ── I2C ──
    bool    i2c_bus_stuck;   // 任一位址回逾時（SDA/SCL 被拉住），黏性
    bool    i2c_bus_error;   // 任一位址回非 ACK/NACK/逾時的錯誤碼，黏性
    uint8_t i2c_error_code;  // i2c_bus_error 成立時第一個看到的錯誤碼，供畫面顯示
    bool    rtc_present;      // 0x68 本輪有 ACK（每輪重新判定，只供畫面顯示）
    bool    rtc_missing_seen; // 0x68 曾經有一輪沒 ACK，黏性——虛焊的 RTC 恢復 ACK 也不能翻回 PASS
    bool    eeprom_present;   // 0x57 有 ACK（選配）
    bool    gauge_present;    // 0x36 有 ACK（選配）
    // ── 主控儲存 ──
    bool    nvs_error;        // NVS 開啟／寫入失敗，黏性（「已 seed」旗標存不下來，備援驗證不可信）
    // ── RTC ──
    bool        rtc_io_error;  // 暫存器讀寫失敗或讀值不是合法 BCD 時間，黏性
    FtRtcTick   rtc_tick;      // 走時狀態；Stuck 黏性（由 ft_apply_rtc_tick 維持）
    FtRtcBackup rtc_backup;    // 備援電池驗證狀態
    // ── 按鍵 ──
    uint8_t       buttons_done;  // 已依序驗收的按鍵數 0~8（下一顆預期索引就是這個值）
    FtButtonError button_error;  // 錯接／短路，黏性
    uint8_t       button_err_a;  // WrongOrder：預期索引；Multiple：第一顆索引
    uint8_t       button_err_b;  // WrongOrder：實際索引；Multiple：第二顆索引

    /**
     * 預設全部「未觀測」：bus 正常、裝置皆不在、RTC 未知、按鍵 0 顆。
     * 與 FuelReading 同理用 ctor 而非 NSDMI：ESP32 目標以 gnu++11 編譯，帶 NSDMI 的 struct
     * 不是 aggregate，列表初始化會編譯失敗（native 是 gnu++17 看不出來）。
     */
    constexpr FactoryTestState()
        : i2c_bus_stuck(false), i2c_bus_error(false), i2c_error_code(0),
          rtc_present(false), rtc_missing_seen(false), eeprom_present(false), gauge_present(false),
          nvs_error(false),
          rtc_io_error(false), rtc_tick(FtRtcTick::Unknown), rtc_backup(FtRtcBackup::Unverified),
          buttons_done(0), button_error(FtButtonError::None), button_err_a(0), button_err_b(0) {}
};

// ── I2C ─────────────────────────────────────────────────────────

/**
 * 把 endTransmission 回傳碼分類
 * @param code Wire.endTransmission() 回傳值
 * @return Present / Absent / BusStuck / BusError
 */
FtProbe ft_classify_i2c_probe(uint8_t code);

/**
 * 套用一輪 I2C 掃描結果（不可變：回傳新狀態）
 * 在線旗標每輪重算；bus_stuck／bus_error／rtc_missing_seen 只會被設起不會被清（黏性），
 * error_code 保留第一個
 * @param s    目前狀態
 * @param scan 三個位址的回傳碼
 * @return 更新後的狀態
 */
FactoryTestState ft_apply_i2c_scan(const FactoryTestState& s, const FtI2cScan& scan);

/**
 * 標記主控 NVS 讀寫失敗（黏性；不可變：回傳新狀態）
 * @param s 目前狀態
 * @return 帶 nvs_error 的新狀態
 */
FactoryTestState ft_mark_nvs_error(const FactoryTestState& s);

// ── 按鍵 ────────────────────────────────────────────────────────

/**
 * 套用同一輪掃描內偵測到的按鍵下降緣（不可變：回傳新狀態）
 * 規則：
 *   - 已有 button_error 或 8 顆已驗收完 → 原樣回傳（黏性／已完成）
 *   - count == 0 → 原樣回傳
 *   - count >= 2 → Multiple，err_a/err_b = 前兩顆索引
 *   - 索引 >= FT_BUTTON_COUNT → 忽略
 *   - 索引 == buttons_done → 驗收，buttons_done + 1
 *   - 索引 == buttons_done - 1（剛驗收過的那顆再按一次）→ 忽略
 *   - 其他 → WrongOrder，err_a = 預期索引、err_b = 實際索引
 * @param s       目前狀態
 * @param indices 本輪下降緣的按鍵索引陣列
 * @param count   陣列長度
 * @return 更新後的狀態
 */
FactoryTestState ft_apply_button_presses(const FactoryTestState& s, const uint8_t* indices, uint8_t count);

/**
 * 8 顆是否已依序驗收完
 * @param s 目前狀態
 * @return true = buttons_done 達 FT_BUTTON_COUNT
 */
bool ft_all_buttons_done(const FactoryTestState& s);

/**
 * 按鍵在 TFT 上的 ASCII 名稱，順序對齊 gpio-allocation.md §1（BTN_PRIMARY..BTN_SHOCK）
 * @param index 按鍵索引 0~7
 * @return 名稱字串；超出範圍回 "?"
 */
const char* ft_button_label(uint8_t index);

// ── RTC ─────────────────────────────────────────────────────────

/**
 * 解碼 DS3231 時間暫存器（BCD，24 小時制）並驗證範圍
 * 任一 nibble > 9、保留位元非 0、12 小時制旗標、日/月為 0 或超界都算失敗；
 * 全 0xFF（SDA 浮空）與全 0x00（日=0）都會被擋下
 * @param raw 7 bytes，對應暫存器 0x00~0x06
 * @param out 解碼結果，失敗時內容不可信
 * @return true = 合法時間
 */
bool ft_rtc_decode(const uint8_t* raw, FtRtcTime& out);

/**
 * 把時間欄位轉成 Unix epoch 秒（視 RTC 為 UTC，2000~2099），相鄰秒差在月底、閏年、年底都恰為 1，
 * 離線秒數與走時判定都靠這個差值
 * @param t 解碼後的時間
 * @return 1970-01-01 起算的秒數
 */
uint32_t ft_rtc_time_to_seconds(const FtRtcTime& t);

/**
 * 依兩次 RTC 讀值與「距上次秒數變化的經過時間」判定走時狀態
 * @param prev_seconds            上次觀測到的秒數
 * @param now_seconds             這次讀到的秒數
 * @param elapsed_since_change_ms 距上次秒數變化的毫秒數
 * @return 倒退或前進超過 FT_RTC_MAX_STEP_S → Invalid；合理前進 → Ok；
 *         沒變且未達 FT_RTC_TICK_TIMEOUT_MS → Unknown；沒變且已達 → Stuck
 */
FtRtcTick ft_classify_rtc_tick(uint32_t prev_seconds, uint32_t now_seconds, uint32_t elapsed_since_change_ms);

/**
 * 把本輪觀測到的走時狀態套進狀態（不可變：回傳新狀態）
 * - Stuck 黏性：已經 Stuck 就不會被之後的 Ok／Unknown 覆蓋，短暫停振不能靠恢復走時翻回 PASS
 * - Invalid 轉成黏性的 rtc_io_error（合法 BCD 但倒退／亂跳，跟讀到亂資料同一類），rtc_tick 不動
 * @param s        目前狀態
 * @param observed 本輪 ft_classify_rtc_tick 的結果
 * @return 更新後的狀態
 */
FactoryTestState ft_apply_rtc_tick(const FactoryTestState& s, FtRtcTick observed);

/**
 * 開機時判定備援電池狀態。
 * 規則（由上而下，OSF 新證據永遠優先，不管是 RST 還是真斷電）：
 *   - osf_set && (seeded_before || has_last_seen) → Lost（有過基準還失憶 = 電池沒撐住，或 RTC VCC 線虛焊）
 *   - osf_set → Unverified（第一次上電，需要斷電驗證）
 *   - !has_last_seen → Unverified（沒有上次記錄就算不出離線時間，必須做一次斷電驗證）
 *   - off_seconds >= FT_POWER_CYCLE_MIN_OFF_S && esp_powered_off → Ok
 *     （兩個獨立證據都要：RTC 說板子離線夠久、ESP 說自己真的斷過電；缺一都可能是按住 RST）
 *   - 否則 → stored（RST 或短暫重開，沒有新證據，沿用 NVS 存的結論）
 * @param osf_set         DS3231 狀態暫存器 OSF 位元
 * @param seeded_before   NVS 記錄本機曾對這顆 RTC seed 過時間
 * @param has_last_seen   NVS 有「上次記錄的 RTC 秒數」
 * @param off_seconds     開機 RTC 讀值 − 上次記錄 − ESP 開機至今秒數；負值代表 RTC 被重設過，視為無證據
 * @param esp_powered_off ESP32 RTC slow memory 的 magic 不在（真斷電後內容隨機；RST 保留與否不影響安全性，
 *                        最壞情況只是多要求一次真正斷電）
 * @param stored          NVS 存的上一次結論
 * @return 備援電池狀態
 */
FtRtcBackup ft_classify_rtc_backup(bool osf_set, bool seeded_before, bool has_last_seen,
                                   int32_t off_seconds, bool esp_powered_off, FtRtcBackup stored);

// ── 判定 ────────────────────────────────────────────────────────

/**
 * 失敗種類（優先序的唯一定義）：
 *   BusStuck > BusError > NvsError > RtcMissing > RtcIo > RtcStuck > RtcBattery > ButtonOrder > ButtonShort
 * @param s 目前狀態
 * @return 第一個成立的失敗種類；沒有失敗時回 None
 */
FtFailKind ft_fail_kind(const FactoryTestState& s);

/**
 * 失敗種類對應的 TFT 顯示字串（ASCII），與 docs/vendor-assembly-brief.html §7.5 對照表一致
 * @param kind 失敗種類
 * @return 原因字串；None 回 nullptr
 */
const char* ft_fail_reason_for(FtFailKind kind);

/**
 * 失敗原因（= ft_fail_reason_for(ft_fail_kind(s))）
 * @param s 目前狀態
 * @return 原因字串；非 Fail 時回 nullptr
 */
const char* ft_fail_reason(const FactoryTestState& s);

/**
 * 等待原因（TFT 顯示用 ASCII）。優先序：POWER CYCLE > RTC > BUTTONS
 * @param s 目前狀態
 * @return "POWER CYCLE" / "RTC" / "BUTTONS"；沒有等待項時回 nullptr
 */
const char* ft_pending_reason(const FactoryTestState& s);

/**
 * 總判定：有失敗原因 → Fail；有等待原因 → Pending；否則 Pass
 * @param s 目前狀態
 * @return Pass / Fail / Pending
 */
FtVerdict ft_evaluate(const FactoryTestState& s);

/**
 * 判定結果的 TFT 顯示字串
 * @param v 判定
 * @return "PASS" / "FAIL" / "WAITING"
 */
const char* ft_verdict_label(FtVerdict v);

/**
 * 備援電池狀態的 TFT 顯示字串（RTC 區第二行）
 * @param b 備援狀態
 * @return ASCII 字串
 */
const char* ft_rtc_backup_label(FtRtcBackup b);

}  // namespace ems
