#!/usr/bin/env bash
# FateMMO database backup script
# Usage: ./scripts/backup_db.sh [output_dir]
#
# Requires: pg_dump, DATABASE_URL env var or explicit connection params
# Schedule via cron: 0 */4 * * * /path/to/backup_db.sh /backups/fate

set -euo pipefail

BACKUP_DIR="${1:-./backups}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RETENTION_DAYS=14

# Connection — prefer DATABASE_URL, fallback to individual vars
DB_HOST="${PGHOST:-db-fate-engine-do-user.db.ondigitalocean.com}"
DB_PORT="${PGPORT:-25060}"
DB_NAME="${PGDATABASE:-fate_engine_dev}"
DB_USER="${PGUSER:-doadmin}"

mkdir -p "$BACKUP_DIR"

BACKUP_FILE="$BACKUP_DIR/fate_${DB_NAME}_${TIMESTAMP}.dump"

echo "[$(date)] Starting backup of $DB_NAME to $BACKUP_FILE"

if [ -n "${DATABASE_URL:-}" ]; then
    pg_dump -Fc --no-owner --no-acl "$DATABASE_URL" -f "$BACKUP_FILE"
else
    pg_dump -Fc --no-owner --no-acl \
        -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
        -f "$BACKUP_FILE"
fi

BACKUP_SIZE=$(du -h "$BACKUP_FILE" | cut -f1)
echo "[$(date)] Backup complete: $BACKUP_FILE ($BACKUP_SIZE)"

# Prune old backups
PRUNED=$(find "$BACKUP_DIR" -name "fate_*.dump" -mtime +$RETENTION_DAYS -delete -print | wc -l)
if [ "$PRUNED" -gt 0 ]; then
    echo "[$(date)] Pruned $PRUNED backups older than $RETENTION_DAYS days"
fi

# Verify backup is readable
pg_restore --list "$BACKUP_FILE" > /dev/null 2>&1
echo "[$(date)] Backup verified OK"
