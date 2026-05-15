# Phase F — Web 驗證階段 TODO

> **策略**：先網頁、後 App。詳見 [`docs/phase-f-web-validation-plan.md`](../docs/phase-f-web-validation-plan.md)。
> **本檔用途**：追蹤當下進度，rate-limit 後 resume 用。
> **建立**：2026-05-12
> **執行順序**：選項 A（純前端先跑，韌體 mock）

---

## 🎯 當前進度快照（2026-05-12）

**已完成**：
- ✅ **F-5** 後端骨架（Cloudflare D1 + Pages Functions + cases GET/POST + cases/:id GET/DELETE + notes GET/PUT）
- ✅ **F-6** 前端骨架（連線頁 + 列表頁 + mock 同步流程驗證 BLE → D1 鏈路）
- ✅ **F-9** 對齊 SoT §17 完整 UI（9 個子里程碑全綠）
  - 4 頁籤詳細頁 / 交班摘要 / 完整總覽 14 欄 / Timeline / 備註 5 欄 autosave / 複製 3 處 / 刪除（D1-only） / OHCA-Training 分列表

**下一步 BLE 鏈路**（按韌體優先順序）：
1. ~~**F-1** 韌體 `ems_pairing` 純函式 TDD（0.5d）~~ ✅ 2026-05-15 完成（21 unit tests，含 review 修正）
2. **F-2** 韌體 `ble_nus`（1.5d）— 純邏輯部分 ✅ 2026-05-15（ble_chunker 17 + ble_rx_queue 16 tests，含 review 修正）；NimBLE 整合層待實機 session
3. **F-3** 韌體 `case_sync_dispatcher` 狀態機（1.5d）— 純邏輯狀態機 ✅ 2026-05-15（ems_sync_dispatcher 20 tests）；整合層（NimBLE service + JSON 序列化 + TFT UI）待實機 session
4. **F-2/F-3 整合層 + F-4a/F-4b/F-7/F-8** — 全部需實機 + 網頁端 + nRF Connect 三方協作，韌體面已盡可做
3. **F-3** 韌體 `case_sync_dispatcher` 狀態機 + **主鍵確認** state（1.5d）
4. **F-4a** 真 Web Bluetooth 連 nRF Connect mock（0.5d）
5. **F-4b** 連真韌體（0.5d）
6. **F-7 + F-8** 端到端整合 + 驗收（1.5d）

## 🎯 Resume 指引（rate-limit 後）

接續工作流程：

1. **讀 plan 文件**：`docs/phase-f-web-validation-plan.md`（戰略 + 完整脈絡）
2. **讀本檔當前進度快照**：知道做到哪
3. **看 git log**：`git log --oneline | grep -iE "phase[- ]?f"`（看上次 commit 軌跡）
4. **若有未 commit 變更**：`git status` 確認還沒收尾的工作
5. **跑網頁端**：`cd web/ && npm run dev` 自動套 migration + 起服務
6. **若要部署雲端**：`npm run deploy`（會檢查 placeholder UUID + 套 remote migration + Pages deploy）

---

## 📋 任務清單

### F-5 後端：Cloudflare D1 + Pages Functions（先做，因前端要打 API）

預估 0.5d

- [x] 建立 `web/` 目錄結構（對齊 plan §8.1）
- [x] `wrangler.toml` 初始化 + D1 binding
- [x] `web/migrations/0001_init.sql` — cases + events 表 + 索引（對齊 plan §5.2）
- [x] `web/functions/api/cases.ts` — POST 接收 payload，`INSERT OR REPLACE`
- [x] `web/functions/api/cases.ts` — GET 列表，sort 白名單
- [x] `web/functions/api/cases/[id].ts` — GET 單筆詳細
- [x] `web/functions/_middleware.ts` — CORS（驗證階段允許 localhost）
- [x] `web/package.json` + `tsconfig.json` + `.gitignore` + `README.md`
- [x] **使用者手動執行**：`cd web/ && npm install && npx wrangler login`（D1 cloud DB 暫不建，本機用假 UUID）
- [x] **使用者手動執行**：`npm run dev` 自動跑 migration + 啟動 server
- [x] 本機 mock 同步流程驗證通過（瀏覽器點按鈕 → 寫入 D1 → 列表頁顯示）
- [ ] 部署到 Cloudflare Pages（雲端部署時才需 `wrangler d1 create` 取得真 UUID 替換 wrangler.toml）

