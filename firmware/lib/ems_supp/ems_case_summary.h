// EMS DoseSync Pro — Phase B: 案件總覽聚合（純函式）
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §11（案件總覽）
// 工程規格：docs/pm-dev-spec.md §3 / §6
// 測試計劃：docs/EMS_DoseSync_Pro_Test_Plan_V1.md §4.4
//
// 設計原則：
//   - 純函式：給一陣列事件 → 算出聚合結果，無 side effect
//   - 計數欄位：epi_total / shock_total / amio_total 為各類加總
//     EPI 細分：epi_local / epi_pre_handover / epi_pure_supp
//     電擊細分：shock_local / shock_pre_handover / shock_pure_supp
//   - 計數規則：依 event.count 累加（一筆 count=N 代表 N 次）
//
// 邊界規格（Test Plan §4.4.2）：
//   - 無本機 EPI → first_epi_local_ms = 0, last_epi_local_ms = 0
//   - 僅補登 EPI、無本機 → first/last = 0，total > 0
//   - last_shock_local_ms：依 V1 §11.3 不顯示「第一次本機電擊」（僅記 last）
#pragma once
#include <cstdint>
#include "ems_supp_model.h"

namespace ems {

// ===== 案件總覽結構（V1 §11） =====
typedef struct {
    // ----- EPI 計數 -----
    uint16_t epi_local;          // 本機 EPI
    uint16_t epi_pre_handover;   // 接手前 EPI
    uint16_t epi_pure_supp;      // 純補登 EPI
    uint16_t epi_total;          // = epi_local + epi_pre_handover + epi_pure_supp

    // ----- 電擊計數 -----
    uint16_t shock_local;        // 本機電擊
    uint16_t shock_pre_handover; // 接手前電擊
    uint16_t shock_pure_supp;    // 純補登電擊
    uint16_t shock_total;        // = shock_local + shock_pre_handover + shock_pure_supp

    // ----- 藥物計數 -----
    uint16_t amio_total;         // Amiodarone 紀錄筆數

    // ----- 時間欄位（epoch ms；0 = 無） -----
    uint64_t first_epi_local_ms; // 第一次本機 EPI
    uint64_t last_epi_local_ms;  // 最後一次本機 EPI
    uint64_t last_shock_local_ms;// 最後一次本機電擊（V1 §11.3 不記第一次）
    uint64_t last_amio_ms;       // 最後一次 Amio

    // ----- 案件起訖 -----
    uint64_t case_start_ms;
    uint64_t case_end_ms;
} ohca_case_summary_t;

/** 初始化（全 0） */
void caseSummary_init(ohca_case_summary_t* s);

/**
 * 把單筆事件聚合進 summary
 *
 * - 依 event.type 累加對應計數欄位（依 event.count 累加，非筆數）
 * - 本機事件更新對應 first/last 時間欄位
 * - 補登事件不更新 first/last 本機時間欄位
 * - Amio 更新 last_amio_ms
 */
void caseSummary_aggregate(ohca_case_summary_t* s, const ems_event_t* e);

/**
 * 從事件陣列建構 summary
 *
 * @param out            輸出結構（呼叫前不需 init，函式內部會 init）
 * @param events         事件陣列
 * @param event_count    事件數
 * @param case_start_ms  案件起點 epoch ms
 * @param case_end_ms    案件結束 epoch ms（0 表案件尚未結束）
 */
void caseSummary_build(ohca_case_summary_t* out,
                       const ems_event_t*   events,
                       uint16_t             event_count,
                       uint64_t             case_start_ms,
                       uint64_t             case_end_ms);

/**
 * 建立 Timeline 排序索引（V1 §11.5 + Test Plan §4.4.3）
 *
 * 排序規則：
 *   - 主鍵：依 timestamp_ms 由小到大（本機與補登皆以此欄位排）
 *   - 次鍵（同 timestamp_ms）：依 event_id 流水號由小到大
 *
 * UI 顯示時：
 *   - 本機事件：顯示 timestamp_ms 對應時間
 *   - 補登事件（actual_time_null=true）：時間欄顯示「-」，不顯示 timestamp_ms
 *
 * @param out_indices  輸出排序後索引陣列（長度 ≥ n）
 * @param events       事件陣列
 * @param n            事件數
 *
 * 注意：函式不重新排列 events，僅輸出索引；caller 依索引讀取顯示
 */
void caseSummary_buildTimeline(uint16_t*          out_indices,
                               const ems_event_t* events,
                               uint16_t           n);

} // namespace ems
