#!/usr/bin/env bash
# regen_vlw.sh — 重新生成 ems_zh_24 字型（.vlw binary + .h PROGMEM header）
#
# 用途：掃描所有 UI 字串散落檔（src/ 底下 glob 自動涵蓋 .cpp／.h，lib/ 底下手動列舉
#       實際上 TFT 的檔案，詳細清單見 STEP 03），union 字集後重生 vlw，避免單檔掃描
#       漏字（如「至 剩 餘 配 對 碼」缺字 bug）。
#
# ⚠️ 實機載入路徑：firmware 用 `display.loadFont(ems_zh_24_vlw)` 載入嵌進 binary
#    的 PROGMEM array（src/ems_zh_24_vlw.h），不是 LittleFS 的 .vlw 檔。所以重生
#    vlw 後必須一併重生 header，否則 firmware build 仍嵌入舊字集（2026-05-25 B1
#    踩坑：只更 .vlw 不更 .h，補字看似完成但實機仍缺）。
#
# 執行位置：firmware/ 目錄
#   cd firmware && bash scripts/regen_vlw.sh
#
# 可選環境變數：
#   TTF_PATH    來源 TTF/TTC 路徑（預設 /System/Library/Fonts/STHeiti Medium.ttc）
#   TTC_INDEX   TTC 多字型索引（預設 0）
#   FONT_SIZE   字符像素高（預設 24）
#   OUT_VLW     輸出 vlw 路徑（預設 data/fonts/ems_zh_24.vlw）
#   OUT_HEADER  輸出 header 路徑（預設 src/ems_zh_24_vlw.h）

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
OUT_HEADER="${OUT_HEADER:-src/ems_zh_24_vlw.h}"

# STEP 02.5: 失敗防護 — .vlw 與 .h 必須成對同步。set -e 會在任一步失敗時中止，
#   但 STEP 07 已寫 .vlw 後若 STEP 09 header 未生，會留下「.vlw 新 / .h 舊」的裂縫
#   → firmware build 嵌入舊字集（2026-05-25 B1 踩坑）。trap 在中止時響亮示警此狀態。
VLW_REGENERATED=0
trap 'rc=$?;
    echo "[error] regen_vlw.sh 中止（exit $rc）" >&2;
    if [ "$VLW_REGENERATED" = "1" ]; then
        echo "[FATAL] .vlw 已重生但 .h header 未同步！" >&2;
        echo "        已更新: $OUT_VLW" >&2;
        echo "        未同步: $OUT_HEADER（仍為舊字集）" >&2;
        echo "        直接 build firmware 會嵌入舊字集（B1 踩坑）。修正問題後務必重跑" >&2;
        echo "        本腳本，確認 STEP 09 header 重生完成再燒錄。" >&2;
    fi' ERR

# STEP 03: 蒐集所有可能含 UI 中文字串的檔案
#         glob 可能無 match，用 nullglob 避免展開成字面值
#
#         ⚠️ 這份清單的涵蓋策略分兩半：
#         - src/ 底下的 .cpp／.h 用 glob 自動掃描（排除生成物 ems_zh_24_vlw.h），
#           新增符合既有 glob 模式的檔案會自動涵蓋，不必手動加
#         - lib/ 底下**手動列舉**，判準是「字串會不會實際上 TFT」——只納入會被畫在
#           螢幕上的檔案；Serial.printf／static_assert 訊息不上 TFT，不納入以省 Flash
#         新增含 UI 字串的檔案（尤其是 lib/ 底下、不受 glob 涵蓋的）時必須同步加進本
#         清單，否則字會被 union 靜默排除在字集外（不是報錯，是悄悄漏掉）。2026-08-24
#         踩坑：MAIN_MENU_LABELS 定義在 src/app_globals.h，這份清單當時只列 .cpp，
#         字型重生 union 重算時把「設」字掃出字集，實機「系統設定」顯示成「系統▯定」。
shopt -s nullglob
SRC_FILES=(
    src/main.cpp
    src/ui_*.cpp
    src/*_handler.cpp
    src/*_logic.cpp
    lib/ui_settings/ui_settings.cpp
    lib/ui_settings/ui_settings.h
    lib/ems_settings/ems_settings.h
)
# .h 另外處理：排除 ems_zh_24_vlw.h（vlw2header.py 生成的 PROGMEM binary array，
# 無雙引號字串字面值，體積大（1 萬+行）納入只會拖慢掃描且無意義）
for f in src/*.h; do
    if [ "$f" != "src/ems_zh_24_vlw.h" ]; then
        SRC_FILES+=("$f")
    fi
done
shopt -u nullglob

if [ ${#SRC_FILES[@]} -eq 0 ]; then
    echo "[error] 沒找到任何來源檔（src/main.cpp / src/ui_*.cpp / src/*_handler.cpp /" >&2
    echo "        src/*_logic.cpp / src/*.h / lib/ui_settings/ui_settings.{cpp,h} /" >&2
    echo "        lib/ems_settings/ems_settings.h）" >&2
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

# .vlw 已重生；此後若中止代表 .h 尚未同步 → trap 會示警（見 STEP 02.5）
VLW_REGENERATED=1

# STEP 08: 列印新 vlw 大小與差異
NEW_SIZE_BYTES=$(wc -c < "$OUT_VLW" | tr -d ' ')
NEW_SIZE_KB=$(awk -v b="$NEW_SIZE_BYTES" 'BEGIN { printf "%.1f", b/1024 }')
DIFF_KB=$(awk -v o="$OLD_SIZE_BYTES" -v n="$NEW_SIZE_BYTES" 'BEGIN { printf "%+.1f", (n-o)/1024 }')
echo "===================================================="
echo "  new vlw size: ${NEW_SIZE_KB} KB (${NEW_SIZE_BYTES} bytes)  diff: ${DIFF_KB} KB"
echo "===================================================="

# STEP 09: 同步重生 PROGMEM header（實機 firmware 實際載入路徑）
python3 tools/vlw2header.py "$OUT_VLW" "$OUT_HEADER"

echo "===================================================="
echo "[done] 重生完成，重燒 firmware 才會生效（PROGMEM 嵌在 binary）："
echo "       ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload"
