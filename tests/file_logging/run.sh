#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/file-logging-tests"
g++ -std=c++17 -D__PSV__ -DFMT_HEADER_ONLY -I"$root/tests/audio" -I"$root/vendor/borealis/library/include" -I"$root/vendor/borealis/library/lib/extern/fmt/include" \
 "$root/tests/file_logging/test.cpp" "$root/vendor/borealis/library/lib/core/logger.cpp" -pthread -o "$root/build/file-logging-tests/test"
"$root/build/file-logging-tests/test"
