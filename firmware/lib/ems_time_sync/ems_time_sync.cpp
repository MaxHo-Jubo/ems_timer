// EMS DoseSync Pro — Dev-Phase 2/3: BLE 對時純函式 lib (impl)
//
// 對應 header：ems_time_sync.h
// 對應 spec：docs/ble-time-sync-protocol.md §2 / §4

#include "ems_time_sync.h"

#include <ArduinoJson.h>
#include <cstring>

namespace ems {

namespace {

// ACK source 字串常數（spec §2.2 source 欄位）
constexpr const char* SOURCE_BLE          = "ble";
constexpr const char* SOURCE_BLE_PLUS_RTC = "ble+rtc";
constexpr const char* SOURCE_REJECTED     = "rejected";

// JSON 欄位 key 常數（避免散落 magic string）
constexpr const char* KEY_TYPE          = "type";
constexpr const char* KEY_EPOCH_MS      = "epoch_ms";
constexpr const char* KEY_TZ_OFFSET     = "tz_offset_min";
constexpr const char* KEY_REQ_ID        = "req_id";
constexpr const char* KEY_DEVICE_NOW    = "device_now_ms";
constexpr const char* KEY_APPLIED       = "applied";
constexpr const char* KEY_SOURCE        = "source";
constexpr const char* KEY_RTC_PRESENT   = "rtc_present";

// type 欄位值（spec §2.1 / §2.2）
constexpr const char* TYPE_TIME_SYNC      = "time_sync";
constexpr const char* TYPE_TIME_SYNC_ACK  = "time_sync_ack";

/**
 * 序列化 ACK JSON 到 out buffer。
 *
 * @param applied     是否成功對時（影響 source 與 applied 欄位）
 * @param rtc_present DS3231 是否在場（applied=true 時影響 source "ble" vs "ble+rtc"）
 * @param device_now_ms ACK device_now_ms 欄位值
 * @param req_id_opt  req_id 字串（nullptr 則 ACK 省略本欄位）
 * @param ack_out / ack_buf_size / ack_len_out  out 參數
 */
void write_ack(bool applied,
               bool rtc_present,
               uint64_t device_now_ms,
               const char* req_id_opt,
               char* ack_out,
               size_t ack_buf_size,
               size_t* ack_len_out) {
    // STEP 01: 組裝 JSON document
    JsonDocument doc;
    doc[KEY_TYPE] = TYPE_TIME_SYNC_ACK;
    if (req_id_opt != nullptr) {
        doc[KEY_REQ_ID] = req_id_opt;
    }
    doc[KEY_DEVICE_NOW] = device_now_ms;
    doc[KEY_APPLIED] = applied;

    const char* source = nullptr;
    if (!applied) {
        source = SOURCE_REJECTED;
    } else if (rtc_present) {
        source = SOURCE_BLE_PLUS_RTC;
    } else {
        source = SOURCE_BLE;
    }
    doc[KEY_SOURCE] = source;
    doc[KEY_RTC_PRESENT] = rtc_present;

    // STEP 02: 預判完整輸出大小（含 '\0'）。
    //          ArduinoJson 7 的 serializeJson 在 buf 不足時會 truncate 並寫 '\0'，
    //          written 不代表「完整 JSON」，必須用 measureJson 預判才能可靠偵測截斷。
    //          截斷時清空 buffer 並回 0，讓整合層用 ack_len_out > 0 判斷是否該 notify。
    size_t required = measureJson(doc);
    if (required + 1 > ack_buf_size) {
        if (ack_buf_size > 0) {
            ack_out[0] = '\0';
        }
        if (ack_len_out != nullptr) {
            *ack_len_out = 0;
        }
        return;
    }

    // STEP 03: 序列化到 out buffer（已預判過容量足夠）
    size_t written = serializeJson(doc, ack_out, ack_buf_size);
    if (ack_len_out != nullptr) {
        *ack_len_out = written;
    }
}

}  // anonymous namespace

void time_sync_init(TimeSyncState* state) {
    state->synced = false;
    state->epoch_ms_offset = 0;
    state->tz_offset_min = 0;
}

uint64_t time_sync_current_epoch_ms(const TimeSyncState* state, uint64_t now_millis) {
    // spec §4.1：未對時態 timestamp 為 0，由 App 端用 elapsed_ms 接力
    if (!state->synced) {
        return 0;
    }
    return state->epoch_ms_offset + now_millis;
}

TimeSyncResult time_sync_handle(
    TimeSyncState* state,
    const uint8_t* input,
    size_t input_len,
    uint64_t now_millis,
    bool rtc_present,
    char* ack_out,
    size_t ack_buf_size,
    size_t* ack_len_out) {
    // STEP 01: 預設不寫 ACK，ack_len 歸零
    if (ack_len_out != nullptr) {
        *ack_len_out = 0;
    }
    if (ack_buf_size > 0) {
        ack_out[0] = '\0';
    }

    // STEP 02: 解析 JSON（input 不一定 null-terminated，明確帶 length）
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, input, input_len);
    if (err) {
        // STEP 02.01: 解析失敗 → ParseError 不寫 ACK（spec §2.3）
        return TimeSyncResult::ParseError;
    }

    // STEP 03: 驗證 type 欄位
    const char* type = doc[KEY_TYPE].as<const char*>();
    if (type == nullptr || std::strcmp(type, TYPE_TIME_SYNC) != 0) {
        return TimeSyncResult::ParseError;
    }

    // STEP 04: 驗證必填欄位 epoch_ms 存在且為數字
    if (!doc[KEY_EPOCH_MS].is<uint64_t>()) {
        return TimeSyncResult::ParseError;
    }
    uint64_t epoch_ms = doc[KEY_EPOCH_MS].as<uint64_t>();

    // STEP 05: 抓 req_id（選填）與 tz_offset_min（spec §2.1 required，但缺失視為 0 不擋）
    const char* req_id = nullptr;
    if (doc[KEY_REQ_ID].is<const char*>()) {
        req_id = doc[KEY_REQ_ID].as<const char*>();
    }
    int16_t tz_offset_min = 0;
    if (doc[KEY_TZ_OFFSET].is<int>()) {
        tz_offset_min = static_cast<int16_t>(doc[KEY_TZ_OFFSET].as<int>());
    }

    // STEP 06: 範圍驗證（spec §2.3 拒絕條件）
    //          spec 用 < / > 拒絕，所以等於邊界值 accept
    if (epoch_ms < TIME_SYNC_MIN_EPOCH_MS || epoch_ms > TIME_SYNC_MAX_EPOCH_MS) {
        // STEP 06.01: 範圍外 → Rejected，state 不更新但寫 ACK applied:false
        uint64_t device_now = time_sync_current_epoch_ms(state, now_millis);
        write_ack(/*applied=*/false, rtc_present, device_now, req_id,
                  ack_out, ack_buf_size, ack_len_out);
        return TimeSyncResult::Rejected;
    }

    // STEP 07: 範圍內 → Applied
    //          offset = epoch_ms - now_millis；之後 current_epoch_ms 由 offset + now_millis 取
    state->synced = true;
    state->epoch_ms_offset = epoch_ms - now_millis;
    state->tz_offset_min = tz_offset_min;

    // STEP 08: 寫 ACK（spec §2.2 device_now_ms = 寫入時鐘後的當下 epoch）
    uint64_t device_now = time_sync_current_epoch_ms(state, now_millis);
    write_ack(/*applied=*/true, rtc_present, device_now, req_id,
              ack_out, ack_buf_size, ack_len_out);
    return TimeSyncResult::Applied;
}

}  // namespace ems
