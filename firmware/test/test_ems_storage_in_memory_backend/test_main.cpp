// EMS DoseSync Pro — Phase E Unit Test: InMemoryBackend 自測（5 cases）
//
// 目的：驗證 mock backend 自己邏輯沒 bug，避免 test_ems_storage_logic 出錯時
//       無法判斷是 storage_logic 還是 mock backend 的責任。
//
// 對應規格：plan ticklish-exploring-dragonfly.md Step 1 RED phase
#include <unity.h>
#include <string.h>

#include "ems_storage_logic.h"
#include "../helpers/in_mem_backend.h"

using namespace test_helpers;

static IStorageBackend g_be;

void setUp() {
    in_mem_reset();
    wire_in_mem_backend(&g_be);
}

void tearDown() {}

// ============================================================
//  M1: write → read 回傳相同 bytes
// ============================================================

static void M1_write_then_read_round_trip() {
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
    TEST_ASSERT_TRUE(g_be.write_file(g_be.ctx, "/foo.bin", data, sizeof(data)));

    uint8_t out[16];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(g_be.read_file(g_be.ctx, "/foo.bin", out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
}

// ============================================================
//  M2: 讀不存在的檔 → 回 false
// ============================================================

static void M2_read_missing_returns_false() {
    uint8_t out[16];
    size_t out_len = 99;
    TEST_ASSERT_FALSE(g_be.read_file(g_be.ctx, "/nope.bin", out, sizeof(out), &out_len));
}

// ============================================================
//  M3: exists 正確區分有無
// ============================================================

static void M3_exists_reports_presence() {
    const uint8_t data[] = {1, 2, 3};
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/a.bin"));
    g_be.write_file(g_be.ctx, "/a.bin", data, sizeof(data));
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/a.bin"));
    g_be.delete_file(g_be.ctx, "/a.bin");
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/a.bin"));
}

// ============================================================
//  M4: rename 把 from 內容搬到 to，from 消失
// ============================================================

static void M4_rename_moves_contents() {
    const uint8_t data[] = {0xAB, 0xCD};
    g_be.write_file(g_be.ctx, "/x.tmp", data, sizeof(data));

    TEST_ASSERT_TRUE(g_be.rename_file(g_be.ctx, "/x.tmp", "/x.bin"));
    TEST_ASSERT_FALSE(g_be.exists(g_be.ctx, "/x.tmp"));
    TEST_ASSERT_TRUE(g_be.exists(g_be.ctx, "/x.bin"));

    uint8_t out[8];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(g_be.read_file(g_be.ctx, "/x.bin", out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));
}

// ============================================================
//  M5: list_dir 只列直屬檔案（不遞迴子目錄）
// ============================================================

static void M5_list_dir_returns_direct_children_only() {
    const uint8_t d[] = {0};
    g_be.write_file(g_be.ctx, "/cases/ohca/0000000001.bin", d, 1);
    g_be.write_file(g_be.ctx, "/cases/ohca/0000000002.bin", d, 1);
    g_be.write_file(g_be.ctx, "/cases/index.json",          d, 1);  // 父層檔
    g_be.write_file(g_be.ctx, "/cases/training/0000000001.bin", d, 1);  // 隔壁目錄

    char names[8][EMS_STORAGE_NAME_MAX];
    size_t n = g_be.list_dir(g_be.ctx, "/cases/ohca", names, 8);
    TEST_ASSERT_EQUAL_UINT32(2, n);

    // 順序由 std::map 字典序保證
    TEST_ASSERT_EQUAL_STRING("0000000001.bin", names[0]);
    TEST_ASSERT_EQUAL_STRING("0000000002.bin", names[1]);
}

// ============================================================
//  main
// ============================================================

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(M1_write_then_read_round_trip);
    RUN_TEST(M2_read_missing_returns_false);
    RUN_TEST(M3_exists_reports_presence);
    RUN_TEST(M4_rename_moves_contents);
    RUN_TEST(M5_list_dir_returns_direct_children_only);
    return UNITY_END();
}
