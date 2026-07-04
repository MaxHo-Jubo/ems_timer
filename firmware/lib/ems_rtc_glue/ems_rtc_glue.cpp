// EMS DoseSync Pro — Dev-Phase 3: RTC × time_sync 整合膠合層 (impl)
//
// 對應 header：ems_rtc_glue.h

#include "ems_rtc_glue.h"

namespace ems {

RtcSeedResult rtc_try_seed(const RtcBackend& backend,
                           TimeSyncState*    state,
                           uint64_t          now_millis,
                           uint64_t          floor_ms,
                           uint64_t          ceiling_ms) {
    // STEP 01: 讀 RTC；無有效時間 → NotSet（未設過 / 不在線，等 BLE 對時）
    const RtcReading r = backend.now();
    if (!r.valid) {
        return {RtcSeedOutcome::NotSet, 0};
    }
    // STEP 02: 範圍檢查（爛資料如年份溢位）→ OutOfRange，不 seed
    //   floor/ceiling 為含端點（對齊 spec §2.3 用 < / > 拒絕，等於邊界值 accept）
    if (r.epoch_ms < floor_ms || r.epoch_ms > ceiling_ms) {
        return {RtcSeedOutcome::OutOfRange, r.epoch_ms};
    }
    // STEP 03: 合法 → seed 軟體對時（seed_from_rtc 內另有 rtc<now 的 underflow guard）
    time_sync_seed_from_rtc(state, r.epoch_ms, now_millis);
    return {RtcSeedOutcome::Seeded, r.epoch_ms};
}

RtcWriteBackResult rtc_write_back(RtcBackend*          backend,
                                  const TimeSyncState* state,
                                  uint64_t             now_millis) {
    // STEP 01: 算「寫回時刻」的軟體時鐘 epoch（now_millis 由 caller 於寫回當下讀）
    const uint64_t app_epoch = time_sync_current_epoch_ms(state, now_millis);
    // STEP 02: 反向寫回 RTC，回傳 set 結果 + epoch 供 caller 分類 log
    return {backend->set_epoch_ms(app_epoch), app_epoch};
}

}  // namespace ems
