# FAQ

### Login redirects back to the login page

- `ATS_BASE_URL` must exactly match the public origin (scheme + host, no
  trailing slash).
- `ATS_COOKIE_DOMAIN` must match the site domain.
- The browser must accept the `auth_jwt` cookie; HTTPS adds `Secure`.
- If the clock is far off, JWT `nbf`/`exp` checks fail.
- Reverse proxies that rewrite the Host header can break the Origin check.

### “Origin” / 403 on login or app changes

The `Origin` header must match `ATS_BASE_URL`'s host. Access the site through
the canonical host, not an IP or an alternate domain.

### The tracking script records nothing

- The page must send a `Referer`; `/rv.js` uses it to validate the host. With
  `Referrer-Policy: no-referrer`, the browser sends no Referer, so no row is
  inserted and the script returns “create new record failed”.
- The app must be configured with the exact host (including `www` if used).
- The script URL must point at your `ATS_BASE_URL`.
- A DNT-enabled browser is skipped when `ATS_RESPECT_DNT=1`.

### Countries are empty

The GeoLite2 database is not included. Install it and set
`ATS_GEOIP_DB_PATH`, or run `geoipupdate`. The service reloads a changed file
within a minute.

### CSV opens with garbled Chinese headers

The export includes a UTF-8 BOM. If an old spreadsheet still garbles it, import
as UTF-8. English headers can be selected with `?lang=en` or the language
cookie.

### The language switch does nothing

- The `ats_lang` cookie is set by `/lang/<code>`; check that cookies are
  allowed for the site.
- `?lang=` overrides the cookie. Use `/lang/en?ref=/dv`.
- Only `zh-CN` and `en` are currently supported; other codes return 404.

### `http://` times out

Check the cloud security group and firewall. The nginx template has an HTTP to
HTTPS redirect, but port 80 must be reachable for it to work. HTTPS is the
supported path.

### `nginx: duplicate upstream "ats_capi"`

An old ATS server block is still enabled alongside the new one. Disable the old
`apps.enabled` symlink and reload nginx.

### The admin UI is public

By default the login page is reachable from the internet. Use a long random
password, keep login throttling enabled and restrict admin routes by IP, VPN or
Cloudflare Access. See `docs/operations.md`.

### Can I use it commercially / fork it?

The code is MIT; yes, including commercial use. Forks must use a different name
and visual identity and must not imply official status. See `TRADEMARK.md`.

### Do I have to donate?

No. Donations are optional and buy no features or support. See `DONATE.md`.

### How do I reset everything?

`sudo deploy/install.sh --reset-data` or stop the service and move
`data/ats.db*` aside. The schema is recreated on startup. Back up first if you
might need the data.

### Why is the database file so large/WAL growing?

SQLite WAL grows with writes and checkpoints automatically. The daily
maintenance script runs `wal_checkpoint(TRUNCATE)` and `optimize`; WAL size is
bounded by the checkpoint threshold. Keep retention tight for high-volume sites.
