#!/bin/sh
# Run as the dedicated unprivileged online-service user, before starting a new image.
set -eu
if [ "$#" -ne 4 ]; then
    echo "usage: migrate.sh POSTGRES_CONTAINER DB_NAME DB_USER BACKUP_DIRECTORY" >&2
    exit 2
fi
container=$1
database=$2
db_user=$3
backup_dir=$4
case "$database:$db_user" in
    *[!a-zA-Z0-9_:]*|:*|*:) echo "invalid database identifier" >&2; exit 2 ;;
esac
mkdir -p "$backup_dir"
chmod 700 "$backup_dir"
umask 077
backup="$backup_dir/online-$(date -u +%Y%m%dT%H%M%SZ).dump"
podman exec "$container" pg_dump -U "$db_user" -d "$database" -Fc > "$backup"
test -s "$backup"
echo "backup ready: $backup"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
podman exec -i "$container" psql -U "$db_user" -d "$database" \
    -v ON_ERROR_STOP=1 --single-transaction < "$script_dir/migrations/001_initial.sql"
echo "online schema version 1 applied"
