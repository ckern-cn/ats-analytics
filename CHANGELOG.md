# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- The nginx deployment config now serves only `ats.ckern.cn`. It no longer
  claims or redirects the `ckern.cn`/`www.ckern.cn` apex, which belongs to the
  separate ckern studio homepage project.
- The dashboard topbar uses the ATS app badge (`icon.svg`) instead of the
  transparent mark, so the small icon keeps its brand background.
- Replaced the unknown/globe asset with a circular world-map silhouette
  generated from Natural Earth 110m public-domain data. The map is slightly
  enlarged and shifted left, with Antarctica and the Arctic cap removed.
  A mild vertical scale reduces the excessive top/bottom whitespace of the
  flat map inside the circular frame, and the map is nudged slightly left and
  down for visual balance. Flag/globe URLs are versioned with
  `ATS_ASSET_VERSION`.
- Confirmed the 246 country/region flag SVGs are byte-identical to
  `HatScripts/circle-flags` commit `98574d2` (MIT); added the upstream license
  and documented the pinned commit in `THIRD_PARTY_NOTICES.md`.

### Security

- Reject CR/LF in redirect targets and validate the login `ref` path. A
  crafted login redirect fragment could otherwise inject arbitrary response
  headers through Kore's verbatim `Location` header.
- Apply the same Origin/Fetch Metadata check to logout that the other
  state-changing admin POSTs use.

### Fixed

- Admin form posts from embedded or sandboxed browsers that send
  `Origin: null` together with `Sec-Fetch-Site: same-origin` are no longer
  rejected as cross-site. A non-null Origin still has to match
  `ATS_BASE_URL`; cross-site requests and null origins without Fetch Metadata
  are still rejected.
- The dashboard brand icon now has explicit 22×22 dimensions, and the admin
  CSS/JS URLs are versioned with a short cache lifetime. A stale cached
  stylesheet can no longer render the 512px SVG at full size in the top-left
  corner.

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
