/**
 * /api/cases/:id/notes
 * - GET: 取備註
 * - PUT: 寫備註（INSERT OR REPLACE，1:1）
 *
 * 對齊 SoT §17.5：備註只存 App/Web 端，不回寫裝置
 */

interface Env {
  DB: D1Database;
}

interface NotesPayload {
  hospital_arrival_at: number | null;
  rosc: boolean | null;
  handover_to: string | null;
  special_situation: string | null;
  other: string | null;
}

function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

function errorResponse(message: string, status = 400): Response {
  return jsonResponse({ ok: false, error: message }, status);
}

// STEP 01: 取 case_id 並驗證
function getCaseId(context: { params: { id: string | string[] } }): string | null {
  const id = context.params.id;
  if (typeof id !== "string" || id.length === 0 || id.length > 64) {
    return null;
  }
  return id;
}

// STEP 02: 驗證 payload 形狀
function validateNotes(payload: unknown): payload is NotesPayload {
  if (!payload || typeof payload !== "object") {
    return false;
  }
  const p = payload as Record<string, unknown>;
  const stringOrNull = (v: unknown) => v === null || typeof v === "string";
  const numberOrNull = (v: unknown) => v === null || typeof v === "number";
  const boolOrNull = (v: unknown) => v === null || typeof v === "boolean";

  return (
    numberOrNull(p.hospital_arrival_at) &&
    boolOrNull(p.rosc) &&
    stringOrNull(p.handover_to) &&
    stringOrNull(p.special_situation) &&
    stringOrNull(p.other)
  );
}

export const onRequestGet: PagesFunction<Env> = async (context) => {
  const caseId = getCaseId(context);
  if (!caseId) {
    return errorResponse("Invalid case_id", 400);
  }

  const row = await context.env.DB
    .prepare(`SELECT * FROM notes WHERE case_id = ?`)
    .bind(caseId)
    .first();

  if (!row) {
    // STEP 01: 沒備註不算錯誤，回空殼
    return jsonResponse({
      ok: true,
      notes: {
        case_id: caseId,
        hospital_arrival_at: null,
        rosc: null,
        handover_to: null,
        special_situation: null,
        other: null,
        updated_at: null,
      },
    });
  }

  return jsonResponse({ ok: true, notes: row });
};

export const onRequestPut: PagesFunction<Env> = async (context) => {
  const caseId = getCaseId(context);
  if (!caseId) {
    return errorResponse("Invalid case_id", 400);
  }

  let payload: unknown;
  try {
    payload = await context.request.json();
  } catch {
    return errorResponse("Invalid JSON", 400);
  }

  if (!validateNotes(payload)) {
    return errorResponse("Invalid notes payload", 400);
  }

  // STEP 01: 先確認 case 存在（FK 約束會擋無效寫入，但提早回 404 對使用者比較友善）
  const caseExists = await context.env.DB
    .prepare(`SELECT 1 FROM cases WHERE case_id = ?`)
    .bind(caseId)
    .first();

  if (!caseExists) {
    return errorResponse("Case not found", 404);
  }

  // STEP 02: INSERT OR REPLACE（notes PK 是 case_id，1:1 關係）
  const updatedAt = Date.now();
  try {
    await context.env.DB
      .prepare(
        `INSERT OR REPLACE INTO notes
         (case_id, hospital_arrival_at, rosc, handover_to, special_situation, other, updated_at)
         VALUES (?, ?, ?, ?, ?, ?, ?)`,
      )
      .bind(
        caseId,
        payload.hospital_arrival_at,
        payload.rosc === null ? null : payload.rosc ? 1 : 0,
        payload.handover_to,
        payload.special_situation,
        payload.other,
        updatedAt,
      )
      .run();
  } catch (err) {
    const message = err instanceof Error ? err.message : "DB error";
    return errorResponse(`Write failed: ${message}`, 500);
  }

  return jsonResponse({ ok: true, case_id: caseId, updated_at: updatedAt });
};
