// EMS DoseSync Pro — Phase C: 6 秒通氣節奏（純函式）
//
// SoT 對齊：
//   - docs/EMS_DoseSync_Pro_Prototype_V1.md §13（獨立模式）/ §14（OHCA 內輔助區塊）
//   - docs/pm-dev-spec.md §7
//   - docs/EMS_DoseSync_Pro_Test_Plan_V1.md §5.3
//
// 設計原則：
//   - 1 秒一拍，6 拍循環（BEAT_1~BEAT_6）
//   - 第 1 拍：加強提示音 + 紅燈閃 + 畫面反紅
//   - 第 2~6 拍：短提示音
//   - 通氣音量 0~5（NVS 獨立 key，與系統音量分開）
//     volume = 0 時：buzz 全關，但 LED 紅閃 + 畫面反紅 + 顯示數字仍保留（V1 §13.10 / §14.6 靜音規則）
//   - 邊緣偵測：buzz_* 在「跨越 beat 邊界」的那一個 tick 為 true，避免重複觸發
#pragma once
#include <cstdint>

namespace ems {

// ===== 時序常數 =====
constexpr uint32_t VENT_BEAT_INTERVAL_MS = 1000;   // 1 秒一拍
constexpr uint32_t VENT_CYCLE_MS         = 6000;   // 6 拍一循環

// ===== 音量常數 =====
constexpr uint8_t  VENT_VOLUME_MIN     = 0;        // 靜音
constexpr uint8_t  VENT_VOLUME_MAX     = 5;
constexpr uint8_t  VENT_VOLUME_DEFAULT = 3;

// ===== Beat enum =====
typedef enum {
    VENT_BEAT_1 = 0,
    VENT_BEAT_2 = 1,
    VENT_BEAT_3 = 2,
    VENT_BEAT_4 = 3,
    VENT_BEAT_5 = 4,
    VENT_BEAT_6 = 5,
} vent_beat_t;

// ===== 輸出 struct =====
typedef struct {
    bool    buzz_short;        // 第 2~6 拍短音（邊緣觸發；volume=0 時恆為 false）
    bool    buzz_emphasis;     // 第 1 拍加強音（邊緣觸發；volume=0 時恆為 false）
    bool    led_red_flash;     // 第 1 拍紅燈閃（不受 volume 影響）
    bool    screen_invert_red; // 第 1 拍畫面反紅（不受 volume 影響）
    uint8_t display_number;    // 1~6（給 OLED 顯示）
} vent_output_t;

/**
 * 依 since_start_ms 計算當前 beat（純函式）
 *
 * 公式：beat = (since_start_ms / VENT_BEAT_INTERVAL_MS) % 6
 * 邊界：since_start_ms = 0    → BEAT_1
 *       since_start_ms = 999  → BEAT_1
 *       since_start_ms = 1000 → BEAT_2
 *       since_start_ms = 5999 → BEAT_6
 *       since_start_ms = 6000 → BEAT_1（第 2 個循環）
 */
vent_beat_t computeVentBeat(uint32_t since_start_ms);

/**
 * 依 since 與 volume 回傳當前輸出（純函式 + 邊緣偵測）
 *
 * 簽章說明：刻意偏離 Test Plan §5.3 概念簽章 `decideVentOutput(since, volume)`，
 * 改採 `(prev, since, volume)` 三參數，與 Phase A `decideOhcaOutput` 一致的
 * stateless 邊緣偵測契約 — lib 不持有計時 state，由 caller 提供前後 tick 比對，
 * 確保 buzz_* 每 beat 只觸發一次，避免 caller 200ms 高頻 update 造成重複觸發。
 *
 * @param prev_since_ms      上一次 tick 的 since_start_ms（用於邊緣偵測）
 * @param since_start_ms     當前 since_start_ms
 * @param volume             通氣音量（0~5；外部由 NVS 讀取，0 = 靜音）
 *
 * 邊緣偵測規則：
 *   buzz_emphasis = (current_beat == BEAT_1) && (current_beat != prev_beat) && (volume > 0)
 *   buzz_short    = (current_beat 1~5) && (current_beat != prev_beat) && (volume > 0)
 *   led_red_flash / screen_invert_red：current_beat == BEAT_1 即為 true（不受 volume 影響）
 *   display_number：current_beat + 1（永遠 1~6）
 *
 * 特殊：since_start_ms == 0 時視為「剛啟動，第一拍未觸發過」，
 *        若 prev_since_ms == 0 且 since_start_ms == 0 → buzz=false（避免初始化時誤觸發）
 *        若 prev_since_ms == 0 且 since_start_ms > 0  → 視 prev_beat 為「無效」，由 current_beat 決定 buzz
 */
vent_output_t decideVentOutput(uint32_t prev_since_ms,
                               uint32_t since_start_ms,
                               uint8_t  volume);

/** 將任意 int16_t volume 值 clamp 到 [VENT_VOLUME_MIN, VENT_VOLUME_MAX] */
uint8_t clampVentVolume(int16_t v);

} // namespace ems
