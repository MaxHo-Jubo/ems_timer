// EMS DoseSync Pro — Phase B: 補登資料模型實作
//
// 對應 header: ems_supp_model.h
#include "ems_supp_model.h"

namespace ems {

// ============================================================
//  補登次數上限
// ============================================================

uint8_t suppCountMax(supp_type_t t) {
    // 接手前 = 5；純補登 = 3（V1 §9.6 + Test Plan §4.1）
    switch (t) {
        case SUPP_TYPE_EPI_PRE_HANDOVER:
        case SUPP_TYPE_SHOCK_PRE_HANDOVER:
            return 5;
        case SUPP_TYPE_EPI_PURE:
        case SUPP_TYPE_SHOCK_PURE:
            return 3;
    }
    return 0;
}

bool isSuppCountValid(supp_type_t t, uint8_t count) {
    return count >= SUPP_COUNT_MIN && count <= suppCountMax(t);
}

// ============================================================
//  supp_type → event_type 映射
// ============================================================

ems_event_type_t suppTypeToEventType(supp_type_t t) {
    switch (t) {
        case SUPP_TYPE_EPI_PRE_HANDOVER:   return EVT_EPI_PRE_HANDOVER;
        case SUPP_TYPE_EPI_PURE:           return EVT_EPI_PURE_SUPP;
        case SUPP_TYPE_SHOCK_PRE_HANDOVER: return EVT_SHOCK_PRE_HANDOVER;
        case SUPP_TYPE_SHOCK_PURE:         return EVT_SHOCK_PURE_SUPP;
    }
    return EVT_EPI_LOCAL;  // unreachable；deafult fallback
}

// ============================================================
//  事件建構
// ============================================================

void buildSuppEvent(ems_event_t* out,
                    uint32_t     event_id,
                    supp_type_t  supp_type,
                    uint8_t      count,
                    uint64_t     recorded_at_ms,
                    uint32_t     elapsed_ms) {
    out->event_id         = event_id;
    out->type             = suppTypeToEventType(supp_type);
    out->timestamp_ms     = recorded_at_ms;
    out->elapsed_ms       = elapsed_ms;
    out->count            = count;
    out->actual_time_null = true;  // 補登事件無實際時間戳
}

void buildLocalEvent(ems_event_t*     out,
                     uint32_t         event_id,
                     ems_event_type_t type,
                     uint64_t         timestamp_ms,
                     uint32_t         elapsed_ms) {
    out->event_id         = event_id;
    out->type             = type;
    out->timestamp_ms     = timestamp_ms;
    out->elapsed_ms       = elapsed_ms;
    out->count            = 1;     // 本機事件固定 1
    out->actual_time_null = false;
}

// ============================================================
//  事件分類 helper
// ============================================================

bool isBackfillEvent(const ems_event_t* e) {
    return e->actual_time_null;
}

bool isLocalEvent(const ems_event_t* e) {
    return !e->actual_time_null;
}

bool isEpiEvent(const ems_event_t* e) {
    return e->type == EVT_EPI_LOCAL ||
           e->type == EVT_EPI_PRE_HANDOVER ||
           e->type == EVT_EPI_PURE_SUPP;
}

bool isShockEvent(const ems_event_t* e) {
    return e->type == EVT_SHOCK_LOCAL ||
           e->type == EVT_SHOCK_PRE_HANDOVER ||
           e->type == EVT_SHOCK_PURE_SUPP;
}

bool isAmioEvent(const ems_event_t* e) {
    return e->type == EVT_AMIODARONE;
}

} // namespace ems