**驗收**：curl 可 POST 一筆假 payload，GET 列表可拿回，時間倒序。

**Note**：D1 建立、登入這些操作要使用者本機跑，AI 無法代執行（需互動認證）。建議使用者跑：

```bash
cd web/
npm install
npx wrangler login    # 開瀏覽器登入 Cloudflare
npm run db:create     # 把回傳的 database_id 貼回 wrangler.toml
npm run db:migrate    # 本機 D1 跑 migration
npm run dev           # 本機起服務
```

然後丟我這個 curl 命令驗證：

```bash
curl -X POST http://localhost:8788/api/cases \
  -H "Content-Type: application/json" \
  -d '{"type":"case_sync","case_id":"test-001","mode":"ohca","started_at_ms":1745740800000,"events":[{"type":0,"timestamp_ms":1745740900000,"elapsed_ms":100000,"count":1}]}'

curl http://localhost:8788/api/cases
```

---

### F-6 前端：列表頁 + 連線頁骨架（靜態 mock）

預估 0.5d

- [x] `web/public/index.html` — 連線頁佈局（plan §6.2）
  - [x] 大按鈕 `min-height: 20vh`（≥ 1/5 螢幕高）
  - [x] 配對碼輸入框（4 格 digit input + 自動跳格 + Backspace 倒退）
  - [x] 同步進度區（progress-log with active/done/error 三種狀態）
  - [x] 配對碼倒數 TTL 顯示
  - [x] 最近同步摘要區
- [x] `web/public/cases.html` — 列表頁佈局（plan §6.3）
  - [x] 表格 + sticky header
  - [x] 預設 `started_at DESC`
  - [x] 欄位點擊切 ASC/DESC + 箭頭視覺（▲/▼）
  - [x] 保留 `timestamp_ms` 顯示（雙欄位：可讀時間 + raw ms）
- [x] `web/public/css/style.css` — 對齊 docs/demo/ 黑底急救色 + monospace
- [x] `web/public/js/api-client.js` — fetch wrapper + sort key 白名單前端側驗證
- [x] `web/public/js/table-view.js` — 後端排序（點欄位重打 API）
- [x] `web/public/js/connect.js` — mock 同步流程 + POST `/api/cases` 驗證鏈路
- [ ] **使用者驗證**：本機 `npm run dev` 後開瀏覽器跑通

**驗收**：本機開頁 → 點按鈕（mock 行為）→ 寫進 D1 → 列表頁顯示 → 點欄位可排序。

**Note**：F-6 的 `connect.js` 是 mock，F-4a 會把 `simulateSync()` 換成真 Web Bluetooth。模式設計可平滑替換。

---

### F-4a 前端：Web Bluetooth scanning（先連 nRF Connect mock）

預估 0.5d

> **依賴**：F-5 + F-6 完成。**不需要韌體**，用手機 nRF Connect App 假扮 BLE peripheral。

- [ ] `web/public/js/ble-client.js` — `navigator.bluetooth.requestDevice()` 連線
- [ ] NUS UUID 常數（對齊 plan §8.2）
- [ ] startNotifications + chunked payload 重組
- [ ] **配對碼輸入後送回裝置** UI 流程：輸完 4 位數 → 透過 NUS RX 寫回
- [ ] **「等待裝置端確認」狀態** UI（對齊 SoT §16.5：裝置端螢幕跳「按主鍵」時，網頁同步顯示「請按裝置主鍵」）
- [ ] payload 完整後 POST 到 `/api/cases`
- [ ] 同步進度 UI 更新（配對碼輸入 / 等主鍵 / 接收中 / 寫雲端中 / 完成）
- [ ] 錯誤處理：連線斷 / 配對失敗 / 主鍵 timeout / payload 解析失敗
- [ ] 用 nRF Connect 模擬 NUS server 推假 JSON 驗證鏈路

