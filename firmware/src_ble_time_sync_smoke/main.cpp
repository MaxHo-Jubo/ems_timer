// ============================================================
// EMS DoseSync Pro — BLE Time Sync Smoke Test
// ------------------------------------------------------------
// 用途：
//   端對端驗證 docs/ble-time-sync-protocol.md spec。
//   1. ESP32-S3 廣播 Nordic UART Service（與 docs/ble-tester/ 網頁端 UUID 對齊）
//   2. 收到 RX 寫入 → 呼叫 ems_time_sync::time_sync_handle()
//   3. 解析結果 → 從 TX characteristic notify 出 ACK
//
// 設計遵守：
//   - feedback_ble_callback_non_blocking：onWrite 跑在 GATT task，禁止呼叫
//     time_sync_handle（可能阻塞），必須 enqueue 到 main loop task 處理
//   - feedback_esp32s3_usb_cdc_serial_wait：setup 開頭等 USB-CDC 列舉
//   - feedback_byte_buffer_explicit_length：傳給 lib 的永遠帶 length，不靠
//     strlen 推斷
//
// 燒錄：pio run -e ble-time-sync-smoke -t upload
// 監看：pio device monitor -e ble-time-sync-smoke
//
// 不在 smoke 範圍：
//   - ble_chunker / ble_rx_queue（單訊息 < MTU，不需分片）
//   - ems_pairing（smoke 不做配對碼驗證，先驗 raw time_sync handler）
//   - DS3231 寫入（smoke 用軟體對時態，rtc_present=false）
//   - 主韌體整合（這支不影響 src/main.cpp）
// ============================================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "ems_time_sync.h"

// === NUS UUIDs（與 docs/ble-time-sync-protocol.md §1 + ble-tester 對齊）===
static constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // App→Device Write
static constexpr const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // Device→App Notify

// === 廣播名稱（與韌體既有約定一致，便於網頁端 filters 用名稱辨識）===
static constexpr const char* BLE_DEVICE_NAME = "EMS-DoseSync-Smoke";

// === 緩衝容量上限 ===
//   time_sync JSON 約 100 bytes（含 req_id），留 4x 餘裕避免邊界異常
static constexpr size_t RX_BUF_MAX = 512;
//   ACK JSON 約 150 bytes，留 2x 餘裕
static constexpr size_t ACK_BUF_MAX = 512;

// === Heartbeat 間隔（serial 印當下 time_sync state 給 dev 觀察）===
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 5000;

// === setup 內等 USB-CDC 列舉的上限（feedback_esp32s3_usb_cdc_serial_wait）===
static constexpr uint32_t USB_CDC_ENUM_TIMEOUT_MS = 3000;

// === main loop 排程間隔（讓 BLE / WiFi 後台 task 有 CPU 時間）===
static constexpr uint32_t LOOP_SLICE_MS = 20;

// === BLE advertising 偏好連線間隔（單位 × 1.25 ms，配合 iOS 連線優化）===
//     min = 7.5 ms / max = 22.5 ms — 與 ESP32 BLE 範例 iPhone 相容建議一致
static constexpr uint16_t BLE_CONN_INTERVAL_MIN = 0x06;
static constexpr uint16_t BLE_CONN_INTERVAL_MAX = 0x12;

// === BLE state（GATT task 與 main loop 共用）===
//     ESP32 BLE callback 跑在 GATT task（與 main loop 不同 task，常不同核），
//     volatile 不足以擋跨 task race。改用 portMUX_TYPE spinlock：
//     - taskENTER_CRITICAL：跨核互斥 + 禁中斷
//     - 兩端臨界區短（512 byte memcpy + 旗標），不會卡 BLE timing
static BLEServer*         g_server = nullptr;
static BLECharacteristic* g_tx_char = nullptr;
static volatile bool      g_client_connected = false;
static volatile bool      g_rx_ready = false;
static uint8_t            g_rx_buf[RX_BUF_MAX];
static volatile size_t    g_rx_len = 0;
static portMUX_TYPE       g_rx_mux = portMUX_INITIALIZER_UNLOCKED;

