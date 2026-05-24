// EMS DoseSync Pro — Dev-Phase 2/3: BLE 對時純函式 lib
//
// 對應 spec：docs/ble-time-sync-protocol.md §2 / §4
// 對應計畫：docs/incremental-impl-plan.md §2E（軟體對時）/ §3B（DS3231 RTC 整合）
// 測試入口：firmware/test/test_time_sync/test_main.cpp
//
// 用途：
//   解析 App → Device 的 time_sync JSON，更新內部 epoch_ms_offset，
//   產生 time_sync_ack JSON 回應。本 lib 不直接寫 DS3231——RTC 整合層
//   依 time_sync_handle() 結果與 rtc_present 旗標自行決定是否寫 RTC。
//
// 設計：
//   純函式 + 純資料 struct + ArduinoJson 7，無 Arduino 硬體依賴，
//   可在 native env 跑 unit test。
//
// Threading 契約：
//   TimeSyncState 無內部鎖。BLE callback（GATT task）收到 RX 後須 enqueue
//   到 main loop 再呼叫 time_sync_handle()，對齊 ble_callback_non_blocking
//   記憶模式。time_sync_current_epoch_ms() 純讀取，但跨 task 讀取仍需 caller
//   保證 state 物件存活與不被半寫。

#pragma once

#include <cstddef>
#include <cstdint>

namespace ems {

// Epoch 有效範圍（spec §2.3 拒絕條件）
// 2024-01-01 00:00:00 UTC → 1704067200000 ms
// 2100-01-01 00:00:00 UTC → 4102444800000 ms
constexpr uint64_t TIME_SYNC_MIN_EPOCH_MS = 1704067200000ULL;
constexpr uint64_t TIME_SYNC_MAX_EPOCH_MS = 4102444800000ULL;

// DS3231 寫入門檻：時間差 > 30 秒才寫，避免頻繁灼燒（spec §4.2）
// 本 lib 不直接寫 RTC，但門檻常數定在這裡供整合層共用。
constexpr uint64_t TIME_SYNC_RTC_WRITE_THRESHOLD_MS = 30000;

/**
 * 處理結果列舉。
 *
 * - Applied：valid JSON + epoch 在範圍內 → state 已更新 + ACK 已寫入 out buffer
 * - Rejected：valid JSON 但 epoch 越界 → state 未更新 + ACK 已寫入（applied:false）
 * - ParseError：JSON 解析失敗 / type 非 "time_sync" / 必填欄位缺失
 *               → state 未更新 + **不**寫 ACK（spec §2.3：「JSON 解析失敗 → 不回 ACK」）
 */
enum class TimeSyncResult : uint8_t {
    Applied    = 0,
    Rejected   = 1,
    ParseError = 2,
};

/**
 * 對時狀態。caller 持有，handle() 寫入，current_epoch_ms() 讀取。
 *
 * 欄位：
 *   synced           — 是否成功對過時。false 時 current_epoch_ms() 永遠回 0
 *   epoch_ms_offset  — = applied_epoch_ms - now_millis_at_apply（時間原點偏移）
 *   tz_offset_min    — 時區偏移（分鐘），台北 = 480。spec §2.1 規定僅供 UI 顯示
 *                       使用，不影響 timestamp 儲存（儲存一律 UTC epoch ms）。
 */
struct TimeSyncState {
    bool     synced;
    uint64_t epoch_ms_offset;
    int16_t  tz_offset_min;
};

/**
 * 重設 state 為「未對時」初始狀態。
 *
 * @param state 目標 state 物件（非 null）
 */
void time_sync_init(TimeSyncState* state);

/**
 * 由 state 與當下 millis() 計算當前 epoch ms。
 *
 * @param state       目前對時狀態（非 null）
 * @param now_millis  當下 millis() 值
 * @return 若已對時：epoch_ms_offset + now_millis；否則 0（未對時態）
 */
uint64_t time_sync_current_epoch_ms(const TimeSyncState* state, uint64_t now_millis);

/**
 * 從 RTC 種 seed 軟體對時 state（boot 後若 DS3231 有時間，不必等 BLE 連線）。
 * 邏輯與 BLE Apply 相同：epoch_ms_offset = rtc_epoch - now_millis，
 * 後續 current_epoch_ms() 用 millis() + offset 算同等於軟體對時的行為。
 *
 * tz_offset_min 保持原值不動（DS3231 不存 tz，由後續 BLE 對時帶入）。
 *
 * 對齊 plan：docs/ds3231-integration-plan.md §5.2
 *
 * @param state       要 seed 的 TimeSyncState（非 null）
 * @param rtc_epoch   從 RTC 讀到的 epoch ms（caller 已確認 > TIME_SYNC_MIN_EPOCH_MS）
 * @param now_millis  當下 millis() 值
 */
void time_sync_seed_from_rtc(TimeSyncState* state,
                             uint64_t rtc_epoch,
                             uint64_t now_millis);

/**
 * 解析 time_sync JSON 訊息，視結果更新 state 並產生 time_sync_ack JSON。
 *
 * @param state          對時 state 物件，Applied 時會被覆寫（非 null）
 * @param input          BLE RX 收到的 JSON bytes（非 null；不需 null-terminated）
 * @param input_len      input 長度（bytes）
 * @param now_millis     當下 millis() 值（spec §4.1 計算 epoch_ms_offset 用）
 * @param rtc_present    整合層告知 DS3231 是否在場且寫入成功（影響 ACK source 欄位）
 * @param ack_out        ACK JSON 輸出 buffer（非 null）；ParseError 時不寫入
 * @param ack_buf_size   ack_out buffer 容量（含 '\0' 結尾位元）
 * @param ack_len_out    輸出 ACK JSON 實際長度（不含 '\0'）；nullptr 視為不需要
 * @return TimeSyncResult — Applied / Rejected / ParseError
 *
 * ACK schema 對齊 spec §2.2：
 *   { "type": "time_sync_ack",
 *     "req_id": "...",          // 鏡射 input 的 req_id，缺則本欄位省略
 *     "device_now_ms": <uint64>, // Applied: 新 offset + now_millis；Rejected: 舊狀態
 *     "applied": <bool>,
 *     "source": "ble" | "ble+rtc" | "rejected",
 *     "rtc_present": <bool> }
 */
TimeSyncResult time_sync_handle(
    TimeSyncState* state,
    const uint8_t* input,
    size_t input_len,
    uint64_t now_millis,
    bool rtc_present,
    char* ack_out,
    size_t ack_buf_size,
    size_t* ack_len_out);

}  // namespace ems
