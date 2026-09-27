#!/bin/bash
# Administrator-side switch of the play.327802521.xyz route from the IP-limited
# capacity prototype to the production 2.3 online service. Keeps a rollback copy.
set -euo pipefail
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
backup_dir=/root/nginx-config-backups
mkdir -p "$backup_dir"
if [ -f /etc/nginx/conf.d/fpdz-online-capacity.conf ]; then
    cp -a /etc/nginx/conf.d/fpdz-online-capacity.conf "$backup_dir/fpdz-online-capacity-$stamp.conf"
    echo "rollback copy: $backup_dir/fpdz-online-capacity-$stamp.conf"
fi
cp -f /home/fpdz_online/online-deploy/nginx-play.conf /etc/nginx/conf.d/fpdz-online.conf
rm -f /etc/nginx/conf.d/fpdz-online-capacity.conf
nginx -t
systemctl reload nginx
sleep 1
curl -sS -o /dev/null -w 'https_status=%{http_code}\n' https://play.327802521.xyz/ws || true
echo 'production route active'