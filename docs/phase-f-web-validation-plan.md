# Phase F — Web 驗證階段實作計畫

> 對應 Impl-Phase F「App 配對碼同步」。本計畫為**先網頁、後 App** 的兩段式實作策略。第一段先用網頁驗證 BLE 通訊、配對碼流程與資料模型，藍牙端走通後再投資 React Native App。
>
> **建立日期**：2026-05-12
> **狀態**：規劃中（尚未動工）
> **前置依賴**：Impl-Phase E（持久化）已完成 ✅；BLE NUS lib 尚未建立 ⏳

---

## 1. 為什麼先做網頁

| 維度 | 走 App | 走網頁 |
|------|--------|--------|
| 環境啟動成本 | RN 工具鏈 / 模擬器 / 真機簽章 | Chrome 即可 |
| BLE debug | 真機 console 不便 | DevTools 直接看 |
| 迭代速度 | rebuild 慢 | F5 即可 |
| 雲端後端 | 仍需做 | 一次到位 |
| 取代 App？ | — | 否，但為 V1 完成後測試/Demo 載體 |

**決策**：網頁是 BLE 協定、配對碼流程、資料 schema 的 **驗證載體**。
- **Phase F 階段**：先做 BLE 鏈路 + D1 落地（mock 簡化版）
- **V1 完成後**：擴展網頁 UI **完整對齊 SoT §17**（4 頁籤 / 交班摘要 / 完整總覽 / 備註 / 複製 / 分列表），作為**測試 + Demo 載體**。給人看 Demo 不用裝 App，URL 一丟即可。
- **正式 App 階段**：另外做 React Native App，網頁與 App 共存（不互斥）。

---

## 2. 範圍邊界

### Phase F-Web 必做（分兩階段）

**階段 1 — BLE 鏈路驗證**（F-1 ~ F-8）：

- ✅ Web Bluetooth API 連線 ESP32 BLE NUS
- ✅ 配對碼 4 位數 + 120s TTL（對齊 SoT §16.4）
- ✅ 主鍵確認步驟（對齊 SoT §16.5）
- ✅ 單案 JSON 接收（對齊 pm-dev-spec §14.1 payload schema）
- ✅ Cloudflare D1 儲存 cases + events
- ✅ 案件列表頁面（時間倒序、欄位可排序）
- ✅ 韌體端 BLE NUS lib + 配對碼 lib

**階段 2 — 對齊 SoT §17 完整 UI**（F-9）：

- ✅ 案件詳細頁 4 頁籤（交班摘要 / 完整總覽 / Timeline / 備註）對齊 §17.2
- ✅ 交班摘要格式 + 一鍵複製對齊 §17.3
- ✅ 完整總覽 13 欄對齊 §17.4
- ✅ 備註 5 欄（到院時間 / ROSC / 交班對象 / 特殊狀況 / 其他）對齊 §17.5，**不回寫裝置**
- ✅ 複製功能（快速摘要 / 完整 Timeline / 交班摘要）對齊 §17.6
- ✅ App 端刪除（只刪 D1，不回裝置）對齊 §17.7
- ✅ OHCA / Training 分列表對齊 §17.8

### Phase F-Web 不做

- ❌ 行動 App（另外案，與網頁共存而非互斥）
- ❌ 雲端帳號登入（SoT §2.2 排除）
- ❌ 病患個資輸入（SoT §2.2 排除）
- ❌ OTA、韌體版本管理（不在 Phase F 範圍）
- ❌ 多裝置同時連線（單裝置即可驗證）

### 與 pm-dev-spec §17 的關係

pm-dev-spec §17.3 規範 App 用 SQLite 本地儲存兩張表（cases + notes）。本計畫的 Cloudflare D1 **採用相同 schema**（含 notes 表），讓網頁實作可作為 App schema 的參考實作。

