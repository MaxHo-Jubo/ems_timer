// EMS DoseSync Pro — Phase E: 持久化邏輯層（不依賴 LittleFS）
//
// 對應規格：
//   - pm-dev-spec §四 Phase E（LittleFS partition + 50 OHCA / 20 Training FIFO 覆蓋）
//   - plan: ticklish-exploring-dragonfly.md（Hybrid storage：JSON index + binary events.bin）
//
// 為何分層：
//   - logic.{h,cpp} 只處理「位元組陣列 ↔ 結構」與「FIFO 規則」，純函式 + 可注入 backend
//   - fs.{h,cpp} 才碰 LittleFS（Arduino runtime，無法在 native test 跑）
//   - native test 用 InMemoryBackend 注入 → 跑 Group A~F 25+ cases
//
// 彈性設計（PM 講「資料設計要有彈性」）：
//   - events.bin 每筆 event 預留 4 bytes reserved（升版加欄位不用動 wire format）
//   - index.json 用 ArduinoJson 動態 parse，未知欄位忽略不報錯（C3 test 驗證）
//   - bin_v / index v 兩個獨立 version → 結構分離升版
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ems_supp_model.h"

// 與 ems_supp_model.h 同 namespace（用 ems::ems_event_t 才能對齊）
namespace ems {

// ============================================================
//  常數
// ============================================================

/** OHCA 案件最大保留數（FIFO 覆蓋） */
#define EMS_STORAGE_OHCA_CAP        50

/** Training 案件最大保留數（Phase D 用，Phase E 先預留） */
#define EMS_STORAGE_TRAINING_CAP    20

/** 單案件最大事件數（對齊 main.cpp MAX_EVENTS） */
#define EMS_STORAGE_MAX_EVENTS      50

/** events.bin magic：ASCII 'EMSE' little-endian */
#define EMS_STORAGE_BIN_MAGIC       0x45534D45u

/** events.bin schema 版本 */
#define EMS_STORAGE_BIN_VERSION     1

/** index.json schema 版本 */
#define EMS_STORAGE_INDEX_VERSION   1

/** 單筆 event 在 wire 上的固定寬度（含 reserved padding，跨 compiler 穩定） */
#define EMS_STORAGE_WIRE_EVENT_SIZE 24

/** events.bin 固定 header / footer 大小 */
#define EMS_STORAGE_BIN_HEADER_SIZE 8     // magic(4) + version(2) + count(2)
#define EMS_STORAGE_BIN_FOOTER_SIZE 4     // crc32

/** events.bin 最大 wire 長度（50 events 滿載） */
#define EMS_STORAGE_MAX_BIN_SIZE                              \
    (EMS_STORAGE_BIN_HEADER_SIZE                              \
     + EMS_STORAGE_MAX_EVENTS * EMS_STORAGE_WIRE_EVENT_SIZE   \
     + EMS_STORAGE_BIN_FOOTER_SIZE)

/** index.json buffer 預估大小（OHCA 50 + Training 20 = 70 cases × ~160B + 包裝 ~64B） */
#define EMS_STORAGE_INDEX_BUFFER_SIZE 12288

/** Case id 字串長度（10 位 zero-padded + NUL） */
#define EMS_STORAGE_ID_LEN          11

/** Case 路徑最大長度（"/cases/ohca/0000000003.bin" + NUL） */
#define EMS_STORAGE_PATH_MAX        48

/** 目錄列舉時單檔名最大長度（"0000000003.bin" + NUL） */
#define EMS_STORAGE_NAME_MAX        32

// ============================================================
//  Case type
// ============================================================

typedef enum {
    EMS_CASE_TYPE_OHCA     = 0,
    EMS_CASE_TYPE_TRAINING = 1,
} storage_case_type_t;

/** Case type → 字串（index.json 與 log 共用，避免 raw string 散落） */
const char* case_type_to_str(storage_case_type_t type);

/** 字串 → case type（找不到回預設 OHCA，配合 index.json 容錯讀取） */
storage_case_type_t case_type_from_str(const char* s);

// ============================================================
//  Case metadata（index.json 內每筆 entry）
// ============================================================

typedef struct {
    char     id[EMS_STORAGE_ID_LEN];        // "0000000003" + NUL
    storage_case_type_t type;
    uint64_t start_ms;                      // 案件開始 epoch ms（0 = 無 RTC 對時）
    uint64_t end_ms;                        // 案件結束 epoch ms
    uint16_t epi_local;
    uint16_t epi_pre_handover;
    uint16_t epi_pure_supp;
    uint16_t shock_local;
    uint16_t shock_pre_handover;
    uint16_t shock_pure_supp;
    uint16_t amio;
    uint16_t event_count;
    uint8_t  bin_v;                         // events.bin schema 版本
} case_meta_t;

/** EPI 總數聚合（對齊 ohca_case_summary_t::epi_total 定義：local + pre_handover + pure_supp） */
inline uint16_t case_meta_epi_total(const case_meta_t& m) {
    return (uint16_t)(m.epi_local + m.epi_pre_handover + m.epi_pure_supp);
}

/** 電擊總數聚合（對齊 ohca_case_summary_t::shock_total） */
inline uint16_t case_meta_shock_total(const case_meta_t& m) {
    return (uint16_t)(m.shock_local + m.shock_pre_handover + m.shock_pure_supp);
}

// ============================================================
//  Storage backend interface（依賴注入，分離 LittleFS）
// ============================================================
//
// 所有路徑為絕對路徑（"/cases/..."）。
// 所有函式回傳 true 為成功。
// list_dir 把 dir 下的檔名（不含路徑）寫進 out_names，回傳實際筆數。

typedef struct IStorageBackend {
    void* ctx;

    bool   (*read_file)  (void* ctx, const char* path,
                          uint8_t* out_buf, size_t buf_max, size_t* out_len);

    bool   (*write_file) (void* ctx, const char* path,
                          const uint8_t* buf, size_t len);

    bool   (*delete_file)(void* ctx, const char* path);

    bool   (*exists)     (void* ctx, const char* path);

    bool   (*rename_file)(void* ctx, const char* from, const char* to);

    size_t (*list_dir)   (void* ctx, const char* dir,
                          char out_names[][EMS_STORAGE_NAME_MAX],
                          size_t max_names);
} IStorageBackend;

// ============================================================
//  CRC32（IEEE 802.3 / Ethernet 多項式 0xEDB88320）
// ============================================================

/** 一次算完整 buffer */
uint32_t storage_crc32(const uint8_t* buf, size_t len);

/** 增量算（給 streaming 用）；初始值傳 0 */
uint32_t storage_crc32_update(uint32_t crc, const uint8_t* buf, size_t len);

// ============================================================
//  events.bin 序列化 / 反序列化
// ============================================================

/**
 * 給定 event 數量，回傳 wire 總長度（bytes）。
 * 公式：HEADER + count * WIRE_EVENT_SIZE + FOOTER
 */
size_t storage_bin_size(uint16_t event_count);

/**
 * 序列化 events 陣列為 binary buffer。
 *
 * @param events    輸入事件陣列
 * @param count     事件筆數（0..EMS_STORAGE_MAX_EVENTS）
 * @param out_buf   輸出 buffer
 * @param buf_max   buffer 容量
 * @param out_len   輸出實際寫入長度
 * @return true 成功；false 表 buffer 不足或參數錯
 */
bool storage_bin_serialize(const ems_event_t* events,
                           uint16_t           count,
                           uint8_t*           out_buf,
                           size_t             buf_max,
                           size_t*            out_len);

/**
 * 反序列化 binary buffer 為 events 陣列。校驗 magic / version / CRC。
 *
 * @param buf         輸入 buffer
 * @param len         buffer 長度
 * @param out_events  輸出事件陣列
 * @param out_max     events 陣列容量
 * @param out_count   輸出實際事件筆數
 * @return true 成功；false 表 magic/version/CRC 任一錯誤，out_count 設 0
 */
bool storage_bin_deserialize(const uint8_t* buf,
                             size_t         len,
                             ems_event_t*   out_events,
                             uint16_t       out_max,
                             uint16_t*      out_count);

// ============================================================
//  index.json 序列化 / 反序列化
// ============================================================

/**
 * 把 case_meta_t 陣列 + next_seq 序列化為 JSON 字串。
 * cases 須為時序排列（最舊在 index 0）。
 *
 * @param cases     case metadata 陣列
 * @param count     筆數
 * @param next_seq  下一個案件編號
 * @param out_json  輸出字串緩衝（含 NUL）
 * @param json_max  緩衝容量
 * @param out_len   輸出實際寫入長度（不含 NUL）
 * @return true 成功；false 表緩衝不足或內部錯誤
 */
bool storage_index_serialize(const case_meta_t* cases,
                             uint16_t           count,
                             uint32_t           next_seq,
                             char*              out_json,
                             size_t             json_max,
                             size_t*            out_len);

/**
 * 反序列化 JSON 字串為 case_meta_t 陣列 + next_seq。
 * 容忍未知欄位（彈性 spec）。
 *
 * @param json          輸入 JSON 字串
 * @param len           長度（不含 NUL）
 * @param out_cases     輸出陣列
 * @param cases_max     陣列容量
 * @param out_count     輸出實際筆數
 * @param out_next_seq  輸出 next_seq
 * @return true 成功；false 表非合法 JSON / 結構錯誤
 */
bool storage_index_parse(const char*  json,
                         size_t       len,
                         case_meta_t* out_cases,
                         uint16_t     cases_max,
                         uint16_t*    out_count,
                         uint32_t*    out_next_seq);

// ============================================================
//  高階操作（透過 backend）
// ============================================================

/**
 * 初始化：讀取 index.json、清理孤兒檔、處理 .tmp 殘檔。
 * @return true 成功（即使 index 不存在也回 true，視為空庫）
 */
bool storage_init(IStorageBackend* be);

/**
 * 儲存一個案件。自動處理 FIFO 覆蓋。
 *
 * @param be              backend
 * @param type            案件類型
 * @param events          事件陣列
 * @param count           事件數
 * @param case_start_ms   案件開始 epoch ms（0 = 無 RTC）
 * @param case_end_ms     案件結束 epoch ms
 * @return true 成功
 */
bool storage_save_case(IStorageBackend*    be,
                       storage_case_type_t type,
                       const ems_event_t*  events,
                       uint16_t            count,
                       uint64_t            case_start_ms,
                       uint64_t            case_end_ms);

/**
 * 列出指定類型的案件 metadata，最新者在 index 0。
 *
 * @return 實際寫入筆數
 */
uint16_t storage_list(IStorageBackend*    be,
                      storage_case_type_t type,
                      case_meta_t*        out,
                      uint16_t            max);

/**
 * 從 storage 載入指定 id 的事件陣列。
 *
 * @param be          backend
 * @param type        類型
 * @param id          10 位 zero-padded id 字串
 * @param out_events  輸出陣列
 * @param out_max     陣列容量
 * @param out_count   輸出實際筆數
 * @return true 成功
 */
bool storage_load_events(IStorageBackend*    be,
                         storage_case_type_t type,
                         const char*         id,
                         ems_event_t*        out_events,
                         uint16_t            out_max,
                         uint16_t*           out_count);

/** 刪除指定 id 的案件（檔案 + index entry） */
bool storage_delete(IStorageBackend*    be,
                    storage_case_type_t type,
                    const char*         id);

/** 全部清空（測試 / 工廠重設用） */
bool storage_format(IStorageBackend* be);

// ============================================================
//  路徑 helpers（暴露給測試與 fs adapter）
// ============================================================

/** 組合 events.bin 完整路徑（"/cases/ohca/0000000003.bin"） */
void storage_build_event_path(char*               out,
                              size_t              out_max,
                              storage_case_type_t type,
                              const char*         id);

/** 取得指定類型的目錄路徑（"/cases/ohca"） */
const char* storage_dir_path(storage_case_type_t type);

/** 把 sequence number 格式化為 10 位 zero-padded id 字串 */
void storage_format_id(uint32_t seq, char* out, size_t out_max);

}  // namespace ems
