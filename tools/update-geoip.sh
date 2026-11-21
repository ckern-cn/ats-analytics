#!/bin/sh
# Refresh data/GeoLite2-Country.mmdb. The service notices a changed file and
# reloads it within a minute, so no restart is needed.
#
# Preferred (MaxMind's own updater):
#   ATS_GEOIP_CONF=/etc/GeoIP-ats.conf \
#   ATS_GEOIPUPDATE=/opt/geoipupdate_6.0.0/geoipupdate \
#   tools/update-geoip.sh
#
# Fallback: copy a newer file produced by another updater:
#   ATS_GEOIP_SRC=/webapps/ats/app/geoip-db/GeoLite2-Country.mmdb \
#   tools/update-geoip.sh
set -eu

# Pick up path/retention overrides from the service environment file.
if [ -r /etc/ats-capi.env ]; then
	. /etc/ats-capi.env
fi

DEST=${ATS_GEOIP_DB_PATH:-/webapps/ats-c/data/GeoLite2-Country.mmdb}
GEOIPUPDATE=${ATS_GEOIPUPDATE:-/opt/geoipupdate_6.0.0/geoipupdate}
CONF=${ATS_GEOIP_CONF:-}
SRC=${ATS_GEOIP_SRC:-/webapps/ats/app/geoip-db/GeoLite2-Country.mmdb}

log() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }

mkdir -p "$(dirname "$DEST")"

if [ -n "$CONF" ] && [ -x "$GEOIPUPDATE" ]; then
	log "geoipupdate -f $CONF -d $(dirname "$DEST")"
	if "$GEOIPUPDATE" -f "$CONF" -d "$(dirname "$DEST")"; then
		log "geoip database updated"
		exit 0
	fi
	log "geoipupdate failed; trying fallback"
fi

if [ ! -f "$SRC" ]; then
	log "source database not found: $SRC"
	exit 1
fi

if [ -f "$DEST" ] && [ ! "$SRC" -nt "$DEST" ]; then
	log "destination is already current"
	exit 0
fi

tmp="$DEST.tmp.$$"
cp -f "$SRC" "$tmp"
chmod 0644 "$tmp"
mv -f "$tmp" "$DEST"
if [ "$(id -u)" = "0" ] && id ats >/dev/null 2>&1; then
	chown ats:ats "$DEST" 2>/dev/null || true
fi
log "copied $SRC -> $DEST"
