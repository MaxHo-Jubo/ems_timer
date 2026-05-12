-- Phase F-Web 初始 schema
-- 對齊 docs/phase-f-web-validation-plan.md §5.2

CREATE TABLE IF NOT EXISTS cases (
  case_id      TEXT PRIMARY KEY,
  mode         TEXT NOT NULL,                     -- 'ohca' / 'training'
  device_name  TEXT,
  device_id    TEXT,
  fw_version   TEXT,
  started_at   INTEGER NOT NULL,                  -- epoch ms（裝置端 session 起點）
  ended_at     INTEGER,                           -- 案件結束時間
  synced_at    INTEGER NOT NULL,                  -- 伺服器收到 payload 的時間
  raw_json     TEXT NOT NULL,                     -- 完整 payload，避免欄位漂移
  created_at   INTEGER DEFAULT (unixepoch() * 1000)
);

CREATE INDEX IF NOT EXISTS idx_cases_started_at ON cases(started_at DESC);
CREATE INDEX IF NOT EXISTS idx_cases_synced_at  ON cases(synced_at DESC);

CREATE TABLE IF NOT EXISTS events (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  case_id          TEXT NOT NULL,
  event_type       INTEGER NOT NULL,
  timestamp_ms     INTEGER NOT NULL,
  elapsed_ms       INTEGER NOT NULL,
  count            INTEGER DEFAULT 1,
  actual_time_null INTEGER DEFAULT 0,             -- SQLite 用 0/1 表示 boolean
  FOREIGN KEY (case_id) REFERENCES cases(case_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_events_case_id ON events(case_id);
