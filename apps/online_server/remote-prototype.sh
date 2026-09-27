#!/bin/sh
# Run as the dedicated fpdz_online user. This capacity prototype is reachable
# only through a loopback-bound SSH tunnel and is not an Internet service.
set -eu
umask 077

state_dir="$HOME/.local/share/fpdz-online-prototype"
mkdir -p "$state_dir"
if [ ! -f "$state_dir/database.env" ]; then
    db_password="$(openssl rand -hex 24)"
    printf 'POSTGRES_USER=fpdz_test\nPOSTGRES_DB=fpdz_online_test\nPOSTGRES_PASSWORD=%s\n' \
        "$db_password" > "$state_dir/database.env"
    printf 'FPDZ_DB_HOST=127.0.0.1\nFPDZ_DB_USER=fpdz_test\nFPDZ_DB_NAME=fpdz_online_test\nFPDZ_DB_PASSWORD=%s\n' \
        "$db_password" > "$state_dir/server.env"
fi

if ! podman volume exists fpdz-online-pg-test-data; then
    podman volume create fpdz-online-pg-test-data >/dev/null
fi
podman pod create --name fpdz-online-capacity-test \
    -p 127.0.0.1:19643:19643 >/dev/null
podman run --detach --name fpdz-online-pg-test \
    --pod fpdz-online-capacity-test --user postgres \
    --env-file "$state_dir/database.env" \
    --volume fpdz-online-pg-test-data:/var/lib/postgresql/data:U \
    --tmpfs /tmp --security-opt no-new-privileges --cap-drop ALL \
    --pids-limit 128 --memory 384m \
    public.ecr.aws/docker/library/postgres:15 >/dev/null

attempt=0
until podman exec fpdz-online-pg-test pg_isready -U fpdz_test \
        -d fpdz_online_test >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 30 ]; then
        echo 'PostgreSQL readiness failed' >&2
        exit 1
    fi
    sleep 1
done

podman run --detach --name fpdz-online-server-test \
    --pod fpdz-online-capacity-test --user 10001:10001 \
    --env-file "$state_dir/server.env" \
    --read-only --tmpfs /tmp --security-opt no-new-privileges --cap-drop ALL \
    --pids-limit 128 --memory 384m \
    fpdz-online-prototype:local >/dev/null
podman ps --filter pod=fpdz-online-capacity-test \
    --format '{{.Names}} {{.Status}}'
