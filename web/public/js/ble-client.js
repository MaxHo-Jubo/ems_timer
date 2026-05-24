/**
 * ble-client.js — Web Bluetooth NUS 傳輸層（Phase F F-4a）
 *
 * 職責單純：BLE 連線 / 斷線 / 收送位元組。不解析任何 JSON 訊息語意 ——
 * 訊息分流（time_sync_ack / pair_status / case_sync）由 connect.js 負責。
 *
 * 框架約定：NUS 上的邏輯訊息以 '\n' 分隔（對齊 docs/ble-time-sync-protocol.md §1
 * 與韌體 ble_rx_queue 切句慣例）。TX notify 可能被 BLE MTU 切成多段，本模組
 * 累積位元組直到遇到 '\n' 才視為一則完整訊息，藉此自然完成 chunked payload 重組。
 */

// === NUS（Nordic UART Service）UUID，對齊 phase-f-web-validation-plan.md §8.2 ===
// Service：NUS 主服務
export const NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
// RX characteristic：App → Device 寫入方向
export const NUS_RX_CHAR = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
// TX characteristic：Device → App notify 方向
export const NUS_TX_CHAR = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

// RX 重組緩衝上限（字元數，_rxBuffer 為 JS 字串）：超過此值仍未見 '\n' 視為協定異常，
// 避免記憶體無限膨脹。case_sync payload（~30 events）約數千字元，64K 留有兩個量級餘裕。
const RX_BUFFER_MAX_CHARS = 64 * 1024;

/**
 * 判斷目前瀏覽器是否支援 Web Bluetooth API。
 * @returns {boolean} true = 支援（Chromium 系列桌面版）
 */
export function isBleSupported() {
  return typeof navigator !== "undefined" && Boolean(navigator.bluetooth);
}

/**
 * Web Bluetooth NUS 連線封裝。一個實例對應一次裝置連線。
 *
 * 對外 callback（由使用端指派）：
 * - onLine(text)      收到一則完整 '\n' 訊息（已去除換行）
 * - onChunk(byteLen)  每次收到 TX notify（供「接收中」進度顯示）
 * - onDisconnect()    非預期 GATT 斷線（呼叫 disconnect() 主動斷線不觸發）
 * - onError(reason)   傳輸層異常，reason 為 "rx_overflow"（緩衝溢位）
 *                     或 "callback_error: ..."（使用端 callback 拋出例外）
 */
export class BleClient {
  constructor() {
    // BluetoothDevice 實例（requestDevice 結果）
    this._device = null;
    // GATT RX characteristic（寫入裝置用）
    this._rxChar = null;
    // GATT TX characteristic（訂閱裝置 notify 用）
    this._txChar = null;
    // TX 行重組緩衝：累積文字直到出現 '\n'
    this._rxBuffer = "";
    // 串流式 UTF-8 解碼器，處理跨 notify 切斷的多位元組字元
    this._decoder = new TextDecoder("utf-8");
    // 標記是否為主動斷線，用於抑制 onDisconnect callback
    this._intentionalDisconnect = false;
    // 對外 callback，預設 null
    this.onLine = null;
    this.onChunk = null;
    this.onDisconnect = null;
    this.onError = null;
    // 綁定 this 的事件處理器參考（addEventListener 需穩定參考）
    this._boundNotify = (e) => this._handleNotification(e);
    this._boundDisconnect = () => this._handleDisconnect();
  }

  /**
   * 是否已建立 GATT 連線。
   * @returns {boolean}
   */
  get connected() {
    return Boolean(this._device && this._device.gatt && this._device.gatt.connected);
  }

