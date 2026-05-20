/**
 * ble-client.test.mjs — BleClient 傳輸層單元測試
 *
 * 用 Node.js 內建 node:test + node:assert，零 npm 依賴（對齊 web/ 無框架慣例）。
 * 涵蓋 BleClient 最易出錯、也最難手動驗證的純邏輯：TX notify 的 '\n' 行重組、
 * 緩衝溢位防護、UTF-8 多位元組跨段解碼、callback 例外隔離。
 *
 * 執行：cd web && npm test
 */

import test from "node:test";
import assert from "node:assert/strict";
import { BleClient } from "../public/js/ble-client.js";

/**
 * 產生一個假的 characteristicvaluechanged 事件，模擬一段 TX notify。
 * @param {Uint8Array|string} data - 位元組陣列，或字串（以 UTF-8 編碼）
 * @returns {{target:{value:{buffer:ArrayBuffer}}}} 假事件物件
 */
function notifyEvent(data) {
  // STEP 01: 字串轉 UTF-8 位元組
  const bytes = typeof data === "string" ? new TextEncoder().encode(data) : data;
  // STEP 02: 包成 _handleNotification 預期的 event.target.value.buffer 形狀
  return { target: { value: { buffer: bytes.buffer } } };
}

test("chunked：訊息跨 3 段 notify、僅末段帶換行 → onLine 觸發一次完整訊息", () => {
  const client = new BleClient();
  const lines = [];
  client.onLine = (t) => lines.push(t);

  const msg = '{"type":"case_sync","case_id":"t1"}';
  client._handleNotification(notifyEvent(msg.slice(0, 12)));
  client._handleNotification(notifyEvent(msg.slice(12, 24)));
  client._handleNotification(notifyEvent(msg.slice(24) + "\n"));

  assert.deepEqual(lines, [msg]);
});

test("單段 notify 內含多則訊息 → onLine 逐則觸發", () => {
  const client = new BleClient();
  const lines = [];
  client.onLine = (t) => lines.push(t);

  client._handleNotification(notifyEvent('{"a":1}\n{"b":2}\n{"c":3}\n'));

  assert.deepEqual(lines, ['{"a":1}', '{"b":2}', '{"c":3}']);
});

test("UTF-8 多位元組字元被切在 notify 邊界 → 串流解碼正確還原", () => {
  const client = new BleClient();
  const lines = [];
  client.onLine = (t) => lines.push(t);

  const msg = '{"device_name":"安康91"}';
  const bytes = new TextEncoder().encode(msg + "\n");
  // 第 18 byte 落在「安」的 UTF-8 三位元組中間，刻意製造跨段切斷
  client._handleNotification(notifyEvent(bytes.slice(0, 18)));
  client._handleNotification(notifyEvent(bytes.slice(18)));

  assert.deepEqual(lines, [msg]);
});

test("單獨換行與純空白行不觸發 onLine", () => {
  const client = new BleClient();
  const lines = [];
  client.onLine = (t) => lines.push(t);

  client._handleNotification(notifyEvent("\n\n"));
  client._handleNotification(notifyEvent("   \n"));

  assert.deepEqual(lines, []);
});

test("緩衝累積超過上限且無換行 → onError('rx_overflow') 並清空緩衝", () => {
  const client = new BleClient();
  let errReason = null;
  const lines = [];
  client.onError = (r) => { errReason = r; };
  client.onLine = (t) => lines.push(t);

  // 推送 70000 字元無換行（上限 64K）
  client._handleNotification(notifyEvent("x".repeat(70000)));
  assert.equal(errReason, "rx_overflow");
  assert.deepEqual(lines, []);

  // 緩衝已清空：後續正常訊息仍可解析
  client._handleNotification(notifyEvent('{"ok":1}\n'));
  assert.deepEqual(lines, ['{"ok":1}']);
});

test("onLine 拋例外 → 例外不外溢、onError 收到 callback_error、中止本批後續訊息", () => {
  const client = new BleClient();
  let errReason = null;
  let callCount = 0;
  client.onError = (r) => { errReason = r; };
  client.onLine = () => { callCount += 1; throw new Error("boom"); };

  assert.doesNotThrow(() => {
    client._handleNotification(notifyEvent('{"a":1}\n{"b":2}\n'));
  });
  assert.equal(callCount, 1, "onLine 拋例外後應中止本批，不處理第二則");
  assert.equal(errReason, "callback_error: boom");
});

test("onChunk 收到本次 notify 的位元組數", () => {
  const client = new BleClient();
  const chunkSizes = [];
  client.onChunk = (n) => chunkSizes.push(n);
  client.onLine = () => {};

  client._handleNotification(notifyEvent("hello\n"));   // 6 ASCII 位元組
  client._handleNotification(notifyEvent("安\n"));        // 3 + 1 = 4 位元組

  assert.deepEqual(chunkSizes, [6, 4]);
});

test("send() 未連線時拒絕（rejected promise）", async () => {
  const client = new BleClient();
  await assert.rejects(() => client.send({ type: "ping" }), /未連線/);
});
