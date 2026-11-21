# Installation

## Requirements

Build dependencies:

- gcc, make, pkg-config
- SQLite development headers (`libsqlite3-dev` / `sqlite-devel`)
- libmaxminddb development headers (`libmaxminddb-dev` /
  `libmaxminddb-devel`)
- PCRE2 development headers (`libpcre2-dev` / `pcre2-devel`)
- OpenSSL development headers (only for the `kodev` build tool)
- `sqlite3` CLI (used by the maintenance scripts)

Ubuntu/Debian:

```sh
sudo apt install build-essential libsqlite3-dev libmaxminddb-dev \
    libpcre2-dev libssl-dev sqlite3
```

Rocky/Alma/RHEL:

```sh
sudo dnf install gcc make sqlite-devel libmaxminddb-devel \
    pcre2-devel openssl-devel sqlite
```

Optional developer tools: `shellcheck`, `cppcheck`.

## Build

```sh
cd ats-c
make          # prod build, outputs bin/ats-capi
make clean
```

The directory must be named `ats-c`: Kore's `kodev` derives the app name and
the `conf/ats-c.conf` path from the directory name.

The build embeds `db/schema.sql` into the binary. `make` also builds the
vendored Kore 4.2.3 under `ats-c/vendor/`.

## Local run

```sh
cd ats-c
export ATS_ADMIN_USER=admin
export ATS_ADMIN_PASSWORD='local-dev-only'
export ATS_BASE_URL=http://127.0.0.1:3001
export ATS_COOKIE_DOMAIN=127.0.0.1
export ATS_JWT_KEY=local-dev-key-not-for-production
./bin/ats-capi -fnr
```

Open <http://127.0.0.1:3001/>. On first start the service creates
`data/ats.db`, `data/body/` and, if `ATS_JWT_KEY` is absent, `data/jwt.key`.

GeoIP is optional. Without `data/GeoLite2-Country.mmdb`, country statistics are
empty but everything else works. See [GeoIP](#geoip).

Run the checks in another terminal:

```sh
ATS_ADMIN_PASSWORD=local-dev-only tools/smoke.sh http://127.0.0.1:3001
tools/http-fuzz-smoke.sh http://127.0.0.1:3001
```

`tools/ci-local.sh prod` and `tools/ci-local.sh asan` do the build, start,
smoke and robustness tests in one command.

## Production deployment

Target: Linux + systemd + nginx. The example host is `ats.ckern.cn`; replace
it with your domain.

### 1. TLS certificate

Put the certificate and key where the nginx template expects them, or edit
`deploy/nginx-ats.conf`:

```text
/etc/nginx/ssl/<domain>/cert.pem
/etc/nginx/ssl/<domain>/key.pem
```

Example with acme.sh and Aliyun DNS (adjust to your DNS provider):

```sh
/root/.acme.sh/acme.sh --issue --dns dns_ali \
    -d example.com -d '*.example.com' --keylength ec-256
mkdir -p /etc/nginx/ssl/example.com
/root/.acme.sh/acme.sh --install-cert -d example.com --ecc \
    --key-file /etc/nginx/ssl/example.com/key.pem \
    --fullchain-file /etc/nginx/ssl/example.com/cert.pem \
    --reloadcmd 'systemctl reload nginx'
```

### 2. Install

From the repository root:

```sh
sudo deploy/install.sh --reset-data
```

The script:

- builds the binary (unless `--no-build`),
- creates the `ats` system user,
- installs to `/webapps/ats-c` and source to `/opt/ats-src`,
- generates `/etc/ats-capi.env` and prints the admin password once,
- installs the systemd unit, nginx config and cron maintenance,
- runs a local health check.

`--reset-data` moves an existing database aside before starting fresh. Omit it
to keep data.

### 3. Nginx

`deploy/install.sh` installs `deploy/nginx-ats.conf` to
`/etc/nginx/apps/ats.ckern.cn.conf` and enables it if the certificate exists.
To do it manually:

```sh
sudo install -m 0644 deploy/nginx-ats.conf \
    /etc/nginx/apps/ats.ckern.cn.conf
sudo ln -sfn /etc/nginx/apps/ats.ckern.cn.conf \
    /etc/nginx/apps.enabled/ats.ckern.cn.conf
sudo nginx -t && sudo systemctl reload nginx
```

The backend binds `127.0.0.1:3001`; do not expose it directly.

Optional: restrict admin routes to your own networks with
`deploy/nginx-admin-allow.conf.example`. The tracking routes `/rv.js`, `/wru`
and `/pv` must remain public.

### 4. GeoIP

The repository does not distribute the MaxMind GeoLite2 database. Use your own
account:

```sh
# /etc/GeoIP-ats.conf contains only the Country edition
geoipupdate -f /etc/GeoIP-ats.conf -d /webapps/ats-c/data
```

Or place a licensed `GeoLite2-Country.mmdb` at
`ATS_GEOIP_DB_PATH`. The service notices a replaced file and reloads it within
a minute.

### 5. Post-install checks

```sh
systemctl status ats-capi
curl http://127.0.0.1:3001/__health
curl -I https://ats.ckern.cn/login
```

Log in with the admin user and the password printed by the install script.
Change it with:

```sh
sudo /webapps/ats-c/tools/set-password.sh
```

### Upgrading

1. Back up `/webapps/ats-c/backup` and `/etc/ats-capi.env`.
2. Pull the new source, update `VERSION` and review the changelog.
3. Run `sudo deploy/install.sh` (without `--reset-data`).
4. Run the smoke tests:

   ```sh
   ATS_ADMIN_PASSWORD=... tools/smoke.sh https://ats.ckern.cn
   ```

See [Operations](operations.md) for rollback and backup details.
