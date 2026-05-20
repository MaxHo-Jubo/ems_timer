/**
 * connect.js — index.html 連線頁邏輯（Phase F F-4a：真 Web Bluetooth）
 *
 * 同步流程（對齊 phase-f-web-validation-plan.md §4 + SoT §16.4 / §16.5）：
 *   連線 → 送 time_sync → 輸入配對碼 → 送 pair_verify → 等 pair_status
 *        → 等裝置主鍵 → 接收 case_sync payload → POST /api/cases → 完成
 *
 * 裝置端（韌體 / nRF Connect mock）→ web 的 TX 訊息：
 *   - time_sync_ack  對時回覆（docs/ble-time-sync-protocol.md §2.2）
 *   - pair_status    配對結果 { result: accepted|rejected|locked_out, remaining_attempts }
 *   - case_sync      單案 payload（pm-dev-spec §14.1）
 */

import { BleClient, isBleSupported } from "./ble-client.js";
import { postCase, fetchCases } from "./api-client.js";

// === 常數 ===
// 配對碼位數（SoT §16.4）
const PAIR_CODE_LEN = 4;
// 配對碼格式驗證 regex（恰 PAIR_CODE_LEN 位數字）
const PAIR_CODE_RE = new RegExp(`^\\d{${PAIR_CODE_LEN}}$`);
// 配對碼有效秒數（SoT §16.4）
const PAIR_TTL_SEC = 120;
// 配對碼倒數每跳一格的間隔（毫秒）
const PAIR_TTL_TICK_MS = 1000;
// 配對成功後等待 case_sync 的看門狗逾時（毫秒）。
// 涵蓋裝置端 30s 主鍵 timeout + nRF Connect mock 由人手動推 JSON 的操作時間。
const SYNC_WAIT_TIMEOUT_MS = 120_000;
// time_sync req_id 隨機段範圍（2^16，產生 4 位 16 進位）
const REQ_ID_HEX_RANGE = 0x10000;

// === 同步流程階段 ===
const Phase = Object.freeze({
  IDLE: "idle",                          // 尚未開始
  CONNECTING: "connecting",              // BLE 掃描 / 連線中
  AWAITING_PAIR_INPUT: "pair_input",     // 等使用者輸入配對碼
  VERIFYING: "verifying",                // 已送 pair_verify，等 pair_status
  AWAITING_MAIN_KEY: "main_key",         // 配對成功，等裝置端按主鍵
  RECEIVING: "receiving",                // 正在接收 case_sync payload
  UPLOADING: "uploading",                // 寫入 Cloudflare D1 中
  DONE: "done",                          // 同步完成
  ERROR: "error",                        // 流程失敗
});

// === DOM 參考 ===
const els = {
  btn: document.getElementById("connectBtn"),
  browserWarning: document.getElementById("browserWarning"),
  pairCard: document.getElementById("pairCard"),
  pairTtl: document.getElementById("pairTtl"),
  pairDigits: document.querySelectorAll(".pair-code__digit"),
  pairSubmitBtn: document.getElementById("pairSubmitBtn"),
  mainKeyCard: document.getElementById("mainKeyCard"),
  progressLog: document.getElementById("progressLog"),
  lastSync: document.getElementById("lastSync"),
};

// === 執行期狀態 ===
const state = {
  phase: Phase.IDLE,              // 目前流程階段
  ble: new BleClient(),           // BLE 傳輸層實例
  pairTtlTimer: null,             // 配對碼倒數 setInterval id
  pairTtlRemaining: 0,            // 配對碼剩餘秒數
  syncWatchdog: null,             // 等待 case_sync 的看門狗 setTimeout id
  rxByteCount: 0,                 // 接收階段累積位元組數（進度顯示用）
  pairInputStepId: null,          // 「請輸入配對碼」progress step id
  verifyStepId: null,             // 「配對碼已送出」progress step id
  mainKeyStepId: null,            // 「請按裝置主鍵」progress step id
  receivingStepId: null,          // 「接收資料中」progress step id
};

