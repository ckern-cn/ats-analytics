#!/bin/sh
# End-to-end smoke test against a running ats-capi instance.
#
#	ATS_ADMIN_PASSWORD=secret tools/smoke.sh http://127.0.0.1:3001
#
# The instance may already contain apps; this test creates a uniquely named
# app, exercises tracking/auth/dashboard/edit/delete, then removes it.
set -eu

BASE=${1:-http://127.0.0.1:3001}
USER=${ATS_ADMIN_USER:-admin}
PASS=${ATS_ADMIN_PASSWORD:?set ATS_ADMIN_PASSWORD}
TMP=${TMPDIR:-/tmp}/ats-smoke.$$
JAR=$TMP/cookies.txt
NAME="smoke-$$"
HOST="smoke-$$.example.com"
FAIL=0

trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP"

say() { printf '%s\n' "$*"; }
fail() { say "FAIL: $*"; FAIL=1; }
ok() { say "ok: $*"; }

code() { curl -s -o "$TMP/body" -w '%{http_code}' "$@"; }
check() {
	desc=$1
	want=$2
	got=$3
	if [ "$got" = "$want" ]; then ok "$desc ($got)"; else fail "$desc: want $want, got $got"; fi
}

say "== health =="
# /__health is loopback-only in the nginx config, so a public run gets 403.
HCODE=$(code "$BASE/__health")
if [ "$HCODE" = 200 ] || [ "$HCODE" = 403 ]; then
	ok "health endpoint present ($HCODE)"
else
	fail "health endpoint: got $HCODE"
fi

say "== auth =="
HASH=$(printf '%s' "$PASS" | sha256sum | cut -d' ' -f1)
check "cross-origin login rejected" 403 "$(code -X POST "$BASE/do_login" \
	-H 'Origin: https://evil.example' \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=$HASH" \
	--data-urlencode "ref=/dv")"
check "spoofed fetch metadata rejected" 403 "$(code -X POST "$BASE/do_login" \
	-H 'Origin: https://evil.example' \
	-H 'Sec-Fetch-Site: same-origin' \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=$HASH" \
	--data-urlencode "ref=/dv")"
check "opaque same-origin login allowed" 401 "$(code -X POST "$BASE/do_login" \
	-H 'Origin: null' -H 'Sec-Fetch-Site: same-origin' \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=nope" \
	--data-urlencode "ref=/dv")"
check "opaque cross-origin login rejected" 403 "$(code -X POST "$BASE/do_login" \
	-H 'Origin: null' -H 'Sec-Fetch-Site: cross-site' \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=nope" \
	--data-urlencode "ref=/dv")"
check "login" 303 "$(code -c "$JAR" -H "Origin: $BASE" -X POST "$BASE/do_login" \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=$HASH" \
	--data-urlencode "ref=/dv")"

HDR=$(curl -s -D - -o /dev/null -H "Origin: $BASE" -X POST "$BASE/do_login" \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=$HASH" \
	--data "ref=%2Fdv%23x%0D%0AX-Injected%3A%20yes")
if printf '%s' "$HDR" | grep -qi '^x-injected:'; then
	fail "login ref header injection"
else
	ok "login ref header injection blocked"
fi
check "dashboard without cookie" 302 "$(code -o /dev/null "$BASE/dv")"
check "bad password" 401 "$(code -X POST "$BASE/do_login" \
	--data-urlencode "username=$USER" --data-urlencode "password=nope")"
check "dashboard with cookie" 200 "$(code -b "$JAR" "$BASE/dv")"

say "== i18n =="
ENPAGE=$(curl -s -H 'Accept-Language: en-US' "$BASE/login")
printf '%s' "$ENPAGE" | grep -q 'User name' && ok "Accept-Language en" || fail "Accept-Language en"
ZHPAGE=$(curl -s -H 'Accept-Language: zh-CN' "$BASE/login")
printf '%s' "$ZHPAGE" | grep -q '用户名' && ok "Accept-Language zh-CN" || fail "Accept-Language zh-CN"
LANGJAR=$TMP/lang-cookies.txt
check "switch to English" 303 "$(code -c "$LANGJAR" "$BASE/lang/en?ref=/login")"
ENPAGE2=$(curl -s -b "$LANGJAR" "$BASE/login")
printf '%s' "$ENPAGE2" | grep -q 'User name' && ok "en cookie" || fail "en cookie"
check "invalid language" 404 "$(code "$BASE/lang/xx")"
check "switch to Chinese" 303 "$(code -b "$LANGJAR" -c "$LANGJAR" "$BASE/lang/zh-CN?ref=/login")"
ZHPAGE2=$(curl -s -b "$LANGJAR" "$BASE/login")
printf '%s' "$ZHPAGE2" | grep -q '用户名' && ok "zh cookie" || fail "zh cookie"

check "cross-origin create rejected" 403 "$(code -b "$JAR" \
	-H 'Origin: https://evil.example' -X POST "$BASE/do_create" \
	--data-urlencode "app_name=csrf-$NAME" \
	--data-urlencode "app_hosts=csrf-$HOST")"

say "== create =="
check "create app" 200 "$(code -b "$JAR" -X POST "$BASE/do_create" \
	--data-urlencode "app_name=$NAME" \
	--data-urlencode "app_hosts=https://$HOST/path, www.$HOST" \
	--data-urlencode "remark=smoke")"
ID=$(grep -oE '/rv.js/[0-9]+' "$TMP/body" | head -1 | cut -d/ -f3)
[ -n "$ID" ] && ok "app id $ID" || fail "could not parse app id"

say "== empty pv widget is valid JS =="
check "pv zero" 200 "$(code "$BASE/pv/$ID/3/box")"
if grep -q 'others=[0-9]' "$TMP/body"; then ok "others is a number"; else
	fail "others is not a number"; fi
