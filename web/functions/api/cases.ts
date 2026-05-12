/**
 * /api/cases
 * - GET: 列表，支援 sort 排序
 * - POST: 接收 case_sync payload，寫入 D1
 *
 * 對齊 docs/phase-f-web-validation-plan.md §8.3
 */

interface Env {
  DB: D1Database;
}

interface CaseSyncEvent {
  type: number;
  timestamp_ms: number;
  elapsed_ms: number;
  count?: number;
  actual_time_null?: boolean;
}

interface CaseSyncPayload {
  type: "case_sync";
  case_id: string;
  mode: "ohca" | "training";
  device_name?: string;
  device_id?: string;
  fw_version?: string;
  started_at_ms: number;
  ended_at_ms?: number;
  events: CaseSyncEvent[];
  summary?: Record<string, unknown>;
}

// STEP 01: sort 白名單，防 SQL injection
const SORT_WHITELIST: Record<string, string> = {
  started_at_desc: "started_at DESC",
  started_at_asc: "started_at ASC",
  synced_at_desc: "synced_at DESC",
  synced_at_asc: "synced_at ASC",
  mode_asc: "mode ASC, started_at DESC",
  device_id_asc: "device_id ASC, started_at DESC",
};

const DEFAULT_SORT = "started_at_desc";

function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function errorResponse(message: string, status = 400): Response {
  return jsonResponse({ ok: false, error: message }, status);
}

// STEP 02: payload 必要欄位驗證
function validatePayload(payload: unknown): payload is CaseSyncPayload {
  if (!payload || typeof payload !== "object") {
    return false;
  }
  const p = payload as Record<string, unknown>;
  if (p.type !== "case_sync") return false;
  if (typeof p.case_id !== "string" || p.case_id.length === 0) return false;
  if (p.mode !== "ohca" && p.mode !== "training") return false;
  if (typeof p.started_at_ms !== "number") return false;
  if (!Array.isArray(p.events)) return false;
  return true;
}

export const onRequestGet: PagesFunction<Env> = async (context) => {
  const url = new URL(context.request.url);
  const sortKey = url.searchParams.get("sort") ?? DEFAULT_SORT;
  const orderBy = SORT_WHITELIST[sortKey];

  if (!orderBy) {
    return errorResponse(`Invalid sort: ${sortKey}`, 400);
  }

  // STEP 01: 查列表，不含 raw_json 與 events（列表頁不需要）
  const stmt = context.env.DB.prepare(
    `SELECT case_id, mode, device_name, device_id, fw_version,
            started_at, ended_at, synced_at,
            (SELECT COUNT(*) FROM events WHERE events.case_id = cases.case_id) AS event_count
     FROM cases
     ORDER BY ${orderBy}
     LIMIT 500`,
  );

  const result = await stmt.all();
  return jsonResponse({ ok: true, cases: result.results });
};

export const onRequestPost: PagesFunction<Env> = async (context) => {
  // STEP 01: 解析 + 驗證 payload
  let payload: unknown;
  try {
    payload = await context.request.json();
  } catch {
    return errorResponse("Invalid JSON", 400);
  }

  if (!validatePayload(payload)) {
    return errorResponse("Invalid payload: missing required fields", 400);
  }

  const syncedAt = Date.now();

  // STEP 02: INSERT OR REPLACE cases（Case ID 去重，對齊 pm-dev-spec §16.8）
  const caseStmt = context.env.DB.prepare(
    `INSERT OR REPLACE INTO cases
     (case_id, mode, device_name, device_id, fw_version, started_at, ended_at, synced_at, raw_json)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
  ).bind(
    payload.case_id,
    payload.mode,
    payload.device_name ?? null,
    payload.device_id ?? null,
    payload.fw_version ?? null,
    payload.started_at_ms,
    payload.ended_at_ms ?? null,
    syncedAt,
    JSON.stringify(payload),
  );

  // STEP 03: 重傳時整筆替換 events（先刪後插，避免重複）
  const deleteEventsStmt = context.env.DB.prepare(
    `DELETE FROM events WHERE case_id = ?`,
  ).bind(payload.case_id);

  const eventInserts = payload.events.map((evt) =>
    context.env.DB.prepare(
      `INSERT INTO events (case_id, event_type, timestamp_ms, elapsed_ms, count, actual_time_null)
       VALUES (?, ?, ?, ?, ?, ?)`,
    ).bind(
      payload.case_id,
      evt.type,
      evt.timestamp_ms,
      evt.elapsed_ms,
      evt.count ?? 1,
      evt.actual_time_null ? 1 : 0,
    ),
  );

  // STEP 04: 原子化 batch（D1 支援）
  try {
    await context.env.DB.batch([caseStmt, deleteEventsStmt, ...eventInserts]);
  } catch (err) {
    const message = err instanceof Error ? err.message : "DB error";
    return errorResponse(`Write failed: ${message}`, 500);
  }

  return jsonResponse({
    ok: true,
    case_id: payload.case_id,
    synced_at: syncedAt,
    event_count: payload.events.length,
  });
};
