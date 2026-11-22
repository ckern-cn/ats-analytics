#!/bin/sh
# Install/upgrade ATS on a systemd host (Rocky/Alma/RHEL 8+, Debian 12+).
#
# Run from the repository root as root:
#
#	sudo deploy/install.sh                 # build if needed, keep data
#	sudo deploy/install.sh --reset-data    # start with an empty database
#	sudo deploy/install.sh --no-build      # use ats-c/bin/ats-capi as-is
#
# The script generates /etc/ats-capi.env on the first install and prints the
# admin password once. It never deletes an existing database unless
# --reset-data is given.
set -eu

DEST=${ATS_DEST:-/webapps/ats-c}
SRC_DIR=${ATS_SRC_DIR:-/opt/ats-src}
DOMAIN=${ATS_DOMAIN:-ats.ckern.cn}
ENV_FILE=${ATS_ENV_FILE:-/etc/ats-capi.env}
SERVICE=ats-capi
NGINX_CONF=/etc/nginx/apps/${DOMAIN}.conf
NGINX_LINK=/etc/nginx/apps.enabled/${DOMAIN}.conf
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STAMP=$(date -u '+%Y%m%d-%H%M%S')
RESET_DATA=0
BUILD=1

for arg in "$@"; do
	case "$arg" in
	--reset-data) RESET_DATA=1 ;;
	--no-build) BUILD=0 ;;
	*) printf 'unknown option: %s\n' "$arg" >&2; exit 2 ;;
	esac
done

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
[ -f "$REPO/ats-c/Makefile" ] || { echo "run from the repository" >&2; exit 1; }

say() { printf '\n== %s ==\n' "$*"; }

say "build"
if [ "$BUILD" = 1 ]; then
	# A clean build avoids reusing a kodev binary built on another distro.
	( cd "$REPO/ats-c" && make clean >/dev/null && make )
fi
[ -x "$REPO/ats-c/bin/ats-capi" ] || { echo "missing ats-c/bin/ats-capi" >&2; exit 1; }

say "user and directories"
if ! id ats >/dev/null 2>&1; then
	useradd --system --home-dir "$DEST" --shell /sbin/nologin ats
fi
mkdir -p "$DEST"/bin "$DEST"/data "$DEST"/log "$DEST"/backup \
	"$DEST"/public "$DEST"/tools
chown -R ats:ats "$DEST"
chmod 755 "$DEST" "$DEST"/public
chmod 750 "$DEST"/data "$DEST"/log "$DEST"/backup

say "stop service"
systemctl stop "$SERVICE" 2>/dev/null || true

say "backup previous install"
if [ -f "$DEST/bin/ats-capi" ]; then
	if ! tar -C "$(dirname "$DEST")" -czf "$DEST.bak-$STAMP.tgz" \
		"$(basename "$DEST")"; then
		echo "backup failed; refusing to replace the existing install" >&2
		exit 1
	fi
	# The archive contains the SQLite database and therefore visitor data.
	chmod 600 "$DEST.bak-$STAMP.tgz"
fi

say "install files"
install -o ats -g ats -m 0755 "$REPO/ats-c/bin/ats-capi" "$DEST/bin/ats-capi"
cp -a "$REPO"/public/. "$DEST"/public/
install -o ats -g ats -m 0755 "$REPO"/tools/maintenance.sh "$DEST"/tools/
install -o ats -g ats -m 0755 "$REPO"/tools/update-geoip.sh "$DEST"/tools/
install -o ats -g ats -m 0755 "$REPO"/tools/smoke.sh "$DEST"/tools/
install -o ats -g ats -m 0755 "$REPO"/tools/http-fuzz-smoke.sh "$DEST"/tools/
install -o ats -g ats -m 0755 "$REPO"/tools/set-password.sh "$DEST"/tools/
for f in README.md LICENSE NOTICE TRADEMARK.md THIRD_PARTY_NOTICES.md \
	SECURITY.md CONTRIBUTING.md CODE_OF_CONDUCT.md GOVERNANCE.md \
	DONATE.md SUPPORT.md RELEASING.md CHANGELOG.md AUTHORS.md VERSION; do
	[ -f "$REPO/$f" ] && install -o ats -g ats -m 0644 "$REPO/$f" "$DEST/$f"
done
if [ -d "$REPO/docs" ]; then
	mkdir -p "$DEST/docs"
	cp -a "$REPO/docs/." "$DEST/docs/"
	chown -R ats:ats "$DEST/docs"
fi
chown -R ats:ats "$DEST"/public "$DEST"/tools

if [ ! -f "$DEST/data/GeoLite2-Country.mmdb" ]; then
	if [ -f "$REPO/data/GeoLite2-Country.mmdb" ]; then
		install -o ats -g ats -m 0644 \
			"$REPO/data/GeoLite2-Country.mmdb" \
			"$DEST/data/GeoLite2-Country.mmdb"
	else
		echo "warning: no GeoLite2-Country.mmdb; country stats stay empty" >&2
	fi
