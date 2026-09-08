#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
build_path="$workspace_path/build/native-host"
cmake -S "$workspace_path/host-gxm" -B "$build_path" -G "Unix Makefiles" \
  -DPLATFORM_PSV=ON -DUSE_GXM=ON -DUSE_VITA_SHARK=OFF \
  -DART3M1S_NATIVE_RENDERER=ON -DART3M1S_SAFE_PRESENT=OFF \
  -DART3M1S_ASYNC_PRESENT=OFF -DART3M1S_IMAGE_QUAD_AB=OFF \
  -DART3M1S_BUILD_IMAGE_QUAD_PROBE=ON \
  -DART3M1S_CORE_LIBRARY="$workspace_path/build/rust-native/armv7-sony-vita-newlibeabihf/release/libart3m1s_core.a" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_path" --parallel "${ART3M1S_BUILD_JOBS:-4}"
