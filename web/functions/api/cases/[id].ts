/**
 * /api/cases/:id
 * GET: 取單筆 case 完整資料（含 events）
 */

interface Env {
  DB: D1Database;
}

function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

export const onRequestGet: PagesFunction<Env> = async (context) => {
  const caseId = context.params.id;

  // STEP 01: 防止異常 id 直接打 DB（雖然 prepare 已有 binding）
  if (typeof caseId !== "string" || caseId.length === 0 || caseId.length > 64) {
    return jsonResponse({ ok: false, error: "Invalid case_id" }, 400);
  }

  // STEP 02: 讀 case 主紀錄
  const caseRow = await context.env.DB
    .prepare(`SELECT * FROM cases WHERE case_id = ?`)
    .bind(caseId)
    .first();

  if (!caseRow) {
    return jsonResponse({ ok: false, error: "Not found" }, 404);
  }

  // STEP 03: 讀對應 events（按 elapsed_ms 升序，呈現案件時間軸）
  const eventsResult = await context.env.DB
    .prepare(
      `SELECT event_type, timestamp_ms, elapsed_ms, count, actual_time_null
       FROM events
       WHERE case_id = ?
       ORDER BY elapsed_ms ASC`,
    )
    .bind(caseId)
    .all();

  return jsonResponse({
    ok: true,
    case: caseRow,
    events: eventsResult.results,
  });
};
