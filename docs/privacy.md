# Privacy

ATS is a self-hosted analytics tool. The operator of the deployment is the data
controller; this document describes what the software stores and which
controls it provides. It is not legal advice.

## Data collected by default

For each tracked visit:

- visit time (UTC),
- page URL and referring URL/host,
- visitor IP,
- User-Agent,
- derived device type, browser, operating system,
- country (from GeoIP, if the database is installed),
- a visitor identifier,
- internal record status.

The admin account, app names/hosts and notes are stored to run the service.

ATS does **not** set tracking cookies. The visitor identifier is
`HMAC-SHA256(User-Agent + stored IP)` keyed with the JWT key. It is
pseudonymous, not anonymous: someone who knows the IP and UA can recompute it.
Changing `ATS_JWT_KEY` changes future identifiers.

## Controls

| Control | Default | Effect |
|---|---|---|
| `ATS_ANONYMIZE_IP` | `0` | `1` stores IPv4 `/24` and IPv6 `/48`; GeoIP still uses the real address. |
| `ATS_RESPECT_DNT` | `1` | `1` ignores requests with `DNT: 1`. |
| `ATS_RETENTION_DAYS` | `365` | Daily maintenance deletes older detail rows. |
| HMAC visitor ID | always | No raw `MD5(IP+UA)` identifier. |
| GeoIP optional | optional | Without a database, no country is stored. |
| Backups | daily, 14 days | Contain the same personal data; protect them accordingly. |

Set `ATS_ANONYMIZE_IP=1` if you do not need full IPs. It reduces precision but
keeps country statistics.

## Logs and backups

- nginx access logs contain IPs and request paths.
- systemd/journald logs contain failed-login warnings (user name and IP).
- Daily backups contain the database, including visitor rows.
- The install script keeps a pre-upgrade archive (mode `0600`).

Apply retention to logs and backups as well, not just the database. Encrypt
off-site backups if they leave the host.

## Operator responsibilities

Depending on your jurisdiction (for example GDPR in the EU or PIPL in China),
you may need:

- a privacy notice describing the data, purpose, retention and contact,
- a legal basis (consent or legitimate interest, as applicable),
- a way to honor access/deletion requests,
- a data processing agreement if you process data for customers,
- appropriate security and breach procedures.

If you install ATS on customer sites, the customer site's privacy notice should
mention the analytics endpoint and the data it receives.

## Deleting data

Delete all traffic for an app (the app delete action also cascades):

```sh
sqlite3 /webapps/ats-c/data/ats.db \
  "DELETE FROM apps_traffic WHERE app_id = <app-id>;"
```

Delete rows matching one visitor hash or IP:

```sh
sqlite3 /webapps/ats-c/data/ats.db \
  "DELETE FROM apps_traffic WHERE unique_visitors = '<hash>';"
```

Delete old rows immediately:

```sh
sqlite3 /webapps/ats-c/data/ats.db \
  "DELETE FROM apps_traffic WHERE visit_date < datetime('now','-90 days');"
```

Then checkpoint/optimize and remove relevant backups if the request requires
it. Stop the service or use the maintenance script's online backup pattern to
avoid concurrent-write surprises.

## Export

`/export/<app-id>` returns the app's recent rows as CSV. It requires an admin
session. Treat the file as personal data.

## Third parties

- MaxMind GeoLite2 is used locally; no visitor data is sent to MaxMind by the
  service. The optional `geoipupdate` job contacts MaxMind for database
  downloads.
- The tracking script is served by your ATS host. If the snippet points at a
  third-party host, that host sees the visitor's request and Referer.
- No advertising, fingerprinting or cross-site tracking is built in.

## Brand and project

Privacy of the software project itself (repository, issues, contributions) is
covered by the platform policies of GitHub/Gitee. Do not put personal data in
issues, pull requests or logs you paste publicly.
