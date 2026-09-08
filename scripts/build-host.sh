#!/usr/bin/env bash
set -euo pipefail
workspace_path="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
host_output="$workspace_path/build/host"
host_flags=()
if [[ "${ART3M1S_EXPERIMENTAL_VITA_HW:-0}" == 1 ]]; then
  host_output="$workspace_path/build/hardware-diagnostic"
  host_flags+=(-DART3M1S_EXPERIMENTAL_VITA_HW=1)
fi
if [[ "${ART3M1S_EXPERIMENTAL_VITA_AUDIO:-0}" == 1 ]]; then
  host_output="$workspace_path/build/audio-diagnostic"
  host_flags+=(-DART3M1S_EXPERIMENTAL_VITA_AUDIO=1)
  if [[ "${ART3M1S_EXPERIMENTAL_VITA_HW:-0}" == 1 ]]; then
    host_output="$workspace_path/build/av-diagnostic"
  fi
fi
if [[ "${ART3M1S_PROFILE_GAME:-0}" == 1 ]]; then
  host_output="$host_output/game-profile"
  host_flags+=(-DART3M1S_PROFILE_GAME=1)
fi
bash "$workspace_path/scripts/build-tremor.sh"
mkdir -p "$host_output"
cd "$host_output"
arm-vita-eabi-gcc -O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
  "${host_flags[@]}" \
  -I"$workspace_path/vendor/tremor" -I"$workspace_path/build/tremor" -L"$workspace_path/build/tremor" \
  -I"$workspace_path/vendor/vitaGL/source" -L"$workspace_path/vendor/vitaGL" \
  -I"$workspace_path/build/media-sdk/include" -I"$workspace_path/vendor/cJSON" -L"$workspace_path/build/media-sdk/lib" \
  -Wl,-q -Wl,--gc-sections -Wl,--defsym=__sce_headroom=0x10000 "$workspace_path/host/main.c" "$workspace_path/host/files.c" \
  "$workspace_path/host/audio.c" "$workspace_path/host/audio_vorbis.c" "$workspace_path/host/media_io.c" "$workspace_path/host/video.c" "$workspace_path/host/video_direct.c" "$workspace_path/host/game_profile.c" "$workspace_path/host/launcher.c" "$workspace_path/vendor/cJSON/cJSON.c" -Wl,--wrap=vglSwapBuffers \
  "$workspace_path/build/rust/armv7-sony-vita-newlibeabihf/release/libart3m1s_core.a" \
  -Wl,--start-group -lvitaGL -lvitashark -lmathneon -lstdc++ -lpthread \
  -lvorbisidec -lavformat -lavcodec -lswresample -lswscale -lavutil -lSceAudio_stub -lSceAudiodec_stub -lSceVideodec_stub -lSceCodecEngine_stub \
  -lSceCommonDialog_stub -lSceGxm_stub -lSceDisplay_stub -lSceAppMgr_stub \
  -ltaihen_stub -lSceShaccCg_stub -lSceKernelDmacMgr_stub -lSceCtrl_stub -lSceTouch_stub -lm -lc -Wl,--end-group \
  -o art3m1s.elf
vita-elf-create art3m1s.elf art3m1s.velf
vita-make-fself -s art3m1s.velf eboot.bin
vita-mksfoex -s TITLE_ID=ART3MPSV2 "Art3m1s PSV" param.sfo
vita-pack-vpk -s param.sfo -b eboot.bin \
  -a "$workspace_path/vendor/tremor/COPYING=assets/TREMOR-LICENSE.txt" \
  -a "$workspace_path/host/assets/menu.ttf=assets/menu.ttf" \
  -a "$workspace_path/host/assets/FONT-LICENSE.txt=assets/FONT-LICENSE.txt" art3m1s.vpk
