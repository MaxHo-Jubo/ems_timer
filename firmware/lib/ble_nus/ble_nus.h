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

// ATT MTU 規範下界（BLE 4.0 最小值）；MTU 協商完成前的安全預設值
static constexpr uint16_t BLE_DEFAULT_ATT_MTU = 23;

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
     *
     * 若 len > (att_mtu - 3) 會自動依 ble_chunker::chunk_size_from_mtu()
     * 分段多次 notify（ESP32 BLE stack 對超 MTU 的單次 notify 會靜默截斷）。
     * 對 central 端透明：ble-client.js 以 '\n' 切句重組訊息邊界，呼叫端
     * 必須在 payload 結尾自帶 '\n' 分隔符。
     *
     * @param data  待送 raw bytes
     * @param len   資料長度
     * @return true 全部 chunk 送出成功；false 表 client 未連線、TX char
     *              無效、或 chunked 傳送途中 client 中途斷線（剩餘 chunks
     *              未送出，caller 應自行 retry 或 log）
     */
    bool send(const uint8_t* data, size_t len);

    /** 目前是否有 client 連線 */
    bool connected() const { return connected_; }

    /**
     * 目前協商後的 ATT MTU。
     *
     * 連線後 central（瀏覽器 / nRF Connect）會發起 MTU 協商，完成時 onMtuChanged
     * 更新此值；協商前回傳規範下界 BLE_DEFAULT_ATT_MTU。chunked TX 用此值算
     * chunk size（MTU − 3 ATT header overhead）。
     *
     * @return 目前 ATT MTU（bytes）
     */
    uint16_t att_mtu() const { return att_mtu_; }

    // GATT callback 存取點（內部用，不公開給使用者）
    void _on_connect();
    void _on_disconnect();
    void _on_mtu_changed(uint16_t mtu);
    void _on_rx_write(const uint8_t* data, size_t len);

private:
    BLEServer*         server_   = nullptr;
    BLECharacteristic* tx_char_  = nullptr;
    volatile bool      connected_ = false;
    // 協商後 ATT MTU；onMtuChanged（GATT task）寫、att_mtu() 於 main loop 讀。
    // uint16_t 為單字對齊存取 → 讀寫原子，故免 portMUX；若日後改大型別需重新評估同步
    volatile uint16_t  att_mtu_  = BLE_DEFAULT_ATT_MTU;

    // RX single-slot buffer + portMUX（GATT task 寫入，main loop 排空）
    uint8_t            rx_buf_[BLE_NUS_RX_BUF_MAX] = {};
    volatile size_t    rx_len_   = 0;
    volatile bool      rx_ready_ = false;
    portMUX_TYPE       rx_mux_   = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace ems