**驗收**：手機 nRF Connect 假扮 ESP32 → 網頁連到 → 配對碼輸入 → 等主鍵確認（mock 端手動觸發） → 收到 mock case_sync payload → 寫入 D1 → 列表頁可看到。

**Resume 提示**：如果做到一半才發現 nRF Connect 操作太麻煩，可以直接跳 F-2（韌體 hello world NUS），下面有詳細項目。

---

### F-1 韌體：`ems_pairing` 純函式 lib（TDD）

預估 0.5d

> **可與 F-5/F-6 並行**（純邏輯不依賴 BLE）。

- [x] `firmware/lib/ems_pairing/ems_pairing.h` — interface（plan §7.1）
- [x] `firmware/test/test_pairing/test_main.cpp` — Unity tests（RED）
  - [x] `generate()` 產 4 位數 + `now + 120_000` TTL
  - [x] `verify()` 正確碼 + 未過期 → true
  - [x] `verify()` 錯誤碼 → false
  - [x] `verify()` 過期 → false
  - [x] `is_expired()` 邊界（`now == expires_at` 視為過期）
  - [x] 1ms 內連續 `generate()` 必須產生不同碼（用 entropy + counter）
  - [x] **3 次失敗 lockout**（plan §10）
- [x] `firmware/lib/ems_pairing/ems_pairing.cpp` — 實作（GREEN）
- [x] `pio test -e native -f test_pairing` 全綠（15 tests / 加碼 null input + 長度錯 + lockout cap + regen clear lockout）

**驗收**：unit test 全綠，覆蓋率含 happy path + 過期 + lockout。✅ 2026-05-15 完成，278 native tests / 277 PASSED（test_storage_hw baseline ERROR 不在此範圍）。

---

### F-2 韌體：`ble_nus` lib（NimBLE 包裝）

預估 1.5d

> **依賴**：F-1（pairing 已驗）+ 確定要接真韌體。

- [x] 評估既有 `firmware/lib/ble_nus`：pm-dev-spec §14.1 提到「延用既有」實際不存在 → 全新建立
- [x] **F-2 純邏輯子模組（native TDD 可覆蓋）**：
  - [x] `firmware/lib/ble_chunker/` — chunk_size_from_mtu / chunk_count / chunk_at（16 tests）
  - [x] `firmware/lib/ble_rx_queue/` — SPSC ring buffer init/push/pop/empty/full/size（12 tests）
- [ ] **F-2 實機整合層（需 ESP32 + nRF Connect，留待實機 session）**：
  - [ ] `BleNusService::begin()` advertise + GATT setup（NimBLE-Arduino）
  - [ ] `BleNusService::tick()` 把 ble_rx_queue dequeue 接到 callback
  - [ ] `BleNusService::send()` 用 ble_chunker 切分 + MTU 247 協商
  - [ ] hello world sketch：advertise 並 echo RX
  - [ ] 實機用 nRF Connect 驗證連線 + 收發

**驗收**：手機 nRF Connect 掃到 `DSP-xxxx`，連線後可雙向收發字串（純邏輯部分 28 unit tests ✅ 2026-05-15）。

---

### F-3 韌體：`case_sync_dispatcher` 狀態機

預估 1.5d

> **依賴**：F-1 + F-2 + `ems_storage`（已完成）。

