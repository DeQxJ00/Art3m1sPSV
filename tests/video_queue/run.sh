#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/video-queue-tests"
gcc -std=c11 -O1 -g -fsanitize=address,undefined -pthread "$root/tests/video_queue/test.c" -o "$root/build/video-queue-tests/test"
timeout 30 "$root/build/video-queue-tests/test"
