# G2.1/G2.2 技術簡報：BLE 裝置名稱寫入（濃縮版，供小 context session 直接照做）

> 2026-07-15 由主 harness 的探索 agent 萃取。目的：實作者**不需要再大量閱讀原始碼**，照本簡報的落點與範例直接動手。

## 架構關鍵認知（先讀）

`ble_nus` 是 ESP32 Arduino BLE 的薄包裝，實作 Nordic UART Service（NUS）：一個 RX char（client 寫入）＋一個 TX char（notify）。目前所有 client→device 指令走 RX char 傳 JSON，在 `main.cpp:309 on_ble_rx()` 用 `doc["type"]` 分派（如 `time_sync`）。「裝置名稱寫入」採 **新增一個 GATT write characteristic** 的做法（plan §2.2.2 字面要求）。

## 1. characteristic 註冊方式（抄 RX char 範例）

- UUID 是 file-private，定義在 `ble_nus.cpp:17-19`（`NUS_SERVICE_UUID` / `NUS_RX_UUID` / `NUS_TX_UUID`）。新增 char 在此加一個新 UUID 常數。
- 寫入 callback 型別：`ble_nus.h:39`
  ```cpp
  using BleNusRxCallback = void (*)(const uint8_t* data, size_t len);
  ```
- callback class 範例（`ble_nus.cpp:42-51`）：
  ```cpp
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
  ```
- 註冊範例（`ble_nus.cpp:79-82`）：
  ```cpp
  BLECharacteristic* rx_char = svc->createCharacteristic(
      NUS_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx_char->setCallbacks(new NusRxCallbacks());
  ```
- callback 靠 singleton `g_instance`（`ble_nus.cpp:22`）轉回實例（ESP32 BLE callback 不帶 user context）。
- **GATT callback 內只准做輕量事**：比照 `_on_rx_write`（`ble_nus.cpp:201-211`，portMUX 保護）memcpy＋設旗標；裝置名稱寫檔屬可接受的同步呼叫（低頻操作）。

## 2. native 測試策略（重要：沒有 BLE mock 層）

**`BleNus` 類別（含 Arduino BLE 依賴）不可在 native 測**，只在 ARDUINO 環境編譯。native 策略＝把純邏輯抽成可獨立測的函式，只測純函式：

- 可測部分：名稱驗證/長度裁切純函式＋用 `mock_fs_*` 驗證持久化。
- mock filesystem API（`ems_settings.h:173-175`）：`mock_fs_clear()` / `mock_fs_write()` / `mock_fs_read()`。
- 測試檔開頭結構照抄既有慣例（`test_ble_rx_queue/test_main.cpp:15-22`）：
  ```cpp
  #include <unity.h>
  #include <cstring>
  #include "ems_settings.h"
  void setUp()    {}
  void tearDown() {}
  static void test_xxx() { TEST_ASSERT_TRUE(...); }
  // main(): UNITY_BEGIN(); RUN_TEST(...); return UNITY_END();
  ```
- 抄寫對象：`test/test_settings/`。**不要在 native 測試 include ble_nus.h。**

## 3. sync_send.cpp 裝置名稱現況（G2.2 落點）

- `sync_send.cpp:143`：`js_meta.device_name = SYNC_DEVICE_NAME;`
- `SYNC_DEVICE_NAME` 是編譯期常數（`app_globals.h:190`，值來自 `SYNC_DEVICE_ID = "DSP-0001"`，`app_globals.h:189`）——目前 hard-code。
- G2.2 改法：改為呼叫 `settings_get_device_name(buf, sizeof buf)` 取動態名稱（注意 buf 生命週期要涵蓋序列化期間）。

## 4. ems_settings 既有簽名（直接用，勿重造）

皆在 `ems_settings.h` 的 `#ifdef ARDUINO` 內：

```cpp
bool settings_set_device_name(const char* name);              // :101  寫 LittleFS /config/device_name.txt
bool settings_get_device_name(char* buf, size_t buf_size);    // :109  讀，不存在回預設「未命名」
```

常數：`DEVICE_NAME_MAX_LEN 32`（:38）、`DEVICE_NAME_DEFAULT "未命名"`（:39）、`DEVICE_NAME_FILE`（:40）。
寫入 payload 先裁到 `DEVICE_NAME_MAX_LEN-1` 再交給 `settings_set_device_name`。

## 5. 初始化與掛載點

- `g_ble.begin(BLE_DEVICE_NAME)` 呼叫點：`main.cpp:534`（`g_ble` 宣告 `main.cpp:157`）。
- **新 characteristic 加在 `BleNus::begin()` 內**：`createService`（:77）→ RX char（:79）→ TX char（:84）→【新 name char 插這】→ `svc->start()`（:89）→ advertising（:91+）。

## 實作落點清單

1. `ble_nus.cpp`：加 UUID 常數（:19 後）、加 callback class（:51 後）、`begin()` 內註冊（:87 後）
2. callback 內：裁切長度 → `settings_set_device_name()` → 成功回 ack（比照既有 TX notify 慣例）
3. `sync_send.cpp:143`：改讀動態名稱（G2.2）
4. 測試：抄 `test_settings` 模式＋`mock_fs_*`，不碰 BleNus
