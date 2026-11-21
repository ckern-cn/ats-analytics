# Operations

## Service

```sh
systemctl status ats-capi
systemctl restart ats-capi
journalctl -u ats-capi -f
```

Health check (loopback only):

```sh
curl http://127.0.0.1:3001/__health
```

The nginx template exposes `/__health` only to `127.0.0.1`.

## Scheduled jobs

`deploy/ats.cron` is installed as `/etc/cron.d/ats`:

| Schedule | Job |
|---|---|
| Daily 03:17 | `/webapps/ats-c/tools/maintenance.sh` |
| Wednesday 04:23 | GeoIP refresh with `/etc/GeoIP-ats.conf` |

Logs go to `/var/log/ats-maintenance.log` and `/var/log/ats-geoip.log`.

## Backups and retention

`maintenance.sh`:

1. takes an online SQLite backup (`.backup`) and gzips it,
2. deletes traffic older than `ATS_RETENTION_DAYS` (default 365),
3. runs `PRAGMA wal_checkpoint(TRUNCATE)` and `PRAGMA optimize`,
4. deletes backups older than `ATS_BACKUP_KEEP` days (default 14).

Backups live in `/webapps/ats-c/backup/`. The install script also keeps a
pre-upgrade archive at `/webapps/ats-c.bak-<timestamp>.tgz` with mode `0600`.

Restore:

```sh
systemctl stop ats-capi
gunzip -c /webapps/ats-c/backup/ats-YYYYmmdd-HHMMSS.db.gz \
    > /webapps/ats-c/data/ats.db
rm -f /webapps/ats-c/data/ats.db-wal /webapps/ats-c/data/ats.db-shm
chown ats:ats /webapps/ats-c/data/ats.db
systemctl start ats-capi
```

Backups contain visitor data. Keep the directory private and consider encrypting
off-site copies.

## GeoIP updates

The repository does not ship the GeoLite2 database. Use `geoipupdate` with a
configuration that contains only the Country edition:

```sh
geoipupdate -f /etc/GeoIP-ats.conf -d /webapps/ats-c/data
```

The service checks the file mtime and reloads it within a minute; no restart is
needed. If the update fails, the previous database stays in use.

You can also run the wrapper:

```sh
ATS_GEOIP_CONF=/etc/GeoIP-ats.conf \
    /webapps/ats-c/tools/update-geoip.sh
```

## Admin password

```sh
sudo /webapps/ats-c/tools/set-password.sh
```

The script writes `ATS_ADMIN_PASSWORD_SHA256` and restarts the service.

## Restrict the admin UI

The tracking endpoints must stay public, but the admin pages can be restricted
by source IP. See `deploy/nginx-admin-allow.conf.example`; it uses nginx
`allow`/`deny` and must be placed before the generic `location /`.

If you use a VPN or Cloudflare Access, put the admin routes behind it instead.

## Upgrade and rollback

Upgrade:

```sh
cd /opt/ats-src
# If /opt/ats-src is a git checkout:
git pull
# Otherwise re-extract the source or release tarball into /opt/ats-src.
sudo deploy/install.sh
```

The install script backs up the current `/webapps/ats-c` before replacing it.

Rollback the binary:

```sh
systemctl stop ats-capi
install -m 0755 /path/to/old/ats-capi /webapps/ats-c/bin/ats-capi
systemctl start ats-capi
```

Rollback the database from a backup if a schema change requires it. Review the
release notes/changelog first.

## Reset data

`deploy/install.sh --reset-data` moves the old database aside and starts empty.
For a manual reset:

```sh
systemctl stop ats-capi
mv /webapps/ats-c/data/ats.db /webapps/ats-c/data/ats.db.old
rm -f /webapps/ats-c/data/ats.db-wal /webapps/ats-c/data/ats.db-shm
systemctl start ats-capi
```

The binary recreates the schema on startup. Keep the old file until you are
sure you do not need it.

## Monitoring

Minimum checks:

- `/__health` returns `200` on loopback,
- systemd unit is `active` and `enabled`,
- nginx `nginx -t` passes,
- certificate expiry (acme.sh cron or your monitoring),
- backup files are appearing in `/webapps/ats-c/backup`,
- disk space and database size.

## Scaling notes

ATS is designed for personal or small-team use on one host:

- SQLite in WAL mode, single writer,
- two Kore workers by default,
- client-side tracking,
- no external queue or cache.

If you need multi-node ingestion or very high write volume, put a queue or a
sampling layer in front, or evaluate a different storage engine. Keep the
retention policy tight to bound database size.
