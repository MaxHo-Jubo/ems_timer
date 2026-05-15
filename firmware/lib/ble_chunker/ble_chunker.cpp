// EMS DoseSync Pro — Impl-Phase F: BLE payload chunker 實作
//
// 對應 header：ble_chunker.h
// 對應 test： firmware/test/test_ble_chunker/test_main.cpp

#include "ble_chunker.h"

namespace ems {

size_t chunk_size_from_mtu(uint16_t att_mtu) {
    if (att_mtu < ATT_MIN_USABLE_MTU) {
        return 0;
    }
    return static_cast<size_t>(att_mtu) - ATT_HEADER_OVERHEAD;
}

size_t chunk_count(size_t payload_len, size_t chunk_size) {
    if (payload_len == 0 || chunk_size == 0) {
        return 0;
    }
    // ceil(payload_len / chunk_size)
    return (payload_len + chunk_size - 1) / chunk_size;
}

chunk_range_t chunk_at(size_t payload_len, size_t chunk_size, size_t idx) {
    // STEP 01: 防呆 — empty payload / 無效 chunk_size → {0, 0}
    if (payload_len == 0 || chunk_size == 0) {
        return {0, 0};
    }

    // STEP 02: idx 超過 chunk_count → {0, 0}
    size_t total = chunk_count(payload_len, chunk_size);
    if (idx >= total) {
        return {0, 0};
    }

    // STEP 03: 算 offset；最後一個 chunk 可能 len < chunk_size（不整除）
    size_t offset = idx * chunk_size;
    size_t remaining = payload_len - offset;
    size_t len = (remaining < chunk_size) ? remaining : chunk_size;
    return {offset, len};
}

}  // namespace ems
