#!/bin/sh
# One-time, idempotent production setup for the dedicated fpdz_online account.
# Run as fpdz_online after the image archive has been copied to ~/online-deploy.
set -eu
umask 077

deploy_dir="$HOME/online-deploy"
state_dir="$HOME/online"
mkdir -p "$state_dir" "$HOME/.config/systemd/user"

if [ ! -f "$state_dir/database.env" ]; then
    db_password="$(openssl rand -hex 24)"
    printf 'POSTGRES_USER=fpdz_online\nPOSTGRES_DB=fpdz_online\nPOSTGRES_PASSWORD=%s\n' \
        "$db_password" > "$state_dir/database.env"
    printf 'FPDZ_DB_HOST=127.0.0.1\nFPDZ_DB_PORT=5432\nFPDZ_DB_NAME=fpdz_online\nFPDZ_DB_USER=fpdz_online\nFPDZ_DB_PASSWORD=%s\nFPDZ_PORT=19643\n' \
        "$db_password" > "$state_dir/server.env"
    chmod 600 "$state_dir/database.env" "$state_dir/server.env"
    echo 'generated isolated database credentials'
fi

if [ -f "$deploy_dir/fpdz-online-production-2.3.tar.gz" ]; then
    podman load -i "$deploy_dir/fpdz-online-production-2.3.tar.gz" >/dev/null
fi
# The deploy archive is built with the release tag, but retag the newest
# fpdz-online-production image as a safety net when a different tag was loaded.
newest="$(podman images --sort created --format '{{.Repository}}:{{.Tag}}' \
    | grep 'fpdz-online-production' | grep -v ':2\.3$' | tail -1)"
if [ -n "$newest" ]; then podman tag "$newest" localhost/fpdz-online-production:2.3; fi
podman image exists localhost/fpdz-online-production:2.3

for script in online-start.sh online-stop.sh; do
    cp -f "$deploy_dir/$script" "$state_dir/$script"
    chmod 700 "$state_dir/$script"
done

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DBUS_SESSION_BUS_ADDRESS="unix:path=${XDG_RUNTIME_DIR}/bus"
cp -f "$deploy_dir/fpdz-online.service" "$HOME/.config/systemd/user/fpdz-online.service"
systemctl --user daemon-reload
systemctl --user enable fpdz-online.service >/dev/null
echo 'systemd user unit installed'