**定位**：
- 網頁 = 雲端版 §17 介面（V1 完成後測試 + Demo 用，URL 一丟即可看）
- App = 本機版 §17 介面（離線可用、無雲端依賴）
- 兩者欄位定義完全對齊，並行不衝突

---

## 3. 技術選型

### 3.1 前端

| 項目 | 選用 | 理由 |
|------|------|------|
| 框架 | **Vanilla HTML + JS**（單檔 or 雙檔） | 救護畫面只有 2 頁，引入框架反而拖累 |
| BLE | **Web Bluetooth API**（瀏覽器原生） | Chromium 系列（Chrome / Edge / Brave）支援；Safari/Firefox 不支援需事先告知 |
| CSS | 純 CSS + 手機優先 viewport | 沿用 docs/demo/ 既有美學（黑底 + 急救色） |
| 部署 | Cloudflare Pages | 免費、global CDN、自帶 HTTPS（Web Bluetooth 強制 HTTPS） |

### 3.2 後端

| 項目 | 選用 | 理由 |
|------|------|------|
| Runtime | Cloudflare Pages Functions（Workers） | 免費 100k req/day 對驗證階段綽綽有餘 |
| DB | **Cloudflare D1**（SQLite-compatible） | 免費 5GB / 5M rows read per day |
| Schema migration | `wrangler d1 migrations` | 官方工具，無需另外設計 |

### 3.3 韌體

| 項目 | 選用 | 理由 |
|------|------|------|
| BLE stack | NimBLE-Arduino | ESP32 主流選擇，比 ESP32 BLE Arduino 省 RAM |
| Service | Nordic UART Service (NUS) | App 端 lib 多、debug 工具豐富 |
| Payload 格式 | JSON（chunked notify） | 對齊 pm-dev-spec §14.1，後期可切自訂 GATT |

---

## 4. 系統架構

```
┌────────────────────────────────────────────────────────────────┐
│                       ESP32-S3 韌體                              │
│                                                                  │
│  ┌────────────┐   ┌──────────┐   ┌─────────────┐                │
│  │ ems_storage│──>│ble_nus   │──>│ems_pairing  │                │
│  │ (LittleFS) │   │(GATT srv)│   │(4-digit TTL)│                │
│  └────────────┘   └──────────┘   └─────────────┘                │
└────────────────────────────────────────────────────────────────┘
                          ↕  BLE 5.0 / NUS
┌────────────────────────────────────────────────────────────────┐
│                   Browser (Chrome / Edge)                        │
│                                                                  │
│  ┌──────────────┐   ┌────────────────┐   ┌──────────────┐      │
│  │ index.html   │──>│ ble-client.js  │──>│ table-view.js│      │
│  │ (1/5 button) │   │ (Web BT scan)  │   │ (sortable)   │      │
│  └──────────────┘   └────────────────┘   └──────────────┘      │
└────────────────────────────────────────────────────────────────┘
                          ↕  HTTPS / fetch
┌────────────────────────────────────────────────────────────────┐
│                    Cloudflare Pages                              │
│                                                                  │
│  ┌──────────────────┐         ┌──────────────────┐              │
│  │ /api/cases (POST)│────────>│ Cloudflare D1     │             │
│  │ /api/cases (GET) │<────────│ cases + events    │             │
│  └──────────────────┘         └──────────────────┘              │
└────────────────────────────────────────────────────────────────┘
```

**關鍵流程**（對齊 SoT V1 §16.4 + §16.5，**雙保險：配對碼 + 主鍵確認**）：

1. 使用者開瀏覽器 → 點「連線」大按鈕
2. Web Bluetooth 跳系統選單 → 選 ESP32（DSP-xxxx）
3. ESP32 螢幕顯示 4 位配對碼 + 120s 倒數
4. 網頁要求輸入配對碼 → 透過 NUS RX 送回 ESP32
5. ESP32 驗證碼通過 → **螢幕顯示「App 已連線，按主鍵開始同步」等待主鍵**
6. 使用者按裝置主鍵 → ESP32 透過 NUS TX 推送單案 JSON（分塊）
7. 網頁組裝完整 JSON → POST 到 `/api/cases`
8. 表格頁拉取 `/api/cases?sort=started_at_desc`

