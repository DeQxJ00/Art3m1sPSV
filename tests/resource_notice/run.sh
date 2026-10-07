#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/stubs/psp2"
printf '#pragma once\nstruct SceGxmTexture {};\n' > "$work/stubs/psp2/gxm.h"
g++ -std=c++17 -fsanitize=address,undefined -g -I"$work/stubs" -I"$root/host-direct/src" "$root/tests/resource_notice/test.cpp" -o "$work/test"
cd "$work"
./test
