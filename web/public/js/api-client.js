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

/**
 * 取列表
 * @param {string} sort - 對齊後端 SORT_WHITELIST 的 key
 * @returns {Promise<{ok: boolean, cases?: any[], error?: string}>}
 */
export async function fetchCases(sort = "started_at_desc") {
  if (!SORT_KEYS.includes(sort)) {
    return { ok: false, error: `Invalid sort: ${sort}` };
  }
  try {
    const res = await fetch(`${API_BASE}/api/cases?sort=${sort}`);
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
