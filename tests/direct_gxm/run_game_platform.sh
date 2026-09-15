#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
g++ -std=c++17 -fsanitize=address,undefined -g "$root/tests/direct_gxm/game_platform_test.cpp" -o "$work/test"
"$work/test" "$work/game"
