#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
mkdir -p "$workspace_path/build/ffmpeg"
cd "$workspace_path/build/ffmpeg"
"$workspace_path/vendor/ffmpeg/configure" --prefix="$workspace_path/build/media-sdk" \
  --enable-vita --target-os=vita --enable-cross-compile --arch=arm --cpu=cortex-a9 \
  --cross-prefix="$VITASDK/bin/arm-vita-eabi-" --disable-runtime-cpudetect --disable-armv5te \
  --extra-cflags="-Wno-error=incompatible-pointer-types" --extra-ldflags="-L$VITASDK/lib" \
  --disable-shared --enable-static --disable-programs --disable-doc --disable-autodetect \
  --disable-network --disable-iconv --disable-lzma --disable-sdl2 --disable-xlib \
  --disable-avdevice --disable-avfilter --disable-encoders --disable-muxers \
  --disable-demuxers --enable-demuxer=mov,ogg,wav,mp3,aac,flac,h264 \
  --disable-parsers --enable-parser=h264,aac,aac_latm,mpegaudio,vorbis,flac \
  --disable-decoders --enable-decoder=h264,h264_vita,aac,aac_vita,mp3,mp3_vita,vorbis,theora,flac,atrac9,pcm_s16le,pcm_u8 \
  --enable-swscale --enable-swresample --enable-pthreads
make -j8
make install
