/**
 * case.html 案件詳細頁
 * 4 頁籤對齊 SoT §17.2: [交班摘要] [完整總覽] [Timeline] [備註]
 *
 * F-9.2 ~ F-9.7 整合於此檔
 */

import { fetchCaseDetail } from "./api-client.js";
import { EVT, EVT_LABEL, isSuppEvent, eventCategory } from "./event-types.js";

const TABS = ["summary", "overview", "timeline", "notes"];

const state = {
  caseId: null,
  caseData: null,
  events: [],
  notes: null,
  activeTab: "summary",
};

// === Utility ===

function escapeHtml(text) {
  if (text === null || text === undefined) return "";
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function formatDateTime(epochMs) {
  if (!epochMs || typeof epochMs !== "number") return "—";
  const d = new Date(epochMs);
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function formatHHMM(epochMs) {
  if (!epochMs || typeof epochMs !== "number") return "—";
  const d = new Date(epochMs);
  const pad = (n) => String(n).padStart(2, "0");
  return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

// elapsed ms → "MM:SS" 或 "HH:MM:SS"
function formatElapsed(ms) {
  if (ms === null || ms === undefined || typeof ms !== "number") return "—";
  const totalSec = Math.floor(ms / 1000);
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  const pad = (n) => String(n).padStart(2, "0");
  if (h > 0) {
    return `${pad(h)}:${pad(m)}:${pad(s)}`;
  }
  return `${pad(m)}:${pad(s)}`;
}

// HH:MM 用於總時間（SoT §17.3 範例格式）
function formatDurationHM(startMs, endMs) {
  if (!startMs || !endMs) return "—";
  const diff = endMs - startMs;
  if (diff < 0) return "—";
  const totalMin = Math.floor(diff / 60000);
  const h = Math.floor(totalMin / 60);
  const m = totalMin % 60;
  const pad = (n) => String(n).padStart(2, "0");
  return `${pad(h)}:${pad(m)}`;
}

// === 計算交班摘要（對齊 SoT §17.3） ===

function buildSummary(caseData, events) {
  const epiTotal = events
    .filter((e) => eventCategory(e.event_type) === "epi")
    .reduce((sum, e) => sum + (e.count ?? 1), 0);

  const shockTotal = events
    .filter((e) => eventCategory(e.event_type) === "shock")
    .reduce((sum, e) => sum + (e.count ?? 1), 0);

  const amioTotal = events
    .filter((e) => e.event_type === EVT.AMIODARONE)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);

  const epiLocals = events.filter((e) => e.event_type === EVT.EPI_LOCAL);
  const shockLocals = events.filter((e) => e.event_type === EVT.SHOCK_LOCAL);

  const firstEpiTime = epiLocals.length > 0
    ? formatHHMM(Math.min(...epiLocals.map((e) => e.timestamp_ms)))
    : "—";
  const lastEpiTime = epiLocals.length > 0
    ? formatHHMM(Math.max(...epiLocals.map((e) => e.timestamp_ms)))
    : "—";
  const lastShockTime = shockLocals.length > 0
    ? formatHHMM(Math.max(...shockLocals.map((e) => e.timestamp_ms)))
    : "—";

  const suppEpiPre = events
    .filter((e) => e.event_type === EVT.EPI_PRE_HANDOVER)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);
  const suppShockPre = events
    .filter((e) => e.event_type === EVT.SHOCK_PRE_HANDOVER)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);
  const suppEpiPure = events
    .filter((e) => e.event_type === EVT.EPI_PURE_SUPP)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);
  const suppShockPure = events
    .filter((e) => e.event_type === EVT.SHOCK_PURE_SUPP)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);

  const title = caseData.mode === "training" ? "Training 交班摘要" : "OHCA 交班摘要";
  const duration = formatDurationHM(caseData.started_at, caseData.ended_at);

  // 對齊 SoT §17.3 範例格式（純文字、可直接複製）
  const lines = [
    title,
    "",
    `總時間：${duration}`,
    `EPI：${epiTotal}`,
    `電擊：${shockTotal}`,
    `Amiodarone：${amioTotal}`,
    "",
    `第一次本機 EPI：${firstEpiTime}`,
    `最後本機 EPI：${lastEpiTime}`,
    `最後本機電擊：${lastShockTime}`,
  ];

  const suppLines = [];
  if (suppEpiPre > 0) suppLines.push(`接手前 EPI ×${suppEpiPre}`);
  if (suppShockPre > 0) suppLines.push(`接手前電擊 ×${suppShockPre}`);
  if (suppEpiPure > 0) suppLines.push(`純補登 EPI ×${suppEpiPure}`);
  if (suppShockPure > 0) suppLines.push(`純補登電擊 ×${suppShockPure}`);

  if (suppLines.length > 0) {
    lines.push("", "補登：", ...suppLines);
  }

  return lines.join("\n");
}

