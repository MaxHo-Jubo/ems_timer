// EMS DoseSync Pro — Phase A: OHCA EPI 倒數引擎（純函式）
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §6（EPI 倒數）/ §23.2（啟動白名單）
// 工程規格：docs/pm-dev-spec.md §4
// 測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §3.1 / §3.3.1
//
// 本模組只做純邏輯：
//   advanceOhcaPhase  — 依 since_last_epi_ms 自動推進 phase
//   decideOhcaOutput  — 依 phase + 邊緣（prev → curr）回傳 LED / 蜂鳴 / 顯示輸出
// 不呼叫 millis()、不做 side effect；可在 native 環境直接編譯測試。
#pragma once
#include <cstdint>

namespace ems {

// ===== 時序常數（pm-dev-spec §4） =====
// OHCA 預設：4 分鐘倒數（Training 可選 30s / 60s / 240s）
constexpr uint32_t OHCA_EPI_CYCLE_MS_DEFAULT = 240000;  // OHCA 預設 4 分鐘倒數
constexpr uint32_t EPI_WARNING_MS            =  60000;  // 剩 1 分鐘進預警
constexpr uint32_t EPI_ALARM_INITIAL_MS      =   5000;  // ALARMING 連續 5s
constexpr uint32_t EPI_ALARM_REMIND_INTERVAL_MS = 15000; // OVERTIME 每 15s 短嗶
constexpr uint32_t EPI_WARN_BEEP_INTERVAL_MS =  15000;  // WARNING 每 15s 短嗶

// §15.7：Training 非標準週期（< TRAINING_WARNING_MIN_CYCLE_MS）不進 WARNING 階段
constexpr uint32_t TRAINING_WARNING_MIN_CYCLE_MS = 240000;  // 需 ≥240s 才顯示 WARNING

// ===== Phase 列舉（pm-dev-spec §4 對齊） =====
typedef enum {
    OHCA_PHASE_WAIT_FIRST_EPI = 0,
    OHCA_PHASE_COUNTDOWN      = 1,
    OHCA_PHASE_WARNING        = 2,
    OHCA_PHASE_ALARMING       = 3,
    OHCA_PHASE_OVERTIME       = 4,
} ohca_phase_t;

// ===== 輸出 struct（pm-dev-spec §4） =====
typedef struct {
    bool        buzz_short;             // 短嗶（單一 tick 觸發；邊緣偵測）
    bool        buzz_alarm_continuous;  // 高優先連續發報
    bool        led_yellow_slow;        // 黃燈慢閃
    bool        led_red_fast;           // 紅燈快閃
    bool        led_red_slow;           // 紅燈慢閃
    bool        vibrate;                // 震動馬達
    bool        screen_flash;           // 畫面閃紅
    const char* display_label;          // 顯示文字（"請準備給藥" / "請給藥" / nullptr）
    uint32_t    display_remaining_ms;   // 倒數剩餘 ms；OVERTIME 改為自上次 EPI 累計時間
} ohca_output_t;

/**
 * 自動推進 phase（不處理 EPI confirm 等外部事件）
 *
 * @param current           當前 phase
 * @param since_last_epi_ms 自上次 EPI 啟動的毫秒數
 * @param epi_cycle_ms      本次倒數週期（OHCA=240000，Training=30000/60000/240000）
 *
 * - WAIT_FIRST_EPI：保持不變（只能由外部 EPI 確認轉 COUNTDOWN）
 * - 其他 phase：依 since_last_epi_ms 計算應屬 phase（測試計劃 §3.3 邊界）
 *   邊界依 epi_cycle_ms 推導：
 *     COUNTDOWN_END = epi_cycle_ms - EPI_WARNING_MS
 *     WARNING 僅當 epi_cycle_ms ≥ TRAINING_WARNING_MIN_CYCLE_MS（§15.7 短倒數跳 WARNING）
 *     ALARMING_END  = epi_cycle_ms + EPI_ALARM_INITIAL_MS
 *
 * §15.7：Training 30s/60s 不進 WARNING，直接從 COUNTDOWN 轉 ALARMING
 */
ohca_phase_t advanceOhcaPhase(ohca_phase_t current,
                              uint32_t since_last_epi_ms,
                              uint32_t epi_cycle_ms);

/**
 * 依 phase + 邊緣偵測回傳輸出
 *
 * @param phase             外部狀態機決定的 phase
 * @param prev_since_ms     上一次 tick 的 since_last_epi_ms（用於邊緣偵測 buzz_short）
 * @param since_last_epi_ms 當前 since_last_epi_ms
 * @param epi_cycle_ms      倒數週期（用於推導邊界與 WARNING 分支）
 *
 * 邊緣偵測：buzz_short 在「跨越」 beep marker 的那一個 tick 為 true，其他時候為 false
 *   WARNING 區 markers：首個 marker 依 epi_cycle_ms 推導
 *   OVERTIME 區 markers：EPI_ALARM_REMIND_INTERVAL_MS 起點
 *
 * §15.7：epi_cycle_ms < TRAINING_WARNING_MIN_CYCLE_MS 時，即使 phase=WARNING 也跳過 display_label
 */
ohca_output_t decideOhcaOutput(ohca_phase_t phase,
                               uint32_t prev_since_ms,
                               uint32_t since_last_epi_ms,
                               uint32_t epi_cycle_ms);

} // namespace ems
