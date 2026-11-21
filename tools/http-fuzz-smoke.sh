#!/bin/sh
# Integration robustness check: malformed URLs, cookies and methods must never
# take the service down or produce a 5xx. This is not a coverage-guided fuzzer;
# it is the cheap regression check that runs in CI.
#
#	tools/http-fuzz-smoke.sh http://127.0.0.1:3001
set -eu

BASE=${1:-http://127.0.0.1:3001}
TMP=${TMPDIR:-/tmp}/ats-fuzz.$$
FAIL=0

trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP"

say() { printf '%s\n' "$*"; }

hit() {
	desc=$1
	shift
	code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$@" 2>/dev/null) || code=000
	case "$code" in
	5*)
		say "FAIL: $desc -> $code"
		FAIL=1
		;;
	000)
		say "ok: $desc -> connection closed"
		;;
	*)
		say "ok: $desc -> $code"
		;;
	esac
}

LONG=$(awk 'BEGIN { for (i = 0; i < 5000; i++) printf "a" }')

say "== malformed requests =="
hit "root" "$BASE/"
hit "unknown path" "$BASE/definitely-not-a-route"
hit "long path" "$BASE/rv.js/$LONG"
hit "bad percent escape" --path-as-is "$BASE/rv.js/%zz"
hit "invalid utf8" --path-as-is "$BASE/rv.js/%FF%FE"
hit "nul in pv" --path-as-is "$BASE/pv/%00/3/x"
hit "truncated wru" "$BASE/wru/"
hit "wru numeric without token" "$BASE/wru/1"
hit "bad wru token" "$BASE/wru/1?t=00000000000000000000000000000000"
hit "pv no app" "$BASE/pv/1/3/x"
hit "pv bad count" "$BASE/pv/1/99/x"
hit "login weird ref" "$BASE/login?ref=%0d%0a%2F%2Fevil.example"
hit "login without password" -X POST "$BASE/do_login" --data-urlencode "username=admin"
hit "unauthenticated dashboard" "$BASE/dv"
hit "unauthenticated export" "$BASE/export/1"
hit "unauthenticated delete" -X POST "$BASE/do_delete" --data-urlencode "id=1"
hit "bad jwt" -b 'auth_jwt=not.a.jwt' "$BASE/dv"
hit "huge cookie" -b "auth_jwt=$(awk 'BEGIN { for (i = 0; i < 4000; i++) printf "a" }')" "$BASE/dv"
hit "options" -X OPTIONS "$BASE/dv"
hit "cross-origin create" -H 'Origin: https://evil.example' -X POST "$BASE/do_create" --data-urlencode "app_name=x" --data-urlencode "app_hosts=x.example"

say "== liveness =="
CODE=$(curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$BASE/__health" 2>/dev/null || echo 000)
case "$CODE" in
200|403) say "ok: health after fuzz -> $CODE" ;;
*)
	say "FAIL: health after fuzz -> $CODE"
	FAIL=1
	;;
esac

if [ "$FAIL" -ne 0 ]; then
	say "FAILED"
	exit 1
fi
say "PASSED"
