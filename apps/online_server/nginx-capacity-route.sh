#!/bin/sh
# Temporary IP-restricted TLS route for the capacity gate. Run via SSH as an
# administrator; SSH_CLIENT supplies the test point's current public IPv4.
set -eu
set -- $SSH_CLIENT
client_ip="${1:-}"
case "$client_ip" in
    ''|*[!0-9.]*) echo 'SSH client IPv4 unavailable' >&2; exit 1 ;;
esac

route=/etc/nginx/conf.d/fpdz-online-capacity.conf
if [ -e "$route" ]; then
    echo 'Capacity route already exists' >&2
    exit 1
fi
nginx -t >/dev/null
cat > "$route" <<EOF
server {
    listen 443 ssl;
    server_name play.327802521.xyz;
    ssl_certificate /etc/nginx/ssl/327802521.xyz/fullchain.pem;
    ssl_certificate_key /etc/nginx/ssl/327802521.xyz/privkey.pem;
    access_log off;
    allow $client_ip;
    deny all;

    location = /ws {
        proxy_pass http://127.0.0.1:19643;
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 75s;
        proxy_send_timeout 75s;
    }
}
EOF
if ! nginx -t; then
    rm -f "$route"
    exit 1
fi
systemctl reload nginx
echo 'IP-restricted capacity route active'
