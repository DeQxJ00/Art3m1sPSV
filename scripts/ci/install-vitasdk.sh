#!/usr/bin/env bash
set -euo pipefail
: "${VITASDK:?Set VITASDK to the SDK installation directory}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/build/ci-deps"
mkdir -p "$work"

# Match the validated local SDK (newlib 64aa7aa, toolchain c527abc).
# The August 27 snapshot has duplicate getentropy connectors and cannot link.
sdk_url="https://github.com/vitasdk/autobuilds/releases/download/sdk-snapshot-20260825.611.1/vitasdk-x86_64-linux-gnu-2026-08-25_12-51-16.tar.bz2"
sdk_sha="ff0e1aa1d968222a98836fcb90ed2cd3e6f5b53074e94d69f8057c718b607a06"
if [[ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
  curl --fail --location --retry 3 "$sdk_url" -o "$work/vitasdk.tar.bz2"
  printf '%s  %s\n' "$sdk_sha" "$work/vitasdk.tar.bz2" | sha256sum --check
  mkdir -p "$VITASDK"
  tar -xjf "$work/vitasdk.tar.bz2" -C "$VITASDK" --strip-components=1
fi
export PATH="$VITASDK/bin:$PATH"

# Catch incomplete newlib/syscall combinations before the full media/core build.
cat > "$work/entropy-link.c" <<'EOF'
#include <unistd.h>
int main(void) {
  unsigned char value;
  return getentropy(&value, sizeof(value));
}
EOF
arm-vita-eabi-gcc "$work/entropy-link.c" -o "$work/entropy-link.elf"

# Match the host's existing non-extension compiler API; no device module is bundled.
shark_revision="0d4a0a4ffc6f62d9bd8fe6509448fe9cd0299439" # vitaShaRK 1.6
if [[ ! -d "$work/vitaShaRK/.git" ]]; then
  git clone https://github.com/Rinnegatamante/vitaShaRK.git "$work/vitaShaRK"
fi
git -C "$work/vitaShaRK" checkout --detach "$shark_revision"
make -C "$work/vitaShaRK" -B -j2 \
  CFLAGS='-O2 -mtune=cortex-a9 -mfpu=neon -DDISABLE_SHACCCG_EXTENSIONS'
make -C "$work/vitaShaRK" install
arm-vita-eabi-gcc --version
