#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
if ! grep -q 'option(DIRECT_UVDB' "$root/host-direct/CMakeLists.txt"; then
  echo 'uvdb integration is archived in snapshot/01.05-uvdb-before-rollback; baseline 01.02 excludes it.' >&2
  exit 1
fi
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
cmake -S "$root/refs/kubridge-uvdb" -B "$root/build/uvdb/kubridge" -DCMAKE_BUILD_TYPE=Release
# Build only the import stub locally. Never installs/replaces a kernel plugin.
cmake --build "$root/build/uvdb/kubridge" --target stubs --parallel 4
cmake -S "$root/host-direct" -B "$root/build/uvdb/host" -DCMAKE_BUILD_TYPE=Release -DDIRECT_UVDB=ON
cmake --build "$root/build/uvdb/host" --parallel 4
