// EMS DoseSync Pro — BLE NUS peripheral 實作

#include "ble_nus.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstring>

#include "ble_chunker.h"  // ATT_HEADER_OVERHEAD 共用常數

namespace ems {

// NUS（Nordic UART Service）標準 UUID
static constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// singleton 指標供 GATT callback 使用（ESP32 BLE callback 沒帶 user context）
static BleNus* g_instance = nullptr;

// ============================================================
//  GATT callbacks（跑在 BLE task，嚴禁阻塞）
// ============================================================

class NusServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* /*pServer*/) override {
        if (g_instance) { g_instance->_on_connect(); }
    }
    void onDisconnect(BLEServer* /*pServer*/) override {
        if (g_instance) { g_instance->_on_disconnect(); }
        BLEDevice::startAdvertising();
    }
    // central 發起 MTU 協商完成 → 記錄協商值供 chunked TX 算 chunk size
    void onMtuChanged(BLEServer* /*pServer*/, esp_ble_gatts_cb_param_t* param) override {
        if (g_instance && param) { g_instance->_on_mtu_changed(param->mtu.mtu); }
    }
};

class NusRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        if (!g_instance) { return; }
        std::string value = pChar->getValue();
        if (!value.empty() && value.length() <= BLE_NUS_RX_BUF_MAX) {
            g_instance->_on_rx_write(
                reinterpret_cast<const uint8_t*>(value.data()), value.length());
        }
    }
};

// ============================================================
//  BleNus 公開 API
// ============================================================

bool BleNus::begin(const char* device_name) {
    g_instance = this;

    BLEDevice::init(device_name);
    // 把 GATT MTU 預設提高到 517（BLE 5.x 上限）。預設 23 bytes 在 Chrome 連線後
    // 的 MTU exchange 可能 hang，導致 getPrimaryServices() 卡死。Chrome 實際協商
    // 結果由 onMtuChanged 回報、寫入 att_mtu_ 供 chunked TX 算 chunk size。
    // 失敗只 WARN log：MTU 維持預設 23，chunked send 仍能 fallback 至 20 bytes/chunk。
    const esp_err_t mtu_err = BLEDevice::setMTU(517);
    if (mtu_err != ESP_OK) {
        Serial.printf("[BLE] WARN setMTU(517) failed err=%d, 維持預設 MTU 23\n",
                      (int)mtu_err);
    }
    server_ = BLEDevice::createServer();
    if (server_ == nullptr) {
        Serial.println("[BLE] WARN createServer failed");
        return false;
    }
    server_->setCallbacks(new NusServerCallbacks());

    BLEService* svc = server_->createService(NUS_SERVICE_UUID);

    BLECharacteristic* rx_char = svc->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx_char->setCallbacks(new NusRxCallbacks());

    tx_char_ = svc->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    tx_char_->addDescriptor(new BLE2902());

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    // Advertising layout：main packet 塞 flags(3) + 128-bit service UUID(18) +
    // name(2+N) ≤ 31 bytes（spec 上限）。scan response 放完整 name 備援。
    //
    // 為何 service UUID 必須進 main packet：macOS Chrome / CoreBluetooth 連線後
    // 做 GATT discovery 時，若 advertising 主包未宣告 service UUID，
    // getPrimaryService(s) 會 hang 不回。
    // 為何 name 也要進 main：namePrefix filter 與 chrome://bluetooth-internals 列表
    // 依賴 main packet 的 name，scan response 在 macOS Chrome passive scan 不穩。
    //
    // device_name 長度上限：31 − 3(flags) − 18(UUID) − 2(name header) = 8 bytes。
    // 超過則 main packet 會被 BLE stack 截斷或塞不下 UUID，導致 GATT discovery 再次
    // hang。執行期檢查並 WARN（v1 hard-code "DSP-0001" = 8 bytes，邊界）。
    constexpr size_t BLE_ADV_NAME_MAX_BYTES = 8;
    const size_t device_name_len = strnlen(device_name, BLE_ADV_NAME_MAX_BYTES + 1);
    if (device_name_len > BLE_ADV_NAME_MAX_BYTES) {
        Serial.printf("[BLE] WARN device_name '%s' len=%u > %u, main packet 將塞不下"
                      " service UUID + name，CoreBluetooth GATT 可能 hang\n",
                      device_name, (unsigned)device_name_len,
                      (unsigned)BLE_ADV_NAME_MAX_BYTES);
    }
    BLEAdvertisementData adv_data;
    adv_data.setFlags(0x06);  // LE General Discoverable + BR/EDR Not Supported
    adv_data.setCompleteServices(BLEUUID(NUS_SERVICE_UUID));
    adv_data.setName(device_name);
    adv->setAdvertisementData(adv_data);

    BLEAdvertisementData scan_resp;
    scan_resp.setName(device_name);
    adv->setScanResponseData(scan_resp);

    adv->setScanResponse(true);
    adv->setMinPreferred(BLE_CONN_INTERVAL_MIN);
    adv->setMaxPreferred(BLE_CONN_INTERVAL_MAX);
    BLEDevice::startAdvertising();
    Serial.printf("[BLE] advertising as %s\n", device_name);
    return true;
}

