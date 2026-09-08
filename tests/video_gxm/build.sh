#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/../.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
cmake -S "$workspace_path/host-gxm" -B "$workspace_path/build/gxm-host" \
  -DPLATFORM_PSV=ON -DUSE_GXM=ON -DUSE_VITA_SHARK=OFF -DART3M1S_BUILD_GXM_NV12_PROBE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$workspace_path/build/gxm-host" --target gxm_nv12_probe.vpk-vpk --parallel 4