// === 進度日誌 ===
// 每筆 step：{ id, text, status }，status ∈ active / done / error
const steps = [];

/**
 * 新增一筆進度日誌。
 * @param {string} text - 顯示文字
 * @param {string} status - active / done / error
 * @returns {number} 該筆 step 的 id
 */
function log(text, status = "active") {
  const id = steps.length;
  steps.push({ id, text, status });
  renderLog();
  return id;
}

/**
 * 更新指定 step 的狀態。
 * @param {number} id - step id
 * @param {string} status - active / done / error
 */
function updateStep(id, status) {
  const step = steps.find((s) => s.id === id);
  if (!step) {
    return;
  }
  step.status = status;
  renderLog();
}

/**
 * 將 steps 陣列重繪到進度區。
 */
function renderLog() {
  els.progressLog.innerHTML = steps.map((s) => {
    const cls = s.status === "done"
      ? "progress-log__step--done"
      : s.status === "error"
        ? "progress-log__step--error"
        : "progress-log__step--active";
    return `<li class="${cls}">${escapeHtml(s.text)}</li>`;
  }).join("");
}

/**
 * 清空進度日誌。
 */
function resetSteps() {
  steps.length = 0;
  renderLog();
}

/**
 * HTML 跳脫，防止裝置端字串注入頁面。
 * @param {*} text - 任意值
 * @returns {string} 已跳脫字串
 */
