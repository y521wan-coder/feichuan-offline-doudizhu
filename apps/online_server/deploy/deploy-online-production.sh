#!/bin/bash
# Administrator-side production setup for the 2.4 online service.
# Run as root on the Debian 12 host:  bash deploy-online-production.sh
# Everything the service itself does afterwards runs as fpdz_online (no sudo).
set -euo pipefail

deploy_dir=/home/fpdz_online/online-deploy
state_dir=/home/fpdz_online/online
uid_of_user="$(id -u fpdz_online)"
run_as_user() {
    su - fpdz_online -s /bin/bash -c "$1"
}
user_systemctl() {
    su - fpdz_online -s /bin/bash -c \
        "XDG_RUNTIME_DIR=/run/user/${uid_of_user} DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/${uid_of_user}/bus systemctl --user $1"
}

echo '== 1. retire the capacity prototype pod (images kept for rollback) =='
run_as_user 'podman stop -t 10 fpdz-online-server-test fpdz-online-pg-test 2>/dev/null || true; podman pod rm -f fpdz-online-capacity-test 2>/dev/null || true; podman pod ps'

echo '== 2. load image, credentials and unit as fpdz_online =='
run_as_user "cd $deploy_dir && ./install-production.sh"

echo '== 3. start the isolated PostgreSQL container =='
run_as_user "$state_dir/online-start.sh --db-only"

echo '== 4. back up and migrate the online database =='
run_as_user "mkdir -p /home/fpdz_online/online-backups && $deploy_dir/migrate.sh fpdz-online-pg fpdz_online fpdz_online /home/fpdz_online/online-backups"

echo '== 5. recreate the server container on the new image and start it =='
run_as_user 'podman rm -f fpdz-online-server 2>/dev/null || true'
user_systemctl 'daemon-reload'
user_systemctl 'restart fpdz-online.service'
sleep 3
user_systemctl 'is-active fpdz-online.service'

echo '== 6. verify non-root processes and loopback binding =='
run_as_user 'podman exec fpdz-online-server /usr/local/bin/feichuan-online-server --diagnostics'
ss -lntp | grep 19643 || true
ps -o user=,pid=,cmd= -C feichuan-online-server,postgres 2>/dev/null | head -5 || true
echo 'admin setup finished'
