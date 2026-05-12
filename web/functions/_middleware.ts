/**
 * 簡單 CORS middleware
 * 驗證階段：允許所有 origin（內部使用）
 * 正式 App 階段需限縮
 */

const ALLOWED_METHODS = "GET, POST, OPTIONS";
const ALLOWED_HEADERS = "Content-Type";

export const onRequest: PagesFunction = async (context) => {
  // STEP 01: preflight 直接回 204
  if (context.request.method === "OPTIONS") {
    return new Response(null, {
      status: 204,
      headers: {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Methods": ALLOWED_METHODS,
        "Access-Control-Allow-Headers": ALLOWED_HEADERS,
        "Access-Control-Max-Age": "86400",
      },
    });
  }

  // STEP 02: 跑後續 handler 再補上 CORS header
  const response = await context.next();
  const headers = new Headers(response.headers);
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Access-Control-Allow-Methods", ALLOWED_METHODS);
  headers.set("Access-Control-Allow-Headers", ALLOWED_HEADERS);

  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
};
