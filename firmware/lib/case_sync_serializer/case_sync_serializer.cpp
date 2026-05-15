// EMS DoseSync Pro — Impl-Phase F: case_sync JSON serializer 實作
//
// 對應 header：case_sync_serializer.h
// 對應 test： firmware/test/test_case_sync_serializer/test_main.cpp

#include "case_sync_serializer.h"

#include <ArduinoJson.h>

namespace ems {

namespace {

// 安全字串：nullptr → 空字串
inline const char* safe_str(const char* s) {
    return s != nullptr ? s : "";
}

// 把 ohca_case_summary_t 寫成 JSON sub-object（對齊 V1 §11 欄位）
void write_summary(JsonObject summary_obj, const ohca_case_summary_t& summary) {
    summary_obj["epi_local"]           = summary.epi_local;
    summary_obj["epi_pre_handover"]    = summary.epi_pre_handover;
    summary_obj["epi_pure_supp"]       = summary.epi_pure_supp;
    summary_obj["epi_total"]           = summary.epi_total;
    summary_obj["shock_local"]         = summary.shock_local;
    summary_obj["shock_pre_handover"]  = summary.shock_pre_handover;
    summary_obj["shock_pure_supp"]     = summary.shock_pure_supp;
    summary_obj["shock_total"]         = summary.shock_total;
    summary_obj["amio_total"]          = summary.amio_total;
    summary_obj["first_epi_local_ms"]  = summary.first_epi_local_ms;
    summary_obj["last_epi_local_ms"]   = summary.last_epi_local_ms;
    summary_obj["last_shock_local_ms"] = summary.last_shock_local_ms;
    summary_obj["last_amio_ms"]        = summary.last_amio_ms;
    summary_obj["case_start_ms"]       = summary.case_start_ms;
    summary_obj["case_end_ms"]         = summary.case_end_ms;
}

}  // namespace

size_t case_sync_serialize_to_json(char* out_buf, size_t buf_size,
                                   const CaseSyncMeta& meta,
                                   const ems_event_t*  events,
                                   size_t              event_count,
                                   const ohca_case_summary_t& summary) {
    // STEP 01: 防呆 — out_buf nullptr / buf_size 0
    if (out_buf == nullptr || buf_size == 0) {
        return 0;
    }

    // STEP 02: 組裝 JsonDocument（ArduinoJson 7，自動成長無需預估容量）
    JsonDocument doc;
    doc["type"]          = "case_sync";
    doc["case_id"]       = safe_str(meta.case_id);
    doc["mode"]          = safe_str(meta.mode);
    doc["device_name"]   = safe_str(meta.device_name);
    doc["device_id"]     = safe_str(meta.device_id);
    doc["fw_version"]    = safe_str(meta.fw_version);
    doc["started_at_ms"] = meta.started_at_ms;
    doc["ended_at_ms"]   = meta.ended_at_ms;

    // STEP 03: events array
    JsonArray events_arr = doc["events"].to<JsonArray>();
    for (size_t i = 0; i < event_count; ++i) {
        JsonObject e = events_arr.add<JsonObject>();
        e["event_id"]         = events[i].event_id;
        e["type"]             = static_cast<uint8_t>(events[i].type);
        e["timestamp_ms"]     = events[i].timestamp_ms;
        e["elapsed_ms"]       = events[i].elapsed_ms;
        e["count"]            = events[i].count;
        e["actual_time_null"] = events[i].actual_time_null;
    }

    // STEP 04: summary sub-object
    JsonObject summary_obj = doc["summary"].to<JsonObject>();
    write_summary(summary_obj, summary);

    // STEP 05: 先用 measureJson 預判完整輸出大小（不含 '\0'），若不足直接回 0
    //          —— 比 serialize 後檢查 written 可靠：ArduinoJson 7 的 serializeJson
    //          在 buf 不足時會 truncate 並寫 '\0'，written 不代表「完整 JSON」
    size_t required = measureJson(doc);
    if (required + 1 > buf_size) {  // 需 '\0' 終止符 1 byte
        return 0;
    }

    // STEP 06: 序列化到 out_buf（已預判過容量足夠）
    return serializeJson(doc, out_buf, buf_size);
}

}  // namespace ems