// === time_sync state（只在 main loop task 讀寫，無 race）===
static ems::TimeSyncState g_ts_state;

// === Heartbeat 排程記錄上次 print 時間，用於 millis() 差判斷 ===
static uint32_t g_last_heartbeat_ms = 0;

/**
 * BLE Server 生命週期 callback。
 * 連線斷掉後立即重啟廣播，方便 web 端重連測試。
 */
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        // STEP 01: 標記連線狀態，main loop 印 log
        g_client_connected = true;
    }
    void onDisconnect(BLEServer* pServer) override {
        // STEP 01: 標記斷線，main loop 印 log
        g_client_connected = false;
        // STEP 02: 立即重啟廣播，否則裝置會「躲」起來無法被掃到
        BLEDevice::startAdvertising();
    }
};

/**
 * RX characteristic write callback。
 * 跑在 ESP32 BLE GATT task — 嚴禁阻塞操作（解析 JSON、notify、Serial 等都嫌慢）。
 * 只做最低限度的 memcpy + 旗標設定，把實際處理推給 main loop。
 */
class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        // STEP 01: 取得 callback 提供的 raw bytes
        //          getValue() 在 Arduino BLE lib 回的是 std::string，但內含的是
        //          binary bytes（沒有 null-termination 保證）
        std::string value = pChar->getValue();
        size_t len = value.length();
        if (len == 0 || len > RX_BUF_MAX) {
            return;
        }

        // STEP 02: 拷貝 + 旗標統一在 portMUX 臨界區（擋住 main loop drain 與
        //          下一輪 GATT write 同時碰 buffer 的 race）
        portENTER_CRITICAL(&g_rx_mux);
        if (g_rx_ready) {
            // STEP 02.01: 已有未排空的訊息就丟新的（smoke 場景單訊息節奏，
            //             main loop 20ms 內就會排空，撞到表示 caller 連發）
            portEXIT_CRITICAL(&g_rx_mux);
            return;
        }
        memcpy(g_rx_buf, value.data(), len);
        g_rx_len = len;
        g_rx_ready = true;
        portEXIT_CRITICAL(&g_rx_mux);
    }
};

/**
 * 印出 time_sync state 當下值，搭配 heartbeat 給 dev 看時鐘有沒有走。
 */
static void print_ts_heartbeat() {
    uint64_t now = ems::time_sync_current_epoch_ms(&g_ts_state, millis());
    Serial.printf("[HEARTBEAT] synced=%d epoch_ms=%llu uptime_s=%lu\n",
                  (int)g_ts_state.synced,
                  (unsigned long long)now,
                  (unsigned long)(millis() / 1000));
}

/**
 * 從 g_rx_buf 取出訊息 → time_sync_handle → 若有 ACK 就 notify。
 * 假設呼叫前已確認 g_rx_ready=true 且 g_rx_len 有效。
 */
