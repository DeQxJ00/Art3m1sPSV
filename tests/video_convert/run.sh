#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/video-convert-tests"
cc -O2 -g -Wall -Wextra -fsanitize=address,undefined "$root/tests/video_convert/test.c" -lswscale -lavutil -o "$root/build/video-convert-tests/test"
"$root/build/video-convert-tests/test" "$@"
