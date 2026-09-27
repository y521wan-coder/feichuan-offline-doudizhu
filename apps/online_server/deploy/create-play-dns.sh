#!/bin/bash
# Create the public DNS record for the online service endpoint.
# Uses the Cloudflare credentials already stored by acme.sh; never prints them.
set -euo pipefail
. /root/.acme.sh/account.conf
token="${SAVED_CF_Token:-}"
if [ -z "$token" ]; then echo 'no saved Cloudflare token' >&2; exit 1; fi
api=https://api.cloudflare.com/client/v4
zone_id="$(curl -sS -H "Authorization: Bearer $token" "$api/zones?name=327802521.xyz" |
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["result"][0]["id"] if d.get("result") else "")')"
if [ -z "$zone_id" ]; then echo 'zone lookup failed' >&2; exit 1; fi
existing="$(curl -sS -H "Authorization: Bearer $token" "$api/zones/$zone_id/dns_records?name=play.327802521.xyz" |
    python3 -c 'import json,sys; d=json.load(sys.stdin); print(len(d.get("result") or []))')"
if [ "$existing" != "0" ]; then
    echo 'play.327802521.xyz record already present'
else
    curl -sS -X POST "$api/zones/$zone_id/dns_records" \
        -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
        --data '{"type":"A","name":"play","content":"154.23.163.68","ttl":60,"proxied":false,"comment":"feichuan 2.3 online service"}' |
        python3 -c 'import json,sys; d=json.load(sys.stdin); print("created" if d.get("success") else "create failed: %s" % d.get("errors"))'
fi
curl -sS -H "Authorization: Bearer $token" "$api/zones/$zone_id/dns_records?name=play.327802521.xyz" |
    python3 -c 'import json,sys
d=json.load(sys.stdin)
for r in d.get("result") or []:
    print("record %s %s -> %s proxied=%s" % (r["type"], r["name"], r["content"], r["proxied"]))'