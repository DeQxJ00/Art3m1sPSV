#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
host="${1:-192.168.1.50}"
debugger="$root/build/uvdb/tools/runtime/usr/bin/gdb-multiarch"
elf="$root/build/uvdb/host/art3m1s_direct"
if [[ ! -x "$debugger" ]]; then
  debugger="$(command -v gdb-multiarch || true)"
fi
if [[ -z "$debugger" || ! -f "$elf" ]]; then
  echo "Need gdb-multiarch and the ELF matching the deployed uvdb build." >&2
  exit 1
fi
cd "$root"
exec "$debugger" --nx -q "$elf" \
  -ex 'set pagination off' \
  -ex 'set debuginfod enabled off' \
  -ex 'set remotetimeout 8' \
  -ex "target remote $host:1234"
