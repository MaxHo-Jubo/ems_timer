// EMS DoseSync Pro — Impl-Phase F: case_sync JSON 序列化（純函式）
//
// 對齊：docs/pm-dev-spec.md §14.1 BLE Phase F 過渡 payload
// 對應 todo：tasks/phase-f-todo.md F-3 整合層 JSON 序列化部分（純邏輯先建）
// 測試入口：firmware/test/test_case_sync_serializer/test_main.cpp
//
// 用途：
//   把 case metadata + events array + ohca_case_summary 組成單一 JSON object，
//   供 ems_sync_dispatcher SENDING state 透過 BLE chunked notify 推送到 App。
//
// 設計：
//   純函式 + ArduinoJson 7（DynamicJsonDocument），無 Arduino 硬體依賴，可在
//   native env 跑 unit test（platformio.ini 已 lib_deps ArduinoJson）。
//
// 不在 lib 範圍：
//   - 從 ems_storage 讀 case_meta_t / events.bin（整合層責任）
//   - BLE chunked send（ble_chunker + BleNusService 責任）
//   - case_id UUID v4 產生（整合層或 App 端責任）

#pragma once

#include <cstddef>
#include <cstdint>

#include "ems_supp_model.h"      // ems_event_t / ems_event_type_t
#include "ems_case_summary.h"    // ohca_case_summary_t

namespace ems {

/**
 * Case sync metadata — 不在現有 storage struct 內的補充欄位。
 *
 * 所有字串欄位由 caller 持有，serializer 只讀。nullptr 視為空字串輸出 ""。
 */
struct CaseSyncMeta {
    const char* case_id;        // UUID v4 字串（caller 產生）
    const char* mode;           // "ohca" 或 "training"
    const char* device_name;    // 救護車編號或裝置名（例「安康91」）
    const char* device_id;      // 裝置硬體 ID（例「DSP-0001」）
    const char* fw_version;     // 韌體版本（例「v1.0.0"）
    uint64_t    started_at_ms;  // 案件起點 epoch ms
    uint64_t    ended_at_ms;    // 案件結束 epoch ms（0 = 未結束）
};

/**
 * 把 case sync data 序列化成 JSON object，寫到 out_buf。
 *
 * 輸出 schema 對齊 pm-dev-spec §14.1：
 *   {
 *     "type": "case_sync",
 *     "case_id": "...", "mode": "ohca",
 *     "device_name": "...", "device_id": "...", "fw_version": "...",
 *     "started_at_ms": ..., "ended_at_ms": ...,
 *     "events": [ {type, timestamp_ms, elapsed_ms, count, actual_time_null}, ... ],
 *     "summary": { ohca_case_summary_t 全欄位 }
 *   }
 *
 * @param out_buf      輸出 buffer
 * @param buf_size     buffer 容量
 * @param meta         case metadata
 * @param events       事件陣列
 * @param event_count  事件數
 * @param summary      聚合好的案件總覽
 * @return             寫入字節數（不含 '\0'）；buf 不足或失敗 → 0
 */
size_t case_sync_serialize_to_json(char* out_buf, size_t buf_size,
                                   const CaseSyncMeta& meta,
                                   const ems_event_t*  events,
                                   size_t              event_count,
                                   const ohca_case_summary_t& summary);

}  // namespace ems
