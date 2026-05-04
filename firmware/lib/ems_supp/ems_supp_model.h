// EMS DoseSync Pro — Phase B: 補登資料模型 + ems_event_t 統一事件型別
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §9（補登邏輯）/ §11（案件總覽）
// 工程規格：docs/pm-dev-spec.md §6（補登資料模型 / event_t 編號段位規範）
// 測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §4.1 / §4.2
//
// 設計原則：
//   - 事件型別 enum 固定 0x00~0x7F 為 V1 核心段位（不可重排）
//   - 0x80~0xFF 為擴充保留段位（V1 韌體只實作 0x01~0x07）
//   - 補登事件：actual_time_null = true、count ≥ 1（依規格 1~5 / 1~3）
//   - 本機即時事件：actual_time_null = false、count 固定 1
//   - 一筆事件 count = N 代表「N 次同類補登合併」（V1 §9.5 顯示「接手前 EPI ×N」）
//
// 重要不變式（V1 §9.7）：
//   - 補登成立後：不可撤銷、不可修改、不可刪除（韌體層拒絕 mutate）
//   - 補登不啟動 / 不重啟 / 不解除 EPI 倒數（§23.2 啟動白名單）
#pragma once
#include <cstdint>

namespace ems {

// ===== 事件型別段位（pm-dev-spec §6） =====
typedef enum {
    // V1 核心事件段位（封版固定）
    EVT_EPI_LOCAL          = 0x01,  // 本機 EPI（有時間戳，重啟倒數）
    EVT_SHOCK_LOCAL        = 0x02,  // 本機電擊（有時間戳，不重啟倒數）
    EVT_AMIODARONE         = 0x03,  // Amiodarone（有時間戳，不重啟倒數）
    EVT_EPI_PRE_HANDOVER   = 0x04,  // 接手前 EPI 補登（無時間戳）
    EVT_EPI_PURE_SUPP      = 0x05,  // 純補登 EPI（無時間戳）
    EVT_SHOCK_PRE_HANDOVER = 0x06,  // 接手前電擊補登（無時間戳）
    EVT_SHOCK_PURE_SUPP    = 0x07,  // 純補登電擊（無時間戳）

    // 0x80~0xFF 為擴充保留段位（V1 韌體不實作）
} ems_event_type_t;

// ===== 事件 struct（pm-dev-spec §6） =====
typedef struct {
    uint32_t          event_id;          // 案件內流水號，從 1 起
    ems_event_type_t  type;              // 事件型別
    uint64_t          timestamp_ms;      // 補登 = recorded_at；本機 = 實際發生時間
    uint32_t          elapsed_ms;        // 自案件起點扣除 PAUSE
    uint8_t           count;             // 補登批量（1~5/1~3）；本機固定 1
    bool              actual_time_null;  // 補登 = true；本機 = false
} ems_event_t;

// ===== 補登類型（UI 層用；對應 4 種補登事件 type） =====
typedef enum {
    SUPP_TYPE_EPI_PRE_HANDOVER   = 0,  // 接手前 EPI（1~5）
    SUPP_TYPE_EPI_PURE           = 1,  // 純補登 EPI（1~3）
    SUPP_TYPE_SHOCK_PRE_HANDOVER = 2,  // 接手前電擊（1~5）
    SUPP_TYPE_SHOCK_PURE         = 3,  // 純補登電擊（1~3）
} supp_type_t;

// ===== 補登次數驗證 =====
//
// 規格（V1 §9.6 / Test Plan §4.1）：
//   接手前 EPI / 接手前電擊：1~5
//   純補登 EPI / 純補登電擊：1~3
//   邊界：嘗試 0 或超過上限 → 拒絕
constexpr uint8_t SUPP_COUNT_MIN = 1;

/** 取得指定補登類型的次數上限（接手前=5，純補登=3） */
uint8_t suppCountMax(supp_type_t t);

/** 驗證 count 是否落在 [MIN, max(t)] 區間 */
bool isSuppCountValid(supp_type_t t, uint8_t count);

/** 補登類型 → 對應的 ems_event_type_t */
ems_event_type_t suppTypeToEventType(supp_type_t t);

// ===== 事件建構 =====

/**
 * 建立補登事件（actual_time_null=true）
 *
 * @param out             輸出 struct
 * @param event_id        案件內流水號
 * @param supp_type       補登類型
 * @param count           次數（必須通過 isSuppCountValid 驗證）
 * @param recorded_at_ms  補登當下的 epoch ms（不是事件實際發生時間）
 * @param elapsed_ms      案件 elapsed
 *
 * 注意：不做 count 驗證；caller 須先呼叫 isSuppCountValid 把關
 */
void buildSuppEvent(ems_event_t* out,
                    uint32_t     event_id,
                    supp_type_t  supp_type,
                    uint8_t      count,
                    uint64_t     recorded_at_ms,
                    uint32_t     elapsed_ms);

/**
 * 建立本機即時事件（actual_time_null=false, count=1）
 *
 * @param type 必須為 EVT_EPI_LOCAL / EVT_SHOCK_LOCAL / EVT_AMIODARONE
 */
void buildLocalEvent(ems_event_t*     out,
                     uint32_t         event_id,
                     ems_event_type_t type,
                     uint64_t         timestamp_ms,
                     uint32_t         elapsed_ms);

// ===== 事件分類 helper =====

/** 是否為補登事件（actual_time_null == true） */
bool isBackfillEvent(const ems_event_t* e);

/** 是否為本機即時事件（actual_time_null == false） */
bool isLocalEvent(const ems_event_t* e);

/** 是否為 EPI 類事件（含本機 + 兩種補登） */
bool isEpiEvent(const ems_event_t* e);

/** 是否為電擊類事件（含本機 + 兩種補登） */
bool isShockEvent(const ems_event_t* e);

/** 是否為 Amiodarone */
bool isAmioEvent(const ems_event_t* e);

} // namespace ems
