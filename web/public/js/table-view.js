/**
 * cases.html 列表頁
 * F-9.9: OHCA / Training 分列表 tabs（對齊 SoT §17.8）
 * F-9.7: 列表加「一鍵複製交班摘要」按鈕
 * F-9.8: 列表加「刪除」按鈕（D1-only，對齊 SoT §17.7）
 */

import { fetchCases, fetchCaseDetail, deleteCase } from "./api-client.js";
import { EVT, eventCategory } from "./event-types.js";

const SORT_MAP = {
  started_at: { asc: "started_at_asc", desc: "started_at_desc" },
  synced_at: { asc: "synced_at_asc", desc: "synced_at_desc" },
  device_id: { asc: "device_id_asc", desc: "device_id_asc" },
};

const state = {
  mode: "ohca",
  sortColumn: "started_at",
  sortDir: "desc",
  cases: [],
};

function escapeHtml(text) {
  if (text === null || text === undefined) return "";
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function formatTime(epochMs) {
  if (!epochMs || typeof epochMs !== "number") return "—";
  const d = new Date(epochMs);
  const pad = (n) => String(n).padStart(2, "0");
  return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function render() {
  const tbody = document.getElementById("caseTableBody");
  const countEl = document.getElementById("caseCount");
  countEl.textContent = String(state.cases.length);

  if (state.cases.length === 0) {
    tbody.innerHTML = `<tr><td colspan="5" class="case-table__empty">${state.mode === "ohca" ? "尚無 OHCA 案件" : "尚無 Training 紀錄"}</td></tr>`;
    return;
  }

  const rows = state.cases.map((c) => {
    const idEsc = encodeURIComponent(c.case_id);
    return `
    <tr data-case-id="${escapeHtml(c.case_id)}">
      <td>
        ${formatTime(c.started_at)}
        <div class="timestamp">${c.started_at ?? "—"}</div>
      </td>
      <td>
        ${escapeHtml(c.device_id ?? "—")}
        <div class="timestamp">${escapeHtml(c.device_name ?? "")}</div>
      </td>
      <td>${c.event_count ?? 0}</td>
      <td>
        ${formatTime(c.synced_at)}
        <div class="timestamp">${c.synced_at ?? "—"}</div>
      </td>
      <td>
        <a class="copy-btn" style="margin-top:0; padding:6px 10px; font-size:11px; display:inline-block; text-decoration:none;"
           href="./case.html?id=${idEsc}">詳細</a>
        <button class="copy-btn js-copy-summary" style="margin-top:0; padding:6px 10px; font-size:11px;"
                data-case-id="${escapeHtml(c.case_id)}">複製摘要</button>
        <button class="copy-btn js-delete" style="margin-top:0; padding:6px 10px; font-size:11px; background:transparent; color:var(--accent-red); border-color:var(--accent-red);"
                data-case-id="${escapeHtml(c.case_id)}">刪除</button>
      </td>
    </tr>
  `;
  }).join("");

  tbody.innerHTML = rows;
  bindRowActions();
  updateHeaderArrows();
}

function updateHeaderArrows() {
  document.querySelectorAll("th[data-sort-key]").forEach((th) => {
    th.removeAttribute("data-sort-active");
    if (th.dataset.sortKey === state.sortColumn) {
      th.setAttribute("data-sort-active", state.sortDir);
    }
  });
}

async function reload() {
  const sortKey = SORT_MAP[state.sortColumn]?.[state.sortDir] ?? "started_at_desc";
  const result = await fetchCases(sortKey, state.mode);
  if (!result.ok) {
    document.getElementById("caseTableBody").innerHTML = `
      <tr><td colspan="5" class="case-table__empty text-red">
        載入失敗：${escapeHtml(result.error ?? "Unknown")}
      </td></tr>
    `;
    return;
  }
  state.cases = result.cases ?? [];
  render();
}

function switchMode(mode) {
  if (mode !== "ohca" && mode !== "training") return;
  state.mode = mode;
  document.querySelectorAll("[data-mode]").forEach((btn) => {
    btn.setAttribute("aria-selected", btn.dataset.mode === mode ? "true" : "false");
  });
  if (location.hash !== `#${mode}`) {
    history.replaceState(null, "", `${location.pathname}#${mode}`);
  }
  reload();
}

function bindModeTabs() {
  document.querySelectorAll("[data-mode]").forEach((btn) => {
    btn.addEventListener("click", () => switchMode(btn.dataset.mode));
  });
}

function bindHeaders() {
  document.querySelectorAll("th[data-sort-key]").forEach((th) => {
    th.addEventListener("click", () => {
      const key = th.dataset.sortKey;
      if (state.sortColumn === key) {
        state.sortDir = state.sortDir === "asc" ? "desc" : "asc";
      } else {
        state.sortColumn = key;
        state.sortDir = "desc";
      }
      reload();
    });
  });
}

function bindRowActions() {
  document.querySelectorAll(".js-copy-summary").forEach((btn) => {
    btn.addEventListener("click", async (e) => {
      e.preventDefault();
      const caseId = btn.dataset.caseId;
      await copySummary(caseId, btn);
    });
  });
  document.querySelectorAll(".js-delete").forEach((btn) => {
    btn.addEventListener("click", async (e) => {
      e.preventDefault();
      const caseId = btn.dataset.caseId;
      await confirmAndDelete(caseId);
    });
  });
}

// === F-9.7: 列表頁一鍵複製交班摘要 ===
// 與 case-detail.js 計算邏輯一致，這裡再寫一份避免循環依賴
async function copySummary(caseId, btn) {
  const original = btn.textContent;
  btn.textContent = "...";
  try {
    const result = await fetchCaseDetail(caseId);
    if (!result.ok) {
      alert(`載入失敗：${result.error}`);
      btn.textContent = original;
      return;
    }
    const text = buildSummaryText(result.case, result.events ?? []);
    await navigator.clipboard.writeText(text);
    btn.textContent = "✓ 已複製";
    btn.classList.add("copy-btn--copied");
    setTimeout(() => {
      btn.textContent = original;
      btn.classList.remove("copy-btn--copied");
    }, 1500);
  } catch (err) {
    btn.textContent = original;
    alert(`複製失敗：${err.message}`);
  }
}

function buildSummaryText(caseData, events) {
  const epiTotal = events.filter((e) => eventCategory(e.event_type) === "epi")
    .reduce((s, e) => s + (e.count ?? 1), 0);
  const shockTotal = events.filter((e) => eventCategory(e.event_type) === "shock")
    .reduce((s, e) => s + (e.count ?? 1), 0);
  const amioTotal = events.filter((e) => e.event_type === EVT.AMIODARONE)
    .reduce((s, e) => s + (e.count ?? 1), 0);

  const formatHHMM = (ms) => {
    if (!ms) return "—";
    const d = new Date(ms);
    const pad = (n) => String(n).padStart(2, "0");
    return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  };
  const formatDurationHM = (start, end) => {
    if (!start || !end) return "—";
    const diff = end - start;
    if (diff < 0) return "—";
    const min = Math.floor(diff / 60000);
    const pad = (n) => String(n).padStart(2, "0");
    return `${pad(Math.floor(min / 60))}:${pad(min % 60)}`;
  };

  const epiLocals = events.filter((e) => e.event_type === EVT.EPI_LOCAL);
  const shockLocals = events.filter((e) => e.event_type === EVT.SHOCK_LOCAL);

  const title = caseData.mode === "training" ? "Training 交班摘要" : "OHCA 交班摘要";
  const lines = [
    title,
    "",
    `總時間：${formatDurationHM(caseData.started_at, caseData.ended_at)}`,
    `EPI：${epiTotal}`,
    `電擊：${shockTotal}`,
    `Amiodarone：${amioTotal}`,
    "",
    `第一次本機 EPI：${epiLocals.length > 0 ? formatHHMM(Math.min(...epiLocals.map((e) => e.timestamp_ms))) : "—"}`,
    `最後本機 EPI：${epiLocals.length > 0 ? formatHHMM(Math.max(...epiLocals.map((e) => e.timestamp_ms))) : "—"}`,
    `最後本機電擊：${shockLocals.length > 0 ? formatHHMM(Math.max(...shockLocals.map((e) => e.timestamp_ms))) : "—"}`,
  ];

  const suppPairs = [
    [EVT.EPI_PRE_HANDOVER, "接手前 EPI"],
    [EVT.SHOCK_PRE_HANDOVER, "接手前電擊"],
    [EVT.EPI_PURE_SUPP, "純補登 EPI"],
    [EVT.SHOCK_PURE_SUPP, "純補登電擊"],
  ];
  const suppLines = suppPairs.flatMap(([type, label]) => {
    const sum = events.filter((e) => e.event_type === type).reduce((s, e) => s + (e.count ?? 1), 0);
    return sum > 0 ? [`${label} ×${sum}`] : [];
  });
  if (suppLines.length > 0) lines.push("", "補登：", ...suppLines);

  return lines.join("\n");
}

// === F-9.8: 刪除（對齊 SoT §17.7） ===
async function confirmAndDelete(caseId) {
  const ok = confirm(`刪除 App 內案件？\n不會刪除裝置端資料\n\nCase ID: ${caseId}`);
  if (!ok) return;
  const result = await deleteCase(caseId);
  if (!result.ok) {
    alert(`刪除失敗：${result.error}`);
    return;
  }
  reload();
}

// === Init ===

bindModeTabs();
bindHeaders();
// 初始 mode 從 URL hash 讀
const initialMode = location.hash.replace(/^#/, "");
switchMode(initialMode === "training" ? "training" : "ohca");
