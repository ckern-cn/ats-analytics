# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Planned

- More locales and translation tooling.
- Optional IP anonymization on by default for new deployments.
- Per-app public/private PV badge setting.
- More release automation and signed builds.

## [0.1.0] - 2026-11-21

First public release.

### Added

- Self-hosted website/app traffic analytics with a single C/Kore binary and
  SQLite storage.
- Tracking script `/rv.js/<app-id>`, real-URL backfill `/wru/<id>` and
  country/PV badge `/pv/<app-id>/<n>/<container>`.
- Admin dashboard with summary cards, 14-day trend, top pages, referrers,
  countries, recent visits, pagination and CSV export.
- Multi-app support with up to three host names per app, app edit/delete.
- JWT login with expiry, HttpOnly/Secure/SameSite cookies, logout and login
  rate limiting.
- i18n with `zh-CN` and `en` catalogs, `Accept-Language`, `?lang=`, cookie
  switching and `/lang/<code>`.
- Project icon, favicon, Apple touch icon and brand assets.
- Optional GeoIP hot reload, daily backup/retention and health endpoint.
- Docker-free systemd/nginx deployment scripts and admin IP allowlist example.
- GitHub Actions CI with prod + ASan/UBSan builds, smoke tests, HTTP robustness
  tests, shellcheck, cppcheck and gitleaks.
- MIT license, third-party notices, trademark/brand policy, security policy,
  contribution guide, code of conduct, governance and release documentation.

### Security

- Parameterized SQL, HTML/JS output escaping and CSV formula-injection
  protection.
- HMAC-signed `/wru` requests so Snowflake IDs cannot be guessed and tampered
  with.
- Origin/CSRF checks on state-changing admin requests.
- HMAC visitor identifiers, optional IP anonymization and optional DNT
  respect.
- Backend bound to loopback; systemd sandbox; secrets kept out of the binary
  and the repository.
- GeoIP configuration files and install backups restricted to owner-only
  permissions.
