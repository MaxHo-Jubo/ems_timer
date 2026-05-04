// EMS DoseSync Pro — Phase B: 案件總覽聚合實作
//
// 對應 header: ems_case_summary.h
#include "ems_case_summary.h"

namespace ems {

// ============================================================
//  初始化
// ============================================================

void caseSummary_init(ohca_case_summary_t* s) {
    s->epi_local            = 0;
    s->epi_pre_handover     = 0;
    s->epi_pure_supp        = 0;
    s->epi_total            = 0;
    s->shock_local          = 0;
    s->shock_pre_handover   = 0;
    s->shock_pure_supp      = 0;
    s->shock_total          = 0;
    s->amio_total           = 0;
    s->first_epi_local_ms   = 0;
    s->last_epi_local_ms    = 0;
    s->last_shock_local_ms  = 0;
    s->last_amio_ms         = 0;
    s->case_start_ms        = 0;
    s->case_end_ms          = 0;
}

// ============================================================
//  單筆事件聚合
// ============================================================

void caseSummary_aggregate(ohca_case_summary_t* s, const ems_event_t* e) {
    // STEP 01: 依 type 累加計數欄位
    switch (e->type) {
        case EVT_EPI_LOCAL:
            s->epi_local += e->count;
            s->epi_total += e->count;
            // STEP 01.01: 本機 EPI 更新 first/last
            if (s->first_epi_local_ms == 0) {
                s->first_epi_local_ms = e->timestamp_ms;
            }
            s->last_epi_local_ms = e->timestamp_ms;
            break;

        case EVT_EPI_PRE_HANDOVER:
            s->epi_pre_handover += e->count;
            s->epi_total        += e->count;
            // 補登不影響本機 EPI 時間欄位
            break;

        case EVT_EPI_PURE_SUPP:
            s->epi_pure_supp += e->count;
            s->epi_total     += e->count;
            break;

        case EVT_SHOCK_LOCAL:
            s->shock_local += e->count;
            s->shock_total += e->count;
            // V1 §11.3：電擊不記 first，僅 last
            s->last_shock_local_ms = e->timestamp_ms;
            break;

        case EVT_SHOCK_PRE_HANDOVER:
            s->shock_pre_handover += e->count;
            s->shock_total        += e->count;
            break;

        case EVT_SHOCK_PURE_SUPP:
            s->shock_pure_supp += e->count;
            s->shock_total     += e->count;
            break;

        case EVT_AMIODARONE:
            s->amio_total += e->count;
            s->last_amio_ms = e->timestamp_ms;
            break;
    }
}

// ============================================================
//  從事件陣列建構 summary
// ============================================================

void caseSummary_build(ohca_case_summary_t* out,
                       const ems_event_t*   events,
                       uint16_t             event_count,
                       uint64_t             case_start_ms,
                       uint64_t             case_end_ms) {
    caseSummary_init(out);
    out->case_start_ms = case_start_ms;
    out->case_end_ms   = case_end_ms;
    for (uint16_t i = 0; i < event_count; i++) {
        caseSummary_aggregate(out, &events[i]);
    }
}

// ============================================================
//  Timeline 排序（insertion sort，N 通常 < 50）
// ============================================================

void caseSummary_buildTimeline(uint16_t*          out_indices,
                               const ems_event_t* events,
                               uint16_t           n) {
    if (n == 0) return;

    // STEP 01: 初始化索引陣列為 [0, 1, ..., n-1]
    for (uint16_t i = 0; i < n; i++) {
        out_indices[i] = i;
    }

    // STEP 02: insertion sort
    //   主鍵：events[idx].timestamp_ms
    //   次鍵：events[idx].event_id（同 ts 時用流水號 tie-break）
    for (uint16_t i = 1; i < n; i++) {
        uint16_t key = out_indices[i];
        uint64_t key_ts = events[key].timestamp_ms;
        uint32_t key_id = events[key].event_id;

        int32_t j = i - 1;
        while (j >= 0) {
            uint16_t cmp = out_indices[j];
            uint64_t cmp_ts = events[cmp].timestamp_ms;
            uint32_t cmp_id = events[cmp].event_id;

            // 比較：先 ts，後 event_id
            bool greater = (cmp_ts > key_ts) ||
                           (cmp_ts == key_ts && cmp_id > key_id);
            if (!greater) break;

            out_indices[j + 1] = out_indices[j];
            j--;
        }
        out_indices[j + 1] = key;
    }
}

} // namespace ems