> ⚠️ **不要省主鍵確認步驟**。SoT §16.5 明確規範「→ App 已連線 → 按主鍵開始同步 → 同步中」，雙保險的目的是：配對碼防誤連到鄰近裝置，主鍵確認防「碼對了但操作者還沒準備好」就開傳。

---

## 5. 資料模型

### 5.1 韌體 → 網頁 BLE Payload

對齊 pm-dev-spec §14.1，無變更：

```json
{
  "type": "case_sync",
  "case_id": "uuid-v4",
  "mode": "ohca",
  "device_name": "安康91",
  "device_id": "DSP-0001",
  "fw_version": "v1.0.0",
  "started_at_ms": 1745740800000,
  "ended_at_ms":   1745741700000,
  "events": [
    { "type": 0, "timestamp_ms": ..., "elapsed_ms": ..., "count": 1, "actual_time_null": false }
  ],
  "summary": { /* ohca_case_summary_t */ }
}
```

### 5.2 Cloudflare D1 Schema

對齊 pm-dev-spec §17.3 的 App SQLite schema（cases + notes 兩張表 + events）。

```sql
-- 案件主表（migrations/0001_init.sql）
CREATE TABLE cases (
  case_id      TEXT PRIMARY KEY,
  mode         TEXT NOT NULL,         -- 'ohca' / 'training'
  device_name  TEXT,
  device_id    TEXT,
  fw_version   TEXT,
  started_at   INTEGER NOT NULL,      -- epoch ms
  ended_at     INTEGER,
  synced_at    INTEGER NOT NULL,      -- 收到 payload 的伺服器時間
  raw_json     TEXT NOT NULL,         -- 完整 payload，避免欄位漂移
  created_at   INTEGER DEFAULT (unixepoch() * 1000)
);

CREATE INDEX idx_cases_started_at ON cases(started_at DESC);
CREATE INDEX idx_cases_synced_at  ON cases(synced_at DESC);

-- 事件序列（migrations/0001_init.sql）
CREATE TABLE events (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  case_id         TEXT NOT NULL,
  event_type      INTEGER NOT NULL,
  timestamp_ms    INTEGER NOT NULL,
  elapsed_ms      INTEGER NOT NULL,
  count           INTEGER DEFAULT 1,
  actual_time_null INTEGER DEFAULT 0,  -- SQLite boolean
  FOREIGN KEY (case_id) REFERENCES cases(case_id) ON DELETE CASCADE
);

CREATE INDEX idx_events_case_id ON events(case_id);

-- 備註表（migrations/0002_notes.sql，F-9 加入）
-- 對齊 pm-dev-spec §17.5 App 備註欄位
CREATE TABLE notes (
  case_id              TEXT PRIMARY KEY,
  hospital_arrival_at  INTEGER,             -- 到院時間（epoch ms）
  rosc                 INTEGER,             -- ROSC 有無（0/1）
  handover_to          TEXT,                -- 交班對象
  special_situation    TEXT,                -- 特殊狀況
  other                TEXT,                -- 其他備註
  updated_at           INTEGER NOT NULL,
  FOREIGN KEY (case_id) REFERENCES cases(case_id) ON DELETE CASCADE
);
```

**為什麼存 `raw_json`**：對應 pm-dev-spec §17.3 同樣策略，避免後續欄位增刪導致歷史資料無法重建。

**Case ID 去重**：`INSERT OR REPLACE` 對應 pm-dev-spec §17.4 + SoT §16.8。

**notes 表為什麼跟 cases 1:1（PK = case_id）**：對齊 pm-dev-spec §17.3 設計，每個 case 最多一筆備註。`INSERT OR REPLACE` 直接覆蓋。

