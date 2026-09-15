#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
gcc -pthread -DDIRECT_BUILTIN_EFFECTS -fsanitize=address,undefined -g -I"$root/tests/host_stream" "$root/tests/host_stream/test.c" "$root/host/files.c" -o "$work/test"
cd "$work"
./test