void BleNus::poll(BleNusRxCallback on_rx) {
    if (!rx_ready_) { return; }

    // 原子取出 buffer 後立即清旗標
    uint8_t local_buf[BLE_NUS_RX_BUF_MAX];
    size_t local_len;
    portENTER_CRITICAL(&rx_mux_);
    local_len = rx_len_;
    memcpy(local_buf, rx_buf_, local_len);
    rx_ready_ = false;
    portEXIT_CRITICAL(&rx_mux_);

    if (on_rx) {
        on_rx(local_buf, local_len);
    }
}

bool BleNus::send(const uint8_t* data, size_t len) {
    if (!connected_ || tx_char_ == nullptr || data == nullptr || len == 0) {
        return false;
    }
    // ESP32 BLE notify 超過 (att_mtu - 3) bytes 會被 stack 靜默截斷。
    // 任何超過此上限的訊息（例如 41-byte pair_status JSON）必須分段 notify，
    // central 端（ble-client.js）會以 '\n' 切句重組，分段對重組透明。
    // 共用 ble_chunker::chunk_size_from_mtu()，與 sync_send.cpp chunked TX
    // 行為一致；MTU 異常（< ATT_MIN_USABLE_MTU）時 fallback 用規範下界 23
    // 對應的 20 bytes payload。
    const uint16_t mtu = att_mtu_;
    size_t chunk_size = chunk_size_from_mtu(mtu);
    if (chunk_size == 0) {
        chunk_size = chunk_size_from_mtu(BLE_DEFAULT_ATT_MTU);
    }
    Serial.printf("[BLE] send len=%u mtu=%u chunk=%u\n",
                  (unsigned)len, (unsigned)mtu, (unsigned)chunk_size);
    for (size_t off = 0; off < len; off += chunk_size) {
        // 中途斷線檢查：ESP32 BLE notify() 本身 void，client 斷線 / TX queue
        // 滿時剩餘 chunks 會被靜默丟。caller 拿到 false 才能 retry / 上報
        // ERROR，否則 web 端會收到截斷 payload → JSON.parse 失敗（time_sync_ack
        // 缺 '\n' 同類 root cause）。
        if (!connected_) {
            Serial.printf("[BLE] WARN send aborted: client disconnected mid-stream"
                          " at off=%u/%u\n", (unsigned)off, (unsigned)len);
            return false;
        }
        const size_t this_chunk = (len - off) > chunk_size ? chunk_size : (len - off);
        tx_char_->setValue(const_cast<uint8_t*>(data + off), this_chunk);
        tx_char_->notify();
        Serial.printf("[BLE]   notify off=%u size=%u\n",
                      (unsigned)off, (unsigned)this_chunk);
    }
    return true;
}

// ============================================================
//  GATT callback 轉接（只做 memcpy + 旗標，不阻塞）
// ============================================================

void BleNus::_on_connect() {
    connected_ = true;
}

void BleNus::_on_disconnect() {
    connected_ = false;
}

void BleNus::_on_mtu_changed(uint16_t mtu) {
    att_mtu_ = mtu;
    Serial.printf("[BLE] MTU negotiated: %u (payload per notify=%u)\n",
                  (unsigned)mtu, (unsigned)(mtu - ATT_HEADER_OVERHEAD));
}

void BleNus::_on_rx_write(const uint8_t* data, size_t len) {
    portENTER_CRITICAL(&rx_mux_);
    if (rx_ready_) {
        portEXIT_CRITICAL(&rx_mux_);
        return;
    }
    memcpy(rx_buf_, data, len);
    rx_len_ = len;
    rx_ready_ = true;
    portEXIT_CRITICAL(&rx_mux_);
}

}  // namespace ems
