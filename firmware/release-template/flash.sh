#!/usr/bin/env bash
# ============================================================
# EMS Timer 韌體燒錄工具（macOS / Linux 版）
# 用途：將 firmware.bin 燒錄到 ESP32-S3 開發板
# 先決條件：
#   1. 已安裝 Python 3.10+
#   2. 已安裝 esptool（pip3 install esptool）
#   3. ESP32-S3 已透過 USB-C 接到電腦
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE="${SCRIPT_DIR}/firmware.bin"

echo
echo "==========================================="
echo "   EMS Timer 韌體燒錄工具 v1.0"
echo "==========================================="
echo

# STEP 01: 檢查 firmware.bin 是否存在
if [ ! -f "${FIRMWARE}" ]; then
    echo "[錯誤] 找不到 firmware.bin"
    echo "請確認 firmware.bin 與 flash.sh 在同一資料夾"
    exit 1
fi

# STEP 02: 檢查 esptool 是否可用
if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL="esptool.py"
elif python3 -m esptool version >/dev/null 2>&1; then
    ESPTOOL="python3 -m esptool"
else
    echo "[錯誤] 找不到 esptool"
    echo "請執行：pip3 install esptool"
    exit 1
fi

# STEP 03: 自動偵測 ESP32-S3 port
echo "偵測中的 USB Serial 裝置："
case "$(uname -s)" in
    Darwin)
        # STEP 03.01: macOS 列出 cu.usbmodem*
        ls /dev/cu.usbmodem* 2>/dev/null || echo "（無偵測到裝置）"
        DEFAULT_PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1 || echo "")"
        ;;
    Linux)
        # STEP 03.02: Linux 列出 ttyUSB* 或 ttyACM*
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "（無偵測到裝置）"
        DEFAULT_PORT="$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1 || echo "")"
        ;;
    *)
        DEFAULT_PORT=""
        ;;
esac
echo

# STEP 04: 詢問使用者選擇 port（提供 default）
if [ -n "${DEFAULT_PORT}" ]; then
    read -r -p "請輸入 port [預設 ${DEFAULT_PORT}]: " PORT
    PORT="${PORT:-${DEFAULT_PORT}}"
else
    read -r -p "請輸入 port: " PORT
fi

if [ -z "${PORT}" ]; then
    echo "[錯誤] 未指定 port"
    exit 1
fi

# STEP 05: 執行燒錄
echo
echo "開始燒錄到 ${PORT} ..."
echo "（若卡住請按住板子 BOOT 鍵 + 按一下 RESET 鍵 + 放開 BOOT 鍵）"
echo

if ${ESPTOOL} --chip esp32s3 --port "${PORT}" --baud 921600 write_flash 0x0 "${FIRMWARE}"; then
    echo
    echo "==========================================="
    echo "   燒錄成功！裝置會自動重啟"
    echo "==========================================="
else
    echo
    echo "==========================================="
    echo "   燒錄失敗，請參考 README.txt 排除問題"
    echo "==========================================="
    exit 1
fi