  /**
   * 觸發 Web Bluetooth 掃描並建立 NUS 連線。必須由使用者手勢（click）呼叫。
   * @returns {Promise<string>} 連線成功的裝置名稱（無名稱則回傳裝置 id）
   * @throws 使用者取消選擇、連線失敗或缺 NUS service 時拋出
   */
  async connect() {
    // STEP 01: 清掉前一次殘留的裝置斷線監聽（連線中途失敗時可能未經 _handleDisconnect 清理）
    if (this._device) {
      this._device.removeEventListener("gattserverdisconnected", this._boundDisconnect);
    }

    // STEP 02: 以裝置名前綴 "DSP-" 過濾掃描（韌體廣播名 DSP-0001，對齊 F-4b 接真 ESP32）。
    // optionalServices 仍須列 NUS：連線後 getPrimaryService 才有權限存取該 service。
    this._device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: "DSP-" }],
      optionalServices: [NUS_SERVICE],
    });

    // STEP 03: 註冊斷線監聽並重置主動斷線旗標
    this._intentionalDisconnect = false;
    this._device.addEventListener("gattserverdisconnected", this._boundDisconnect);

    // STEP 04: 建立 GATT 連線並取得 RX / TX characteristic
    const server = await this._device.gatt.connect();
    const service = await server.getPrimaryService(NUS_SERVICE);
    this._rxChar = await service.getCharacteristic(NUS_RX_CHAR);
    this._txChar = await service.getCharacteristic(NUS_TX_CHAR);

    // STEP 05: 重置重組緩衝與解碼器（清掉前次 session 殘留的串流狀態）後訂閱 TX notify
    this._rxBuffer = "";
    this._decoder = new TextDecoder("utf-8");
    await this._txChar.startNotifications();
    this._txChar.addEventListener("characteristicvaluechanged", this._boundNotify);
    console.log("[DEBUG] startNotifications done, listener attached on TX", this._txChar.uuid);

    return this._device.name || this._device.id;
  }

  /**
   * 序列化物件為單行 JSON 並寫入裝置 RX characteristic。
   * @param {object} obj - 要送出的訊息物件（會被 JSON.stringify）
   * @returns {Promise<void>}
   * @throws 未連線或寫入失敗時拋出
   */
  async send(obj) {
    // STEP 01: 連線狀態防護
    if (!this._rxChar) {
      throw new Error("BLE 未連線，無法送出");
    }
    // STEP 02: 補 '\n' 行結尾（對齊韌體 ble_rx_queue 切句慣例）後寫入
    const line = JSON.stringify(obj) + "\n";
    const bytes = new TextEncoder().encode(line);
    await this._rxChar.writeValueWithoutResponse(bytes);
  }

  /**
   * 主動中斷 GATT 連線。此路徑不會觸發 onDisconnect callback。
   */
  disconnect() {
    // STEP 01: 標記為主動斷線，抑制 onDisconnect
    this._intentionalDisconnect = true;
    // STEP 02: 僅在連線中才呼叫 disconnect，避免重複觸發
    if (this.connected) {
      this._device.gatt.disconnect();
    }
  }

  /**
   * 安全呼叫對外 callback：callback 內部例外不外溢至 Web Bluetooth 事件分派，
   * 改路由至 onError，避免單一訊息的例外中斷整批後續訊息處理。
   * @param {Function|null} callback - 要呼叫的 callback（未指派視為成功略過）
   * @param {*} arg - 傳入 callback 的單一參數
   * @returns {boolean} true = 正常（或無 callback）；false = callback 拋出例外
   */
  _safeInvoke(callback, arg) {
    // STEP 01: 未指派 callback 視為成功略過
    if (typeof callback !== "function") {
      return true;
    }
    // STEP 02: 呼叫並攔截例外，例外路由至 onError
    try {
      callback(arg);
      return true;
    } catch (err) {
      if (typeof this.onError === "function") {
        this.onError(`callback_error: ${err instanceof Error ? err.message : err}`);
      }
      return false;
    }
  }

  /**
   * TX notify 事件處理：解碼位元組、累積至緩衝、依 '\n' 切出完整訊息。
   * @param {Event} event - characteristicvaluechanged 事件
   */
  _handleNotification(event) {
    // STEP 01: 取出本次 notify 的原始位元組並回拋位元組數
    const view = event.target.value;
    const bytes = new Uint8Array(view.buffer);
    console.log("[DEBUG] notify", bytes.length, "bytes:",
                JSON.stringify(new TextDecoder().decode(bytes)));
    this._safeInvoke(this.onChunk, bytes.length);

    // STEP 02: 串流解碼後併入重組緩衝
    this._rxBuffer += this._decoder.decode(bytes, { stream: true });

    // STEP 03: 緩衝溢位防護 —— 一直收不到 '\n' 視為協定異常，回拋 onError
    if (this._rxBuffer.length > RX_BUFFER_MAX_CHARS) {
      this._rxBuffer = "";
      if (typeof this.onError === "function") {
        this.onError("rx_overflow");
      }
      return;
    }

    // STEP 04: 逐一切出 '\n' 結尾的完整訊息並回拋
    let newlineIdx = this._rxBuffer.indexOf("\n");
    while (newlineIdx >= 0) {
      // STEP 04.01: 取出一行並去除前後空白
      const lineText = this._rxBuffer.slice(0, newlineIdx).trim();
      this._rxBuffer = this._rxBuffer.slice(newlineIdx + 1);
      // STEP 04.02: 非空行回拋給使用端；callback 拋例外則中止本批，避免例外外溢
      if (lineText && !this._safeInvoke(this.onLine, lineText)) {
        return;
      }
      newlineIdx = this._rxBuffer.indexOf("\n");
    }
  }

  /**
   * GATT 斷線事件處理：移除事件監聽、清理 characteristic 參考，
   * 非主動斷線才回拋 onDisconnect。
   */
  _handleDisconnect() {
    // STEP 01: 移除事件監聽，避免同一裝置物件重連時 listener 疊加
    if (this._txChar) {
      this._txChar.removeEventListener("characteristicvaluechanged", this._boundNotify);
    }
    if (this._device) {
      this._device.removeEventListener("gattserverdisconnected", this._boundDisconnect);
    }
    // STEP 02: 清理 characteristic 參考
    this._rxChar = null;
    this._txChar = null;
    this._rxBuffer = "";
    // STEP 03: 主動斷線不通知使用端；非預期斷線才回拋
    if (!this._intentionalDisconnect && typeof this.onDisconnect === "function") {
      this.onDisconnect();
    }
  }
}
