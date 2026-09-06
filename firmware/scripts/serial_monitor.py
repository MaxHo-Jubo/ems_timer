#!/usr/bin/env python3
"""上機驗收用的 serial log 監聽器。

用途：驗收時一邊有人操作板子、一邊把韌體的 log 收下來當證據（狀態轉移、NVS 載入值、
按鍵事件）。跟 `pio device monitor` 的差別在於本腳本刻意處理了這片板子的兩個特性。

    cd firmware && python3 scripts/serial_monitor.py [輸出檔]
    # 預設輸出 /tmp/ems_serial.log；Ctrl-C 結束

⚠️ 兩個踩過的坑，改這支腳本時不要拿掉：

1. **開啟連接埠時讓 DTR/RTS 維持 pyserial 的預設（拉高），不要改成設 False。**

   先講清楚 pyserial 的實際行為，因為它跟參數名稱給人的直覺相反：`SerialBase.__init__`
   預設 `_dtr_state = True` / `_rts_state = True`，而 `serialposix.Serial.open()` 只要
   `dsrdtr`／`rtscts` 為 False（即未啟用硬體流量控制）就會呼叫 `_update_dtr_state()` /
   `_update_rts_state()`。所以傳 `dsrdtr=False, rtscts=False` **不等於不碰控制線**，
   反而保證每次開啟都把兩條線拉高一次。

   看到這裡的直覺修法是「先建未開啟物件 → 設 dtr/rts = False → 再 open()」。
   **在這片板子上實測過，那樣會把板子重開**：ESP32-S3 的 USB-Serial-JTAG 在晶片內部
   用 DTR/RTS 組合模擬傳統 auto-reset 電路，設成 inactive 正好命中 reset 條件，log
   立刻出現 `rst:0x15 (USB_UART_CHIP_RESET)` 後跟著整段 bootloader 輸出。維持預設的
   「兩條都拉高」反而安全——本 session 兩輪驗收監聽都沒有造成重開。

   結論：這裡不是「哪種寫法比較乾淨」的問題，是硬體行為決定的。改之前先接板子實測，
   看 log 有沒有 `USB_UART_CHIP_RESET`；沒有實機就不要動。

2. **port 名稱會漂移**。macOS 上 USB CDC 每次重新列舉，`cu.usbmodem101` 可能變成
   `cu.usbmodem1101`，reset 後尤其會。所以每次重連都重新 glob，不記住上一次的名字。

另註：主韌體不像 smoke test 會持續輸出，開機 banner 印完就靜默——收到 0 bytes 通常
代表「板子好好跑著但沒事發生」，不是連線失敗。要確認它活著就請人按個鍵。

要取得開機 log：請人按板上 RST 鍵。本腳本靠「讀取時丟出 SerialException」偵測斷線後
重連，實測這片板子按 RST 會讓 USB CDC 重新列舉、確實會觸發（畫面會出現 `=== detached ===`
接著 `=== attached ===`）。但這條路徑依賴 host 真的看到 detach，不保證每片板子、每次
都成立；**若按下 RST 後遲遲沒出現那兩行，就直接拔插 USB 強制重新列舉**，不要空等。
"""
import glob
import sys
import time

import serial

# 連線參數
BAUD = 115200            # 韌體 Serial.begin() 的鮑率（bit/s），兩邊必須一致
READ_TIMEOUT_S = 0.2     # 單次 read() 阻塞上限（秒）。夠短才能及時察覺斷線，
                         #   又不必忙碌輪詢
READ_CHUNK_BYTES = 4096  # 單次 read() 上限（bytes）。log 是行導向的小量文字，
                         #   這個大小一次就能吃完整批輸出
RECONNECT_DELAY_S = 0.5  # 找不到 port 或開啟失敗時的重試間隔（秒）

# 輸出 log 檔路徑：argv[1] 指定，否則用預設值
LOG_PATH = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ems_serial.log"

# 監聽起始時刻（epoch 秒），每行時間戳都以此為基準，方便和操作步驟對時間
start = time.time()


