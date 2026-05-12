-- Phase F-Web 階段 2：notes 表
-- 對齊 pm-dev-spec §17.3 App SQLite schema + SoT §17.5 App 備註欄位
-- 備註不回寫裝置端（SoT §17.5 強制）

CREATE TABLE IF NOT EXISTS notes (
  case_id              TEXT PRIMARY KEY,
  hospital_arrival_at  INTEGER,                      -- 到院時間（epoch ms）
  rosc                 INTEGER,                      -- ROSC 有無（0/1，SQLite boolean）
  handover_to          TEXT,                         -- 交班對象
  special_situation    TEXT,                         -- 特殊狀況
  other                TEXT,                         -- 其他備註
  updated_at           INTEGER NOT NULL,             -- 最後修改時間（epoch ms）
  FOREIGN KEY (case_id) REFERENCES cases(case_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_notes_updated_at ON notes(updated_at DESC);
