#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/video-async-tests"
cd "$root/build/video-async-tests"
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=64x64:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow.ogv
ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=64x64:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
gcc -O1 -g -DART3M1S_HOST_GXM -fsanitize=address,undefined -I"$root/tests/video_async" -I"$root/tests/audio" -I"$root/vendor/cJSON" \
 "$root/tests/video_async/test.c" "$root/host/media_io.c" "$root/vendor/cJSON/cJSON.c" -lavformat -lavcodec -lavutil -lswscale -pthread -lm -o test
timeout 30 ./test oracle-arm
timeout 30 ./test fullscreen
timeout 30 ./test multi
ffmpeg -v error -y -f lavfi -i 'testsrc=size=32x32:rate=30:duration=0.25' -pix_fmt yuv444p -c:v libtheora small.ogv
ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=32x32:rate=30:duration=0.25' -pix_fmt yuv444p -c:v libtheora small_m.ogv
timeout 30 ./test multi-yuva
timeout 30 ./test
timeout 30 ./test 64 64 empty
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow.ogv
ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
timeout 30 ./test 960 540
timeout 30 ./test 960 540 stall
timeout 30 ./test 960 540 loop
timeout 30 ./test 960 540 slow-codec
timeout 30 ./test 960 540 yuva
timeout 30 ./test 960 540 yuva-mapped
timeout 30 ./test 960 540 yuva-fallback
# Production uses shared group leases instead of the legacy active-loop copy.
gcc -O1 -g -DART3M1S_HOST_GXM -DTEST_OGV_GROUP_CACHE -fsanitize=address,undefined -I"$root/tests/video_async" -I"$root/tests/audio" -I"$root/vendor/cJSON" \
 "$root/tests/video_async/test.c" "$root/host/media_io.c" "$root/vendor/cJSON/cJSON.c" -lavformat -lavcodec -lavutil -lswscale -pthread -lm -o group_cache_test
timeout 30 ./group_cache_test 960 540 cache
timeout 30 ./group_cache_test 960 540 cache-late
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=2' -pix_fmt yuv444p -c:v libtheora arrow.ogv
timeout 30 ./test 960 540 preload
ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt yuv444p -c:v libtheora arrow_m.ogv
timeout 30 ./test 960 540 mask-skip

# Subsampled color and paired alpha must use the same lifetime/EOF machinery.
for fmt in yuv420p yuv422p; do
 ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt "$fmt" -c:v libtheora arrow.ogv
 ffmpeg -v error -y -f lavfi -i 'color=c=0x808080:size=960x540:rate=30:duration=0.5' -pix_fmt "$fmt" -c:v libtheora arrow_m.ogv
 timeout 30 ./test 960 540 yuva
 timeout 30 ./test 960 540 yuva-mapped
 timeout 30 ./test 960 540 yuva-fallback
 timeout 30 ./test 960 540
done

# Untagged paired masks with full endpoints carry colour/alpha range through
# queued and mapped frames, including loop restarts. The first column is gray.
for fmt in yuv420p yuv444p; do
 ffmpeg -v error -y -f lavfi -i 'testsrc2=size=960x540:rate=30:duration=0.5' -pix_fmt "$fmt" -c:v libtheora arrow.ogv
 ffmpeg -v error -y -f lavfi -i "nullsrc=size=960x540:rate=30:duration=0.5,geq=lum='if(lt(X,16),128,if(lt(X,W/2),0,255))':cb=128:cr=128" -pix_fmt "$fmt" -c:v libtheora arrow_m.ogv
 TEST_FULL_RANGE=1 timeout 30 ./test 960 540 yuva
 TEST_FULL_RANGE=1 timeout 30 ./test 960 540 yuva-mapped
 TEST_FULL_RANGE=1 timeout 30 ./test 960 540 yuva-fallback
 TEST_FULL_RANGE=1 timeout 30 ./test 960 540
done