function escapeHtml(text) {
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/**
 * 切換主連線按鈕外觀。
 * @param {string} stateName - syncing / done / error / 空字串=預設
 * @param {string} [label] - 按鈕文字（省略則不改）
 */
function setButtonState(stateName, label) {
  els.btn.className = "connect-button";
  if (stateName === "syncing") {
    els.btn.classList.add("connect-button--syncing");
  } else if (stateName === "done") {
    els.btn.classList.add("connect-button--done");
  } else if (stateName === "error") {
    els.btn.classList.add("connect-button--error");
  }
  if (label) {
    els.btn.textContent = label;
  }
}

/**
 * 設定流程階段，並依階段啟用 / 停用主連線按鈕。
 * @param {string} phase - Phase 列舉值
 */
function setPhase(phase) {
  // STEP 01: 更新階段
  state.phase = phase;
  // STEP 02: 僅在閒置 / 完成 / 失敗階段允許再次點擊連線按鈕
  const clickable = phase === Phase.IDLE || phase === Phase.DONE || phase === Phase.ERROR;
  els.btn.disabled = !clickable;
}

// === 計時器管理 ===

/**
 * 啟動配對碼 120 秒倒數；歸零時若仍在配對階段則判定逾時。
 */
function startPairTtl() {
  // STEP 01: 重置倒數秒數並即時顯示
  state.pairTtlRemaining = PAIR_TTL_SEC;
  els.pairTtl.textContent = String(state.pairTtlRemaining);
  // STEP 02: 每秒遞減
  state.pairTtlTimer = setInterval(() => {
    state.pairTtlRemaining -= 1;
    els.pairTtl.textContent = String(Math.max(0, state.pairTtlRemaining));
    // STEP 02.01: 歸零 → 停止倒數，配對階段則收尾為逾時錯誤
    if (state.pairTtlRemaining <= 0) {
      stopPairTtl();
      if (state.phase === Phase.AWAITING_PAIR_INPUT || state.phase === Phase.VERIFYING) {
        finishWithError(`配對碼已逾時（${PAIR_TTL_SEC} 秒），請重新連線`);
      }
    }
  }, PAIR_TTL_TICK_MS);
}

/**
 * 停止配對碼倒數。
 */
function stopPairTtl() {
  if (state.pairTtlTimer !== null) {
    clearInterval(state.pairTtlTimer);
    state.pairTtlTimer = null;
  }
}

/**
 * 清除等待 case_sync 的看門狗。
 */
function clearWatchdog() {
  if (state.syncWatchdog !== null) {
    clearTimeout(state.syncWatchdog);
    state.syncWatchdog = null;
  }
}

// === 流程主線 ===

/**
 * 重置上一輪殘留的計時器、連線與 UI，準備開始新一輪同步。
 */
function resetForNewSession() {
  // STEP 01: 清理計時器與既有連線
  stopPairTtl();
  clearWatchdog();
  if (state.ble.connected) {
    state.ble.disconnect();
  }
  // STEP 02: 重置進度日誌與卡片
  resetSteps();
  els.pairCard.hidden = true;
  els.mainKeyCard.hidden = true;
  clearPairDigits();
  // STEP 03: 重置計數器與 step id
  state.rxByteCount = 0;
  state.pairInputStepId = null;
  state.verifyStepId = null;
  state.mainKeyStepId = null;
  state.receivingStepId = null;
}

/**
 * 主流程入口：掃描連線 → 送對時 → 進入配對碼輸入。
 * @returns {Promise<void>}
 */
async function startConnect() {
  // STEP 01: 重置 UI 並進入連線中階段
  resetForNewSession();
  setPhase(Phase.CONNECTING);
  setButtonState("syncing", "連線中...");
  const connectStep = log("掃描 BLE 裝置（請在彈窗選擇）", "active");

  // STEP 02: 觸發 Web Bluetooth 連線
  try {
    const deviceName = await state.ble.connect();
    updateStep(connectStep, "done");
    log(`已連線：${escapeHtml(deviceName)}`, "done");
  } catch (err) {
    updateStep(connectStep, "error");
    finishWithError(`連線失敗：${errMsg(err)}`);
    return;
  }

  // STEP 03: 連線後立刻送對時（對齊 ble-time-sync-protocol.md §7.1，fire-and-forget）
  sendTimeSync();

  // STEP 04: 顯示配對碼輸入區
  enterPairInput();
}

/**
 * 送出 time_sync 訊息（不阻塞配對流程，ACK 由 onLine 處理）。
 */
function sendTimeSync() {
  // STEP 01: 組訊息 —— tz_offset_min 為「UTC 偏移分鐘」，台北 = 480
  const reqId = `ts-${Math.floor(Math.random() * REQ_ID_HEX_RANGE).toString(16).padStart(4, "0")}`;
  const msg = {
    type: "time_sync",
    epoch_ms: Date.now(),
    tz_offset_min: -new Date().getTimezoneOffset(),
    req_id: reqId,
  };
  // STEP 02: 非阻塞送出，成敗皆只記進度日誌
  state.ble.send(msg)
    .then(() => { log("已送出對時請求", "done"); })
    .catch((err) => { log(`對時送出失敗：${errMsg(err)}`, "error"); });
}

/**
 * 顯示配對碼輸入卡片並啟動 TTL 倒數。
 */
function enterPairInput() {
  // STEP 01: 進入等待輸入階段
  setPhase(Phase.AWAITING_PAIR_INPUT);
  // STEP 02: 顯示卡片、清空輸入框、聚焦第一格
  els.pairCard.hidden = false;
  els.pairSubmitBtn.disabled = false;
  clearPairDigits();
  els.pairDigits[0].focus();
  // STEP 03: 提示並啟動倒數
  state.pairInputStepId = log("請輸入裝置螢幕上的 4 位配對碼", "active");
  startPairTtl();
}

/**
 * 收集 4 格配對碼並送出 pair_verify。
 */
function submitPairCode() {
  // STEP 01: 僅在等待輸入階段可送出
  if (state.phase !== Phase.AWAITING_PAIR_INPUT) {
    return;
  }
  // STEP 02: 收集並驗證為 4 位數字
  const code = Array.from(els.pairDigits).map((d) => d.value.trim()).join("");
  if (!PAIR_CODE_RE.test(code)) {
    log(`配對碼需為 ${PAIR_CODE_LEN} 位數字`, "error");
    return;
  }
  // STEP 03: 送出 pair_verify（格式對齊韌體 main.cpp on_ble_rx STEP 03）
  setPhase(Phase.VERIFYING);
  els.pairSubmitBtn.disabled = true;
  state.verifyStepId = log(`配對碼 ${code} 已送出，等待裝置驗證`, "active");
  state.ble.send({ type: "pair_verify", input: code }).catch((err) => {
    updateStep(state.verifyStepId, "error");
    finishWithError(`配對碼送出失敗：${errMsg(err)}`);
  });
}

/**
 * 處理裝置端 pair_status 回覆。
 * @param {object} doc - 解析後的 pair_status 訊息
 */
function handlePairStatus(doc) {
  // STEP 01: 僅在配對 / 驗證階段處理
  if (state.phase !== Phase.VERIFYING && state.phase !== Phase.AWAITING_PAIR_INPUT) {
    return;
  }
  const result = typeof doc.result === "string" ? doc.result : "";
  if (state.verifyStepId !== null) {
    updateStep(state.verifyStepId, result === "accepted" ? "done" : "error");
  }

  // STEP 02: 配對成功 → 進入等待主鍵
  if (result === "accepted") {
    enterAwaitingMainKey();
    return;
  }

  // STEP 03: 鎖定 → 直接收尾（對齊計畫 §10 三次失敗 lockout）
  if (result === "locked_out") {
    finishWithError("配對失敗已鎖定，請重新連線");
    return;
  }

  // STEP 04: 配對碼錯誤 → 允許重新輸入
  const remaining = Number.isFinite(doc.remaining_attempts) ? doc.remaining_attempts : null;
  log(
    remaining !== null ? `配對碼錯誤，剩餘 ${remaining} 次` : "配對碼錯誤，請重試",
    "error",
  );
  setPhase(Phase.AWAITING_PAIR_INPUT);
  els.pairSubmitBtn.disabled = false;
  clearPairDigits();
  els.pairDigits[0].focus();
}

/**
 * 進入「等待裝置端按主鍵」階段（對齊 SoT §16.5 雙保險）。
 */
function enterAwaitingMainKey() {
  // STEP 01: 停止配對碼倒數，切換階段與卡片
  stopPairTtl();
  setPhase(Phase.AWAITING_MAIN_KEY);
  els.pairCard.hidden = true;
  els.mainKeyCard.hidden = false;
  // STEP 01.01: 配對碼輸入指示步驟已完成，標記為 done
  if (state.pairInputStepId !== null) {
    updateStep(state.pairInputStepId, "done");
  }
  log("配對成功", "done");
  state.mainKeyStepId = log("請按裝置主鍵開始同步", "active");
  // STEP 02: 啟動看門狗，避免裝置端遲不傳輸導致 UI 卡死
  state.syncWatchdog = setTimeout(() => {
    finishWithError("等待逾時：裝置端未開始傳輸（請確認已按主鍵）");
  }, SYNC_WAIT_TIMEOUT_MS);
}

/**
 * BLE TX 收到原始位元組：在等待主鍵 / 接收階段更新接收進度。
 * @param {number} byteLen - 本次 notify 的位元組數
 */
function onChunk(byteLen) {
  // STEP 01: 等待主鍵時收到首段資料 → 視為主鍵已按下，轉入接收中
  if (state.phase === Phase.AWAITING_MAIN_KEY) {
    setPhase(Phase.RECEIVING);
    els.mainKeyCard.hidden = true;
    // STEP 01.01: 主鍵等待指示步驟已完成，標記為 done
    if (state.mainKeyStepId !== null) {
      updateStep(state.mainKeyStepId, "done");
    }
    state.rxByteCount = 0;
    state.receivingStepId = log("接收資料中...", "active");
  }
  // STEP 02: 接收階段累計位元組數並更新進度文字
  if (state.phase === Phase.RECEIVING && state.receivingStepId !== null) {
    state.rxByteCount += byteLen;
    const step = steps.find((s) => s.id === state.receivingStepId);
    if (step) {
      step.text = `接收資料中... ${state.rxByteCount} bytes`;
      renderLog();
    }
  }
}

/**
 * BLE TX 收到一則完整 '\n' 訊息：解析 JSON 並依 type 分流。
 * @param {string} text - 去除換行的訊息字串
 */
function onLine(text) {
  // STEP 01: 嘗試解析 JSON
  let doc = null;
  try {
    doc = JSON.parse(text);
  } catch (e) {
    // STEP 01.01: 接收階段收到無法解析的內容 → payload 損毀
    if (state.phase === Phase.RECEIVING) {
      if (state.receivingStepId !== null) {
        updateStep(state.receivingStepId, "error");
      }
      finishWithError("payload 解析失敗：JSON 格式錯誤");
    } else {
      log("收到非 JSON 訊息（已略過）", "active");
    }
    return;
  }

  // STEP 02: 依訊息 type 分流
  const type = doc && typeof doc.type === "string" ? doc.type : "";
  if (type === "time_sync_ack") {
    log(`對時 ACK：source=${escapeHtml(String(doc.source ?? "—"))}`, "done");
    return;
  }
  if (type === "pair_status") {
    handlePairStatus(doc);
    return;
  }
  if (type === "case_sync") {
    // handleCaseSync 為 async，這裡 fire-and-forget，例外須自行收尾
    handleCaseSync(doc).catch((err) => {
      finishWithError(`同步失敗：${errMsg(err)}`);
    });
    return;
  }
  log(`收到未知訊息 type='${escapeHtml(type)}'`, "active");
}

/**
 * 處理完整 case_sync payload：基本驗證後 POST 到 /api/cases。
 * @param {object} doc - 解析後的 case_sync 訊息
 * @returns {Promise<void>}
 */
async function handleCaseSync(doc) {
  // STEP 01: 僅在等待主鍵 / 接收階段接受 case 資料
  if (state.phase !== Phase.RECEIVING && state.phase !== Phase.AWAITING_MAIN_KEY) {
    return;
  }
  clearWatchdog();

  // STEP 02: 基本欄位驗證（系統邊界，不信任外部資料）
  if (typeof doc.case_id !== "string" || !Array.isArray(doc.events)) {
    if (state.receivingStepId !== null) {
      updateStep(state.receivingStepId, "error");
    }
    finishWithError("payload 格式錯誤：缺少 case_id 或 events");
    return;
  }
  if (state.receivingStepId !== null) {
    updateStep(state.receivingStepId, "done");
  }
  log(`接收完成（${doc.events.length} 個事件）`, "done");

  // STEP 03: 寫入 Cloudflare D1
  setPhase(Phase.UPLOADING);
  els.mainKeyCard.hidden = true;
  const uploadStep = log("寫入 Cloudflare D1", "active");
  const result = await postCase(doc);
  if (!result.ok) {
    updateStep(uploadStep, "error");
    finishWithError(`寫入失敗：${escapeHtml(result.error ?? "Unknown")}`);
    return;
  }

  // STEP 04: 完成收尾
  updateStep(uploadStep, "done");
  log(
    `完成：${escapeHtml(result.case_id)}（${result.event_count ?? doc.events.length} events）`,
    "done",
  );
  setPhase(Phase.DONE);
  setButtonState("done", "✓ 同步完成（點此同步新案）");
  await refreshLastSync();
}

/**
 * BLE 非預期斷線：流程進行中則收尾為錯誤。
 */
function onDisconnect() {
  // STEP 01: 流程進行中的斷線才視為錯誤；閒置 / 完成 / 已是錯誤則忽略
  const activePhases = [
    Phase.CONNECTING,
    Phase.AWAITING_PAIR_INPUT,
    Phase.VERIFYING,
    Phase.AWAITING_MAIN_KEY,
    Phase.RECEIVING,
    Phase.UPLOADING,
  ];
  if (activePhases.includes(state.phase)) {
    finishWithError("BLE 連線已中斷");
  }
}

/**
 * BLE 傳輸層異常 callback。
 * @param {string} reason - 異常代碼："rx_overflow"（接收緩衝溢位）
 *                          或 "callback_error: ..."（使用端 callback 例外）
 */
function onBleError(reason) {
  // STEP 01: 緩衝溢位 —— 裝置端訊息未正確以換行結尾
  if (reason === "rx_overflow") {
    finishWithError("接收緩衝溢位：裝置端訊息未正確以換行結尾");
    return;
  }
  // STEP 02: 其他未預期傳輸異常
  finishWithError(`BLE 傳輸異常：${reason}`);
}

/**
 * 收尾為錯誤狀態：清理計時器、記錯誤日誌、切換按鈕。
 * @param {string} message - 錯誤訊息
 */
function finishWithError(message) {
  // STEP 01: 清理計時器
  stopPairTtl();
  clearWatchdog();
  // STEP 02: 記錯誤、隱藏等待卡片、切換階段與按鈕
  log(message, "error");
  els.mainKeyCard.hidden = true;
  setPhase(Phase.ERROR);
  setButtonState("error", "失敗，點此重試");
}

/**
 * 將任意例外轉為可讀字串。
 * @param {*} err - 例外物件或值
 * @returns {string}
 */
function errMsg(err) {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}

/**
 * 拉取最新一筆同步紀錄並更新「最近同步」摘要區。
 * @returns {Promise<void>}
 */
async function refreshLastSync() {
  const result = await fetchCases("synced_at_desc");
  if (!result.ok || !result.cases || result.cases.length === 0) {
    els.lastSync.textContent = "—";
    return;
  }
  const latest = result.cases[0];
  const t = new Date(latest.synced_at).toLocaleString("zh-TW");
  els.lastSync.innerHTML = `
    <div>${escapeHtml(t)}</div>
    <div class="text-mute" style="font-size: 12px; margin-top: 4px;">
      ${escapeHtml(latest.device_id ?? "—")} ｜
      ${escapeHtml(latest.mode ?? "—")} ｜
      ${latest.event_count ?? 0} events
    </div>
  `;
}

// === 配對碼輸入框互動 ===

/**
 * 清空 4 格配對碼輸入框。
 */
function clearPairDigits() {
  els.pairDigits.forEach((d) => { d.value = ""; });
}

/**
 * 綁定 4 格配對碼輸入框：自動跳格、Backspace 倒退、Enter 送出。
 */
function bindPairCodeInputs() {
  els.pairDigits.forEach((input, idx) => {
    // STEP 01: 輸入一碼後自動跳下一格
    input.addEventListener("input", () => {
      if (input.value.length === 1 && idx < els.pairDigits.length - 1) {
        els.pairDigits[idx + 1].focus();
      }
    });
    // STEP 02: Backspace 在空格時倒退；Enter 直接送出
    input.addEventListener("keydown", (e) => {
      if (e.key === "Backspace" && input.value === "" && idx > 0) {
        els.pairDigits[idx - 1].focus();
      } else if (e.key === "Enter") {
        submitPairCode();
      }
    });
  });
}

// === 初始化 ===

// STEP 01: 瀏覽器支援檢查 —— 不支援則顯示警告並停用連線按鈕
if (!isBleSupported()) {
  els.browserWarning.hidden = false;
  els.btn.disabled = true;
}

// STEP 02: 綁定 BLE 傳輸層 callback
state.ble.onLine = onLine;
state.ble.onChunk = onChunk;
state.ble.onDisconnect = onDisconnect;
state.ble.onError = onBleError;

// STEP 03: 綁定 UI 事件
bindPairCodeInputs();
els.btn.addEventListener("click", () => {
  startConnect().catch((err) => {
    finishWithError(`例外：${errMsg(err)}`);
  });
});
els.pairSubmitBtn.addEventListener("click", submitPairCode);

// STEP 04: 載入時拉一次最近同步摘要
refreshLastSync();