static void process_pending_rx() {
    // STEP 01: 原子取出 buffer 後立即清旗標。portMUX spinlock 擋住 GATT task
    //          同時 onWrite 寫入；同時禁中斷讓臨界區乾淨（memcpy ~µs 等級）
    uint8_t local_buf[RX_BUF_MAX];
    size_t local_len;
    portENTER_CRITICAL(&g_rx_mux);
    local_len = g_rx_len;
    memcpy(local_buf, g_rx_buf, local_len);
    g_rx_ready = false;
    portEXIT_CRITICAL(&g_rx_mux);

    // STEP 02: 印收到的原文（注意 BLE 端可能不會以 '\n' 結尾，所以印之前先 cap）
    Serial.printf("[RX] %u bytes: ", (unsigned)local_len);
    Serial.write(local_buf, local_len);
    Serial.println();

    // STEP 03: 呼叫純邏輯 lib 處理（測試已驗 19 cases）
    char ack_buf[ACK_BUF_MAX];
    size_t ack_len = 0;
    ems::TimeSyncResult r = ems::time_sync_handle(
        &g_ts_state,
        local_buf, local_len,
        millis(),
        /*rtc_present=*/false,  // smoke 階段無 DS3231 整合
        ack_buf, sizeof(ack_buf), &ack_len);

    // STEP 04: 印 handler 結果便於 dev 對照
    const char* result_str =
        (r == ems::TimeSyncResult::Applied)  ? "Applied"  :
        (r == ems::TimeSyncResult::Rejected) ? "Rejected" :
                                               "ParseError";
    Serial.printf("[HANDLE] result=%s ack_len=%u\n", result_str, (unsigned)ack_len);

    // STEP 05: ParseError 規格規定不送 ACK（spec §2.3）
    if (ack_len == 0) {
        return;
    }

    // STEP 06: notify ACK 出去
    if (g_tx_char != nullptr && g_client_connected) {
        g_tx_char->setValue(reinterpret_cast<uint8_t*>(ack_buf), ack_len);
        g_tx_char->notify();
        Serial.printf("[TX] ");
        Serial.write(reinterpret_cast<uint8_t*>(ack_buf), ack_len);
        Serial.println();
    } else {
        Serial.println("[TX] skip — no connected client");
    }
}

void setup() {
    // STEP 01: 啟 USB-CDC Serial 並等列舉（避免 [BOOT] 前的列印被吃掉）
    Serial.begin(115200);
    uint32_t wait_start = millis();
    while (!Serial && millis() - wait_start < USB_CDC_ENUM_TIMEOUT_MS) {
        delay(10);
    }

    Serial.println();
    Serial.println("============================================");
    Serial.println("EMS DoseSync Pro — BLE Time Sync Smoke Test");
    Serial.printf("  Device name: %s\n", BLE_DEVICE_NAME);
    Serial.printf("  NUS service: %s\n", NUS_SERVICE_UUID);
    Serial.println("============================================");

    // STEP 02: 初始化 time_sync state
    ems::time_sync_init(&g_ts_state);

    // STEP 03: 初始化 BLE
    BLEDevice::init(BLE_DEVICE_NAME);
    g_server = BLEDevice::createServer();
    g_server->setCallbacks(new ServerCallbacks());

    // STEP 04: 建立 NUS service 與兩個 characteristics
    BLEService* service = g_server->createService(NUS_SERVICE_UUID);

    // STEP 04.01: RX（write）
    BLECharacteristic* rx_char = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx_char->setCallbacks(new RxCallbacks());

    // STEP 04.02: TX（notify）
    g_tx_char = service->createCharacteristic(
        NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    // CCCD 描述符：讓 client 可以訂閱 notify
    g_tx_char->addDescriptor(new BLE2902());

    // STEP 05: 啟動 service + advertise
    service->start();
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(NUS_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(BLE_CONN_INTERVAL_MIN);
    advertising->setMaxPreferred(BLE_CONN_INTERVAL_MAX);
    BLEDevice::startAdvertising();

    Serial.println("[READY] BLE advertising started, waiting for connection...");
}

void loop() {
    // STEP 01: 連線狀態變化時印 log（由 callback 設旗標，這裡只負責印）
    static bool last_connected = false;
    if (g_client_connected != last_connected) {
        Serial.printf("[BLE] client %s\n", g_client_connected ? "connected" : "disconnected");
        last_connected = g_client_connected;
    }

    // STEP 02: 處理待處理的 RX 訊息
    if (g_rx_ready) {
        process_pending_rx();
    }

    // STEP 03: 定期 heartbeat（讓 dev 看時鐘有沒有走、state 有沒有對到）
    if (millis() - g_last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
        g_last_heartbeat_ms = millis();
        print_ts_heartbeat();
    }

    delay(LOOP_SLICE_MS);
}
