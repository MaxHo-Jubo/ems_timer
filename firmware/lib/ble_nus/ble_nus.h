// EMS DoseSync Pro — BLE NUS (Nordic UART Service) peripheral 包裝
//
// 封裝 ESP32 Arduino BLE stack 的 NUS peripheral 初始化、RX queue、TX notify。
// 設計遵守 feedback_ble_callback_non_blocking：GATT callback 只做 memcpy + 旗標，
// 實際訊息處理推給 main loop 的 poll()。
//
// 用法：
//   BleNus ble;
//   ble.begin("EMS-DoseSync-Pro");    // setup() 內呼叫
//   ble.poll(callback);               // loop() 每 tick 呼叫，有 RX 資料時觸發 callback
//   ble.send(buf, len);               // TX notify 推資料給 client

#pragma once

#include <Arduino.h>    // portMUX_TYPE（FreeRTOS spinlock）
#include <cstddef>
#include <cstdint>

class BLEServer;
class BLECharacteristic;

namespace ems {

// RX buffer 上限（time_sync ~100B + pair_verify ~50B + chunk ACK ~150B，留 3-5x 餘裕）
static constexpr size_t BLE_NUS_RX_BUF_MAX = 512;

// BLE 連線間隔偏好（單位 × 1.25 ms）
static constexpr uint16_t BLE_CONN_INTERVAL_MIN = 0x06;  // 7.5 ms
static constexpr uint16_t BLE_CONN_INTERVAL_MAX = 0x12;  // 22.5 ms

/**
 * RX 訊息 callback 型別。
 * @param data  接收到的 raw bytes（不保證 null-terminated）
 * @param len   資料長度
 */
using BleNusRxCallback = void (*)(const uint8_t* data, size_t len);

/**
 * BLE NUS peripheral 服務。
 *
 * 生命週期：begin() 一次 → loop 內反覆 poll() + 按需 send()。
 * thread-safety：GATT callback 跑在不同 task，內部用 portMUX 保護 RX buffer。
 */
class BleNus {
public:
    /**
     * 初始化 BLE stack、建立 NUS service、開始 advertising。
     * @param device_name  廣播裝置名稱
     * @return true 初始化成功
     */
    bool begin(const char* device_name);

    /**
     * 主 loop 每 tick 呼叫。若有 RX 資料，拷貝到 local buffer 後觸發 callback。
     * @param on_rx  收到資料時的 callback（nullptr 則只排空不處理）
     */
    void poll(BleNusRxCallback on_rx);

    /**
     * TX notify 推資料給已連線的 client。
     * @param data  待送 raw bytes
     * @param len   資料長度
     * @return true 成功送出（client 已連線 + TX char 有效）
     */
    bool send(const uint8_t* data, size_t len);

    /** 目前是否有 client 連線 */
    bool connected() const { return connected_; }

    // GATT callback 存取點（內部用，不公開給使用者）
    void _on_connect();
    void _on_disconnect();
    void _on_rx_write(const uint8_t* data, size_t len);

private:
    BLEServer*         server_   = nullptr;
    BLECharacteristic* tx_char_  = nullptr;
    volatile bool      connected_ = false;

    // RX single-slot buffer + portMUX（GATT task 寫入，main loop 排空）
    uint8_t            rx_buf_[BLE_NUS_RX_BUF_MAX] = {};
    volatile size_t    rx_len_   = 0;
    volatile bool      rx_ready_ = false;
    portMUX_TYPE       rx_mux_   = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace ems
