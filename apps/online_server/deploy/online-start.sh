#!/bin/sh
# Start the 2.4 online service pod as the dedicated unprivileged user.
# Idempotent: safe to run at boot, after a crash, or by hand.
set -eu

state_dir="$HOME/online"
mkdir -p "$state_dir"
. "$state_dir/database.env"
. "$state_dir/server.env"

pod="fpdz-online"

if ! podman pod exists "$pod"; then
    podman pod create --name "$pod" -p 127.0.0.1:19643:19643 >/dev/null
fi

if ! podman container exists fpdz-online-pg; then
    podman run --detach --name fpdz-online-pg --pod "$pod" \
        --user postgres --env-file "$state_dir/database.env" \
        --volume fpdz-online-pg-data:/var/lib/postgresql/data:U \
        --tmpfs /tmp --security-opt no-new-privileges --cap-drop ALL \
        --pids-limit 128 --memory 512m \
        public.ecr.aws/docker/library/postgres:15 >/dev/null
fi
podman start fpdz-online-pg >/dev/null 2>&1 || true

attempt=0
until podman exec fpdz-online-pg pg_isready -U "$POSTGRES_USER" -d "$POSTGRES_DB" >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 60 ]; then echo 'PostgreSQL readiness failed' >&2; exit 1; fi
    sleep 1
done

if [ "${1:-}" = "--db-only" ]; then echo 'database ready'; exit 0; fi

if ! podman container exists fpdz-online-server; then
    podman run --detach --name fpdz-online-server --pod "$pod" --user 10001:10001 \
        --env-file "$state_dir/server.env" \
        --read-only --tmpfs /tmp --security-opt no-new-privileges --cap-drop ALL \
        --pids-limit 256 --memory 512m \
        localhost/fpdz-online-production:2.4 >/dev/null
fi
podman start fpdz-online-server >/dev/null 2>&1 || true

attempt=0
until podman exec fpdz-online-server /usr/local/bin/feichuan-online-server --diagnostics >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then echo 'online server health check failed' >&2; exit 1; fi
    sleep 1
done
echo 'online service ready'
