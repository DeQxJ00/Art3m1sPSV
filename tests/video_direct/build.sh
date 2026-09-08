#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
mkdir -p "$root/build/nv12-probe"
cd "$root/build/nv12-probe"
arm-vita-eabi-gcc -O2 -Wall -Wextra -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
  -I"$root/host" -I"$root/vendor/vitaGL/source" -I"$root/build/media-sdk/include" \
  -L"$root/vendor/vitaGL" -L"$root/build/media-sdk/lib" -Wl,-q -Wl,--gc-sections -Wl,--defsym=__sce_headroom=0x10000 \
  "$root/tests/video_direct/probe.c" "$root/host/video_direct.c" \
  -Wl,--start-group -lvitaGL -lvitashark -lmathneon -lstdc++ -lpthread -lavutil \
  -lSceGxm_stub -lSceDisplay_stub -lSceCommonDialog_stub -lSceAppMgr_stub -lSceCtrl_stub \
  -ltaihen_stub -lSceShaccCg_stub -lSceKernelDmacMgr_stub -lm -lc -Wl,--end-group -o probe.elf
vita-elf-create probe.elf probe.velf
vita-make-fself -s probe.velf eboot.bin
vita-mksfoex -s TITLE_ID=ART3MNV12 "Art3m1s NV12 Probe" param.sfo
vita-pack-vpk -s param.sfo -b eboot.bin probe.vpk
