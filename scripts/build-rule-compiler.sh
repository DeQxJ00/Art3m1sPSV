#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
cmake -S "$workspace_path/tests/rule_shader" -B "$workspace_path/build/rule-compiler" -DCMAKE_BUILD_TYPE=Release
cmake --build "$workspace_path/build/rule-compiler" --parallel 4
