# Development

## Build dependencies

See [Installation](installation.md). On Ubuntu:

```sh
sudo apt install build-essential libsqlite3-dev libmaxminddb-dev \
    libpcre2-dev libssl-dev sqlite3 shellcheck cppcheck
```

## Build

```sh
cd ats-c
make          # prod
make clean
```

The build uses `-Wall -Wextra -Werror`. The schema is generated into
`src/schema_sql.h` from `db/schema.sql` and embedded in the binary.

## Tests

```sh
tools/ci-local.sh prod
tools/ci-local.sh asan
```

Each command:

1. cleans and builds the selected flavor,
2. starts `ats-capi` on `127.0.0.1:3001`,
3. runs `tools/smoke.sh`,
4. runs `tools/http-fuzz-smoke.sh`,
5. stops the service.

The smoke test covers login, CSRF rejection, app lifecycle, tracking, HMAC,
PV, dashboard, CSV, CSV injection, i18n (Accept-Language, cookie, invalid
language) and logout. The robustness test sends malformed paths, cookies,
methods and language requests and checks the service stays alive.

For a manual run:

```sh
cd ats-c
ATS_ADMIN_PASSWORD=local-dev-only ATS_BASE_URL=http://127.0.0.1:3001 \
ATS_COOKIE_DOMAIN=127.0.0.1 ATS_JWT_KEY=local-dev-key-not-for-production \
./bin/ats-capi -fnr
```

Then:

```sh
ATS_ADMIN_PASSWORD=local-dev-only tools/smoke.sh http://127.0.0.1:3001
tools/http-fuzz-smoke.sh http://127.0.0.1:3001
```

## CI

`.github/workflows/ci.yml` runs on GitHub:

- prod and ASan/UBSan builds,
- smoke and HTTP robustness tests,
- `shellcheck -S error`,
- `cppcheck` (advisory for now),
- `gitleaks`.

Dependabot keeps GitHub Actions up to date.

## Code style

- C99, tabs, existing style; warnings are errors.
- User-facing strings belong in `ats-c/src/lang/*.h` with a key in
  `ats-c/src/i18n.h`; do not hardcode UI text in `views.c`/`handlers.c`.
- All SQL uses prepared statements.
- Escape output at the boundary: `ats_sb_html`, `ats_sb_js`.
- Keep secrets out of source, logs and tests.
- Add a test or a clear manual test plan for behavior changes.
- Keep tracking endpoint formats backward compatible.

## i18n

See [Internationalization](i18n.md). Translation-only contributions are
welcome and do not require deep C knowledge.

## Commits and contributions

- No CLA; contributions are MIT.
- Sign off commits: `git commit -s`.
- Keep pull requests focused; see `CONTRIBUTING.md`.
- Security reports go through `SECURITY.md`, not public issues.

## Release

See [RELEASING.md](../RELEASING.md) and `tools/release.sh`. Official releases
are built and published by the maintainer only.
