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

function validateCaseId(caseId: unknown): caseId is string {
  return typeof caseId === "string" && caseId.length > 0 && caseId.length <= 64;
}

export const onRequestGet: PagesFunction<Env> = async (context) => {
  const caseId = context.params.id;

  if (!validateCaseId(caseId)) {
    return jsonResponse({ ok: false, error: "Invalid case_id" }, 400);
  }

  // STEP 01: 讀 case 主紀錄
  const caseRow = await context.env.DB
    .prepare(`SELECT * FROM cases WHERE case_id = ?`)
    .bind(caseId)
    .first();

  if (!caseRow) {
    return jsonResponse({ ok: false, error: "Not found" }, 404);
  }

  // STEP 02: 讀對應 events（按 elapsed_ms 升序，呈現案件時間軸）
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

/**
 * DELETE /api/cases/:id
 * 對齊 SoT §17.7：只刪 App/Web 端資料，不影響裝置端原始紀錄
 * FK CASCADE 會連帶刪 events + notes
 */
export const onRequestDelete: PagesFunction<Env> = async (context) => {
  const caseId = context.params.id;

  if (!validateCaseId(caseId)) {
    return jsonResponse({ ok: false, error: "Invalid case_id" }, 400);
  }

  const result = await context.env.DB
    .prepare(`DELETE FROM cases WHERE case_id = ?`)
    .bind(caseId)
    .run();

  if (result.meta.changes === 0) {
    return jsonResponse({ ok: false, error: "Not found" }, 404);
  }

  return jsonResponse({ ok: true, case_id: caseId, deleted: true });
};
