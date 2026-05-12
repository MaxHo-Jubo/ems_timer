// EMS DoseSync Pro — On-target Unity test: 真實 LittleFS + EmsStorageFs 持久化驗證
//
// 對應規格：
//   - tasks/testing-infrastructure.md #3 On-target Unity test
//   - partitions.csv（驗證 littlefs partition offset/size 對得上 SoT）
//
// 設計重點：
//   - 在 ESP32-S3 native USB port（VID 303A:1001 USB-JTAG）上跑。GOOUUU
//     ESP32-S3 兩個 Type-C 都接 chip USB，沒有 CP2102，**完全沒 RTS/DTR**，
//     所以 esptool autoreset 印的「Hard resetting via RTS pin」對這板無效。
//   - 因此放棄真實 ESP.restart() 跨 session 持久化驗證 — 板子重啟期間 USB CDC
//     重列舉，pio test 的 monitor 會斷線變 `Device not configured`。
//   - 改用 LittleFS.end() + emsStorage_fs_mount() 在同 session 模擬 unmount/remount，
//     資料還是真正落到 flash partition、重新讀 inode table，能抓 LittleFS 持久化、
//     CRC、index.json 序列化漏欄位等核心 bug；唯一蓋不到的是 RTC 狀態 / NVS
//     殘留交互這類「真重啟才有」的場景（這層留給 Phase F 接 BLE 後另外手測）。
//
// 跑法：
//   pio test -e esp32-s3-test
//
// 板子卡住怎麼救：
//   1. BOOT + RESET 按住 BOOT 不放，短按 RESET，放開 BOOT → 進 download mode
//   2. pio device list 看到 "USB JTAG/serial debug unit" 就是 download mode
//   3. 重燒：pio test -e esp32-s3-test
//   4. 燒完純按 RESET（不按 BOOT），板子跑新韌體

#include <Arduino.h>
#include <unity.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <string.h>

#include "ems_storage_logic.h"
#include "ems_storage_fs.h"
#include "ems_supp_model.h"

using ems::IStorageBackend;
using ems::case_meta_t;
using ems::ems_event_t;
using ems::storage_case_type_t;

// ============================================================
//  常數：partition 預期值 + fixture event
// ============================================================

/** 預期 LittleFS partition 配置（對齊 partitions.csv） */
static constexpr uint32_t EXPECTED_LITTLEFS_OFFSET = 0x3D0000u;
static constexpr uint32_t EXPECTED_LITTLEFS_SIZE   = 0xC30000u;  // 12.19 MB
/** SubType 0x82 = ESP-IDF LittleFS subtype */
static constexpr uint8_t  EXPECTED_LITTLEFS_SUBTYPE = 0x82u;

/** 跨 unmount/remount 驗證用的固定事件 — 故意挑各欄位都有非零值 */
static const ems_event_t FIXTURE_EVENT = {
    /*.event_id        =*/ 42u,
    /*.type            =*/ ems::EVT_EPI_LOCAL,
    /*.timestamp_ms    =*/ 1737000000000ULL,
    /*.elapsed_ms      =*/ 60000u,
    /*.count           =*/ 1u,
    /*.actual_time_null=*/ false,
};
static constexpr uint64_t FIXTURE_CASE_START_MS = 1737000000000ULL - 60000ULL;
static constexpr uint64_t FIXTURE_CASE_END_MS   = 1737000000000ULL;

// ============================================================
//  全域狀態：backend 每次 mount 後重新填表
// ============================================================

static IStorageBackend g_backend = {};

/** 統一的 mount + init 序列；同時用於初始與「remount 模擬 reboot」 */
static bool mount_and_init(bool format_first) {
    if (!ems::emsStorage_fs_mount(&g_backend)) {
        return false;
    }
    if (!ems::emsStorage_fs_ensure_dirs()) {
        return false;
    }
    if (format_first && !ems::storage_format(&g_backend)) {
        return false;
    }
    return ems::storage_init(&g_backend);
}

/** unmount LittleFS，把 backend 清零，模擬 reboot 後 LittleFS 從 cold start 再 mount */
static void simulate_reboot_unmount() {
    LittleFS.end();
    memset(&g_backend, 0, sizeof(g_backend));
    delay(50);  // 讓 flash driver 內部 state 沉澱
}

