#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/build/audio-tests"
mkdir -p "$work"
cd "$work"
gcc -O2 -g -DDIRECT_RESOURCE_LEDGER=1 -fsanitize=address,undefined "$root/tests/audio/ledger_alloc_test.c" -o ledger_alloc_test
./ledger_alloc_test
cp "$root/build/tremor/config_types.h" .
for rate in 48000 44100; do
  for channels in 1 2; do
    ffmpeg -v error -y -f lavfi -i "sine=frequency=713:sample_rate=$rate:duration=0.23" -ac "$channels" -c:a libvorbis -q:a 5 "$rate-$channels.ogg"
    ffmpeg -v error -y -i "$rate-$channels.ogg" -ar 48000 -ac 2 -f f32le "$rate-$channels.pcm"
  done
done
ffmpeg -v error -y -i 48000-2.ogg -c:a pcm_s16le fallback.wav
ffmpeg -v error -y -f lavfi -i 'anoisesrc=sample_rate=48000:duration=12:seed=37' -ac 2 -c:a libvorbis -q:a 5 long.ogg
# Upstream fixed-point code intentionally uses signed shifts; apply UBSan to our
# integration, and ASan to both our code and the decoder.
sources=()
for name in mdct dsp info misc floor1 floor0 floor_lookup vorbisfile res012 mapping0 codebook framing bitwise; do
  gcc -O2 -g -fwrapv -DHAVE_ALLOCA_H -fsanitize=address -I. -I"$root/vendor/tremor" -c "$root/vendor/tremor/$name.c" -o "$name.o"
  sources+=("$name.o")
done
gcc -O2 -g -DDIRECT_RESOURCE_LEDGER=1 -fsanitize=address,undefined -I. -I"$root/tests/audio" -I"$root/vendor/tremor" -I"$root/vendor/cJSON" \
  "$root/tests/audio/test.c" "$root/host/audio_vorbis.c" "$root/host/media_io.c" "$root/vendor/cJSON/cJSON.c" \
  "${sources[@]}" -lavformat -lavcodec -lavutil -lswresample -lm -pthread -o test
./test
