// EMS DoseSync Pro — Impl-Phase F: BLE RX queue（SPSC ring buffer）
//
// 對應計畫：docs/phase-f-web-validation-plan.md §7.2 callback 解耦
// 對應 todo：tasks/phase-f-todo.md F-2
// 測試入口：firmware/test/test_ble_rx_queue/test_main.cpp
//
// 用途：
//   NimBLE characteristic callback 跑在 GATT task，主迴圈跑在 loop task。
//   callback **不可阻塞**（對齊 feedback memory ble_callback_non_blocking），
//   因此 callback 只做 enqueue，main loop tick() 內 dequeue 處理。
//   本 lib 提供固定容量 ring buffer 純資料結構。
//
// 設計：
//   純資料 struct + 純函式，無內部分配，無 Arduino 依賴，可在 native env 跑 unit test。
//
// Thread safety：
//   **本 lib 自身不做 thread safety**。caller 必須在 ESP32 critical section / mutex /
//   FreeRTOS queue primitive 內包裝 push/pop，或改用 atomic head/tail（SPSC lockless）。
//   選擇權留給 F-3 case_sync_dispatcher 整合層。

#pragma once

#include <cstddef>
#include <cstdint>

namespace ems {

// Queue 容量：8 frames（NUS RX 控制訊息小+少，配對碼/ack 等，8 足夠 buffer 短暫 burst）
constexpr size_t BLE_RX_QUEUE_CAPACITY = 8;

// 單一 frame 最大 bytes：對齊 ATT_MTU 247 - 3 header = 244，rounded up 256 給 future MTU 提升
constexpr size_t BLE_RX_MAX_FRAME_BYTES = 256;

/**
 * RX frame：固定 buffer + 實際長度。
 */
struct ble_rx_frame_t {
    uint8_t data[BLE_RX_MAX_FRAME_BYTES];
    size_t  len;
};

/**
 * SPSC ring buffer state。
 *
 * head：consumer 讀指標（main loop dequeue 端）
 * tail：producer 寫指標（GATT callback enqueue 端）
 * full：head == tail 時用此旗標區分「空」(false) 與「滿」(true)
 * dropped：滿 queue 拒收的 frame 累計數（telemetry — F-3 dispatcher 可定期讀印 log
 *          以偵測 BLE burst 流量丟包）
 */
struct ble_rx_queue_t {
    ble_rx_frame_t buf[BLE_RX_QUEUE_CAPACITY];
    size_t         head;
    size_t         tail;
    bool           full;
    uint32_t       dropped;
};

/**
 * 初始化 queue：清空頭尾指標 + full flag。
 *
 * 不歸零 buf 內容（避免 memset 大 buffer 浪費 cycle；push 時自會覆蓋）。
 *
 * @param q  queue 指標
 */
void rx_queue_init(ble_rx_queue_t* q);

/**
 * 把一個 frame 寫入 queue。
 *
 * 拒絕條件：q 滿 / data == nullptr / len == 0 / len > BLE_RX_MAX_FRAME_BYTES
 *
 * @param q     queue 指標
 * @param data  source buffer
 * @param len   bytes（必須 > 0 且 <= BLE_RX_MAX_FRAME_BYTES）
 * @return      true = 成功；false = 滿 / 參數錯
 */
bool rx_queue_push(ble_rx_queue_t* q, const uint8_t* data, size_t len);

/**
 * 從 queue 取出最舊一個 frame。
 *
 * 拒絕條件：q 空 / out == nullptr / out_len == nullptr
 *
 * @warning  out buffer 必須 >= BLE_RX_MAX_FRAME_BYTES。caller 慣例宣告：
 *           `uint8_t buf[BLE_RX_MAX_FRAME_BYTES];` 即可。
 *
 * @param q        queue 指標
 * @param out      target buffer（必須 >= BLE_RX_MAX_FRAME_BYTES）
 * @param out_len  寫入實際長度（空時不變動）
 * @return         true = 成功；false = 空 / 參數錯
 */
bool rx_queue_pop(ble_rx_queue_t* q, uint8_t* out, size_t* out_len);

/** 是否空 */
bool rx_queue_empty(const ble_rx_queue_t* q);

/** 是否滿 */
bool rx_queue_full(const ble_rx_queue_t* q);

/** 目前 element 數（0 ~ BLE_RX_QUEUE_CAPACITY） */
size_t rx_queue_size(const ble_rx_queue_t* q);

}  // namespace ems
