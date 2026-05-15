// EMS DoseSync Pro — Impl-Phase F: BLE payload chunker（純函式）
//
// 對應計畫：docs/phase-f-web-validation-plan.md §7.2 (chunk 切分邏輯) / §12 風險
// 對應 todo：tasks/phase-f-todo.md F-2
// 測試入口：firmware/test/test_ble_chunker/test_main.cpp
//
// 用途：
//   BLE GATT notify 受 ATT MTU 限制（預設 23B，協商可到 247B），扣掉 3 byte ATT header
//   後得 payload chunk size。本 lib 算出給定 payload 應切成幾個 chunk + 每 chunk 的
//   (offset, len) range，呼叫端依此把 payload buffer 切片送 notify。
//
// 設計：
//   純函式 + 無內部狀態，無 Arduino 依賴，可在 native env 跑 unit test。

#pragma once

#include <cstddef>
#include <cstdint>

namespace ems {

// ATT header overhead：GATT notify 每 packet 前 3 byte 是 ATT 控制位（opcode + handle）
constexpr size_t ATT_HEADER_OVERHEAD = 3;

// MTU 下限：ATT 規範最小 MTU 是 23，但為了實作防呆，<4 直接判定不可用
constexpr uint16_t ATT_MIN_USABLE_MTU = 4;

/**
 * Chunk 範圍：payload 內某段的 [offset, offset+len) range。
 */
struct chunk_range_t {
    size_t offset;
    size_t len;
};

/**
 * 從 ATT MTU 算出單一 chunk 可容納的 payload bytes。
 *
 * 公式：chunk_size = MTU - ATT_HEADER_OVERHEAD
 *
 * @param att_mtu  協商後的 ATT MTU（NimBLE 預設 23，高速協商可到 247）
 * @return         chunk size；MTU < ATT_MIN_USABLE_MTU 回 0（不可用）
 */
size_t chunk_size_from_mtu(uint16_t att_mtu);

/**
 * 給定 payload 長度 + chunk size，算出總共幾個 chunks。
 *
 * 公式：ceil(payload_len / chunk_size)；payload_len = 0 或 chunk_size = 0 回 0。
 *
 * @param payload_len  payload 總 bytes
 * @param chunk_size   單一 chunk 容量
 * @return             chunks 總數
 */
size_t chunk_count(size_t payload_len, size_t chunk_size);

/**
 * 取第 idx 個 chunk 的 (offset, len) range。
 *
 * 最後一個 chunk 可能 len < chunk_size（payload 不整除）。
 * idx 超過 chunk_count() 或 payload_len = 0 → 回 {0, 0}（防呆）。
 *
 * @param payload_len  payload 總 bytes
 * @param chunk_size   單一 chunk 容量
 * @param idx          chunk 索引（0-based）
 * @return             chunk_range_t；out-of-range 時 len = 0
 */
chunk_range_t chunk_at(size_t payload_len, size_t chunk_size, size_t idx);

}  // namespace ems
