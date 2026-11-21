#!/bin/sh
# Daily ATS maintenance: online backup, retention, WAL checkpoint, optimize.
#
# Environment:
#   ATS_DB_PATH        default /webapps/ats-c/data/ats.db
#   ATS_BACKUP_DIR     default /webapps/ats-c/backup
#   ATS_RETENTION_DAYS default 365
#   ATS_BACKUP_KEEP    default 14 (days)
set -eu

# Pick up path/retention overrides from the service environment file.
if [ -r /etc/ats-capi.env ]; then
	. /etc/ats-capi.env
fi

DB=${ATS_DB_PATH:-/webapps/ats-c/data/ats.db}
BACKUP_DIR=${ATS_BACKUP_DIR:-/webapps/ats-c/backup}
DAYS=${ATS_RETENTION_DAYS:-365}
KEEP=${ATS_BACKUP_KEEP:-14}
LOCK=${BACKUP_DIR}/.maintenance.lock

log() { printf '%s %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$*"; }

command -v sqlite3 >/dev/null 2>&1 || {
	log "sqlite3 not found"
	exit 1
}

[ -f "$DB" ] || {
	log "database not found: $DB"
	exit 1
}

mkdir -p "$BACKUP_DIR"
chmod 750 "$BACKUP_DIR" 2>/dev/null || true

# Never run two maintenance jobs at once.
if command -v flock >/dev/null 2>&1; then
	exec 9>"$LOCK"
	if ! flock -n 9; then
		log "another maintenance job is running"
		exit 0
	fi
fi

stamp=$(date -u '+%Y%m%d-%H%M%S')
tmp="$BACKUP_DIR/ats-$stamp.db"

log "backup -> $tmp.gz"
sqlite3 "$DB" ".backup '$tmp'"
gzip -9 -f "$tmp"
chmod 640 "$BACKUP_DIR/ats-$stamp.db.gz" 2>/dev/null || true

cutoff=$(date -u -d "$DAYS days ago" '+%Y-%m-%d %H:%M:%S')
log "deleting traffic older than $cutoff (${DAYS}d)"
sqlite3 -cmd ".timeout 5000" "$DB" "
DELETE FROM apps_traffic WHERE visit_date < '$cutoff';
PRAGMA wal_checkpoint(TRUNCATE);
PRAGMA optimize;" >/dev/null

log "removing backups older than ${KEEP}d"
find "$BACKUP_DIR" -maxdepth 1 -type f -name 'ats-*.db.gz' -mtime +"$KEEP" -delete

log "done"
