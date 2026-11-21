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
# Ship a complete, installable tree with the tested binary. Exclude local
# runtime data, build intermediates, secrets and the dist directory itself.
tar -C "$ROOT" \
	--exclude='ats-c/build' \
	--exclude='ats-c/data' \
	--exclude='ats-c/log' \
	--exclude='ats-c/src/schema_sql.h' \
	--exclude='ats-c/vendor/kore-4.2.3/kodev/kodev' \
	--exclude='.git' \
	--exclude='dist' \
	-cf - . | tar -C "$DIST/$STAGE" -xf -

tar -C "$DIST" -czf "$DIST/$STAGE.tar.gz" "$STAGE"
rm -rf "$DIST/$STAGE"

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