// === Render: Meta Header ===

function renderMeta() {
  const el = document.getElementById("caseMeta");
  if (!state.caseData) {
    el.innerHTML = `<div class="case-meta__loading">案件不存在</div>`;
    return;
  }
  const c = state.caseData;
  el.innerHTML = `
    <div class="case-meta__field">
      <span class="case-meta__field-label">案件 ID</span>
      <span class="case-meta__field-value">${escapeHtml(c.case_id)}</span>
    </div>
    <div class="case-meta__field">
      <span class="case-meta__field-label">模式</span>
      <span class="case-meta__field-value ${c.mode === "ohca" ? "text-red" : "text-amber"}">${escapeHtml(c.mode.toUpperCase())}</span>
    </div>
    <div class="case-meta__field">
      <span class="case-meta__field-label">裝置</span>
      <span class="case-meta__field-value">${escapeHtml(c.device_id ?? "—")}</span>
    </div>
    <div class="case-meta__field">
      <span class="case-meta__field-label">事件數</span>
      <span class="case-meta__field-value">${state.events.length}</span>
    </div>
  `;
  document.title = `${c.case_id} - EMS Timer 案件詳細`;
}

// === Render: Summary Tab (F-9.3 + F-9.7 複製) ===

function renderSummary() {
  const panel = document.getElementById("panel-summary");
  if (!state.caseData) {
    panel.innerHTML = `<div class="tab-panel__placeholder">案件不存在</div>`;
    return;
  }
  const summary = buildSummary(state.caseData, state.events);
  panel.innerHTML = `
    <pre class="summary-text">${escapeHtml(summary)}</pre>
    <button class="copy-btn" id="copySummaryBtn">一鍵複製交班摘要</button>
  `;
  document.getElementById("copySummaryBtn").addEventListener("click", () => {
    copyToClipboard(summary, "copySummaryBtn");
  });
}

// === Render: Overview Tab (F-9.4 對齊 §17.4 的 13 欄) ===

function renderOverview() {
  const panel = document.getElementById("panel-overview");
  if (!state.caseData) {
    panel.innerHTML = `<div class="tab-panel__placeholder">案件不存在</div>`;
    return;
  }
  const c = state.caseData;
  const totalDuration = formatDurationHM(c.started_at, c.ended_at);

  const epiCount = state.events
    .filter((e) => eventCategory(e.event_type) === "epi")
    .reduce((sum, e) => sum + (e.count ?? 1), 0);
  const shockCount = state.events
    .filter((e) => eventCategory(e.event_type) === "shock")
    .reduce((sum, e) => sum + (e.count ?? 1), 0);
  const amioCount = state.events
    .filter((e) => e.event_type === EVT.AMIODARONE)
    .reduce((sum, e) => sum + (e.count ?? 1), 0);

  // §17.4 的 13 欄
  const fields = [
    { label: "案件 ID", value: c.case_id },
    { label: "模式", value: c.mode.toUpperCase() },
    { label: "開始時間", value: formatDateTime(c.started_at) },
    { label: "結束時間", value: formatDateTime(c.ended_at) },
    { label: "總時間", value: totalDuration },
    { label: "EPI 詳細", value: `${epiCount} 次（本機 + 補登）` },
    { label: "電擊詳細", value: `${shockCount} 次（本機 + 補登）` },
    { label: "Amiodarone", value: `${amioCount} 次` },
    { label: "Timeline", value: `共 ${state.events.length} 個事件` },
    { label: "同步資訊", value: "已同步至 D1" },
    { label: "裝置名稱", value: c.device_name ?? "—" },
    { label: "裝置 ID", value: c.device_id ?? "—" },
    { label: "韌體版本", value: c.fw_version ?? "—" },
    { label: "同步時間", value: formatDateTime(c.synced_at) },
  ];

  panel.innerHTML = `
    <div class="overview-grid">
      ${fields.map((f) => `
        <div class="overview-field">
          <div class="overview-field__label">${escapeHtml(f.label)}</div>
          <div class="overview-field__value">${escapeHtml(f.value)}</div>
        </div>
      `).join("")}
    </div>
  `;
}

// === Render: Timeline Tab (F-9.5) ===

