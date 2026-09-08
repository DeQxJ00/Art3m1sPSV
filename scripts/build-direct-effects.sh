#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
cmake -S "$root/host-direct-effects" -B "$root/build/shader-completion/host" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/shader-completion/host" --parallel 4 "$@"
