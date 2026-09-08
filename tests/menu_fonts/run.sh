#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/menu-font-tests"
gcc -O1 -g -fsanitize=address,undefined -I"$root/vendor/borealis/library/include/borealis/extern/nanovg" "$root/tests/menu_fonts/test.c" -lm -o "$root/build/menu-font-tests/test"
"$root/build/menu-font-tests/test" "$root/host/assets/menu.ttf"