// ============================================================
//  Test cases
// ============================================================

/**
 * 驗證 partitions.csv 的 littlefs partition 真實註冊到 ESP32 partition table，
 * 且 offset / size 對得上 SoT（plan ticklish-exploring-dragonfly.md §1）。
 * 抓 bug：partitions.csv 改了 offset 但沒同步 spec，或燒錯 partition table。
 */
static void test_partition_info_matches_spec() {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(EXPECTED_LITTLEFS_SUBTYPE),
        "littlefs");

    TEST_ASSERT_NOT_NULL_MESSAGE(part, "partitions.csv 缺 'littlefs' partition 或 subtype 錯");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(EXPECTED_LITTLEFS_OFFSET, part->address,
                                    "LittleFS offset 與 partitions.csv §1 不符");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(EXPECTED_LITTLEFS_SIZE, part->size,
                                    "LittleFS size 與 partitions.csv §1 不符");
}

/**
 * 驗證 LittleFS 能成功 mount + ensure_dirs；EmsStorageFs 把 backend 函式表填好。
 * 抓 bug：partitions.csv 沒 littlefs / formatOnFail 失敗 / ensure_dirs mkdir 出問題。
 */
static void test_littlefs_mount_and_ensure_dirs() {
    TEST_ASSERT_TRUE_MESSAGE(mount_and_init(/*format_first=*/true),
                             "mount + ensure_dirs + format + init 失敗");

    TEST_ASSERT_NOT_NULL_MESSAGE(g_backend.write_file, "backend.write_file 未綁定");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_backend.read_file,  "backend.read_file 未綁定");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_backend.exists,     "backend.exists 未綁定");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_backend.list_dir,   "backend.list_dir 未綁定");

    case_meta_t metas[5];
    uint16_t count = ems::storage_list(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas, 5);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, count, "format 後 OHCA list 應為空");
}

/**
 * 把 FIXTURE_EVENT 包成一筆 OHCA 案件存進真 LittleFS。
 * storage_save_case 內部會：寫 events.bin + 寫 index.json + 更新序號。
 * 抓 bug：events.bin 寫入失敗（flash region 錯） / index.json 序列化炸 / FIFO 邏輯誤刪。
 */
static void test_save_event_to_littlefs() {
    bool ok = ems::storage_save_case(&g_backend,
                                     ems::EMS_CASE_TYPE_OHCA,
                                     &FIXTURE_EVENT,
                                     /*count=*/1,
                                     FIXTURE_CASE_START_MS,
                                     FIXTURE_CASE_END_MS);
    TEST_ASSERT_TRUE_MESSAGE(ok, "storage_save_case 失敗");

    case_meta_t metas[5];
    uint16_t count = ems::storage_list(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas, 5);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, count, "save 後 list 應有 1 筆");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FIXTURE_CASE_START_MS, metas[0].start_ms,
                                     "case_meta.start_ms 不符");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FIXTURE_CASE_END_MS, metas[0].end_ms,
                                     "case_meta.end_ms 不符");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, metas[0].event_count, "event_count 不符");
}

/**
 * 模擬 reboot：LittleFS.end() → 重新 mount + init（不 format）。
 * 抓 bug：mount 失敗、ensure_dirs 在已存在目錄上炸、storage_init 對舊 index 誤判。
 */
static void test_remount_after_simulated_reboot() {
    simulate_reboot_unmount();
    TEST_ASSERT_TRUE_MESSAGE(mount_and_init(/*format_first=*/false),
                             "模擬 reboot 後 mount + init 失敗");
}

/**
 * 模擬 reboot 後核心斷言：FIXTURE_EVENT 寫入後重新 mount，每個欄位都應原封不動讀回。
 * 抓 bug：
 *   - events.bin CRC mismatch（write_file 對 LittleFS append 行為的誤解）
 *   - index.json 漏寫某欄位（升版時序列化 / 反序列化不對稱）
 *   - 序號 / case id 跨 mount 重置（沒 persist next_seq）
 */
