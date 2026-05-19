// EMS DoseSync Pro — BLE NUS peripheral 實作

#include "ble_nus.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <cstring>

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
    adv->addServiceUUID(NUS_SERVICE_UUID);
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
    tx_char_->setValue(const_cast<uint8_t*>(data), len);
    tx_char_->notify();
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