- [x] **F-3 純邏輯狀態機**（`firmware/lib/ems_sync_dispatcher/`，20 unit tests，2026-05-15）：
  - [x] States 簡化合併 CODE_DISPLAYED → AWAITING_INPUT（顯示是 entry action，state 本質是等輸入）：
        `IDLE / AWAITING_CONNECT / AWAITING_INPUT / AWAITING_MAIN_KEY / SENDING / DONE / ERROR`
  - [x] Events：START / BLE_CONNECTED / BLE_DISCONNECTED / MAIN_KEY_PRESS / BACK_KEY_PRESS / CHUNK_ACKED / TICK
  - [x] dispatch_input 專用入口（payload 帶 byte buffer 避免 enum 掛 payload）
  - [x] AWAITING_INPUT entry action：pairing_generate（120s TTL）
  - [x] AWAITING_INPUT → ERROR：120s pairing 過期 / 3 次失敗 lockout
  - [x] AWAITING_MAIN_KEY → ERROR：30s MAIN_KEY_TIMEOUT_MS
  - [x] SENDING → DONE：sent_chunks_count >= total_chunks
  - [x] DONE → IDLE：DONE_DISPLAY_MS = 1s
  - [x] ERROR → IDLE：ERROR_DISPLAY_MS = 2s
  - [x] BLE_DISCONNECTED 任一非 IDLE state → ERROR
  - [x] BACK_KEY 任一非 IDLE/SENDING state → IDLE（SENDING 中無法中止）
- [x] **F-3 JSON 序列化 lib**（`firmware/lib/case_sync_serializer/`，6 unit tests，2026-05-15）：
  - [x] 對齊 pm-dev-spec §14.1 case_sync schema
  - [x] CaseSyncMeta struct + case_sync_serialize_to_json() 純函式
  - [x] 涵蓋：empty case / events array / summary fields / buffer 不足 / nullptr 字串 / training mode
  - [x] ArduinoJson 7 DynamicJsonDocument 動態擴張
- [ ] **F-3 src/ 整合層（需 ESP32 + 既有 firmware/src/main.cpp + TFT，留待實機 session）**：
  - [ ] `firmware/src/case_sync_dispatcher.cpp` 包裝 NimBLE service + 接 ems_sync_dispatcher
  - [ ] 從 `ems_storage` 讀最新 case → 用 case_sync_serializer 產 JSON → 用 ble_chunker 切分 → BleNusService 推送
  - [ ] UI：案件總覽頁加「同步」入口 + 各 state TFT 畫面
  - [ ] 中斷重試 = 整筆重傳（plan §11 F-8 驗收要求）

**驗收**：實機按「同步」→ 螢幕顯示配對碼 → nRF Connect 連線 + 輸入正確碼 → **螢幕跳「按主鍵開始同步」→ 按主鍵** → 收到完整 JSON（純邏輯狀態機 20 unit tests ✅ 2026-05-15）。

---

### F-4b 前端：接真韌體

預估 0.5d

> **依賴**：F-3 + F-4a。

- [ ] 把 F-4a 的 mock 換成真連 ESP32
- [ ] device filter 改 `namePrefix: 'DSP-'`
- [ ] 配對碼 UI：使用者讀韌體螢幕碼 → 網頁輸入框送 → NUS RX 送回韌體驗證
- [ ] 驗證失敗 3 次 lockout UI 提示

**驗收**：實機 → 網頁 → D1，一筆 OHCA case 從韌體完整落到 D1，列表頁可看到。

---

### F-7 端到端整合測試

預估 1d

- [ ] 跑一筆真實 OHCA case（按鍵操作）→ 同步 → D1
- [ ] 同案重傳：`INSERT OR REPLACE` 不重複（plan §11 驗收）
- [ ] 中斷模擬：傳輸途中網頁刷新 → 重連 → 整筆重傳成功
- [ ] 大 case：50 events 滿載傳輸不丟資料
- [ ] Training case 也跑一筆，`mode` 欄位正確
- [ ] 列表頁排序所有欄位 ASC/DESC 都正常

---

### F-8 驗收（階段 1 BLE 鏈路）

對應 `pm-dev-spec §四 Phase F 驗收`：

- [ ] ✅ 同案件同步 2 次，D1 端不重複
- [ ] ✅ 中斷後重試成功
- [ ] ✅ 配對碼 4 位 + 120s TTL（過期自動重生）
- [ ] ✅ 3 次失敗 lockout 生效（plan 額外加碼）
- [ ] ✅ 主鍵確認步驟可運作（對齊 SoT §16.5）

