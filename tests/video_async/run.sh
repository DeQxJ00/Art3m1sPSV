#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/video-async-tests"
cd "$root/build/video-async-tests"
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=64x64:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow.ogv
ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=64x64:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
gcc -O1 -g -DART3M1S_HOST_GXM -fsanitize=address,undefined -I"$root/tests/video_async" -I"$root/tests/audio" -I"$root/vendor/cJSON" \
 "$root/tests/video_async/test.c" "$root/host/media_io.c" "$root/vendor/cJSON/cJSON.c" -lavformat -lavcodec -lavutil -lswscale -pthread -lm -o test
timeout 30 ./test
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow.ogv
ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
timeout 30 ./test 960 540
timeout 30 ./test 960 540 stall
timeout 30 ./test 960 540 loop
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
timeout 30 ./test 960 540 mask-skip
