#!/bin/sh
# Change the ATS admin password.
#
#	sudo tools/set-password.sh
#
# The password is never stored in plaintext: the script writes
# ATS_ADMIN_PASSWORD_SHA256 to /etc/ats-capi.env and restarts the service.
set -eu

ENV_FILE=${ATS_ENV_FILE:-/etc/ats-capi.env}

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
[ -f "$ENV_FILE" ] || { echo "missing $ENV_FILE" >&2; exit 1; }

printf 'New password: '
stty -echo
IFS= read -r PASS
stty echo
printf '\n'

[ -n "$PASS" ] || { echo "empty password" >&2; exit 1; }

printf 'Repeat password: '
stty -echo
IFS= read -r PASS2
stty echo
printf '\n'

[ "$PASS" = "$PASS2" ] || { echo "passwords do not match" >&2; exit 1; }

HASH=$(printf '%s' "$PASS" | sha256sum | cut -d' ' -f1)
TMP=$(mktemp)

# Drop both password forms, then add the digest.
grep -vE '^ATS_ADMIN_PASSWORD(=|_SHA256=)' "$ENV_FILE" > "$TMP" || true
printf 'ATS_ADMIN_PASSWORD_SHA256=%s\n' "$HASH" >> "$TMP"

chmod 600 "$TMP"
chown root:root "$TMP"
mv "$TMP" "$ENV_FILE"

systemctl restart ats-capi
sleep 1
if systemctl is-active --quiet ats-capi; then
	echo "Password updated; ats-capi restarted."
else
	echo "Service failed to restart; see: journalctl -u ats-capi -n 50" >&2
	exit 1
fi
