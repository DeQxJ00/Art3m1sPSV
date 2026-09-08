#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/../.." && pwd)"
build_path="$workspace_path/build/tests/math_compat"
mkdir -p "$build_path"
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
    -Dfmod=art3m1s_test_fmod -c "$workspace_path/host-gxm/src/math_compat.c" \
    -o "$build_path/math_compat.o"
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-builtin-fmod \
    "$workspace_path/tests/math_compat/test_fmod.c" "$build_path/math_compat.o" \
    -lm -o "$build_path/test_fmod"
"$build_path/test_fmod"
