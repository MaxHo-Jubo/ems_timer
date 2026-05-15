// EMS DoseSync Pro — Impl-Phase F: BLE RX queue 實作
//
// 對應 header：ble_rx_queue.h
// 對應 test： firmware/test/test_ble_rx_queue/test_main.cpp

#include "ble_rx_queue.h"

#include <cstring>

namespace ems {

void rx_queue_init(ble_rx_queue_t* q) {
    q->head = 0;
    q->tail = 0;
    q->full = false;
    q->dropped = 0;
}

bool rx_queue_push(ble_rx_queue_t* q, const uint8_t* data, size_t len) {
    // STEP 01: 參數防呆 — nullptr / 長度 0 / 超 max frame → 拒絕（不計 dropped，視為 caller bug）
    if (data == nullptr || len == 0 || len > BLE_RX_MAX_FRAME_BYTES) {
        return false;
    }

    // STEP 02: 滿 → 拒絕並累計 dropped 給 F-3 dispatcher 偵測 burst 丟包
    if (q->full) {
        ++q->dropped;
        return false;
    }

    // STEP 03: 寫入 tail 位置
    std::memcpy(q->buf[q->tail].data, data, len);
    q->buf[q->tail].len = len;

    // STEP 04: tail 環狀前進，若追上 head → 標記滿
    q->tail = (q->tail + 1) % BLE_RX_QUEUE_CAPACITY;
    if (q->tail == q->head) {
        q->full = true;
    }
    return true;
}

bool rx_queue_pop(ble_rx_queue_t* q, uint8_t* out, size_t* out_len) {
    // STEP 01: 參數防呆 — out/out_len nullptr → 拒絕（caller bug）
    if (out == nullptr || out_len == nullptr) {
        return false;
    }

    // STEP 02: 空 → 拒絕（不寫 out_len，caller 哨兵值得以保留）
    if (rx_queue_empty(q)) {
        return false;
    }

    // STEP 03: 從 head 位置複製出去
    size_t len = q->buf[q->head].len;
    std::memcpy(out, q->buf[q->head].data, len);
    *out_len = len;

    // STEP 04: head 環狀前進；只要 pop 成功 full 必清掉
    q->head = (q->head + 1) % BLE_RX_QUEUE_CAPACITY;
    q->full = false;
    return true;
}

bool rx_queue_empty(const ble_rx_queue_t* q) {
    return !q->full && q->head == q->tail;
}

bool rx_queue_full(const ble_rx_queue_t* q) {
    return q->full;
}

size_t rx_queue_size(const ble_rx_queue_t* q) {
    if (q->full) {
        return BLE_RX_QUEUE_CAPACITY;
    }
    if (q->tail >= q->head) {
        return q->tail - q->head;
    }
    // tail 已繞回 head 前面
    return BLE_RX_QUEUE_CAPACITY - q->head + q->tail;
}

}  // namespace ems
