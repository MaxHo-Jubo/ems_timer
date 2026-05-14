// EMS DoseSync Pro — Phase E: 持久化邏輯層 GREEN 實作
//
// 對應規格：見 ems_storage_logic.h
// 設計取捨：見 plan ticklish-exploring-dragonfly.md
//
// 內部 state：
//   - s_state.cases[]：所有案件 metadata（時序排列，最舊在 index 0）
//   - s_state.case_count：陣列實際長度
//   - s_state.next_seq：下一個 case 拿到的 seq number（單調遞增、OHCA + Training 共用）
//     → 同類別 id 不連續（被另一類別 case 拿走 seq 會跳號），但全域單調
//     → 方便跨類別排序與未來 BLE 增量同步（依 seq 取「last seen 之後新增的 case」）
//   - storage_init() 會清空再從 backend 重建

#include "ems_storage_logic.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ems_case_summary.h"

// 邏輯層 native test 與 ESP32 共用同檔；native 無 Arduino.h 不能用 Serial。
// 包成 macro 後 native 編譯時化為 no-op，ESP32 編譯走 Serial.printf。
#ifdef ARDUINO
#include <Arduino.h>
#define EMS_STORAGE_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
#define EMS_STORAGE_LOG(fmt, ...) ((void)0)
#endif

