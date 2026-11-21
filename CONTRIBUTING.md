# Contributing to ATS

Thanks for helping. Contributions of code, translations, documentation, bug
reports and testing are all welcome.

The project aims to keep the barrier low:

- **No CLA.** Contributions are accepted under the MIT License.
- Sign off your commits with the Developer Certificate of Origin:

  ```sh
  git commit -s -m "your change"
  ```

  This adds a `Signed-off-by: Your Name <you@example.com>` trailer.

- Small, focused pull requests are easier to review and merge.
- Security issues should not be reported in public issues; see `SECURITY.md`.

## Ways to contribute

- Report a reproducible bug with the issue template.
- Fix a bug or implement a small feature.
- Improve documentation under `docs/`.
- Add or improve a translation under `ats-c/src/lang/`.
- Test a release on your own server and report the result.
- Improve tests, CI, build portability or performance.

Look for the `good first issue` label if you want a small starting point.

## Build and test

Dependencies are listed in `docs/installation.md`.

```sh
cd ats-c
make
```

A full local check builds, starts the service and runs the smoke and robustness
tests:

```sh
tools/ci-local.sh prod
tools/ci-local.sh asan   # ASan + UBSan build
```

The CI workflow is in `.github/workflows/ci.yml`.

## Code style

- C99; tabs for indentation; keep the existing style.
- The build uses `-Wall -Wextra -Werror`; warnings are errors.
- Keep user-facing text out of `views.c` and `handlers.c`. Add a key to
  `ats-c/src/i18n.h` and translations to `ats-c/src/lang/*.h`.
- Use prepared SQL statements; never format SQL with request data.
- Escape HTML/JS at the output boundary (`ats_sb_html`, `ats_sb_js`).
- Do not log secrets, passwords, tokens or full request bodies.
- Keep the tracking endpoint response formats stable; existing embeds rely on
  them.
- New runtime configuration belongs in environment variables and must be
  documented in `docs/configuration.md`.

## Translations

You do not need deep C knowledge to add a language:

1. Copy `ats-c/src/lang/en.h` to `ats-c/src/lang/<code>.h` and translate the
   strings. Keep `%s` / `%lld` placeholders intact.
2. Add the locale to `enum ats_locale` in `ats-c/src/i18n.h`.
3. Add the catalog, code and native name to the tables in `ats-c/src/i18n.c`.
4. Add matching rules to `ats_locale_from_code`.
5. Run `tools/ci-local.sh prod` and check the language switcher, login page,
   dashboard, CSV header and PV badge.

Do not translate the hidden `console.log` debug messages or technical values
such as browser names and country codes.

## Pull request checklist

- [ ] The change is focused and has a clear description.
- [ ] `tools/ci-local.sh prod` passes.
- [ ] `tools/ci-local.sh asan` passes for code changes.
- [ ] User-facing text is in the i18n catalogs.
- [ ] Docs are updated if behavior or configuration changed.
- [ ] No secrets, credentials, database files or GeoIP databases are added.
- [ ] Commits are signed off (`git commit -s`).
- [ ] The PR does not change the project name, logo or official release
      channels.

## Review and merge

The maintainer reviews pull requests. Not every contribution can be merged;
scope, maintenance cost, security and consistency with the project goals are
considered. The maintainer has the final say per `GOVERNANCE.md`.

## Code of conduct

Participation is covered by `CODE_OF_CONDUCT.md`.
