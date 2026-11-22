# Configuration

All runtime configuration is supplied through environment variables. Production
deployments keep them in `/etc/ats-capi.env` (mode `0600`, owner `root`), which
systemd reads before dropping privileges to the `ats` user.

## Variables

| Variable | Default | Description |
|---|---|---|
| `ATS_BASE_URL` | `http://127.0.0.1:3001` | Public HTTPS URL used for tracking snippets, redirects and the language switch. Must be set in production. |
| `ATS_COOKIE_DOMAIN` | `127.0.0.1` | Domain for the `auth_jwt` cookie. `ats_lang` is host-only. Set to the site domain in production. |
| `ATS_ADMIN_USER` | `admin` | Admin user name. |
| `ATS_ADMIN_PASSWORD` | none | Plaintext admin password; hashed at startup. |
| `ATS_ADMIN_PASSWORD_SHA256` | none | 64-hex SHA-256 digest. Preferred over plaintext; written by the install and password scripts. Treat it as a password-equivalent secret. |
| `ATS_JWT_KEY` | generated | HS256/JWT key, also used for `/wru` HMACs and visitor hashes. If unset, `data/jwt.key` is created with mode `0600`. |
| `ATS_DEFAULT_LOCALE` | `zh-CN` | Default UI locale. Supported: `zh-CN`, `en`. |
| `ATS_TZ_OFFSET_MIN` | `480` | Local offset in minutes used for display and day buckets. |
| `ATS_SESSION_SECONDS` | `43200` | JWT/cookie lifetime in seconds. |
| `ATS_RETENTION_DAYS` | `365` | Traffic detail retention; enforced by the maintenance script. |
| `ATS_ANONYMIZE_IP` | `0` | `1` stores IPv4 `/24` and IPv6 `/48` addresses. GeoIP lookup still uses the real address. |
| `ATS_RESPECT_DNT` | `1` | `1` ignores requests with `DNT: 1`; set `0` to track them. |
| `ATS_DB_PATH` | `<root>/data/ats.db` | SQLite database path. |
| `ATS_GEOIP_DB_PATH` | `<root>/data/GeoLite2-Country.mmdb` | GeoIP database path. |

`<root>` is the directory above the binary (`/webapps/ats-c` in the example
deployment). The binary resolves it from `/proc/self/exe`.

The maintenance scripts (not the service) also read:

| Variable | Default | Used by |
|---|---|---|
| `ATS_BACKUP_DIR` | `/webapps/ats-c/backup` | `tools/maintenance.sh` |
| `ATS_BACKUP_KEEP` | `14` (days) | `tools/maintenance.sh` |
| `ATS_GEOIP_CONF` | unset | `tools/update-geoip.sh` (path to the MaxMind updater config) |
| `ATS_GEOIP_SRC` | legacy path | `tools/update-geoip.sh` fallback copy source |

## Admin credentials

The install script generates a 24-character random password, stores only its
SHA-256 digest in `/etc/ats-capi.env` and prints the password once. Change it
later with:

```sh
sudo /webapps/ats-c/tools/set-password.sh
```

If neither password variable is set, login is disabled and a warning is logged.

## JWT and visitor keys

`ATS_JWT_KEY` is used for:

- admin session JWTs,
- `/wru` HMAC tokens,
- visitor identifiers (`HMAC-SHA256(UA + stored IP)`).

Rotating the key invalidates existing admin sessions and changes future visitor
identifiers. It does not rewrite historical rows.

## Locale selection

Order:

1. `?lang=<code>` query parameter (mainly for the PV badge),
2. `ats_lang` cookie (set by the language switcher),
3. browser `Accept-Language`,
4. `ATS_DEFAULT_LOCALE`.

See [Internationalization](i18n.md).

## Base URL and cookies

`ATS_BASE_URL` must match the public origin exactly:

- scheme (`https://`),
- host,
- no trailing slash.

A path prefix such as `https://example.com/ats` is not supported by the
built-in router; mount ATS at the origin root or add an explicit reverse-proxy
rewrite.

If it does not match, login cookies and tracking snippets point at the wrong
origin, and the `Origin` CSRF check rejects browser POSTs. This is the most
common deployment mistake.

## Timezone and retention

Traffic timestamps are stored in UTC. `ATS_TZ_OFFSET_MIN` only affects display
and the “today / yesterday / last 7 days” buckets. The maintenance script uses
`ATS_RETENTION_DAYS` to delete old detail rows.

## File locations

| Path | Purpose |
|---|---|
| `/etc/ats-capi.env` | Runtime configuration and secrets (0600 root) |
| `/etc/GeoIP-ats.conf` | MaxMind updater configuration (0600 root) |
| `/webapps/ats-c/bin/ats-capi` | Binary |
| `/webapps/ats-c/data/ats.db` | SQLite database (0600 ats) |
| `/webapps/ats-c/data/GeoLite2-Country.mmdb` | Optional GeoIP database |
| `/webapps/ats-c/backup/` | Daily backups (0750 ats, files 0640 root) |
| `/webapps/ats-c/log/` | pid and runtime logs |
| `/opt/ats-src` | Source tree for rebuilds |

Never commit `ats.db`, `jwt.key`, `*.env`, GeoIP databases or backups.