namespace ems {

// ============================================================
//  內部 state
// ============================================================

namespace {

constexpr uint16_t kTotalCapacity =
    EMS_STORAGE_OHCA_CAP + EMS_STORAGE_TRAINING_CAP;

struct StorageState {
    uint32_t    next_seq;
    uint16_t    case_count;
    case_meta_t cases[kTotalCapacity];
};

StorageState s_state{};

// ----- Little-endian I/O helpers -----

inline void put_u16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void put_u32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

inline void put_u64_le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

inline uint16_t get_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

inline uint32_t get_u32_le(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

inline uint64_t get_u64_le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (uint64_t)p[i] << (i * 8);
    }
    return v;
}

// ----- 是否為 .bin 檔（rebuild 時掃描用） -----

constexpr const char* kBinSuffix    = ".bin";
constexpr size_t      kBinSuffixLen = 4;  // strlen(".bin")

bool name_is_bin(const char* name, char* out_id, size_t id_max) {
    // STEP 01: 形式驗證 "%010u.bin"
    if (!name) {
        return false;
    }
    size_t n = strlen(name);
    if (n < (EMS_STORAGE_ID_LEN - 1) + kBinSuffixLen) {
        return false;
    }
    // STEP 02: 結尾必須是 ".bin"
    if (strcmp(name + n - kBinSuffixLen, kBinSuffix) != 0) {
        return false;
    }
    // STEP 03: 抽出 id（".bin" 前的部分）
    size_t id_len = n - kBinSuffixLen;
    if (id_len + 1 > id_max) {
        return false;
    }
    // STEP 04: 必須全部是數字
    for (size_t i = 0; i < id_len; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    memcpy(out_id, name, id_len);
    out_id[id_len] = '\0';
    return true;
}

// ----- 從 events.bin 載回事件、用 caseSummary_build 聚合 metadata -----
//
// 契約：out_meta->start_ms / end_ms 強制設 0（bin 無 epoch 資訊）。
// caller 必須能處理 0 = 時戳不可用（drawOhcaSummary historySummaryMode 分支已涵蓋）。
// Phase 3 DS3231 上機後若改回真 epoch，這個函式仍須保留 0 fallback 給 corrupt 路徑。

bool fill_meta_from_bin(IStorageBackend* be,
                        storage_case_type_t type,
                        const char*  id,
                        case_meta_t* out_meta) {
    // STEP 01: 構造完整路徑
    char path[EMS_STORAGE_PATH_MAX];
    storage_build_event_path(path, sizeof(path), type, id);

    // STEP 02: 讀檔
    uint8_t buf[EMS_STORAGE_MAX_BIN_SIZE];
    size_t blen = 0;
    if (!be->read_file(be->ctx, path, buf, sizeof(buf), &blen)) {
        return false;
    }

    // STEP 03: 反序列化
    ems_event_t events[EMS_STORAGE_MAX_EVENTS];
    uint16_t count = 0;
    if (!storage_bin_deserialize(buf, blen, events,
                                 EMS_STORAGE_MAX_EVENTS, &count)) {
        return false;
    }

    // STEP 04: 聚合 summary 填欄位
    ohca_case_summary_t s;
    caseSummary_build(&s, events, count, /*start*/ 0, /*end*/ 0);

    strncpy(out_meta->id, id, sizeof(out_meta->id) - 1);
    out_meta->id[sizeof(out_meta->id) - 1] = '\0';
    out_meta->type               = type;
    out_meta->start_ms           = 0;  // 從 bin 無法還原；rebuild 路徑可接受遺失
    out_meta->end_ms             = 0;
    out_meta->epi_local          = s.epi_local;
    out_meta->epi_pre_handover   = s.epi_pre_handover;
    out_meta->epi_pure_supp      = s.epi_pure_supp;
    out_meta->shock_local        = s.shock_local;
    out_meta->shock_pre_handover = s.shock_pre_handover;
    out_meta->shock_pure_supp    = s.shock_pure_supp;
    out_meta->amio               = s.amio_total;
    out_meta->event_count        = count;
    out_meta->bin_v              = EMS_STORAGE_BIN_VERSION;
    return true;
}

// ----- Persist index.json（atomic：寫 tmp + rename） -----

bool persist_index(IStorageBackend* be) {
    static char json[EMS_STORAGE_INDEX_BUFFER_SIZE];
    size_t jlen = 0;

    // STEP 01: 序列化
    if (!storage_index_serialize(s_state.cases, s_state.case_count,
                                 s_state.next_seq,
                                 json, sizeof(json), &jlen)) {
        return false;
    }

    // STEP 02: 寫 tmp
    if (!be->write_file(be->ctx, "/cases/index.json.tmp",
                        (const uint8_t*)json, jlen)) {
        return false;
    }

    // STEP 03: rename = atomic commit
    if (!be->rename_file(be->ctx, "/cases/index.json.tmp",
                         "/cases/index.json")) {
        be->delete_file(be->ctx, "/cases/index.json.tmp");
        return false;
    }
    return true;
}

// ----- FIFO 容量強制 -----

uint16_t cap_for_type(storage_case_type_t type) {
    return (type == EMS_CASE_TYPE_OHCA)
         ? EMS_STORAGE_OHCA_CAP
         : EMS_STORAGE_TRAINING_CAP;
}

void enforce_fifo_cap(IStorageBackend* be, storage_case_type_t type) {
    // STEP 01: 計算當前 type 的 case 數
    uint16_t cap = cap_for_type(type);
    uint16_t typed = 0;
    for (uint16_t i = 0; i < s_state.case_count; ++i) {
        if (s_state.cases[i].type == type) {
            typed++;
        }
    }

    // STEP 02: 擠到 < cap（pre-evict 語意：騰位給即將寫入的新 case）
    //   原本 `> cap` 對應「先寫再擠」的 post-write 用法；現改為 pre-write 呼叫，
    //   需要把該類別擠到 cap-1 才能讓新案件寫入後剛好回到 cap。
    while (typed >= cap) {
        // STEP 02.01: 找最舊 index
        uint16_t oldest = UINT16_MAX;
        for (uint16_t i = 0; i < s_state.case_count; ++i) {
            if (s_state.cases[i].type == type) {
                oldest = i;
                break;
            }
        }
        if (oldest == UINT16_MAX) {
            break;
        }

        // STEP 02.02: 刪 events.bin
        //   fs 層的 delete_file 永遠 return true（見 ems_storage_fs.cpp 註解），
        //   失敗 trace 已在 fs 層印 path 級別 warn；此處印 logic 層 audit log，
        //   讓使用者看 log 能 trace「為什麼這筆案件突然消失」= FIFO 淘汰。
        char path[EMS_STORAGE_PATH_MAX];
        storage_build_event_path(path, sizeof(path), type,
                                 s_state.cases[oldest].id);
        EMS_STORAGE_LOG("[STORAGE] FIFO evict type=%d id=%s\n",
                        (int)type, s_state.cases[oldest].id);
        be->delete_file(be->ctx, path);

        // STEP 02.03: 從陣列移除
        for (uint16_t i = oldest; i + 1 < s_state.case_count; ++i) {
            s_state.cases[i] = s_state.cases[i + 1];
        }
        s_state.case_count--;
        typed--;
    }
}

// ----- 從現有的 case_meta_t 陣列 idx 找指定 id 與 type -----

int32_t find_case_idx(storage_case_type_t type, const char* id) {
    if (!id) {
        return -1;
    }
    for (uint16_t i = 0; i < s_state.case_count; ++i) {
        if (s_state.cases[i].type == type
            && strncmp(s_state.cases[i].id, id, EMS_STORAGE_ID_LEN) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

}  // anonymous namespace

// ============================================================
//  CRC32 IEEE 802.3
// ============================================================

uint32_t storage_crc32_update(uint32_t crc,
                              const uint8_t* buf,
                              size_t len) {
    // STEP 01: 空 buffer 直接回傳當前累加值
    if (!buf || len == 0) {
        return crc;
    }
    // STEP 02: 位元反轉（~ NOT）為 internal accumulator（標準 IEEE 802.3 慣例）
    uint32_t c = ~crc;
    // STEP 03: 逐 byte 處理 8 次 shift + xor with reflected polynomial
    for (size_t i = 0; i < len; ++i) {
        c ^= buf[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = (uint32_t)(-(int32_t)(c & 1));
            c = (c >> 1) ^ (0xEDB88320u & mask);
        }
    }
    // STEP 04: 反轉回 external 表示
    return ~c;
}

uint32_t storage_crc32(const uint8_t* buf, size_t len) {
    // STEP 01: 空 buffer 直接 0（test B2 約定）
    if (!buf || len == 0) {
        return 0;
    }
    return storage_crc32_update(0, buf, len);
}

// ============================================================
//  events.bin 序列化 / 反序列化
// ============================================================

size_t storage_bin_size(uint16_t event_count) {
    return EMS_STORAGE_BIN_HEADER_SIZE
         + (size_t)event_count * EMS_STORAGE_WIRE_EVENT_SIZE
         + EMS_STORAGE_BIN_FOOTER_SIZE;
}

bool storage_bin_serialize(const ems_event_t* events,
                           uint16_t           count,
                           uint8_t*           out_buf,
                           size_t             buf_max,
                           size_t*            out_len) {
    // STEP 01: 基本參數檢查
    if (!out_buf) {
        return false;
    }
    if (count > EMS_STORAGE_MAX_EVENTS) {
        return false;
    }
    size_t need = storage_bin_size(count);
    if (need > buf_max) {
        return false;
    }
    if (count > 0 && !events) {
        return false;
    }

    // STEP 02: header
    put_u32_le(out_buf + 0, EMS_STORAGE_BIN_MAGIC);
    put_u16_le(out_buf + 4, EMS_STORAGE_BIN_VERSION);
    put_u16_le(out_buf + 6, count);

    // STEP 03: events（fixed-width 24 bytes 含兩塊 reserved padding）
    //   layout：event_id(4) + type(1) + count(1) + actual_time_null(1)
    //         + reserved(1，p[7]) + ts(8) + elapsed(4) + reserved2(4) = 24
    //   升版策略：先用 p[7] 1 byte 加 enum/flag，再用 p+20 4 bytes 加 field
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t* p = out_buf
                   + EMS_STORAGE_BIN_HEADER_SIZE
                   + (size_t)i * EMS_STORAGE_WIRE_EVENT_SIZE;
        put_u32_le(p + 0,  events[i].event_id);
        p[4] = (uint8_t)events[i].type;
        p[5] = events[i].count;
        p[6] = events[i].actual_time_null ? 1 : 0;
        p[7] = 0;  // reserved (1 byte，升版用)
        put_u64_le(p + 8,  events[i].timestamp_ms);
        put_u32_le(p + 16, events[i].elapsed_ms);
        put_u32_le(p + 20, 0);  // reserved2 (4 bytes，升版用)
    }

    // STEP 04: CRC32 over header + events
    size_t crc_offset = EMS_STORAGE_BIN_HEADER_SIZE
                      + (size_t)count * EMS_STORAGE_WIRE_EVENT_SIZE;
    uint32_t crc = storage_crc32(out_buf, crc_offset);
    put_u32_le(out_buf + crc_offset, crc);

    if (out_len) {
        *out_len = need;
    }
    return true;
}

bool storage_bin_deserialize(const uint8_t* buf,
                             size_t         len,
                             ems_event_t*   out_events,
                             uint16_t       out_max,
                             uint16_t*      out_count) {
    // STEP 01: 預設失敗回傳 count=0（test A4 要求不污染 output）
    if (out_count) {
        *out_count = 0;
    }
    if (!buf) {
        return false;
    }
    if (len < EMS_STORAGE_BIN_HEADER_SIZE + EMS_STORAGE_BIN_FOOTER_SIZE) {
        return false;
    }

    // STEP 02: magic / version 校驗
    if (get_u32_le(buf + 0) != EMS_STORAGE_BIN_MAGIC) {
        return false;
    }
    if (get_u16_le(buf + 4) != EMS_STORAGE_BIN_VERSION) {
        return false;
    }

    // STEP 03: count + 長度校驗
    uint16_t count = get_u16_le(buf + 6);
    if (count > out_max) {
        return false;
    }
    if (count > EMS_STORAGE_MAX_EVENTS) {
        return false;
    }
    if (storage_bin_size(count) != len) {
        return false;
    }

    // STEP 04: CRC32 校驗
    size_t crc_offset = EMS_STORAGE_BIN_HEADER_SIZE
                      + (size_t)count * EMS_STORAGE_WIRE_EVENT_SIZE;
    uint32_t expected = get_u32_le(buf + crc_offset);
    if (expected != storage_crc32(buf, crc_offset)) {
        return false;
    }

    // STEP 05: 反序列化 events（CRC 通過才寫 output）
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* p = buf
                         + EMS_STORAGE_BIN_HEADER_SIZE
                         + (size_t)i * EMS_STORAGE_WIRE_EVENT_SIZE;
        out_events[i].event_id         = get_u32_le(p + 0);
        out_events[i].type             = (ems_event_type_t)p[4];
        out_events[i].count            = p[5];
        out_events[i].actual_time_null = (p[6] != 0);
        out_events[i].timestamp_ms     = get_u64_le(p + 8);
        out_events[i].elapsed_ms       = get_u32_le(p + 16);
    }

    if (out_count) {
        *out_count = count;
    }
    return true;
}

// ============================================================
//  index.json 序列化 / 反序列化（ArduinoJson v7）
// ============================================================

bool storage_index_serialize(const case_meta_t* cases,
                             uint16_t           count,
                             uint32_t           next_seq,
                             char*              out_json,
                             size_t             json_max,
                             size_t*            out_len) {
    // STEP 01: 參數檢查
    if (!out_json || json_max == 0) {
        return false;
    }
    if (count > 0 && !cases) {
        return false;
    }

    // STEP 02: 用 ArduinoJson 7 建 document
    JsonDocument doc;
    doc["v"] = EMS_STORAGE_INDEX_VERSION;
    doc["next_seq"] = next_seq;
    JsonArray arr = doc["cases"].to<JsonArray>();

    for (uint16_t i = 0; i < count; ++i) {
        const case_meta_t& m = cases[i];
        JsonObject c = arr.add<JsonObject>();
        c["id"]                 = m.id;
        c["type"]               = case_type_to_str(m.type);
        c["start_ms"]           = m.start_ms;
        c["end_ms"]             = m.end_ms;
        c["epi_local"]          = m.epi_local;
        c["epi_pre_handover"]   = m.epi_pre_handover;
        c["epi_pure_supp"]      = m.epi_pure_supp;
        c["shock_local"]        = m.shock_local;
        c["shock_pre_handover"] = m.shock_pre_handover;
        c["shock_pure_supp"]    = m.shock_pure_supp;
        c["amio"]               = m.amio;
        c["event_count"]        = m.event_count;
        c["bin_v"]              = m.bin_v;
    }

    // STEP 03: 序列化到 buffer
    size_t n = serializeJson(doc, out_json, json_max);
    if (n == 0 || n >= json_max) {
        return false;
    }
    if (out_len) {
        *out_len = n;
    }
    return true;
}

bool storage_index_parse(const char*  json,
                         size_t       len,
                         case_meta_t* out_cases,
                         uint16_t     cases_max,
                         uint16_t*    out_count,
                         uint32_t*    out_next_seq) {
    // STEP 01: 預設失敗回傳 0
    if (out_count) {
        *out_count = 0;
    }
    if (out_next_seq) {
        *out_next_seq = 0;
    }
    if (!json || len == 0) {
        return false;
    }

    // STEP 02: ArduinoJson 解析
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        return false;
    }
    // 必須是 object：scalar / array / 純字串雖可 deserialize 成功（scalar/string）
    // 但結構不對；C4 test 確認這條 guard 會拒絕。
    if (!doc.is<JsonObjectConst>()) {
        return false;
    }

    // STEP 03: 取出 next_seq（必填欄位）
    JsonVariantConst ns_v = doc["next_seq"];
    if (ns_v.isNull()) {
        return false;
    }
    uint32_t ns = ns_v.as<uint32_t>();
    if (out_next_seq) {
        *out_next_seq = ns;
    }

    // STEP 04: 取出 cases 陣列（容忍缺失，視為空）
    JsonArrayConst arr = doc["cases"];
    uint16_t i = 0;
    for (JsonObjectConst c : arr) {
        if (i >= cases_max) {
            break;
        }
        case_meta_t& m = out_cases[i];
        memset(&m, 0, sizeof(m));

        const char* id = c["id"] | "";
        strncpy(m.id, id, sizeof(m.id) - 1);
        m.id[sizeof(m.id) - 1] = '\0';

        const char* tstr = c["type"] | "ohca";
        m.type = case_type_from_str(tstr);

        m.start_ms           = c["start_ms"]           | (uint64_t)0;
        m.end_ms             = c["end_ms"]             | (uint64_t)0;
        m.epi_local          = c["epi_local"]          | (uint16_t)0;
        m.epi_pre_handover   = c["epi_pre_handover"]   | (uint16_t)0;
        m.epi_pure_supp      = c["epi_pure_supp"]      | (uint16_t)0;
        m.shock_local        = c["shock_local"]        | (uint16_t)0;
        m.shock_pre_handover = c["shock_pre_handover"] | (uint16_t)0;
        m.shock_pure_supp    = c["shock_pure_supp"]    | (uint16_t)0;
        m.amio               = c["amio"]               | (uint16_t)0;
        m.event_count        = c["event_count"]        | (uint16_t)0;
        m.bin_v              = c["bin_v"]              | (uint8_t)EMS_STORAGE_BIN_VERSION;
        i++;
    }
    if (out_count) {
        *out_count = i;
    }
    return true;
}

// ============================================================
//  路徑 helpers
// ============================================================

const char* case_type_to_str(storage_case_type_t type) {
    switch (type) {
        case EMS_CASE_TYPE_OHCA:     return "ohca";
        case EMS_CASE_TYPE_TRAINING: return "training";
    }
    return "ohca";  // unknown enum value fallback（外部寫入錯誤值的最後一道防線）
}

// 字串 → case type。**已知缺陷**：未知字串 silent fallback OHCA（避免 index.json
// 升版時舊韌體完全讀不進）。代價是把 corrupt 字串當合法 OHCA 列出。
// 對應 spec 取捨：「能讀到部分資料」優於「整個 index 解析失敗」。
// 補測：test_main.cpp 應加 case verifying this behavior（B3 follow-up）。
storage_case_type_t case_type_from_str(const char* s) {
    if (s && strcmp(s, "training") == 0) {
        return EMS_CASE_TYPE_TRAINING;
    }
    return EMS_CASE_TYPE_OHCA;
}

const char* storage_dir_path(storage_case_type_t type) {
    switch (type) {
        case EMS_CASE_TYPE_OHCA:     return "/cases/ohca";
        case EMS_CASE_TYPE_TRAINING: return "/cases/training";
    }
    return "/cases/ohca";
}

void storage_format_id(uint32_t seq, char* out, size_t out_max) {
    if (!out || out_max < EMS_STORAGE_ID_LEN) {
        return;
    }
    snprintf(out, out_max, "%010u", (unsigned)seq);
}

void storage_build_event_path(char*               out,
                              size_t              out_max,
                              storage_case_type_t type,
                              const char*         id) {
    if (!out || !id || out_max == 0) {
        if (out && out_max > 0) {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, out_max, "%s/%s.bin", storage_dir_path(type), id);
}

// ============================================================
//  高階操作
// ============================================================

bool storage_init(IStorageBackend* be) {
    // STEP 01: 重置 state
    s_state.next_seq = 1;
    s_state.case_count = 0;

    if (!be || !be->read_file) {
        return false;
    }

    // STEP 02: 清理 .tmp 殘檔（power-loss recovery 第一道防線）
    if (be->exists(be->ctx, "/cases/index.json.tmp")) {
        be->delete_file(be->ctx, "/cases/index.json.tmp");
    }

    // STEP 03: 嘗試載入 index.json
    bool index_loaded = false;
    if (be->exists(be->ctx, "/cases/index.json")) {
        static char json[EMS_STORAGE_INDEX_BUFFER_SIZE];
        size_t jlen = 0;
        if (be->read_file(be->ctx, "/cases/index.json",
                          (uint8_t*)json, sizeof(json) - 1, &jlen)) {
            json[jlen] = '\0';
            uint16_t count = 0;
            uint32_t next_seq = 0;
            if (storage_index_parse(json, jlen,
                                    s_state.cases, kTotalCapacity,
                                    &count, &next_seq)) {
                s_state.case_count = count;
                s_state.next_seq   = (next_seq > 0) ? next_seq : 1;
                index_loaded = true;
            }
        }
    }

    // STEP 04: 列出實際檔案
    //   放 static (BSS) 而非函式 local；OHCA 1600B + Training 640B 若放 local 會吃
    //   28% main task 8KB stack（TFT/BLE callback 也共用，留不得）。
    //   storage_init 只在 setup 跑一次，BSS 永久佔用 2.24KB 換來 stack peak 安全。
    static char ohca_names[EMS_STORAGE_OHCA_CAP][EMS_STORAGE_NAME_MAX];
    size_t n_ohca = be->list_dir(be->ctx,
                                 storage_dir_path(EMS_CASE_TYPE_OHCA),
                                 ohca_names, EMS_STORAGE_OHCA_CAP);
    static char train_names[EMS_STORAGE_TRAINING_CAP][EMS_STORAGE_NAME_MAX];
    size_t n_train = be->list_dir(be->ctx,
                                  storage_dir_path(EMS_CASE_TYPE_TRAINING),
                                  train_names, EMS_STORAGE_TRAINING_CAP);

    // STEP 05: 依 index_loaded 走 rebuild（無 index）或雙向 sync（有 index）兩條路徑
    if (!index_loaded) {
        // STEP 05.01: 沒 index → 從檔案 rebuild
        for (size_t i = 0; i < n_ohca; ++i) {
            char id[EMS_STORAGE_ID_LEN];
            if (!name_is_bin(ohca_names[i], id, sizeof(id))) {
                continue;
            }
            if (s_state.case_count >= kTotalCapacity) {
                break;
            }
            if (fill_meta_from_bin(be, EMS_CASE_TYPE_OHCA, id,
                                   &s_state.cases[s_state.case_count])) {
                uint32_t seq = (uint32_t)atol(id);
                if (seq + 1 > s_state.next_seq) {
                    s_state.next_seq = seq + 1;
                }
                s_state.case_count++;
            } else {
                // CRC mismatch / magic 錯 / version 不支援 → silent skip 會讓使用者看不到資料消失原因
                // 損壞檔暫留原處（後續可考慮搬到 /cases/corrupt/，列 todo 不在此 batch 處理）
                EMS_STORAGE_LOG("[STORAGE] WARN rebuild skip corrupt OHCA id=%s\n", id);
            }
        }
        for (size_t i = 0; i < n_train; ++i) {
            char id[EMS_STORAGE_ID_LEN];
            if (!name_is_bin(train_names[i], id, sizeof(id))) {
                continue;
            }
            if (s_state.case_count >= kTotalCapacity) {
                break;
            }
            if (fill_meta_from_bin(be, EMS_CASE_TYPE_TRAINING, id,
                                   &s_state.cases[s_state.case_count])) {
                uint32_t seq = (uint32_t)atol(id);
                if (seq + 1 > s_state.next_seq) {
                    s_state.next_seq = seq + 1;
                }
                s_state.case_count++;
            } else {
                EMS_STORAGE_LOG("[STORAGE] WARN rebuild skip corrupt Training id=%s\n", id);
            }
        }
    } else {
        // STEP 05.02: 有 index → 雙向 sync

        // STEP 05.02.01: 把 index 內檔案不存在的 entry 刪掉
        uint16_t write = 0;
        for (uint16_t read = 0; read < s_state.case_count; ++read) {
            char path[EMS_STORAGE_PATH_MAX];
            storage_build_event_path(path, sizeof(path),
                                     s_state.cases[read].type,
                                     s_state.cases[read].id);
            if (be->exists(be->ctx, path)) {
                if (write != read) {
                    s_state.cases[write] = s_state.cases[read];
                }
                write++;
            }
        }
        s_state.case_count = write;

        // STEP 05.02.02: 把檔案存在但 index 沒記錄的孤兒刪掉
        auto purge_orphans = [&](storage_case_type_t type,
                                  char names[][EMS_STORAGE_NAME_MAX],
                                  size_t n) {
            for (size_t i = 0; i < n; ++i) {
                char id[EMS_STORAGE_ID_LEN];
                if (!name_is_bin(names[i], id, sizeof(id))) {
                    continue;
                }
                if (find_case_idx(type, id) < 0) {
                    char path[EMS_STORAGE_PATH_MAX];
                    storage_build_event_path(path, sizeof(path), type, id);
                    be->delete_file(be->ctx, path);
                }
            }
        };
        purge_orphans(EMS_CASE_TYPE_OHCA,     ohca_names,  n_ohca);
        purge_orphans(EMS_CASE_TYPE_TRAINING, train_names, n_train);
    }

    // STEP 06: 持久化 clean state（覆寫任何不一致的 index.json）
    //   persist 失敗時 in-RAM s_state 與 disk index.json 會不一致：下次 reboot
    //   會走 rebuild 路徑（或讀到舊 index）導致 case 數對不上。先 warn，下次
    //   storage_save_case 成功時會順帶覆寫修復。
    if (!persist_index(be)) {
        EMS_STORAGE_LOG("[STORAGE] WARN storage_init persist_index failed "
                        "(RAM != disk; expect next save to repair)\n");
    }

    return true;
}

bool storage_save_case(IStorageBackend*    be,
                       storage_case_type_t type,
                       const ems_event_t*  events,
                       uint16_t            count,
                       uint64_t            case_start_ms,
                       uint64_t            case_end_ms) {
    // STEP 01: 參數檢查
    if (!be || !be->write_file) {
        return false;
    }
    if (count > 0 && !events) {
        return false;
    }
    if (count > EMS_STORAGE_MAX_EVENTS) {
        return false;
    }
    // 不檢查 case_count >= kTotalCapacity：pre-evict 後同類別必 < cap，
    // 而 cap_for_type(OHCA) + cap_for_type(TRAINING) == kTotalCapacity，
    // 故 case_count + 1 <= kTotalCapacity 由 cap 算術保證。

    // STEP 01.5: pre-evict 同類別最舊 case（救護現場：FIFO 不能擋住新案件）
    //   原本 STEP 07 post-write 呼叫會在總容量 guard 後失效；改成 pre-write 確保
    //   即使跨類別 case_count 已滿，新案件仍能透過擠掉同類別最舊一筆騰位寫入。
    enforce_fifo_cap(be, type);

    // STEP 02: 配發 seq 與 id
    uint32_t seq = s_state.next_seq;
    char id[EMS_STORAGE_ID_LEN];
    storage_format_id(seq, id, sizeof(id));

    // STEP 03: 序列化 events
    uint8_t bin_buf[EMS_STORAGE_MAX_BIN_SIZE];
    size_t bin_len = 0;
    if (!storage_bin_serialize(events, count, bin_buf,
                               sizeof(bin_buf), &bin_len)) {
        return false;
    }

    // STEP 04: 寫 events.bin
    char path[EMS_STORAGE_PATH_MAX];
    storage_build_event_path(path, sizeof(path), type, id);
    if (!be->write_file(be->ctx, path, bin_buf, bin_len)) {
        return false;
    }

    // STEP 05: 寫成功才 commit state
    s_state.next_seq = seq + 1;

    // STEP 06: 聚合 summary 填 metadata
    ohca_case_summary_t summary;
    caseSummary_build(&summary, events, count, case_start_ms, case_end_ms);

    case_meta_t& m = s_state.cases[s_state.case_count];
    memset(&m, 0, sizeof(m));
    strncpy(m.id, id, sizeof(m.id) - 1);
    m.id[sizeof(m.id) - 1] = '\0';
    m.type               = type;
    m.start_ms           = case_start_ms;
    m.end_ms             = case_end_ms;
    m.epi_local          = summary.epi_local;
    m.epi_pre_handover   = summary.epi_pre_handover;
    m.epi_pure_supp      = summary.epi_pure_supp;
    m.shock_local        = summary.shock_local;
    m.shock_pre_handover = summary.shock_pre_handover;
    m.shock_pure_supp    = summary.shock_pure_supp;
    m.amio               = summary.amio_total;
    m.event_count        = count;
    m.bin_v              = EMS_STORAGE_BIN_VERSION;
    s_state.case_count++;

    // STEP 07: 持久化 index.json
    //   FIFO 容量強制已在 STEP 01.5 pre-evict 處理，此處無需再呼叫。
    persist_index(be);

    return true;
}

// 純讀 in-memory s_state，不觸 backend。保留 be 參數是為 API 對稱性
// （其他 high-level storage_* 都吃 backend）+ 未來若加 lazy-load 留接口。
uint16_t storage_list(IStorageBackend*    /*be*/,
                      storage_case_type_t type,
                      case_meta_t*        out,
                      uint16_t            max) {
    if (!out || max == 0) {
        return 0;
    }
    // STEP 01: 逆向遍歷（newest first）
    uint16_t n = 0;
    for (int32_t i = (int32_t)s_state.case_count - 1; i >= 0 && n < max; --i) {
        if (s_state.cases[i].type == type) {
            out[n++] = s_state.cases[i];
        }
    }
    return n;
}

bool storage_load_events(IStorageBackend*    be,
                         storage_case_type_t type,
                         const char*         id,
                         ems_event_t*        out_events,
                         uint16_t            out_max,
                         uint16_t*           out_count) {
    if (out_count) {
        *out_count = 0;
    }
    if (!be || !be->read_file || !id) {
        return false;
    }

    // STEP 01: 在 state 找對應 case；不存在直接 false
    if (find_case_idx(type, id) < 0) {
        return false;
    }

    // STEP 02: 讀 events.bin
    char path[EMS_STORAGE_PATH_MAX];
    storage_build_event_path(path, sizeof(path), type, id);
    uint8_t buf[EMS_STORAGE_MAX_BIN_SIZE];
    size_t blen = 0;
    if (!be->read_file(be->ctx, path, buf, sizeof(buf), &blen)) {
        return false;
    }

    // STEP 03: 反序列化
    return storage_bin_deserialize(buf, blen, out_events,
                                   out_max, out_count);
}

bool storage_delete(IStorageBackend*    be,
                    storage_case_type_t type,
                    const char*         id) {
    if (!be || !id) {
        return false;
    }
    int32_t idx = find_case_idx(type, id);
    if (idx < 0) {
        return false;
    }

    // STEP 01: 刪檔
    char path[EMS_STORAGE_PATH_MAX];
    storage_build_event_path(path, sizeof(path), type, id);
    be->delete_file(be->ctx, path);

    // STEP 02: 從 state 移除
    for (uint16_t i = (uint16_t)idx; i + 1 < s_state.case_count; ++i) {
        s_state.cases[i] = s_state.cases[i + 1];
    }
    s_state.case_count--;

    // STEP 03: 持久化
    persist_index(be);
    return true;
}

bool storage_format(IStorageBackend* be) {
    if (!be) {
        return false;
    }
    // STEP 01: 刪所有檔案（OHCA + Training 走相同流程，抽 lambda 避免目錄字串散落）
    auto purge_dir = [&](storage_case_type_t type) {
        const char* dir = storage_dir_path(type);
        char names[kTotalCapacity][EMS_STORAGE_NAME_MAX];
        size_t n = be->list_dir(be->ctx, dir, names, kTotalCapacity);
        for (size_t i = 0; i < n; ++i) {
            char path[EMS_STORAGE_PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
            be->delete_file(be->ctx, path);
        }
    };
    purge_dir(EMS_CASE_TYPE_OHCA);
    purge_dir(EMS_CASE_TYPE_TRAINING);
    be->delete_file(be->ctx, "/cases/index.json");
    be->delete_file(be->ctx, "/cases/index.json.tmp");

    // STEP 02: 重置 state
    s_state.next_seq = 1;
    s_state.case_count = 0;
    return true;
}

}  // namespace ems
