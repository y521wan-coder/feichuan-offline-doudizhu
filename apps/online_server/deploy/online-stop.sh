#!/bin/sh
# Stop the production online pod. The database volume is preserved.
set -eu
podman stop -t 20 fpdz-online-server 2>/dev/null || true
podman stop -t 30 fpdz-online-pg 2>/dev/null || true
podman pod rm -f fpdz-online 2>/dev/null || true
echo 'online service stopped'