#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
make -C "$workspace_path/vendor/vitaGL" -j8 HAVE_SHARK_LOG=1 LOG_ERRORS=1 NO_SPLASHSCREEN=1
