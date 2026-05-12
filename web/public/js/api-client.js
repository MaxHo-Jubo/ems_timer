/**
 * /api/cases 簡單 fetch wrapper
 * 對齊 web/functions/api/cases.ts
 */

const API_BASE = "";  // 同 origin，Pages Functions 直接打 /api/...

const SORT_KEYS = [
  "started_at_desc",
  "started_at_asc",
  "synced_at_desc",
  "synced_at_asc",
  "mode_asc",
  "device_id_asc",
];

const MODE_KEYS = ["ohca", "training"];

/**
 * 取列表
 * @param {string} sort - 對齊後端 SORT_WHITELIST 的 key
 * @param {string|null} mode - 對齊後端 MODE_WHITELIST，null = 不過濾
 * @returns {Promise<{ok: boolean, cases?: any[], error?: string}>}
 */
export async function fetchCases(sort = "started_at_desc", mode = null) {
  if (!SORT_KEYS.includes(sort)) {
    return { ok: false, error: `Invalid sort: ${sort}` };
  }
  if (mode !== null && !MODE_KEYS.includes(mode)) {
    return { ok: false, error: `Invalid mode: ${mode}` };
  }
  try {
    const params = new URLSearchParams({ sort });
    if (mode !== null) params.set("mode", mode);
    const res = await fetch(`${API_BASE}/api/cases?${params.toString()}`);
    if (!res.ok) {
      return { ok: false, error: `HTTP ${res.status}` };
    }
    return await res.json();
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : "Network error" };
  }
}

/**
 * 寫入單筆 case
 * @param {object} payload - case_sync payload
 */
export async function postCase(payload) {
  try {
    const res = await fetch(`${API_BASE}/api/cases`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    return await res.json();
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : "Network error" };
  }
}

/**
 * 取單筆完整資料（含 events）
 */
export async function fetchCaseDetail(caseId) {
  try {
    const res = await fetch(`${API_BASE}/api/cases/${encodeURIComponent(caseId)}`);
    return await res.json();
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : "Network error" };
  }
}

/**
 * 刪除單筆 case（D1-only，不影響韌體端原始紀錄，對齊 SoT §17.7）
 */
export async function deleteCase(caseId) {
  try {
    const res = await fetch(`${API_BASE}/api/cases/${encodeURIComponent(caseId)}`, {
      method: "DELETE",
    });
    return await res.json();
  } catch (err) {
    return { ok: false, error: err instanceof Error ? err.message : "Network error" };
  }
}
