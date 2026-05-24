#!/usr/bin/env bash
# regen_vlw.sh — 重新生成 firmware/data/fonts/ems_zh_24.vlw
#
# 用途：掃描所有 UI 字串散落檔（main.cpp / ui_*.cpp / *_handler.cpp / *_logic.cpp），
#       union 字集後重生 vlw，避免單檔掃描漏字（如「至 剩 餘 配 對 碼」缺字 bug）。
#
# 執行位置：firmware/ 目錄
#   cd firmware && bash scripts/regen_vlw.sh
#
# 可選環境變數：
#   TTF_PATH   來源 TTF/TTC 路徑（預設 /System/Library/Fonts/STHeiti Medium.ttc）
#   TTC_INDEX  TTC 多字型索引（預設 0）
#   FONT_SIZE  字符像素高（預設 24）
#   OUT_VLW    輸出 vlw 路徑（預設 data/fonts/ems_zh_24.vlw）

set -e

# STEP 01: 切到 firmware/ 目錄（腳本支援從任意位置呼叫）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$FIRMWARE_DIR"

# STEP 02: 解析參數（環境變數可覆寫預設）
TTF_PATH="${TTF_PATH:-/System/Library/Fonts/STHeiti Medium.ttc}"
TTC_INDEX="${TTC_INDEX:-0}"
FONT_SIZE="${FONT_SIZE:-24}"
OUT_VLW="${OUT_VLW:-data/fonts/ems_zh_24.vlw}"

# STEP 03: 蒐集所有可能含 UI 中文字串的檔案
#         glob 可能無 match，用 nullglob 避免展開成字面值
shopt -s nullglob
SRC_FILES=(
    src/main.cpp
    src/ui_*.cpp
    src/*_handler.cpp
    src/*_logic.cpp
)
shopt -u nullglob

if [ ${#SRC_FILES[@]} -eq 0 ]; then
    echo "[error] 沒找到任何來源檔（src/main.cpp / src/ui_*.cpp / src/*_handler.cpp / src/*_logic.cpp）" >&2
    exit 1
fi

# STEP 04: 檢查 TTF 存在
if [ ! -f "$TTF_PATH" ]; then
    echo "[error] TTF 不存在: $TTF_PATH" >&2
    echo "        可設 TTF_PATH 環境變數指定其他字型路徑。" >&2
    exit 1
fi

# STEP 05: 列印輸入摘要
echo "===================================================="
echo "  TTF source : $TTF_PATH (index=$TTC_INDEX)"
echo "  Output vlw : $OUT_VLW"
echo "  Font size  : ${FONT_SIZE}px"
echo "  Source cnt : ${#SRC_FILES[@]} files"
for f in "${SRC_FILES[@]}"; do
    echo "    - $f"
done
echo "===================================================="

# STEP 06: 紀錄舊 vlw 大小（若存在）做前後對比
OLD_SIZE_BYTES=0
if [ -f "$OUT_VLW" ]; then
    OLD_SIZE_BYTES=$(wc -c < "$OUT_VLW" | tr -d ' ')
    OLD_SIZE_KB=$(awk -v b="$OLD_SIZE_BYTES" 'BEGIN { printf "%.1f", b/1024 }')
    echo "[info] old vlw size: ${OLD_SIZE_KB} KB (${OLD_SIZE_BYTES} bytes)"
fi

# STEP 07: 跑轉換
python3 tools/ttf2vlw.py \
    "$TTF_PATH" \
    "$OUT_VLW" \
    --size "$FONT_SIZE" \
    --ttc-index "$TTC_INDEX" \
    --chars-from "${SRC_FILES[@]}"

# STEP 08: 列印新 vlw 大小與差異
NEW_SIZE_BYTES=$(wc -c < "$OUT_VLW" | tr -d ' ')
NEW_SIZE_KB=$(awk -v b="$NEW_SIZE_BYTES" 'BEGIN { printf "%.1f", b/1024 }')
DIFF_KB=$(awk -v o="$OLD_SIZE_BYTES" -v n="$NEW_SIZE_BYTES" 'BEGIN { printf "%+.1f", (n-o)/1024 }')
echo "===================================================="
echo "  new vlw size: ${NEW_SIZE_KB} KB (${NEW_SIZE_BYTES} bytes)  diff: ${DIFF_KB} KB"
echo "===================================================="
echo "[done] 上傳 LittleFS 燒錄指令："
echo "       ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t uploadfs"
