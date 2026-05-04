@echo off
REM ============================================================
REM EMS Timer 韌體燒錄工具（Windows 版）
REM 用途：將 firmware.bin 燒錄到 ESP32-S3 開發板
REM 先決條件：
REM   1. 已安裝 Python 3.10+（安裝時必須勾選「Add to PATH」）
REM   2. 已安裝 esptool（pip install esptool）
REM   3. ESP32-S3 已透過 USB-C 接到電腦
REM ============================================================

setlocal

echo.
echo ===========================================
echo    EMS Timer 韌體燒錄工具 v1.0
echo ===========================================
echo.

REM STEP 01: 檢查 firmware.bin 是否存在
if not exist "%~dp0firmware.bin" (
    echo [錯誤] 找不到 firmware.bin
    echo 請確認 firmware.bin 與 flash.bat 在同一資料夾
    echo.
    pause
    exit /b 1
)

REM STEP 02: 檢查 esptool 是否可用
where esptool.py >nul 2>nul
if %errorlevel% neq 0 (
    echo [警告] esptool.py 不在 PATH 中，改用 python -m esptool
    set ESPTOOL=python -m esptool
) else (
    set ESPTOOL=esptool.py
)

REM STEP 03: 列出可用 COM Port 提示使用者
echo 偵測中的 COM Ports（請對照裝置管理員）：
powershell -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object"
echo.

REM STEP 04: 詢問使用者輸入 COM Port
set /p PORT="請輸入 ESP32-S3 的 COM Port（例如 COM3）："
if "%PORT%"=="" (
    echo [錯誤] 未輸入 COM Port
    pause
    exit /b 1
)

REM STEP 05: 執行燒錄
echo.
echo 開始燒錄到 %PORT% ...
echo （若卡住請按住板子 BOOT 鍵 + 按一下 RESET 鍵 + 放開 BOOT 鍵）
echo.

%ESPTOOL% --chip esp32s3 --port %PORT% --baud 921600 write_flash 0x0 "%~dp0firmware.bin"

if %errorlevel% equ 0 (
    echo.
    echo ===========================================
    echo    燒錄成功！裝置會自動重啟
    echo ===========================================
) else (
    echo.
    echo ===========================================
    echo    燒錄失敗，請參考 README.txt 排除問題
    echo ===========================================
)

echo.
pause
endlocal