static void test_event_persists_across_remount() {
    case_meta_t metas[5];
    uint16_t count = ems::storage_list(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas, 5);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, count, "remount 後 list 應仍為 1 筆");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FIXTURE_CASE_START_MS, metas[0].start_ms,
                                     "remount 後 start_ms 不符（index.json 漏寫？）");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FIXTURE_CASE_END_MS, metas[0].end_ms,
                                     "remount 後 end_ms 不符");

    ems_event_t loaded[5] = {};
    uint16_t loaded_count = 0;
    bool ok = ems::storage_load_events(&g_backend,
                                       ems::EMS_CASE_TYPE_OHCA,
                                       metas[0].id,
                                       loaded, 5, &loaded_count);
    TEST_ASSERT_TRUE_MESSAGE(ok, "storage_load_events 失敗（events.bin 讀不到？）");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, loaded_count, "讀回事件數應為 1");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(FIXTURE_EVENT.event_id, loaded[0].event_id,
                                     "event_id 不符");
    TEST_ASSERT_EQUAL_MESSAGE(FIXTURE_EVENT.type, loaded[0].type, "event.type 不符");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(FIXTURE_EVENT.timestamp_ms, loaded[0].timestamp_ms,
                                     "timestamp_ms 不符");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(FIXTURE_EVENT.elapsed_ms, loaded[0].elapsed_ms,
                                     "elapsed_ms 不符");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(FIXTURE_EVENT.count, loaded[0].count, "count 不符");
    TEST_ASSERT_EQUAL_MESSAGE(FIXTURE_EVENT.actual_time_null, loaded[0].actual_time_null,
                              "actual_time_null 不符");
}

/**
 * 清乾淨：delete 該案件 + list 確認回 0。讓下次跑 test 從乾淨狀態起步。
 * 抓 bug：storage_delete 沒同步移除 index entry / 殘留 events.bin。
 */
static void test_delete_after_verify() {
    case_meta_t metas[5];
    uint16_t count = ems::storage_list(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas, 5);
    TEST_ASSERT_EQUAL_UINT16(1u, count);

    bool ok = ems::storage_delete(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas[0].id);
    TEST_ASSERT_TRUE_MESSAGE(ok, "storage_delete 失敗");

    count = ems::storage_list(&g_backend, ems::EMS_CASE_TYPE_OHCA, metas, 5);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, count, "delete 後 list 應為 0");
}

// ============================================================
//  Unity entry
// ============================================================

void setUp() {}
void tearDown() {}

void setup() {
    // STEP 01: 等 USB CDC 列舉完成才開 Serial（避免前幾條 Unity 輸出被吃掉）
    //          長 delay 也讓 host 來得及 attach monitor 看後續 trace
    Serial.begin(115200);
    delay(5000);
    Serial.println();
    Serial.println("[test_storage_hw] setup() entered, USB CDC ready");
    Serial.flush();

    UNITY_BEGIN();

    // STEP 03: Partition layout sanity check（不碰 LittleFS，先確認 Unity 框架本身能跑）
    Serial.println("[test_storage_hw] -> partition_info"); Serial.flush();
    RUN_TEST(test_partition_info_matches_spec);

    // STEP 04: Mount + format + 寫入 fixture
    Serial.println("[test_storage_hw] -> mount_and_ensure_dirs"); Serial.flush();
    RUN_TEST(test_littlefs_mount_and_ensure_dirs);
    Serial.println("[test_storage_hw] -> save_event"); Serial.flush();
    RUN_TEST(test_save_event_to_littlefs);

    // STEP 05: 模擬 reboot（end + remount）+ 跨 remount 驗證持久化
    Serial.println("[test_storage_hw] -> remount"); Serial.flush();
    RUN_TEST(test_remount_after_simulated_reboot);
    Serial.println("[test_storage_hw] -> persists"); Serial.flush();
    RUN_TEST(test_event_persists_across_remount);

    // STEP 06: 清理
    Serial.println("[test_storage_hw] -> delete"); Serial.flush();
    RUN_TEST(test_delete_after_verify);

    Serial.println("[test_storage_hw] all tests done"); Serial.flush();
    UNITY_END();
}

void loop() {
    // pio test 流程不會跑到 loop（setup 結尾呼叫 UNITY_END）
}
