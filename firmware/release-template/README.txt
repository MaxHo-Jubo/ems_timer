============================================================
EMS Timer 韌體燒錄包
============================================================

本壓縮包用途：將 EMS Timer 韌體燒錄到 ESP32-S3 開發板
不需安裝 PlatformIO 或 Arduino IDE，只要 Python + esptool。

------------------------------------------------------------
目錄內容
------------------------------------------------------------
firmware-merged.bin 韌體 binary（bootloader + partition + app 三合一，由開發者編譯）
flash.bat           Windows 一鍵燒錄腳本
flash.sh            macOS / Linux 一鍵燒錄腳本
README.txt          本說明文件

------------------------------------------------------------
事前準備（一次性）
------------------------------------------------------------

【Windows】
  1. 下載 Python 3.10+
     https://www.python.org/downloads/
     ⚠️ 安裝時必須勾選「Add Python to PATH」

  2. 開 CMD 或 PowerShell：
     pip install esptool

  3. 確認驅動：
     - ESP32-S3 USB-OTG 內建：免裝（Windows 10/11 自動辨識）
     - CH340 晶片開發板：裝 CH340 driver
       http://www.wch-ic.com/downloads/CH341SER_ZIP.html
     - CP2102 晶片開發板：裝 Silicon Labs CP210x driver
       https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

【macOS】
  1. 確認已有 Python 3：
     python3 --version

  2. 安裝 esptool：
     pip3 install esptool

  3. macOS 14+ 對 USB Serial 裝置可能跳出隱私權限提示，
     按「允許」即可。

【Linux】
  1. 安裝 Python 3 與 pip：
     sudo apt install python3 python3-pip

  2. 安裝 esptool：
     pip3 install esptool

  3. 加入 dialout 群組（避免 permission denied）：
     sudo usermod -a -G dialout $USER
     # 加完要 logout / login 才生效

------------------------------------------------------------
燒錄步驟
------------------------------------------------------------

1. 用 USB-C 線將 ESP32-S3 接到電腦

2. 執行對應腳本：
   - Windows：雙擊 flash.bat
   - macOS / Linux：終端機執行 ./flash.sh
                    （第一次需 chmod +x flash.sh）

3. 依提示輸入 COM Port（Windows）或 /dev path（Mac/Linux）

4. 等 30 秒，看到「燒錄成功」訊息即完成

------------------------------------------------------------
常見錯誤排除
------------------------------------------------------------

❌ 'esptool.py' 不是內部或外部命令（Windows）
   → Python Scripts 路徑沒加進 PATH
   → 解法：腳本會自動 fallback 到 python -m esptool，
           若仍失敗請重裝 Python 並勾選「Add to PATH」

❌ Failed to connect to ESP32-S3: No serial data received
   → 板子沒進燒錄模式
   → 解法：
     1. 按住板子 BOOT 鍵不放
     2. 按一下 RESET 鍵
     3. 放開 BOOT 鍵
     4. 重跑燒錄腳本

❌ Permission denied / Access denied: COM3 / /dev/ttyUSB0
   → COM port 被其他程式占用
   → 解法：關閉所有 Serial Monitor、Arduino IDE、PuTTY、
           PlatformIO Monitor 等程式
   → Linux 額外：確認已加入 dialout 群組

❌ A fatal error occurred: Could not open COM3
   → COM port 編號不對
   → 解法（Windows）：開「裝置管理員」→「連接埠」確認編號
   → 解法（Mac）：終端機執行 ls /dev/cu.usbmodem*
   → 解法（Linux）：終端機執行 ls /dev/ttyUSB* /dev/ttyACM*

❌ 燒錄成功但裝置沒反應
   → 韌體燒到但沒重啟
   → 解法：手動按板子 RESET 鍵

------------------------------------------------------------
驗證燒錄成功
------------------------------------------------------------

燒錄完成後，可開 Serial Monitor 看開機 log：

  Windows：
    pip install pyserial
    python -m serial.tools.miniterm COM3 115200

  macOS / Linux：
    screen /dev/cu.usbmodem* 115200
    （離開：Ctrl+A 後按 K）

預期看到：
  - ESP32-S3 開機 banner
  - EMS Timer 主功能表初始化訊息
  - TFT 顯示主功能表畫面

若本包為「產測韌體」（VERSION.txt 第一行有標示）：
  - 螢幕先出現紅／綠／藍／白四色彩條約 1.5 秒，蜂鳴一短聲
  - 接著進入「EMS FACTORY TEST」狀態畫面
  - 判讀方式與 FAIL 對照見隨附的《焊接組裝交付說明》第 7 節，
    不需要開 Serial Monitor

------------------------------------------------------------
版本資訊
------------------------------------------------------------

韌體版本：詳見 firmware-merged.bin 同層的 VERSION.txt（若有）
燒錄工具：esptool.py
目標硬體：ESP32-S3 GOOUUU 開發板
聯絡窗口：（請洽 EMS Timer 專案開發者）
