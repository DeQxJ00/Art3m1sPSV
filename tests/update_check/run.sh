#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/build/update-check-tests"
mkdir -p "$work"
gcc -c -fsanitize=address,undefined "$root/vendor/cJSON/cJSON.c" -o "$work/json.o"
g++ -std=c++17 -pthread -g -fsanitize=address,undefined \
  -I"$root/host-direct/src" -I"$root/vendor/cJSON" \
  "$root/tests/update_check/test.cpp" "$root/host-direct/src/update_check.cpp" "$work/json.o" -o "$work/test"
"$work/test"
if [[ $# -gt 0 ]]; then
  mkdir -p "$work/stubs/psp2"
  printf '#pragma once\nstruct SceGxmTexture {};\n' > "$work/stubs/psp2/gxm.h"
  g++ -std=c++17 -pthread -g -fsanitize=address,undefined \
    -I"$work/stubs" -I"$root/host-direct/src" -I"$root/vendor/cJSON" \
    "$root/tests/update_check/notice.cpp" "$root/host-direct/src/update_check.cpp" "$work/json.o" -o "$work/notice"
  "$work/notice" "$1"
fi