**備註不回寫裝置**：對齊 SoT §17.5 與 pm-dev-spec §17。網頁/App 端修改 notes，**不會**透過 BLE 寄回 ESP32，裝置端原始紀錄保持不變。

---

## 6. UI/UX 設計

### 6.1 頁面總覽

兩頁：

1. **`/`（連線頁）**：大按鈕 + 配對碼輸入 + 同步進度
2. **`/cases`（列表頁）**：可排序表格

### 6.2 連線頁佈局（手機優先）

```
┌────────────────────────────┐
│ EMS Timer 同步工具           │  ← header 1/10
├────────────────────────────┤
│                            │
│    [連線並接收資料]          │  ← 大按鈕 1/5 高度
│                            │
├────────────────────────────┤
│ 配對碼：[ _ _ _ _ ]         │  ← 1/10
├────────────────────────────┤
│ 同步進度：                  │
│ ─ 掃描中 / 連線中 / ...    │
│ ─ 收到 12/45 個事件         │  ← 進度區 3/10
│ ─ 寫入雲端中                │
├────────────────────────────┤
│ 最近同步：                  │
│ 2026-05-12 21:30  ohca     │  ← 摘要 2/10
│ DSP-0001 / 45 事件          │
├────────────────────────────┤
│  [查看所有案件 →]           │  ← 導頁 1/10
└────────────────────────────┘
```

**設計準則**：

- 視口：`viewport-fit=cover` + `max-width: 480px` 置中
- 連線按鈕至少佔螢幕 1/5 高度（使用者明確要求）
- 急救色系：背景 `#000` + 主色 `#dc2626`（紅）/`#10b981`（綠）/`#fbbf24`（黃）
- 字型：monospace（對齊 docs/demo/ 既有風格）

### 6.3 列表頁佈局

```
┌────────────────────────────────────────────────┐
│ EMS Timer 案件列表                              │
├──┬───────┬──────┬─────────┬─────────┬────────┤
│↕ │開始時間│ 模式 │ 裝置     │ 事件數  │ 操作   │
├──┼───────┼──────┼─────────┼─────────┼────────┤
│  │05-12  │ OHCA │ DSP-0001│   45    │ 詳細   │
│  │21:30  │      │ 安康91  │         │        │
├──┼───────┼──────┼─────────┼─────────┼────────┤
│  │05-12  │ TRAIN│ DSP-0001│   12    │ 詳細   │
│  │14:15  │      │ 安康91  │         │        │
└──┴───────┴──────┴─────────┴─────────┴────────┘
```

- 預設 `started_at DESC`（最新在上，使用者明確要求）
- 欄位點擊切 ASC/DESC，箭頭視覺反饋
- 「詳細」進個案頁（顯示 events timeline，先簡化）
- 保留 `timestamp_ms`（epoch ms）顯示，旁邊括弧顯示可讀格式

---

## 7. 韌體工作項目

按 TDD-first + 三 agent alignment gate（對齊 feedback memory `tdd_alignment_gate_workflow`）。

### 7.1 `firmware/lib/ems_pairing/`（新建）

純函式 lib，可在 native 測。

```cpp
struct PairingCode {
  char digits[5];        // "1234\0"
  uint64_t expires_at_ms;
};

PairingCode generate(uint64_t now_ms);
bool verify(const PairingCode& code, const char* input, uint64_t now_ms);
bool is_expired(const PairingCode& code, uint64_t now_ms);
```

**測試案例**：
- `generate()` 產出 4 位數字 + `now + 120_000` TTL
- `verify()` 正確碼 + 未過期 → true
- `verify()` 錯誤碼 → false
- `is_expired()` `now > expires_at` → true
- 重新 `generate()` 必須產生新碼（即使在 1ms 內呼叫兩次）

### 7.2 `firmware/lib/ble_nus/`（新建）

包裝 NimBLE NUS service。

