#!/usr/bin/env bash
# EMS Timer Phase F web — 雲端部署 SOP
#
# 用途：把網頁 + D1 schema + Pages Functions 一鍵部署到 Cloudflare
# 前置：
#   - 已執行 `wrangler login`
#   - wrangler.toml 的 database_id 已替換為 `wrangler d1 create` 回傳的真實 UUID
#     （本機開發用 placeholder UUID 00000000-0000-4000-8000-000000000001，雲端會被拒）

set -euo pipefail

cd "$(dirname "$0")"

FAKE_UUID="00000000-0000-4000-8000-000000000001"
DB_NAME="ems-timer-cases"
WRANGLER="npx wrangler"

# === Step 0: 健檢 ===

if ! command -v wrangler >/dev/null 2>&1 && ! [ -x "./node_modules/.bin/wrangler" ]; then
  echo "[ERR] 找不到 wrangler，請先 cd web/ && npm install"
  exit 1
fi

# === Step 1: 確認不是 placeholder UUID ===

if grep -q "${FAKE_UUID}" wrangler.toml; then
  echo "[ERR] wrangler.toml 還是 placeholder UUID: ${FAKE_UUID}"
  echo ""
  echo "請執行雲端 D1 建立流程:"
  echo "  1. ${WRANGLER} d1 create ${DB_NAME}"
  echo "  2. 把回傳的 database_id 替換到 wrangler.toml 的 database_id 欄位"
  echo "  3. 重新跑 ./deploy.sh"
  echo ""
  echo "若不想動雲端，本機開發 placeholder 即可: npm run dev"
  exit 1
fi

# === Step 2: 套用 remote D1 migration ===

echo "[INFO] 套用 remote D1 migration..."
${WRANGLER} d1 migrations apply "${DB_NAME}" --remote

# === Step 3: 部署 Pages ===

echo ""
echo "[INFO] 部署 Cloudflare Pages..."
${WRANGLER} pages deploy public

echo ""
echo "[OK] 部署完成"
echo ""
echo "Tips:"
echo "  - 後續只改前端: ${WRANGLER} pages deploy public"
echo "  - 後續加 migration: ${WRANGLER} d1 migrations apply ${DB_NAME} --remote"
echo "  - 看 remote D1 內容: ${WRANGLER} d1 execute ${DB_NAME} --remote --command 'SELECT ...'"
