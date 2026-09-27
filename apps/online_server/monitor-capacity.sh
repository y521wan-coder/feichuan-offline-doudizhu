#!/bin/sh
# Read-only production-host sampler. Output contains aggregate resource values.
set -eu
samples="${1:-180}"
printf 'timestamp_utc mem_available_kib swap_in_kib_s swap_out_kib_s cpu_busy_percent\n'
index=0
while [ "$index" -lt "$samples" ]; do
    line="$(vmstat 1 2 | tail -n 1)"
    set -- $line
    available="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
    busy="$((100 - ${15}))"
    printf '%s %s %s %s %s\n' "$(date -u +%FT%TZ)" \
        "$available" "$7" "$8" "$busy"
    index="$((index + 1))"
    sleep 9
done