def find_port():
    """找出當下的 usbmodem port。

    名稱會隨 USB 重新列舉漂移（cu.usbmodem101 → cu.usbmodem1101），所以每次重連
    都重新掃描，不記住上一次的結果。

    @return 裝置路徑字串（如 "/dev/cu.usbmodem1101"）；找不到時為 None
    """
    # STEP 01: 掃出所有候選裝置並排序，讓多個裝置時的選擇是穩定的
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))  # 候選裝置路徑清單

    # STEP 02: 取第一個；沒有候選就回 None 交給呼叫端重試
    return ports[0] if ports else None


def open_port(port):
    """開啟連接埠。

    刻意用「帶 port 的建構子一步到位」，讓 DTR/RTS 維持 pyserial 預設的拉高狀態——
    改成先建未開啟物件再設 `dtr/rts = False` 會觸發 USB_UART_CHIP_RESET 把板子重開，
    實測結論見檔頭第 1 點。`dsrdtr`／`rtscts` 傳 False 是停用硬體流量控制（這條線路
    沒接流控訊號），不是「不碰控制線」的意思。

    @param port 裝置路徑（find_port() 的回傳值）
    @return 已開啟的 serial.Serial 物件；開啟失敗時為 None
    """
    # STEP 01: 開啟；裝置正在重新列舉時會失敗，交給呼叫端重試
    try:
        return serial.Serial(port, BAUD, timeout=READ_TIMEOUT_S,
                             dsrdtr=False, rtscts=False)
    except serial.SerialException:
        return None


def emit(log, text):
    """把一段文字逐行加上時間戳，同時輸出到終端機與 log 檔。

    @param log  已開啟的 log 檔物件（text mode）
    @param text 要輸出的文字，可含多行；空行會被略過
    @return None
    """
    # STEP 01: 逐行處理，讓多行輸入的每一行都有自己的時間戳
    for line in text.splitlines():
        # STEP 01.01: 時間戳為「監聽開始後第幾秒」，不是絕對時間——驗收要對照的是
        #   操作順序，相對秒數比時鐘好讀
        stamped = f"[{time.time() - start:6.1f}s] {line}"  # 加上時間戳的單行輸出

        # STEP 01.02: 兩邊都寫，並立刻 flush；驗收時是邊做邊看，不能等緩衝滿才出現
        print(stamped)
        log.write(stamped + "\n")
        log.flush()


def main():
    """持續監聽並記錄 serial 輸出，斷線後自動重連，直到 Ctrl-C。

    @return None
    """
    with open(LOG_PATH, "w", encoding="utf-8") as log:
        # STEP 01: 標記監聽起點，讓 log 檔自身就能看出這是哪一輪的紀錄
        emit(log, "=== monitor start ===")

        buf = ""    # 尚未遇到換行的殘餘位元組，跨 read() 累積
        ser = None  # 目前的連接埠物件；None 代表尚未連上或剛斷線

        # STEP 02: 主迴圈——連線、讀取、斷線重連三態循環
        while True:
            # STEP 02.01: 尚未連上（或剛斷線）→ 重找 port 並重開
            if ser is None:
                port = find_port()  # 當下的裝置路徑，可能為 None
                if port is None:
                    time.sleep(RECONNECT_DELAY_S)
                    continue
                ser = open_port(port)
                if ser is None:
                    time.sleep(RECONNECT_DELAY_S)
                    continue
                emit(log, f"=== attached {port} ===")
                continue

            # STEP 02.02: 讀取；斷線（按 RST／拔線／板子重開）就回 STEP 02.01 重連
            try:
                chunk = ser.read(READ_CHUNK_BYTES)  # 本次讀到的原始位元組
            except serial.SerialException as e:
                emit(log, f"=== detached: {e} ===")
                try:
                    ser.close()
                except OSError:
                    # 裝置已消失時 close() 也可能失敗；此處只需釋放參照，
                    # 讓下一輪重新開啟，失敗本身沒有額外資訊
                    pass
                ser = None
                continue

            # STEP 02.03: 以換行為界輸出，避免半行被切開難讀；讀到空資料是正常的
            #   （韌體靜默），不視為斷線
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    emit(log, line)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[info] 監聽結束")
