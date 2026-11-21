# Architecture

## Components

```text
browser / customer site
        │  /rv.js, /wru, /pv
        ▼
      nginx  ── static /res/, /favicon.ico, /robots.txt
        │  proxy 127.0.0.1:3001
        ▼
   ats-capi (C + Kore, 2 workers)
        │
        ├── SQLite  /webapps/ats-c/data/ats.db (WAL)
        └── MaxMind GeoLite2 Country (optional, hot reload)
```

The backend binds loopback only. nginx terminates TLS, serves static assets and
injects `X-Real-IP`.

## Request flow

### Tracking

1. A page loads `<script async src="/rv.js/<app-id>">`.
2. `/rv.js/<app-id>` validates the `Referer` host against the app's up to three
   stored hosts. It does not trust the path/query of the referrer for identity.
3. It inserts a row with `record_status = 0`, the referrer as the initial
   `page_url`, the visitor IP (optionally anonymized), UA-derived fields, GeoIP
   country and a keyed visitor hash.
4. The response is a small JavaScript snippet pointing at
   `/wru/<id>?ref_url=...&page_url=...&t=<HMAC>`.
5. The browser loads `/wru/<id>`. The server verifies the HMAC, updates the row
   with the real page and referring URLs and sets `record_status = 1`.

The HMAC prevents an attacker from guessing a Snowflake ID and overwriting
another pending record.

### PV badge

`/pv/<app-id>/<n>/<container>?lang=...` returns a self-contained script that
renders total views plus the top `n` (3–5) countries and “Others”. It reads the
request locale from the query, cookie or `Accept-Language`.

### Admin

- `GET /login`, `POST /do_login` issue an HS256 JWT in an HttpOnly cookie.
- `GET /dv/<app>` renders server-side HTML for the selected app.
- `GET /create`, `/edit/<id>`, `POST /do_create`, `/do_update`, `/do_delete`
  manage apps. State-changing requests check `Origin`.
- `GET /export/<id>` returns a CSV; the header follows the request locale and
  starts with a UTF-8 BOM for Excel.
- `/lang/<code>` sets the language cookie and redirects to a validated
  same-site path.

## Data model

`apps`:

| Column | Purpose |
|---|---|
| `id` | Snowflake ID |
| `name`, `remark` | display metadata |
| `host_url`, `host_url_01`, `host_url_02` | up to three normalized host names |
| `created_at` | UTC creation time |

`apps_traffic`:

| Column | Purpose |
|---|---|
| `id` | Snowflake ID |
| `app_id` | foreign key, cascade delete |
| `page_url`, `referring_url`, `referring_host` | attribution |
| `visit_date` | UTC `YYYY-MM-DD HH:MM:SS` |
| `visitor_ip` | optionally masked |
| `user_agent`, `device_type`, `browser`, `operating_system` | UA parsing |
| `country`, `city` | GeoIP |
| `visit_duration`, `traffic_source` | reserved; currently unused |
| `record_status` | 0 = pending, 1 = URL backfilled |
| `unique_visitors` | HMAC-SHA256 visitor identifier |
| `remark` | reserved |

Indexes cover `(app_id, visit_date)`, `(app_id, record_status)`,
`visit_date`, `(app_id, country)` and `(app_id, referring_host)`.

## Security model

- Backend loopback only; nginx is the only client.
- Prepared SQL statements; no SQL string interpolation.
- HTML/JS escaping at output boundaries.
- CSV formula-injection prefixing for spreadsheet safety.
- JWT HS256 with `exp`, `nbf`, `iat` and `user_name` checks.
- Login throttle (20 attempts / 5 minutes per IP) and a global 600/min limit.
- `Origin` checks on state-changing POSTs; `SameSite=Lax` cookies.
- `/wru` HMAC tokens.
- systemd sandbox (`ProtectSystem=strict`, `ProtectHome`, `PrivateTmp`, no
  capabilities).
- Secrets in `/etc/ats-capi.env` (0600 root), JWT key in `data/jwt.key` (0600)
  or the environment.
- GeoIP update config and install backups restricted to owner-only permissions.

## i18n

`src/i18n.c` owns locale negotiation and the two catalogs in `src/lang/`.
Views receive an explicit `enum ats_locale`; there is no mutable global locale.
This keeps concurrent requests independent.

## Design constraints

- **Single binary**: config and schema are embedded; the binary plus static
  assets and an optional GeoIP database are the runtime.
- **SQLite**: simple, reliable, easy to back up; suitable for a personal tool,
  not for multi-node ingestion.
- **Client-side tracking**: no server-to-server integration; a `Referer`-less
  browser or `Referrer-Policy: no-referrer` limits attribution by design.
- **No cookies for visitors**: the visitor identifier is a keyed hash, not a
  cookie. It is pseudonymous, not anonymous.
- **Brand separation**: the code is MIT; the name/logo/domain/official releases
  are governed by `TRADEMARK.md`.
