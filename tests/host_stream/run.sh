#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
gcc -pthread -DDIRECT_BUILTIN_EFFECTS -fsanitize=address,undefined -g -I"$root/tests/host_stream" "$root/tests/host_stream/test.c" "$root/host/files.c" -Wl,--wrap=fopen,--wrap=fseek,--wrap=pthread_cond_broadcast -o "$work/test"
cd "$work"
./test
mkdir missed-wakeup
cd missed-wakeup
TEST_DROP_FILE_WAKEUPS=1 timeout 30 ../test