function renderTimeline() {
  const panel = document.getElementById("panel-timeline");
  if (state.events.length === 0) {
    panel.innerHTML = `<div class="tab-panel__placeholder">無事件</div>`;
    return;
  }

  // 已從後端依 elapsed_ms 升序排
  const items = state.events.map((e) => {
    const cat = eventCategory(e.event_type);
    const isSupp = isSuppEvent(e.event_type);
    // 補登事件絕對時間 = "-"（對齊 pm-dev-spec §四 Phase B）
    const timeCell = isSupp
      ? `<span class="timeline-time timeline-time--placeholder">—</span>`
      : `<span class="timeline-time">${formatHHMM(e.timestamp_ms)}</span>`;
    const elapsedCell = isSupp
      ? `<span class="timeline-elapsed timeline-time--placeholder">—</span>`
      : `<span class="timeline-elapsed">+${formatElapsed(e.elapsed_ms)}</span>`;
    const cls = [
      "timeline-item",
      isSupp ? "timeline-item--supp" : "",
      `timeline-item--${cat}`,
    ].filter(Boolean).join(" ");
    return `
      <li class="${cls}">
        ${timeCell}
        ${elapsedCell}
        <span class="timeline-label">${escapeHtml(EVT_LABEL[e.event_type] ?? `type ${e.event_type}`)}</span>
        <span class="timeline-count">×${e.count ?? 1}</span>
      </li>
    `;
  });

  panel.innerHTML = `
    <ul class="timeline-list">${items.join("")}</ul>
    <button class="copy-btn" id="copyTimelineBtn">完整複製 Timeline</button>
  `;
  document.getElementById("copyTimelineBtn").addEventListener("click", () => {
    const text = state.events.map((e) => {
      const supp = isSuppEvent(e.event_type);
      const time = supp ? "—" : formatHHMM(e.timestamp_ms);
      const elapsed = supp ? "—" : `+${formatElapsed(e.elapsed_ms)}`;
      return `${time}  ${elapsed}  ${EVT_LABEL[e.event_type] ?? `type ${e.event_type}`}  ×${e.count ?? 1}`;
    }).join("\n");
    copyToClipboard(text, "copyTimelineBtn");
  });
}

// === Render: Notes Tab (F-9.6 對齊 §17.5) ===

let notesAutosaveTimer = null;

function renderNotes() {
  const panel = document.getElementById("panel-notes");
  const n = state.notes ?? {};
  const hospitalArrivalLocal = n.hospital_arrival_at
    ? formatDateTimeLocal(n.hospital_arrival_at)
    : "";

  panel.innerHTML = `
    <div class="notes-form">
      <div class="notes-form__warning">
        ⚠️ 備註只儲存於雲端，不會回寫裝置端原始紀錄（SoT §17.5）
      </div>

      <div class="notes-form__field">
        <label class="notes-form__field-label" for="noteHospital">到院時間</label>
        <input class="notes-form__input" type="datetime-local" id="noteHospital" value="${hospitalArrivalLocal}" />
      </div>

      <div class="notes-form__field">
        <label class="notes-form__field-label">ROSC</label>
        <div class="notes-form__checkbox-row">
          <input type="checkbox" id="noteRosc" ${n.rosc ? "checked" : ""} />
          <label for="noteRosc">已恢復自主循環（ROSC）</label>
        </div>
      </div>

      <div class="notes-form__field">
        <label class="notes-form__field-label" for="noteHandover">交班對象</label>
        <input class="notes-form__input" type="text" id="noteHandover" value="${escapeHtml(n.handover_to ?? "")}" placeholder="例：XX 醫院 急診室" />
      </div>

      <div class="notes-form__field">
        <label class="notes-form__field-label" for="noteSituation">特殊狀況</label>
        <textarea class="notes-form__textarea" id="noteSituation" placeholder="病史 / 過敏 / 現場狀況...">${escapeHtml(n.special_situation ?? "")}</textarea>
      </div>

      <div class="notes-form__field">
        <label class="notes-form__field-label" for="noteOther">其他備註</label>
        <textarea class="notes-form__textarea" id="noteOther">${escapeHtml(n.other ?? "")}</textarea>
      </div>

      <div class="notes-status" id="notesStatus">
        ${n.updated_at ? `最後修改：${formatDateTime(n.updated_at)}` : "尚未填寫"}
      </div>
    </div>
  `;

  // STEP 01: 綁所有欄位 → debounce 1s 自動存
  const fields = ["noteHospital", "noteRosc", "noteHandover", "noteSituation", "noteOther"];
  fields.forEach((id) => {
    document.getElementById(id).addEventListener("input", scheduleAutosave);
    document.getElementById(id).addEventListener("change", scheduleAutosave);
  });
}

