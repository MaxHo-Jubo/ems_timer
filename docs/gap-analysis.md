# PM 規格 vs 現有韌體 Gap Analysis

> 基準：pm-flow-spec.md + pm-dev-spec.md（PM 目標）vs Phase 2C 現有實作
> 日期：2026-04-22

---

## 總覽

| 層級 | PM 目標 | 現況 | 差距等級 |
|------|---------|------|---------|
| 按鍵配置 | 5 鍵語意化（主鍵、上下、電源、靜音）| 8 顆藥物/系統按鈕，無明確語意對應 | 🔴 大 |
| 狀態機 | IDLE → RUNNING → PAUSE → END | 無正式狀態機，session 開機自動產生 | 🔴 大 |
| 任務控制 | 長按主鍵開始/結束任務 | 無長按觸發任務概念 | 🔴 大 |
| 節律提醒 | 6 秒 CPR 節拍 + 4 分鐘給藥提醒 | 有倒數計時但無 CPR 6 秒節拍模式 | 🟡 中 |
| 資料結構 | event_t（8 欄位）| EmsEvent（3 欄位：type, timestamp, elapsed_ms）| 🔴 大 |
| 容量 | Ring Buffer 100~500 events | MAX_EVENTS = 30 | 🟡 中 |
| 持久儲存 | SPI Flash 批次寫入 + wear leveling | 無持久儲存，重開機資料消失 | 🔴 大 |
| BLE 架構 | 主機/副機 GATT（4 service）| 單機 NUS + JSON | 🔴 大 |
| RTOS | FreeRTOS（8 task）| Arduino loop 單執行緒 | 🟡 中（可先不動）|
| 電源管理 | Li-ion + TP4056 + light sleep | USB-C 直供，無電源管理 | 🟡 中（硬體問題）|
| 硬體感測 | 電量顯示、震動馬達 | 無電量偵測、無震動馬達 | 🟡 中（硬體問題）|
| App | 完整 BLE App + SQLite + 視覺化 | 無 App | 🔴 大 |
| Backend | Cloud + REST API + 分析引擎 | 無 Backend | 🔴 大（Phase 3+）|

---

## 詳細 Gap 分析

### 1. 按鍵配置與語意（🔴 大）

**PM 期望：**
- 主鍵（正下方大鍵）：短按記錄事件，長按開始/結束任務
- 左側上下鍵：切換給藥模式 / 通氣模式
- 右側鍵（電源）：短按螢幕亮滅，長按開關機
- 左上角靜音鍵：短按標記靜音事件，長按切換靜音 ON/OFF

**現況：**
- 8 顆按鈕分 G0/G1 藥物群組（BTN1-4 = G0，BTN5-8 = 系統按鈕）
- BTN5 = 選單，BTN6 = 群組切換，BTN7/8 = 部分未完整實作
- 無長按語意、無電源鍵、無靜音鍵概念

**結論：** 按鍵物理配置需重新討論。PM 設計是 5 鍵裝置，現有硬體是 8 鍵。兩者可以對應，但語意需重新映射。

---

### 2. 狀態機（🔴 大）

**PM 期望：**
```
IDLE → (長按主鍵) → RUNNING → (短按) → PAUSE → (短按) → RUNNING
                                       → (長按) → END → IDLE
```

**現況：**
- 無明確 IDLE/RUNNING/PAUSE 狀態
- Session 開機自動建立，首次按鈕鎖定 sessionStartMs
- 無暫停機制
- 無正式結束任務流程

**影響：** 這是整個任務流程的核心骨架，缺少這層會導致後續所有功能都無法正確掛載。

---

### 3. 資料結構（🔴 大）

**PM 期望（event_t）：**
```c
typedef struct {
    uint32_t event_id;    // 事件編號
    uint64_t timestamp;   // 時間戳
    uint8_t  event_type;  // 給藥/通氣/其他
    uint8_t  source;      // 按鍵/系統
    uint8_t  device_id;   // 裝置 ID
    uint8_t  mode;        // 模式（給藥/通氣）
    char     extra_data[32]; // 備註
    uint8_t  sync_flag;   // 是否同步
} event_t;
```