回填 `docs/progress.md` 階段 1 進度 5：Phase F BLE 鏈路完成。

---

## 🎬 階段 2 — 對齊 SoT §17 完整 UI

> **觸發時機**：V1 韌體 Phase A~H 全部完成後啟動。階段 1 BLE 鏈路必須先驗證通過。
>
> **目的**：把網頁從「BLE 驗證工具」升級為「V1 完成後測試 + Demo 載體」。給人看 Demo URL 一丟即可。
>
> **不動韌體**，純前端 + D1 schema 擴充。

### F-9.1 後端：notes 表 + API（對齊 §17.5）

預估 0.5d

- [x] `web/migrations/0002_notes.sql` — notes 表（5 欄 + updated_at + FK on cases）
- [x] `web/functions/api/cases/[id]/notes.ts` — GET 取備註（沒有就回空殼）
- [x] `web/functions/api/cases/[id]/notes.ts` — PUT 寫備註（INSERT OR REPLACE）
- [x] 後端驗證：備註修改不觸發任何寫回裝置的邏輯（D1-only，§17.5 強制）
- [ ] curl 驗證 GET/PUT 流程（待 user 跑）

### F-9.2 前端：案件詳細頁 4 頁籤框架（對齊 §17.2）

預估 0.5d

- [x] 路由：`/case.html?id=<case_id>` → 4 tab UI（query string，避免 SPA router）
- [x] Tab 切換：交班摘要 / 完整總覽 / Timeline / 備註
- [x] 預設第一頁 = 交班摘要（§17.2 明示）
- [x] URL hash 記錄當前 tab（`#summary` / `#overview` / `#timeline` / `#notes`）

### F-9.3 前端：交班摘要頁（對齊 §17.3）

預估 1d

- [x] 從 `/api/cases/:id` 拿到 raw_json + events，前端組摘要（buildSummary）
- [x] 格式對齊 SoT §17.3 範例：
  - [x] OHCA 交班摘要 / Training 交班摘要 標題
  - [x] 總時間 / EPI / 電擊 / Amiodarone 統計（含補登）
  - [x] 第一次本機 EPI / 最後本機 EPI / 最後本機電擊 時間
  - [x] 補登：接手前 EPI ×N / 接手前電擊 ×N / 純補登 EPI ×N / 純補登電擊 ×N
- [x] 一鍵複製按鈕（對齊 §17.6）

### F-9.4 前端：完整總覽頁（對齊 §17.4）

預估 0.5d

- [x] 13 欄全數呈現（grid layout 自適應）：
  - [x] 案件 ID / 模式 / 開始時間 / 結束時間 / 總時間
  - [x] EPI 詳細 / 電擊詳細 / Amiodarone
  - [x] Timeline 計數 / 同步資訊 / 裝置名稱 / 裝置 ID / 韌體版本 / 同步時間

### F-9.5 前端：Timeline 頁（對齊 pm-dev-spec §四 Phase B）

預估 0.5d

- [x] 事件依 `elapsed_ms` 升序排（後端 SQL 已排序）
- [x] 每筆顯示：絕對時間 / 經過時間 / 事件名稱 / count
- [x] **補登事件絕對時間 + 經過時間 = `—`**（對齊 pm-dev-spec §四 Phase B）
- [x] 視覺分組：EPI 紅 / 電擊 琥珀 / Amiodarone 綠 / 補登 半透明

### F-9.6 前端：備註頁（對齊 §17.5）

預估 0.5d

- [x] 5 欄表單：到院時間（datetime-local） / ROSC（checkbox） / 交班對象（text） / 特殊狀況（textarea） / 其他（textarea）
- [x] 自動存（debounce 1s 後 PUT `/api/cases/:id/notes`）+ 狀態指示器
- [x] 明確提示「本欄位不會寫回裝置端」（§17.5 強制）— 琥珀警告卡片