```cpp
class BleNusService {
public:
  void begin(const char* device_name);
  void tick(uint64_t now_ms);              // 在 loop() 呼叫
  void on_rx(std::function<void(const char* data, size_t len)> cb);
  bool send(const char* data, size_t len); // chunked notify
  bool is_connected() const;
};
```

**踩雷預防**（對齊 feedback memory `ble_callback_non_blocking`）：
- NimBLE callback 只 enqueue，**不在 callback 內 delay / notify**
- 實際處理在 main loop tick() 內

**測試**：
- callback enqueue/dequeue 邏輯 unit test（mock）
- chunk 切分邏輯 unit test（測 MTU 邊界）

### 7.3 `firmware/src/case_sync_dispatcher.cpp`（新建）

協調 `ems_storage` → `ems_pairing` → `ble_nus`。

狀態機（對齊 SoT §16.5）：

```
IDLE
 → AWAITING_CONNECT          (advertise + 等 BLE 連線)
 → CODE_DISPLAYED            (產配對碼 + 顯示 + 120s 倒數)
 → AWAITING_INPUT            (等網頁透過 NUS RX 寄碼回)
 → AWAITING_MAIN_KEY         (碼對了，螢幕顯示「按主鍵開始同步」)  ← SoT §16.5
 → SENDING(chunks)           (主鍵按下，推送 JSON)
 → DONE                      (App 回 ACK，顯示「同步完成」1 秒)
 → IDLE

       ↓ timeout / 3 次錯碼 / 主鍵未按
   ERROR → IDLE
```

**關鍵 transition**：
- `AWAITING_INPUT → AWAITING_MAIN_KEY`：配對碼比對通過
- `AWAITING_MAIN_KEY → SENDING`：主鍵 single-press 觸發
- `AWAITING_MAIN_KEY → ERROR`：30 秒未按主鍵 timeout（避免卡住）
- 返回鍵可在任一階段中止（除 SENDING 必須完整推送）

### 7.4 UI 整合

- 案件總覽頁增加「同步」按鈕（主鍵長按 or 新功能表項）
- 同步中顯示配對碼 + 進度 bar
- 對齊 docs/demo/ 美學

### 7.5 驗收

- 配對碼產生 / 驗證 unit test 全綠
- 實機可被瀏覽器掃到並連線
- 一筆 OHCA case 完整同步到網頁，欄位無漏
- 同案重傳：D1 端 `INSERT OR REPLACE` 不重複
- 120s 超時：配對碼失效，必須重新產生

---

## 8. 網頁工作項目

### 8.1 專案結構

```
web/
├── public/
│   ├── index.html        # 連線頁
│   ├── cases.html        # 列表頁
│   ├── js/
│   │   ├── ble-client.js
│   │   ├── api-client.js
│   │   └── table-view.js
│   └── css/
│       └── style.css
├── functions/
│   ├── api/
│   │   ├── cases.ts      # POST + GET
│   │   └── cases/[id].ts # GET single
│   └── _middleware.ts    # CORS / 簡單 rate limit
├── migrations/
│   └── 0001_init.sql
├── wrangler.toml
└── package.json
```

### 8.2 BLE 客戶端

```js
// 對齊韌體 NUS UUID
const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX_CHAR = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // device → host
const NUS_RX_CHAR = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // host → device

async function connect() {
  const device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'DSP-' }],
    optionalServices: [NUS_SERVICE]
  });
  // ... GATT connect, start notifications, accumulate chunks
}
```

**注意事項**：
- Web Bluetooth 需 HTTPS（Cloudflare Pages 預設啟用）
- 必須由使用者手勢觸發（button click），不可 page load 自動跑
- 寫 chunk 重組邏輯（payload 可能超過 MTU 23~512B）

### 8.3 Pages Functions API