**現況（EmsEvent）：**
```c
// 推測現有結構（依 CLAUDE.md 記錄）
struct EmsEvent {
    uint8_t  event_type;
    uint64_t timestamp;   // epoch ms
    uint32_t elapsed_ms;  // 從 session 開始的經過時間
    // elapsed_end_ms 已在 Phase 2C 新增（倒數結束時間）
}
```

**缺少欄位：** event_id、source、device_id、mode、extra_data、sync_flag

**建議：** 不要直接升級 struct，先確認哪些欄位真的需要，避免在現有 MAX_EVENTS=30 的 RAM 限制下塞爆。

---

### 4. 持久儲存（🔴 大）

**PM 期望：**
- SPI Flash 批次寫入（每 5 秒）
- Wear leveling
- Fail-safe 斷電保護
- 寫入後標記 sync_flag

**現況：**
- 完全沒有持久儲存
- 重開機所有事件資料消失
- 這是 MVP 的最大缺口之一

---

### 5. BLE 架構（🔴 大）

**PM 期望：**
- 主機（Central）掃描副機（Peripheral）
- GATT 4 個 Service：Device Info / Sync / Command / Event Upload
- 心跳檢測、斷線重連

**現況：**
- NUS（Nordic UART Service）+ JSON
- 單機 Peripheral，等待 App 連線
- 無主副機概念（PM 描述的主副機是兩台硬體裝置互聯，不是裝置配 App）

**重要澄清需求：** PM 文件的「主機/副機」是兩台 EMS Timer 裝置互聯，還是「裝置 + 手機 App」？這個答案會大幅影響架構方向。

---

### 6. 節律提醒（🟡 中）

**PM 期望：**
- 通氣模式：每 6 秒 BEEP（CPR 節拍器）
- 給藥模式：每 4 分鐘高頻提醒

**現況：**
- 有倒數計時後 BEEP 的機制
- 無明確「通氣模式 6 秒節拍」實作
- 無「給藥模式 4 分鐘」定時提醒

---

### 7. FreeRTOS（🟡 中，可延後）

**PM 期望：** 8 個獨立 task（Main/Timer/Input/BLE/Storage/UI/Audio/Power）

**現況：** Arduino loop 單執行緒，BLE callback 在 GATT task 跑

**判斷：** FreeRTOS 是工程品質問題，不是功能問題。MVP 階段可暫緩，但如果要做 Flash 寫入 + BLE 同時運作，遲早要切。

---

## Gap 優先順序建議

### 必須做（MVP 韌體缺口）

1. **狀態機**：IDLE / RUNNING / PAUSE / END — 整個任務流程的基礎
2. **長按主鍵觸發任務開始/結束** — 與狀態機配套
3. **持久儲存（SPI Flash）** — 資料不能掉，這是最大的功能缺口
4. **event_t struct 擴充**：至少加 event_id、mode、sync_flag

### 需要對齊（設計討論）

5. **按鍵語意重新映射**：8 鍵 vs 5 鍵概念需跟 PM 確認
6. **「主副機」定義釐清**：兩台裝置互聯 vs 裝置+App？
7. **6 秒 CPR 節拍模式**：是否要實作通氣模式

### 延後（Phase 2+）

8. App 開發
9. Backend / Cloud
10. FreeRTOS 重構
11. 震動馬達、Wi-Fi、Pogo Pin

---

## 小結

現有韌體（Phase 2C）完成了：按鈕事件記錄、BLE NUS 傳輸、OLED 顯示、蜂鳴器提醒、選單系統基礎架構。

**最大的三個缺口：**
1. 沒有任務狀態機（IDLE/RUNNING/PAUSE）
2. 沒有持久儲存（重開機資料消失）
3. 資料結構欄位不足（缺 mode、sync_flag 等）

這三件事決定了後續 App 端能不能正確接收和分析資料，是下一個開發週期的核心。
