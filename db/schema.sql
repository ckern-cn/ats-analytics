-- ATS schema (SQLite).
--
-- Storage timezone is always UTC, as TEXT 'YYYY-MM-DD HH:MM:SS'.  Display and
-- day buckets are shifted by ATS_TZ_OFFSET_MIN (default UTC+8) at query time.

PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;

PRAGMA user_version = 2;

CREATE TABLE IF NOT EXISTS apps (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL DEFAULT '',
    host_url    TEXT NOT NULL DEFAULT '',
    host_url_01 TEXT NOT NULL DEFAULT '',
    host_url_02 TEXT NOT NULL DEFAULT '',
    remark      TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT ''
);

-- A hostname belongs to exactly one app. Empty strings are excluded so an
-- app without a host (not produced by the UI) cannot clash with another.
CREATE UNIQUE INDEX IF NOT EXISTS idx_apps_host_url
    ON apps(host_url) WHERE host_url <> '';

CREATE TABLE IF NOT EXISTS apps_traffic (
    id               INTEGER PRIMARY KEY,
    app_id           INTEGER NOT NULL REFERENCES apps(id)
                         ON DELETE CASCADE ON UPDATE CASCADE,
    page_url         TEXT NOT NULL DEFAULT '',
    visit_date       TEXT NOT NULL,
    visitor_ip       TEXT NOT NULL DEFAULT '',
    user_agent       TEXT NOT NULL DEFAULT '',
    referring_url    TEXT NOT NULL DEFAULT '',
    referring_host   TEXT NOT NULL DEFAULT '',
    visit_duration   INTEGER NOT NULL DEFAULT 0,
    country          TEXT NOT NULL DEFAULT '',
    city             TEXT NOT NULL DEFAULT '',
    device_type      TEXT NOT NULL DEFAULT '',
    browser          TEXT NOT NULL DEFAULT '',
    operating_system TEXT NOT NULL DEFAULT '',
    traffic_source   TEXT NOT NULL DEFAULT '',
    record_status    INTEGER NOT NULL DEFAULT 0,
    unique_visitors  TEXT NOT NULL DEFAULT '',
    remark           TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_traffic_app_date
    ON apps_traffic(app_id, visit_date);
CREATE INDEX IF NOT EXISTS idx_traffic_app_status
    ON apps_traffic(app_id, record_status);
CREATE INDEX IF NOT EXISTS idx_traffic_visit_date
    ON apps_traffic(visit_date);
CREATE INDEX IF NOT EXISTS idx_traffic_app_country
    ON apps_traffic(app_id, country);
CREATE INDEX IF NOT EXISTS idx_traffic_app_refhost
    ON apps_traffic(app_id, referring_host);