function formatDateTimeLocal(epochMs) {
  // datetime-local input 需要 "YYYY-MM-DDTHH:MM"
  const d = new Date(epochMs);
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function scheduleAutosave() {
  if (notesAutosaveTimer) clearTimeout(notesAutosaveTimer);
  const status = document.getElementById("notesStatus");
  status.className = "notes-status notes-status--saving";
  status.textContent = "編輯中...";
  notesAutosaveTimer = setTimeout(saveNotes, 1000);
}

async function saveNotes() {
  const status = document.getElementById("notesStatus");
  const hospitalInput = document.getElementById("noteHospital").value;
  const payload = {
    hospital_arrival_at: hospitalInput ? new Date(hospitalInput).getTime() : null,
    rosc: document.getElementById("noteRosc").checked,
    handover_to: document.getElementById("noteHandover").value || null,
    special_situation: document.getElementById("noteSituation").value || null,
    other: document.getElementById("noteOther").value || null,
  };

  try {
    const res = await fetch(`/api/cases/${encodeURIComponent(state.caseId)}/notes`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const result = await res.json();
    if (!result.ok) {
      status.className = "notes-status notes-status--error";
      status.textContent = `儲存失敗：${result.error ?? "Unknown"}`;
      return;
    }
    status.className = "notes-status notes-status--saved";
    status.textContent = `已儲存 ${formatDateTime(result.updated_at)}`;
  } catch (err) {
    status.className = "notes-status notes-status--error";
    status.textContent = `儲存失敗：${err.message}`;
  }
}

// === Tab Switching ===

function switchTab(tab) {
  if (!TABS.includes(tab)) tab = "summary";
  state.activeTab = tab;
  document.querySelectorAll(".tab").forEach((btn) => {
    btn.setAttribute("aria-selected", btn.dataset.tab === tab ? "true" : "false");
  });
  document.querySelectorAll(".tab-panel").forEach((panel) => {
    panel.classList.toggle("hidden", panel.id !== `panel-${tab}`);
  });
  // URL hash 同步
  if (location.hash !== `#${tab}`) {
    history.replaceState(null, "", `${location.pathname}${location.search}#${tab}`);
  }
}

function bindTabs() {
  document.querySelectorAll(".tab").forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.dataset.tab));
  });
  window.addEventListener("hashchange", () => {
    const tab = location.hash.replace(/^#/, "");
    if (TABS.includes(tab)) switchTab(tab);
  });
}

// === Copy Utility ===

async function copyToClipboard(text, btnId) {
  try {
    await navigator.clipboard.writeText(text);
    const btn = document.getElementById(btnId);
    const original = btn.textContent;
    btn.textContent = "✓ 已複製";
    btn.classList.add("copy-btn--copied");
    setTimeout(() => {
      btn.textContent = original;
      btn.classList.remove("copy-btn--copied");
    }, 1500);
  } catch (err) {
    alert(`複製失敗：${err.message}`);
  }
}

// === Load + Init ===

async function loadCase() {
  // 先吃 ?id=，再 fallback 到 hash 前段（保留給未來 /case/:id 路由）
  const params = new URLSearchParams(location.search);
  state.caseId = params.get("id");

  if (!state.caseId) {
    document.getElementById("caseMeta").innerHTML = `
      <div class="case-meta__loading text-red">缺少 ?id= 參數</div>
    `;
    return;
  }

  const result = await fetchCaseDetail(state.caseId);
  if (!result.ok) {
    document.getElementById("caseMeta").innerHTML = `
      <div class="case-meta__loading text-red">載入失敗：${escapeHtml(result.error ?? "Unknown")}</div>
    `;
    return;
  }

  state.caseData = result.case;
  state.events = result.events ?? [];

  // STEP 01: 取備註（沒有就空殼）
  try {
    const notesRes = await fetch(`/api/cases/${encodeURIComponent(state.caseId)}/notes`);
    const notesData = await notesRes.json();
    state.notes = notesData.ok ? notesData.notes : null;
  } catch {
    state.notes = null;
  }

  renderMeta();
  renderSummary();
  renderOverview();
  renderTimeline();
  renderNotes();
}

// === Bootstrap ===

bindTabs();
const initialTab = location.hash.replace(/^#/, "");
switchTab(TABS.includes(initialTab) ? initialTab : "summary");
loadCase();
