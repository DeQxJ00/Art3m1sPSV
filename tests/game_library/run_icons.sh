#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/build/game-library-tests"
mkdir -p "$work"
g++ -std=c++17 -g -fsanitize=address,undefined -I"$root/host-direct/src" \
  "$root/tests/game_library/icon_cache_test.cpp" -o "$work/icons"
"$work/icons"
g++ -std=c++17 -g -fsanitize=address,undefined -I"$root/host-direct/src" \
  "$root/tests/game_library/unicode_test.cpp" "$root/host-direct/src/game_library.cpp" -o "$work/library"
"$work/library"