### F-9.7 前端：複製功能（對齊 §17.6）

預估 0.5d

- [x] 「一鍵複製交班摘要」按鈕（在交班摘要頁）
- [x] 「完整複製 Timeline」按鈕（在 Timeline 頁）
- [x] 「複製摘要」按鈕（在案件列表頁的每筆右側）

### F-9.8 前端：App 端刪除（對齊 §17.7）

預估 0.5d

- [x] 案件列表加「刪除」按鈕（紅色 outline 樣式提示破壞性）
- [x] 確認對話框：「刪除 App 內案件？不會刪除裝置端資料」（§17.7 原文）
- [x] DELETE `/api/cases/:id`（後端：刪 cases + FK CASCADE 連帶刪 events + notes）
- [x] 刪除後重新拉取列表

### F-9.9 前端：OHCA / Training 分列表（對齊 §17.8）

預估 0.5d

- [x] 案件列表頁加兩個 tab：`[OHCA 案件]` / `[Training 紀錄]`
- [x] 預設 OHCA tab（URL hash `#ohca` / `#training` 記憶）
- [x] 後端 API：`/api/cases?mode=ohca` / `?mode=training` 篩選（白名單防注入）
- [x] **Training 不可混入 OHCA 列表**（§17.8 強制）

### F-9 階段驗收

- [ ] 4 頁籤切換流暢
- [ ] 交班摘要文字格式與 SoT §17.3 範例一致
- [ ] 完整總覽 13 欄全顯示
- [ ] Timeline 補登事件時間顯示為 `-`
- [ ] 備註修改後重啟瀏覽器仍存在（D1 持久化）
- [ ] 複製功能 clipboard 內容正確
- [ ] 刪除後韌體端資料不受影響（同步同 case 仍可重來）
- [ ] OHCA / Training 分列表正確

---

## 🚨 提交切細建議（rate-limit 友善）

每個子項目（例如 F-5 的每個 bullet）做完就 commit，commit message 帶 `phase-f` 標記方便日後 `git log --grep`：

```
[ERPD-XXXX] feat(Web): EMS Timer-phase-f-D1 schema + cases POST 端點
```

不需 Jira ID 時直接：

```
feat(Web): EMS Timer-phase-f-列表頁排序邏輯
```

這樣下次 resume 看 `git log` 就知道做到哪。

---

## 📌 已知陷阱（rate-limit 後別忘）

| 陷阱 | 來源 |
|------|------|
| Web Bluetooth 必須 HTTPS + 使用者手勢觸發 | plan §3.1 |
| iOS Safari 不支援 Web Bluetooth | plan §12 |
| BLE callback 不可阻塞，notify 搬主 loop | feedback memory `ble_callback_non_blocking` |
| sort 參數白名單，禁 raw SQL 拼接 | plan §8.3 + ~/.claude security.md |
| `pio device list` 確認當下 port，別 hardcode | feedback memory `macos_usb_port_name_drift` |
| Cloudflare D1 仍 beta，偶有不穩 | plan §12 |
| ESP32-S3 GPIO 35/36/37 不可用（N16R8 octal PSRAM） | feedback memory `esp32s3_n16r8_octal_psram` |

---

## 🔗 對應文件

| 文件 | 用途 |
|------|------|
| [`docs/phase-f-web-validation-plan.md`](../docs/phase-f-web-validation-plan.md) | 戰略 + 完整脈絡（先讀） |
| [`docs/pm-dev-spec.md`](../docs/pm-dev-spec.md) §14, §16, §17, Phase F | 上層需求 |
| [`docs/EMS_DoseSync_Pro_Prototype_V1.md`](../docs/EMS_DoseSync_Pro_Prototype_V1.md) §16 | PM 原始配對碼規格 |
| [`docs/progress.md`](../docs/progress.md) | 階段完成後回填 |
| `tasks/todo.md` | 主 TODO（含 Phase A~E 歷史，本檔僅補 Phase F） |
