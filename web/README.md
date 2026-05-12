# EMS Timer Sync — Phase F Web 驗證

> Phase F「先網頁、後 App」策略的網頁實作。詳見專案根目錄 [`docs/phase-f-web-validation-plan.md`](../docs/phase-f-web-validation-plan.md)。

## 目的

- 用瀏覽器（Chrome / Edge）透過 Web Bluetooth 接收 ESP32 BLE NUS 推送的 case_sync payload
- 寫入 Cloudflare D1（SQLite-compatible）
- 列表頁顯示所有案件，時間倒序，可排序

**非正式 App。** 僅為 BLE 協定 / 配對碼流程 / 資料 schema 的驗證載體。

## 目錄

```
web/
├── public/                 # 靜態前端（F-6 之後填入）
│   ├── index.html
│   ├── cases.html
│   ├── js/
│   └── css/
├── functions/              # Cloudflare Pages Functions（已建）
│   ├── _middleware.ts      # CORS
│   └── api/
│       ├── cases.ts        # GET 列表 / POST 寫入
│       └── cases/[id].ts   # GET 單筆
├── migrations/
│   └── 0001_init.sql       # cases + events schema
├── wrangler.toml
├── tsconfig.json
└── package.json
```

## 開發環境

### 一次性設定

```bash
cd web/
npm install
npx wrangler login
npm run db:create        # 建立 D1 database
# 把回傳的 database_id 填回 wrangler.toml
npm run db:migrate       # 本機 migration
```

### 本機跑

```bash
npm run dev
```

預設 http://localhost:8788

### 雲端部署

**首次部署**：

```bash
# 1. 建立真實 D1 cloud DB（會回傳 UUID）
npm run db:create

# 2. 把回傳的 database_id 貼到 wrangler.toml
#    取代本機開發用的 placeholder "00000000-0000-4000-8000-000000000001"

# 3. 一鍵部署（remote migration + Pages deploy）
npm run deploy
```

**後續更新**：

```bash
# 只改前端 → 直接 pages deploy
npm run deploy:pages-only

# 加新 migration → 跑 deploy.sh（會自動 apply remote migration + pages deploy）
npm run deploy
```

`deploy.sh` 會先檢查 `wrangler.toml` 不是 placeholder UUID 才會繼續，避免誤把本機開發配置推上雲端。

## API

### `POST /api/cases`

接收 case_sync payload。Body 對齊 `docs/pm-dev-spec.md §14.1`：

```json
{
  "type": "case_sync",
  "case_id": "uuid-v4",
  "mode": "ohca",
  "started_at_ms": 1745740800000,
  "events": [{ "type": 0, "timestamp_ms": ..., "elapsed_ms": ... }]
}
```

回傳：

```json
{ "ok": true, "case_id": "...", "synced_at": 1700000000, "event_count": 45 }
```

**Case ID 去重**：`INSERT OR REPLACE`，同案重傳會覆蓋舊資料。

### `GET /api/cases?sort=<key>`

可用 sort key（白名單）：

- `started_at_desc`（預設）
- `started_at_asc`
- `synced_at_desc`
- `synced_at_asc`
- `mode_asc`
- `device_id_asc`

### `GET /api/cases/:id`

單筆 case 完整資料（含 events，按 `elapsed_ms` 升序）。

## 已知限制

- Web Bluetooth 僅 Chromium 系列支援（Chrome / Edge / Brave / 安卓 Chrome）
- iOS Safari、Firefox 完全不支援
- 必須由使用者手勢觸發（page load 自動跑會被擋）
- 必須 HTTPS（Cloudflare Pages 預設啟用；本機 localhost 例外可用）
