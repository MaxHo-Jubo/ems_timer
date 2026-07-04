// EMS DoseSync Pro — Dev-Phase 3: RTC × time_sync 整合膠合層（純函式）
//
// 對應 plan：docs/ds3231-integration-plan.md §4.1（boot seed）/ §5.1（write-back）
// 測試入口：firmware/test/test_rtc_integration/test_main.cpp
//
// 用途：
//   把原本內嵌在 main.cpp setup()/BLE handler 的兩段「RtcBackend × TimeSyncState」
//   膠合邏輯抽成純函式，讓 production 與 native test 共用同一段決策（消除 test 端
//   複製品，對齊 memory feedback_extract_testable_pure_logic）。
//   caller（main.cpp）保留硬體偵測（begin）、Applied/rtc_present gate 與 Serial log；
//   本 lib 只負責「讀 RTC → 決策 → seed / write-back」的純資料運算。
//
// 設計：純函式 + 純資料 struct，無 Arduino 硬體依賴，可在 native env 跑 unit test。

#pragma once

#include <cstdint>

#include "ems_rtc.h"
#include "ems_time_sync.h"

namespace ems {

/**
 * boot seed 結果分類（取代舊 main.cpp 內嵌的三分支 if/else）。
 * - Seeded：RTC 有有效且落在 [floor, ceiling] 的時間 → state 已 seed
 * - NotSet：backend 無有效時間（!valid，含不在線 / present 但未設過）→ 未 seed
 * - OutOfRange：有讀到時間但越界（爛資料，如年份溢位）→ 未 seed
 */
enum class RtcSeedOutcome : uint8_t {
    Seeded     = 0,
    NotSet     = 1,
    OutOfRange = 2,
};

/** rtc_try_seed 回傳：outcome + 讀到的 epoch（供 caller log；NotSet 時為 0） */
struct RtcSeedResult {
    RtcSeedOutcome outcome;
    uint64_t       epoch_ms;
};

/**
 * Boot 時嘗試用 backend 的 RTC 時間 seed 軟體對時 state。
 * 封裝 main.cpp setup() 的三分支決策，讓 production 與 test 共用同一段邏輯。
 * caller 負責 begin()/偵測與依 outcome 印 log；本函式只讀 now() + 決策 + seed。
 *
 * @param backend     已偵測的 RtcBackend（不在線者傳 NullRtcBackend 亦可，now()→invalid）
 * @param state       要 seed 的 TimeSyncState（非 null）
 * @param now_millis  當下 millis()
 * @param floor_ms    可接受 epoch 下界（含），對齊 spec §2.3（典型 TIME_SYNC_MIN_EPOCH_MS）
 * @param ceiling_ms  可接受 epoch 上界（含），典型 TIME_SYNC_MAX_EPOCH_MS
 * @return RtcSeedResult — outcome 分類 + 讀到的 epoch
 */
RtcSeedResult rtc_try_seed(const RtcBackend& backend,
                           TimeSyncState*    state,
                           uint64_t          now_millis,
                           uint64_t          floor_ms,
                           uint64_t          ceiling_ms);

/** rtc_write_back 回傳：set 結果 + 寫回的 epoch（供 caller log） */
struct RtcWriteBackResult {
    SetResult result;
    uint64_t  epoch_ms;
};

/**
 * BLE time_sync Applied 後，把當下軟體時鐘反向寫回 RTC（App 變主時鐘源）。
 * 封裝 main.cpp 的「算 current_epoch_ms → set_epoch_ms」兩步，讓 production 與
 * test 共用。caller 負責 Applied / rtc_present gate 與依 result 印 log。
 *
 * now_millis 由 caller 於「寫回時刻」讀（與 time_sync_handle 各讀一次 millis()，
 * 容忍其間 loop 前進的漂移——寫回值反映寫回當下而非 apply 當下）。
 *
 * @param backend     目標 RtcBackend（非 null）；不在線回 NotPresent（no-op）
 * @param state       已對時的 TimeSyncState（非 null）
 * @param now_millis  寫回時刻的 millis()
 * @return RtcWriteBackResult — set 結果 + 實際寫回的 epoch
 */
RtcWriteBackResult rtc_write_back(RtcBackend*          backend,
                                  const TimeSyncState* state,
                                  uint64_t             now_millis);

}  // namespace ems
