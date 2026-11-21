#!/bin/sh
# Build one flavor, start the service on loopback, run the smoke tests, stop.
#
#	tools/ci-local.sh prod
#	tools/ci-local.sh asan
#
# Requires the build dependencies from the README and a free port 3001.
set -eu

FLAVOR=${1:-prod}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BASE=http://127.0.0.1:3001
PASS=${ATS_ADMIN_PASSWORD:-ci-test-password}
LOG=${TMPDIR:-/tmp}/ats-ci-$FLAVOR.log
PID=""

cleanup() {
	if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
		kill "$PID" 2>/dev/null || true
		wait "$PID" 2>/dev/null || true
	fi
}
trap cleanup EXIT INT TERM

echo "== build ($FLAVOR) =="
make -C "$ROOT/ats-c" clean >/dev/null
make -C "$ROOT/ats-c" FLAVOR="$FLAVOR"

echo "== start =="
cd "$ROOT/ats-c"
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
ATS_ADMIN_USER=admin \
ATS_ADMIN_PASSWORD="$PASS" \
ATS_BASE_URL="$BASE" \
ATS_COOKIE_DOMAIN=127.0.0.1 \
ATS_JWT_KEY=0123456789abcdef0123456789abcdef \
./bin/ats-capi -fnr >"$LOG" 2>&1 &
PID=$!

i=0
while [ "$i" -lt 30 ]; do
	if curl -fsS --max-time 2 "$BASE/__health" >/dev/null 2>&1; then
		break
	fi
	i=$((i + 1))
	sleep 1
done

if ! curl -fsS --max-time 2 "$BASE/__health" >/dev/null 2>&1; then
	echo "service did not become healthy; log:" >&2
	cat "$LOG" >&2 || true
	exit 1
fi

echo "== smoke ($FLAVOR) =="
cd "$ROOT"
ATS_ADMIN_PASSWORD="$PASS" tools/smoke.sh "$BASE"

echo "== http fuzz smoke ($FLAVOR) =="
tools/http-fuzz-smoke.sh "$BASE"

echo "== $FLAVOR passed =="