```ts
// functions/api/cases.ts
export async function onRequestPost(context) {
  const { request, env } = context;
  const payload = await request.json();
  // 驗證必要欄位
  // INSERT OR REPLACE cases
  // DELETE events WHERE case_id = ?
  // INSERT events
  // return { ok: true, case_id }
}

export async function onRequestGet(context) {
  const { env, request } = context;
  const url = new URL(request.url);
  const sort = url.searchParams.get('sort') ?? 'started_at_desc';
  const orderBy = whitelist(sort); // 防 SQL injection
  const rows = await env.DB.prepare(`SELECT ... ORDER BY ${orderBy}`).all();
  return Response.json(rows);
}
```

**安全**：
- `sort` 參數白名單，禁止 raw SQL 拼接（對齊 ~/.claude rules `security.md` SQL injection 條目）
- 預設無 auth（驗證階段內部用），但 Pages 端可加 Cloudflare Access 限 IP/email

### 8.4 表格排序

純前端排序即可（資料量小），點 header 切 ASC/DESC，sticky header。

---

## 9. Cloudflare 部署

### 9.1 一次性設定

```bash
npm i -g wrangler
wrangler login
wrangler d1 create ems-timer-cases
# 把回傳的 database_id 寫進 wrangler.toml
wrangler d1 migrations apply ems-timer-cases
```

### 9.2 部署流程

```bash
# 開發：本機跑 D1 + Functions
wrangler pages dev web/public --d1 DB=ems-timer-cases

# 部署：推 main 即可（Cloudflare Pages git 整合）
git push origin main
```

### 9.3 既有腳本對齊

專案已有 `deploy-cf-pages.sh`（S472 觀察記錄）。確認其用途，若是不同 Pages 專案則新建一個 Pages project（建議命名 `ems-timer-sync`）。

---

## 10. 配對碼安全性說明

配對碼僅為**人在現場確認連到正確裝置**用，**非密碼學等級**安全：

- 4 位數 = 10000 組
- 120s TTL
- 暴力測：理論 83 次/秒可窮舉，但 NUS 寫入有 BLE GATT overhead，現實 ~5 次/秒
- 仍可在 120s 內穿透（最壞 600 次嘗試）

**緩解**：
- 配對失敗 3 次 → 強制重新產生新碼（額外失敗 lockout 邏輯，pm-dev-spec 未規範，**本計畫加上**）
- 配對碼僅在裝置螢幕顯示，攻擊者需要視線可見
- 醫療現場威脅模型：實體接觸已被破，配對碼足夠

---

## 11. 實作順序與里程碑

### 階段 1 — BLE 鏈路驗證

| 里程碑 | 內容 | 預估工時 | 驗收 |
|--------|------|---------|------|
| **F-1 韌體 pairing lib** | TDD `ems_pairing` 純函式 | 0.5d | unit test 全綠 |
| **F-2 韌體 BLE NUS lib** | `ble_nus` + chunked notify | 1.5d | 實機 nRF Connect 可連 + 收 hello world |
| **F-3 韌體 dispatcher** | case_sync 狀態機 + 主鍵確認 + UI 整合 | 1.5d | 實機可送一筆假 case 給 nRF Connect |
| **F-4 Web BLE 客戶端** | 連線 + chunk 重組 + 配對碼 UI + 主鍵等待 UI | 1d | 連韌體可收到完整 JSON |
| **F-5 Cloudflare D1** | schema + Functions API（cases + events） | 0.5d ✅ | curl 可 POST/GET |
| **F-6 Web 列表頁** | 排序 + 簡化列表 | 0.5d ✅ | 表格可用 |
| **F-7 端到端整合** | 韌體 → 網頁 → D1 全鏈路 | 1d | 一筆 OHCA case 完整落地 |
| **F-8 驗收測試** | pm-dev-spec §四 Phase F 驗收 | 0.5d | 同案重傳不重複 + 中斷重試成功 |

**階段 1 總計**：約 7 個工作天（韌體 3.5d + 網頁/雲 2d + 整合 1.5d）

### 階段 2 — 對齊 SoT §17 完整 UI

> **時機**：V1（韌體 Phase A~H）全部完成後啟動。此階段純前端 + 後端 schema 擴充，不動韌體。