if grep -q 'img.src=.*\/res\/' "$TMP/body"; then ok "pv asset URL"; else
	fail "pv asset URL"; fi
check "pv range" 200 "$(code "$BASE/pv/$ID/1/box")"
grep -q 'out of range' "$TMP/body" && ok "pv range message" || fail "pv range message"

say "== tracking =="
REF="https://$HOST/page?x=1&y=2"
RV=$(curl -s -H "Referer: $REF" "$BASE/rv.js/$ID")
WID=$(printf '%s' "$RV" | grep -oE '/wru/[0-9]+' | head -1 | cut -d/ -f3)
TOK=$(printf '%s' "$RV" | grep -oE 't=[a-f0-9]{32}' | head -1 | cut -d= -f2)
[ -n "$WID" ] && ok "wru id $WID" || fail "no wru id: $RV"
[ -n "$TOK" ] && ok "wru token" || fail "no wru token: $RV"
check "wru bad token" 200 "$(code "$BASE/wru/$WID?ref_url=https%3A%2F%2Fexample.org%2F&page_url=https%3A%2F%2F$HOST%2Fpage&t=00000000000000000000000000000000")"
grep -q 'status: 0' "$TMP/body" && ok "bad token rejected" || fail "bad token accepted"
check "wru" 200 "$(code "$BASE/wru/$WID?ref_url=https%3A%2F%2Fexample.org%2F&page_url=https%3A%2F%2F$HOST%2Fpage&t=$TOK")"
grep -q 'status: 1' "$TMP/body" && ok "wru updated one row" || fail "wru did not update"
check "wrong host" 200 "$(code -H 'Referer: https://evil.example/' "$BASE/rv.js/$ID")"
grep -q 'app not exists' "$TMP/body" && ok "host check" || fail "host check"

say "== dashboard data =="
check "dashboard app" 200 "$(code -b "$JAR" "$BASE/dv/$ID")"
grep -q "$NAME" "$TMP/body" && ok "app name rendered" || fail "app name missing"
grep -q "$HOST" "$TMP/body" && ok "host rendered" || fail "host missing"
grep -q '热门页面' "$TMP/body" && ok "top lists" || fail "top lists missing"
grep -q 'example.org' "$TMP/body" && ok "referrer rendered" || fail "referrer missing"

say "== edit =="
check "edit form" 200 "$(code -b "$JAR" "$BASE/edit/$ID")"
grep -q "value=\"$NAME\"" "$TMP/body" && ok "edit prefilled" || fail "edit prefilled"
check "update" 303 "$(code -b "$JAR" -X POST "$BASE/do_update" \
	--data-urlencode "id=$ID" \
	--data-urlencode "app_name=$NAME-2" \
	--data-urlencode "app_hosts=$HOST" \
	--data-urlencode "remark=updated")"

say "== export =="
check "export csv" 200 "$(code -b "$JAR" "$BASE/export/$ID")"
grep -q '访问时间(UTC)' "$TMP/body" && ok "csv header" || fail "csv header"
grep -q "$HOST" "$TMP/body" && ok "csv contains host" || fail "csv missing host"

# A second visit with a formula-looking page_url must be exported as text.
RV2=$(curl -s -H "Referer: https://$HOST/inject" "$BASE/rv.js/$ID")
W2=$(printf '%s' "$RV2" | grep -oE '/wru/[0-9]+' | head -1 | cut -d/ -f3)
T2=$(printf '%s' "$RV2" | grep -oE 't=[a-f0-9]{32}' | head -1 | cut -d= -f2)
check "wru injection" 200 "$(code "$BASE/wru/$W2?ref_url=https%3A%2F%2Fexample.org%2F&page_url=%3Dcmd%7Ccalc&t=$T2")"
check "export csv injection" 200 "$(code -b "$JAR" "$BASE/export/$ID")"
if grep -q "\"'=cmd" "$TMP/body"; then
	ok "csv formula escaped"
else
	fail "csv formula not escaped"
fi

say "== i18n dashboard =="
check "switch to English again" 303 "$(code -b "$LANGJAR" -c "$LANGJAR" "$BASE/lang/en?ref=/login")"
check "login en" 303 "$(code -b "$LANGJAR" -c "$LANGJAR" -H "Origin: $BASE" \
	-X POST "$BASE/do_login" \
	--data-urlencode "username=$USER" \
	--data-urlencode "password=$HASH" \
	--data-urlencode "ref=/dv")"
ENDASH=$(curl -s -b "$LANGJAR" "$BASE/dv/$ID")
printf '%s' "$ENDASH" | grep -q 'Visitors' && ok "en dashboard" || fail "en dashboard"
ENCSV=$(curl -s -b "$LANGJAR" "$BASE/export/$ID")
printf '%s' "$ENCSV" | grep -q 'visit_date_utc' && ok "en csv header" || fail "en csv header"

say "== cleanup =="
check "delete" 303 "$(code -b "$JAR" -X POST "$BASE/do_delete" \
	--data-urlencode "id=$ID" --data-urlencode "confirm=yes")"
check "deleted dashboard" 200 "$(code -b "$JAR" "$BASE/dv")"

say "== logout =="
check "cross-origin logout rejected" 403 "$(code -b "$JAR" \
	-H 'Origin: https://evil.example' -X POST -d "" "$BASE/logout")"
check "logout" 303 "$(code -b "$JAR" -c "$JAR" -X POST -d "" "$BASE/logout")"
check "dashboard after logout" 302 "$(code -b "$JAR" "$BASE/dv")"

if [ "$FAIL" -ne 0 ]; then
	say "FAILED"
	exit 1
fi
say "PASSED"
