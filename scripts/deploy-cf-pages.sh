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
mkdir -p "$DIST_DIR/demo"

echo "▶ Copying public assets"
cp "$ROOT_DIR/docs/EMS_DoseSync_Pro_Prototype_V1_flow.html" "$DIST_DIR/"
cp "$ROOT_DIR/docs/demo/index.html"                          "$DIST_DIR/demo/"
cp "$ROOT_DIR/scripts/landing.html"                          "$DIST_DIR/index.html"

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
wrangler pages deploy "$DIST_DIR" --project-name "$PROJECT_NAME"