fi

if [ "$RESET_DATA" = 1 ]; then
	say "reset data"
	for f in "$DEST"/data/ats.db "$DEST"/data/ats.db-wal "$DEST"/data/ats.db-shm; do
		[ -e "$f" ] && mv "$f" "$f.bak-$STAMP"
	done
fi

say "source tree"
mkdir -p "$SRC_DIR"
TMP_TAR=$(mktemp "${TMPDIR:-/tmp}/ats-src.XXXXXX")
trap 'rm -f "$TMP_TAR"' EXIT INT TERM
tar -C "$REPO" \
	--exclude=ats-c/build --exclude=ats-c/bin --exclude=ats-c/data \
	--exclude=ats-c/log --exclude=ats-c/backup \
	--exclude=ats-c/src/schema_sql.h \
	--exclude=ats-c/vendor/kore-4.2.3/kodev/kodev \
	--exclude=.git --exclude=dist --exclude=backup \
	--exclude='*.db' --exclude='*.db-wal' --exclude='*.db-shm' \
	--exclude='*.env' --exclude=jwt.key --exclude='*.p12' \
	--exclude='*.mmdb' \
	-cf "$TMP_TAR" .
tar -C "$SRC_DIR" -xf "$TMP_TAR"
rm -f "$TMP_TAR"
trap - EXIT INT TERM
# The snapshot is built and installed by root; keep it root-owned so a
# non-root deploy user cannot modify code that root later rebuilds.
chown -R root:root "$SRC_DIR"
chmod -R go-w "$SRC_DIR"
echo "source installed in $SRC_DIR"

say "environment file"
if [ ! -f "$ENV_FILE" ]; then
	PASS=$(LC_ALL=C tr -dc 'A-Za-z0-9._-' </dev/urandom | head -c 24)
	JWT=$(LC_ALL=C tr -dc 'a-f0-9' </dev/urandom | head -c 64)
	HASH=$(printf '%s' "$PASS" | sha256sum | cut -d' ' -f1)
	umask 077
	cat > "$ENV_FILE" <<EOF
ATS_BASE_URL=https://${DOMAIN}
ATS_COOKIE_DOMAIN=${DOMAIN}
ATS_ADMIN_USER=admin
ATS_ADMIN_PASSWORD_SHA256=${HASH}
ATS_JWT_KEY=${JWT}
ATS_TZ_OFFSET_MIN=480
ATS_SESSION_SECONDS=43200
ATS_RETENTION_DAYS=365
EOF
	chmod 600 "$ENV_FILE"
	chown root:root "$ENV_FILE"
	echo
	echo "Admin user: admin"
	echo "Admin password: ${PASS}"
	echo "Stored in ${ENV_FILE} (mode 0600). Change it there and restart."
else
	echo "keeping existing $ENV_FILE"
fi

say "systemd unit"
install -o root -g root -m 0644 "$REPO/deploy/ats-capi.service" \
	"/etc/systemd/system/${SERVICE}.service"
systemctl daemon-reload
systemctl enable "$SERVICE"
systemctl restart "$SERVICE"
sleep 1
systemctl --no-pager --lines=5 status "$SERVICE" || true

say "nginx"
if [ -f /etc/nginx/ssl/ckern.cn/cert.pem ]; then
	install -o root -g root -m 0644 "$REPO/deploy/nginx-ats.conf" "$NGINX_CONF"
	ln -sfn "$NGINX_CONF" "$NGINX_LINK"
	if nginx -t; then
		systemctl reload nginx
	else
		echo "nginx -t failed; fix the configuration before reloading" >&2
	fi
else
	echo "warning: /etc/nginx/ssl/ckern.cn/cert.pem is missing" >&2
	echo "issue a certificate, then install deploy/nginx-ats.conf manually" >&2
fi

say "GeoIP update config"
if [ -f /opt/geoipupdate_6.0.0/GeoIP.conf ] && [ ! -f /etc/GeoIP-ats.conf ]; then
	sed 's/^EditionIDs.*/EditionIDs GeoLite2-Country/' \
		/opt/geoipupdate_6.0.0/GeoIP.conf > /etc/GeoIP-ats.conf
	chmod 600 /etc/GeoIP-ats.conf
fi

say "cron"
install -o root -g root -m 0644 "$REPO/deploy/ats.cron" /etc/cron.d/ats

say "health"
if curl -fsS --max-time 5 "http://127.0.0.1:3001/__health"; then
	echo "service is healthy"
else
	echo "health check failed; see: journalctl -u $SERVICE -n 50" >&2
fi

say "done"
echo "Dashboard: https://${DOMAIN}/"
echo "Logs:      journalctl -u ${SERVICE} -f"
echo "Backups:   ${DEST}/backup (daily via /etc/cron.d/ats)"
echo "Source:    ${SRC_DIR}"
