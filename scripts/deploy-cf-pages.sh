#!/usr/bin/env bash
# Build dist/ from docs/ and deploy to Cloudflare Pages.
#
# Usage:
#   ./scripts/deploy-cf-pages.sh                # build + deploy
#   ./scripts/deploy-cf-pages.sh --build-only   # only build dist/, no deploy
#
# Pre-requisite (one-time):
#   npm install -g wrangler
#   wrangler login

set -euo pipefail

PROJECT_NAME="ems-dosesync-demo"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
BUILD_ONLY="${1:-}"

echo "▶ Cleaning $DIST_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/demo" "$DIST_DIR/ble-tester"

echo "▶ Copying public assets"
cp "$ROOT_DIR/docs/EMS_DoseSync_Pro_Prototype_V1_flow.html" "$DIST_DIR/"
cp "$ROOT_DIR/docs/demo/index.html"                          "$DIST_DIR/demo/"
cp "$ROOT_DIR/docs/ble-tester/index.html"                    "$DIST_DIR/ble-tester/"
cp "$ROOT_DIR/scripts/landing.html"                          "$DIST_DIR/index.html"

# Test Plan: 部署但不在 landing / flow.html 加連結，僅透過直接 URL 訪問
cp "$ROOT_DIR/docs/EMS_DoseSync_Pro_Test_Plan_V1.html"      "$DIST_DIR/"

# 電源模組採購指南: 部署但不掛連結，僅透過直接 URL 訪問
cp "$ROOT_DIR/docs/power-module-purchase.html"              "$DIST_DIR/"

# 硬體採購清單 v2: 部署但不掛連結，僅透過直接 URL 訪問
cp "$ROOT_DIR/docs/hardware-procurement-v2.html"            "$DIST_DIR/"

# PM 進度報告: 部署但不掛連結，僅透過直接 URL 訪問（累加結構）
cp "$ROOT_DIR/docs/progress.html"                            "$DIST_DIR/"

# GPIO 分配總表: 部署但不掛連結，僅透過直接 URL 訪問
cp "$ROOT_DIR/docs/gpio-allocation.html"                     "$DIST_DIR/"

echo "▶ Files staged for deploy:"
find "$DIST_DIR" -type f -print0 | xargs -0 -n1 | sed "s|$DIST_DIR/|  |"

if [ "$BUILD_ONLY" = "--build-only" ]; then
  echo ""
  echo "✔ Build complete. dist/ ready at: $DIST_DIR"
  echo "  To deploy: wrangler pages deploy dist --project-name $PROJECT_NAME"
  exit 0
fi

if ! command -v wrangler >/dev/null 2>&1; then
  echo ""
  echo "✘ wrangler not installed. Run: npm install -g wrangler"
  exit 1
fi

echo ""
echo "▶ Deploying to Cloudflare Pages project: $PROJECT_NAME"

# Cloudflare API（code 8000111）對 commit message 含非 ASCII 會 reject，
# 因此手動帶純 ASCII 訊息，但保留 commit hash 供追溯到 repo。
GIT_HASH="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
GIT_SHORT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo main)"
SAFE_MSG="Deploy ${GIT_SHORT} from ${GIT_BRANCH}"

wrangler pages deploy "$DIST_DIR" \
  --project-name "$PROJECT_NAME" \
  --commit-hash "$GIT_HASH" \
  --commit-message "$SAFE_MSG" \
  --branch "$GIT_BRANCH"
