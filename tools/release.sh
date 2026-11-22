#!/bin/sh
# Build a release artifact. The maintainer runs this; it does not publish.
#
#	tools/release.sh
#
# Produces dist/ats-analytics-<version>-<os>-<arch>.tar.gz and SHA256SUMS.
# See RELEASING.md for the full checklist.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(cat "$ROOT/VERSION")
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)
STAGE="ats-analytics-$VERSION-$OS-$ARCH"
DIST="$ROOT/dist"

cd "$ROOT"

if [ -n "$(git status --porcelain)" ]; then
	echo "working tree is dirty; commit or stash first" >&2
	git status --short >&2
	exit 1
fi

echo "== tests ($VERSION) =="
tools/ci-local.sh prod

[ -x ats-c/bin/ats-capi ] || {
	echo "missing ats-c/bin/ats-capi after the test build" >&2
	exit 1
}

echo "== package $STAGE =="
rm -rf "$DIST/$STAGE"
mkdir -p "$DIST/$STAGE"

# Build from the tracked tree, not from the working directory: this excludes
# ignored data, secrets and build intermediates without maintaining a fragile
# exclusion list. The working tree is required to be clean above.
TMP_TAR="$DIST/.$STAGE.tar"
rm -f "$TMP_TAR"
git archive --format=tar --prefix="$STAGE/" -o "$TMP_TAR" HEAD
tar --no-same-owner --no-same-permissions -C "$DIST" -xf "$TMP_TAR"
rm -f "$TMP_TAR"

# The tested binary is a build artifact, not tracked in git.
mkdir -p "$DIST/$STAGE/ats-c/bin"
install -m 0755 "$ROOT/ats-c/bin/ats-capi" "$DIST/$STAGE/ats-c/bin/ats-capi"
chmod -R go-w "$DIST/$STAGE"

tar -C "$DIST" --owner=0 --group=0 --numeric-owner \
    -czf "$DIST/$STAGE.tar.gz" "$STAGE"
rm -rf "$DIST/$STAGE"

# Refuse to publish a tarball that contains runtime data, secrets or a GeoIP
# database. The release notes promise these are absent.
if tar tzf "$DIST/$STAGE.tar.gz" | grep -Eq '(^|/)(\.env|jwt\.key|[^/]+\.env|.*\.db|.*\.db-wal|.*\.db-shm|.*\.mmdb|.*\.p12)$'; then
	echo "refusing to publish: secret or runtime data in artifact" >&2
	exit 1
fi

(
	cd "$DIST"
	sha256sum "$STAGE.tar.gz" > SHA256SUMS
)

echo
echo "artifact: $DIST/$STAGE.tar.gz"
cat "$DIST/SHA256SUMS"
echo
echo "Next steps: sign SHA256SUMS, tag the commit, publish on GitHub and Gitee."
echo "See RELEASING.md."
