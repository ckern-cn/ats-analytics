# Releasing

Only the maintainer publishes official releases. This document describes the
process so builds are reproducible and users can verify what is official.

## Versioning

- The version lives in `VERSION` (Semantic Versioning: `MAJOR.MINOR.PATCH`).
- The changelog lives in `CHANGELOG.md` (Keep a Changelog style).
- Update both before tagging.

## Checklist

1. Start from a clean `main` checkout:

   ```sh
   git status --short   # must be empty
   git pull
   ```

2. Run the full local checks:

   ```sh
   tools/ci-local.sh prod
   tools/ci-local.sh asan
   ```

3. Update `VERSION` and `CHANGELOG.md`; commit them.
4. Build the release artifact:

   ```sh
   tools/release.sh
   ```

   The script refuses a dirty tree, builds and tests, then writes
   `dist/ats-analytics-<version>-<os>-<arch>.tar.gz` and `SHA256SUMS`.

5. Optionally sign the checksum file:

   ```sh
   gpg --armor --detach-sign dist/SHA256SUMS
   ```

6. Tag and push:

   ```sh
   git tag -s v$(cat VERSION) -m "ATS $(cat VERSION)"
   git push origin main --tags
   ```

7. Publish the GitHub release with the tarball, `SHA256SUMS` and its signature.
   Mirror the tag and release on Gitee.
8. Deploy the artifact and run the production smoke test:

   ```sh
   ATS_ADMIN_PASSWORD=... tools/smoke.sh https://ats.ckern.cn
   tools/http-fuzz-smoke.sh https://ats.ckern.cn
   ```

9. Update the changelog link and the “latest release” link if needed.

## Artifact contents

The release tarball is a complete, installable tree:

- the full source tree (including vendored Kore),
- a tested binary at `ats-c/bin/ats-capi`,
- `public/` assets,
- `deploy/`, `tools/`, `docs/` and `db/schema.sql`,
- `LICENSE`, `NOTICE`, `README.md`, `THIRD_PARTY_NOTICES.md`,
  `SECURITY.md`, `TRADEMARK.md`, `CHANGELOG.md`, `VERSION`.

It contains no database, GeoIP database, JWT key, `.env` or build intermediates.
Install it with:

```sh
tar -xzf ats-analytics-<version>-<os>-<arch>.tar.gz
cd ats-analytics-<version>-<os>-<arch>
sudo deploy/install.sh --no-build
```

The source archives are also provided by GitHub/Gitee from the tagged commit.

## Official channels

- GitHub: <https://github.com/ckern-cn/ats-analytics/releases>
- Gitee: <https://gitee.com/mywliu/ats-analytics/releases>

Do not publish official binaries from CI automatically. Release approval is
manual and belongs to the maintainer. Forks may publish their own builds under
their own name and identity (see `TRADEMARK.md`).

## Rollback

Keep the previous release tarball and the service’s `.bak` archive. To roll
back:

```sh
systemctl stop ats-capi
install -m 0755 ats-capi /webapps/ats-c/bin/ats-capi
systemctl start ats-capi
curl http://127.0.0.1:3001/__health
```

Database migrations, if any, are documented in the release notes.
