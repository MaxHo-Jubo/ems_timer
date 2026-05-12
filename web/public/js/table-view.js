/**
 * cases.html 列表頁邏輯
 * - 載入 /api/cases
 * - 點欄位切 ASC/DESC（後端排序，重打 API）
 * - 時間戳記用 ms + 可讀格式雙顯示
 */

import { fetchCases } from "./api-client.js";

// STEP 01: 欄位 → 後端 sort key 對應（顯示用箭頭也靠它）
const SORT_MAP = {
  started_at: { asc: "started_at_asc", desc: "started_at_desc" },
  synced_at: { asc: "synced_at_asc", desc: "synced_at_desc" },
  mode: { asc: "mode_asc", desc: "mode_asc" },          // 後端僅 ASC
  device_id: { asc: "device_id_asc", desc: "device_id_asc" },
};

const state = {
  sortColumn: "started_at",
  sortDir: "desc",
  cases: [],
};

/**
 * epoch ms → 顯示用「MM-DD HH:mm:ss」
 * 保留原 ms 數值另一欄顯示（使用者明確要求）
 */
function formatTime(epochMs) {
  if (!epochMs || typeof epochMs !== "number") {
    return "—";
  }
  const d = new Date(epochMs);
  const pad = (n) => String(n).padStart(2, "0");
  return `${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function modeBadge(mode) {
  if (mode === "ohca") {
    return `<td class="mode-ohca">OHCA</td>`;
  }
  if (mode === "training") {
    return `<td class="mode-training">TRAIN</td>`;
  }
  return `<td class="text-mute">${escapeHtml(mode ?? "?")}</td>`;
}

function escapeHtml(text) {
  if (text === null || text === undefined) {
    return "";
  }
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function render() {
  const tbody = document.getElementById("caseTableBody");
  const countEl = document.getElementById("caseCount");
  countEl.textContent = String(state.cases.length);

  if (state.cases.length === 0) {
    tbody.innerHTML = `<tr><td colspan="6" class="case-table__empty">尚無資料</td></tr>`;
    return;
  }

  const rows = state.cases.map((c) => `
    <tr>
      <td>
        ${formatTime(c.started_at)}
        <div class="timestamp">${c.started_at ?? "—"}</div>
      </td>
      ${modeBadge(c.mode)}
      <td>
        ${escapeHtml(c.device_id ?? "—")}
        <div class="timestamp">${escapeHtml(c.device_name ?? "")}</div>
      </td>
      <td>${c.event_count ?? 0}</td>
      <td>
        ${formatTime(c.synced_at)}
        <div class="timestamp">${c.synced_at ?? "—"}</div>
      </td>
      <td class="text-mono">${c.started_at ?? "—"}</td>
    </tr>
  `).join("");

  tbody.innerHTML = rows;
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
  const result = await fetchCases(sortKey);
  if (!result.ok) {
    document.getElementById("caseTableBody").innerHTML = `
      <tr><td colspan="6" class="case-table__empty text-red">
        載入失敗：${escapeHtml(result.error ?? "Unknown")}
      </td></tr>
    `;
    return;
  }
  state.cases = result.cases ?? [];
  render();
}

function bindHeaders() {
  document.querySelectorAll("th[data-sort-key]").forEach((th) => {
    th.addEventListener("click", () => {
      const key = th.dataset.sortKey;
      if (state.sortColumn === key) {
        // STEP 01: 同欄位切方向
        state.sortDir = state.sortDir === "asc" ? "desc" : "asc";
      } else {
        // STEP 02: 換欄位預設 desc（時間/數量類別自然順序）
        state.sortColumn = key;
        state.sortDir = "desc";
      }
      reload();
    });
  });
}

// STEP 01: init
bindHeaders();
reload();