| 里程碑 | 內容 | 預估工時 | 驗收 |
|--------|------|---------|------|
| **F-9.1 notes 表 + API** | `migrations/0002_notes.sql` + `/api/cases/:id/notes` GET/PUT | 0.5d | curl 可寫入/讀出備註 |
| **F-9.2 案件詳細頁框架** | 4 頁籤路由（交班摘要 / 完整總覽 / Timeline / 備註） | 0.5d | URL `/case/:id` 4 頁籤切換正常 |
| **F-9.3 交班摘要頁** | 對齊 §17.3 格式（總時間/EPI/電擊/Amiodarone/補登 + 一鍵複製） | 1d | 文字格式 byte-by-byte 對齊 SoT 範例 |
| **F-9.4 完整總覽頁** | 對齊 §17.4 的 13 欄 | 0.5d | 13 欄全數呈現 |
| **F-9.5 Timeline 頁** | 事件時間軸（補登時間 = `-`，對齊 pm-dev-spec §四 Phase B） | 0.5d | OHCA case timeline 可視 |
| **F-9.6 備註頁** | §17.5 五欄表單 + 自動存（PUT notes） | 0.5d | 修改備註不寫回裝置（D1 only） |
| **F-9.7 複製功能** | §17.6 快速摘要 / 完整 Timeline / 交班摘要三個複製按鈕 | 0.5d | clipboard 內容正確 |
| **F-9.8 App 端刪除** | §17.7 刪 D1 + 確認對話框 | 0.5d | 列表移除，不影響韌體 |
| **F-9.9 OHCA / Training 分列表** | §17.8 兩個獨立 tab | 0.5d | Training 不混入 OHCA |

**階段 2 總計**：約 5 個工作天（純前端）

### 可並行

**階段 1**：
- F-1 + F-4 可平行（韌體 pairing 與 web BLE 框架）
- F-5 + F-6 已並行完成 ✅

**階段 2**（V1 完成後）：
- F-9.3 / F-9.4 / F-9.5 可三人並行（同一份 API，不同頁籤）
- F-9.6 + F-9.1 必須在 F-9.2 之後

---

## 12. 風險與已知限制

| 風險 | 影響 | 緩解 |
|------|------|------|
| Web Bluetooth 僅 Chromium 系列支援 | iOS Safari 完全沒有 | 文件明示「驗證階段用 PC/Android Chrome」；正式 App 解此問題 |
| Cloudflare D1 仍 beta | 偶有不穩 | 驗證階段可接受；正式 App 走本機 SQLite |
| NUS MTU 預設 23B，分塊多 | 大 case payload 慢 | 連線後協商 MTU 到 247（NimBLE 支援） |
| 配對碼 4 位數安全性低 | 理論可被暴力 | §10 已說明，加 3 次失敗 lockout |
| 韌體 BLE 與 WiFi 共存（未來） | 同 radio 干擾 | Phase F 不開 WiFi，無問題；後續 OTA 再評估 |

---

## 13. 下一步（本次討論結束後）

1. 確認本計畫範圍與優先序（使用者確認 OK 後）
2. 進 Plan Mode 寫 `tasks/todo.md` Phase F-1 ~ F-8 checkbox
3. 從 F-1 開工：先建 `firmware/lib/ems_pairing/` TDD

---

## 附錄：相關文件索引

| 文件 | 章節 | 用途 |
|------|------|------|
| `docs/pm-dev-spec.md` | §14 BLE 通訊 / §16 配對碼 / Phase F 驗收 | 上層需求 |
| `docs/EMS_DoseSync_Pro_Prototype_V1.md` | §16 配對碼 / §17 App | PM 原始規格 |
| `docs/progress.md` | 進度追蹤 | Phase F 進度回填處 |
| `docs/demo/` | UI 美學參考 | Web 樣式對齊 |
| `firmware/lib/ems_storage/` | 案件來源 | F-3 dispatcher 從此讀取 |
