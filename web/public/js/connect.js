/**
 * index.html 連線頁邏輯（F-6 階段：mock 版）
 *
 * F-4a 會把 simulateSync() 換成真 Web Bluetooth 流程。
 * 目前點按鈕只跑假流程驗證 UI + POST /api/cases 鏈路。
 */

import { postCase, fetchCases } from "./api-client.js";
import { EVT } from "./event-types.js";

const els = {
  btn: document.getElementById("connectBtn"),
  pairCard: document.getElementById("pairCard"),
  pairTtl: document.getElementById("pairTtl"),
  pairDigits: document.querySelectorAll(".pair-code__digit"),
  progressLog: document.getElementById("progressLog"),
  lastSync: document.getElementById("lastSync"),
};

const steps = [];

function log(text, status = "active") {
  const id = steps.length;
  steps.push({ id, text, status });
  renderLog();
  return id;
}

function updateStep(id, status) {
  const step = steps.find((s) => s.id === id);
  if (!step) return;
  step.status = status;
  renderLog();
}

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

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function resetSteps() {
  steps.length = 0;
  renderLog();
}

function setButtonState(state, label) {
  els.btn.className = "connect-button";
  if (state === "syncing") {
    els.btn.classList.add("connect-button--syncing");
  } else if (state === "done") {
    els.btn.classList.add("connect-button--done");
  } else if (state === "error") {
    els.btn.classList.add("connect-button--error");
  }
  if (label) {
    els.btn.textContent = label;
  }
}

// STEP 01: 配對碼 4 格 input 自動跳格
function bindPairCodeInputs() {
  els.pairDigits.forEach((input, idx) => {
    input.addEventListener("input", () => {
      if (input.value.length === 1 && idx < els.pairDigits.length - 1) {
        els.pairDigits[idx + 1].focus();
      }
    });
    input.addEventListener("keydown", (e) => {
      if (e.key === "Backspace" && input.value === "" && idx > 0) {
        els.pairDigits[idx - 1].focus();
      }
    });
  });
}

// STEP 02: 產生 mock case payload（豐富版，給 F-9 §17 UI demo 用）
// 包含本機 + 接手前補登 + 純補登 + Amiodarone，可在詳細頁看到完整摘要效果
function buildMockPayload() {
  const now = Date.now();
  const started = now - 15 * 60 * 1000;
  const caseId = `mock-${now}`;

  // STEP 02.01: 模擬一筆完整 OHCA case
  // 接手前補登 → 本機 EPI/電擊 4 分鐘節奏 → Amiodarone → 純補登
  const events = [
    // 補登（無時間戳）— elapsed_ms 為 0 表示無 timing
    { type: EVT.EPI_PRE_HANDOVER, timestamp_ms: 0, elapsed_ms: 0, count: 1, actual_time_null: true },
    { type: EVT.SHOCK_PRE_HANDOVER, timestamp_ms: 0, elapsed_ms: 0, count: 2, actual_time_null: true },

    // 本機紀錄（有時間戳）
    { type: EVT.EPI_LOCAL, timestamp_ms: started + 60_000, elapsed_ms: 60_000, count: 1 },
    { type: EVT.SHOCK_LOCAL, timestamp_ms: started + 90_000, elapsed_ms: 90_000, count: 1 },
    { type: EVT.EPI_LOCAL, timestamp_ms: started + 300_000, elapsed_ms: 300_000, count: 1 },
    { type: EVT.AMIODARONE, timestamp_ms: started + 360_000, elapsed_ms: 360_000, count: 1 },
    { type: EVT.SHOCK_LOCAL, timestamp_ms: started + 480_000, elapsed_ms: 480_000, count: 1 },
    { type: EVT.EPI_LOCAL, timestamp_ms: started + 540_000, elapsed_ms: 540_000, count: 1 },
    { type: EVT.SHOCK_LOCAL, timestamp_ms: started + 720_000, elapsed_ms: 720_000, count: 1 },
    { type: EVT.EPI_LOCAL, timestamp_ms: started + 780_000, elapsed_ms: 780_000, count: 1 },

    // 純補登（事後想起忘了打的）
    { type: EVT.EPI_PURE_SUPP, timestamp_ms: 0, elapsed_ms: 0, count: 1, actual_time_null: true },
  ];

  return {
    type: "case_sync",
    case_id: caseId,
    mode: "ohca",
    device_name: "Mock 安康91",
    device_id: "DSP-MOCK",
    fw_version: "v0.0.0-mock",
    started_at_ms: started,
    ended_at_ms: now,
    events,
  };
}

// STEP 03: 假同步流程（F-4a 會換成真 Web BT）
async function simulateSync() {
  resetSteps();
  setButtonState("syncing", "同步中...");
  els.pairCard.hidden = false;

  const s1 = log("[mock] 開始連線", "active");
  await sleep(400);
  updateStep(s1, "done");

  const s2 = log("[mock] 顯示配對碼 1234（請手動輸入）", "active");
  await sleep(500);
  updateStep(s2, "done");

  const payload = buildMockPayload();
  const s3 = log(`[mock] 傳輸資料中（${payload.events.length} events）`, "active");
  await sleep(600);
  updateStep(s3, "done");

  const s4 = log("寫入 Cloudflare D1", "active");
  const result = await postCase(payload);

  if (!result.ok) {
    updateStep(s4, "error");
    log(`寫入失敗：${result.error ?? "Unknown"}`, "error");
    setButtonState("error", "失敗，點此重試");
    return;
  }

  updateStep(s4, "done");
  log(`完成：${result.case_id}（${result.event_count} events）`, "done");
  setButtonState("done", "✓ 同步完成（再點一次模擬新案）");

  // STEP 03.01: 更新「最近同步」摘要
  await refreshLastSync();
}

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

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// STEP 01: init
bindPairCodeInputs();
els.btn.addEventListener("click", () => {
  simulateSync().catch((err) => {
    log(`例外：${err.message}`, "error");
    setButtonState("error", "錯誤，點此重試");
  });
});

refreshLastSync();
