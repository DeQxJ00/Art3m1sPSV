#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
out="$root/build/tremor"
mkdir -p "$out"
# Xiph Tremor 89a7534 + vitasdk/packages libtremor ARM constraint fix.
# Explicit types avoid running an obsolete configure script on the host.
cat > "$out/config_types.h" <<'EOF'
#include <stdint.h>
typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
EOF
cd "$out"
for name in mdct dsp info misc floor1 floor0 floor_lookup vorbisfile res012 mapping0 codebook framing bitwise; do
  "$VITASDK/bin/arm-vita-eabi-gcc" -O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm \
    -D_ARM_ASSEM_ -D_REENTRANT -DHAVE_ALLOCA_H -fsigned-char -fwrapv \
    -I. -I"$root/vendor/tremor" -c "$root/vendor/tremor/$name.c" -o "$name.o"
done
"$VITASDK/bin/arm-vita-eabi-ar" rcs libvorbisidec.a ./*.o
