#!/usr/bin/env bash
set -euo pipefail
: "${VITASDK:?Set VITASDK to the SDK installation directory}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/build/ci-deps"
mkdir -p "$work"

# Pin the toolchain instead of silently taking a new nightly on each run.
sdk_url="https://github.com/vitasdk/autobuilds/releases/download/sdk-snapshot-20260827.648.1/vitasdk-x86_64-linux-gnu-2026-08-27_18-11-42.tar.bz2"
sdk_sha="14b78180a173ca1f7b9e92432f3df3217ee95edc8ee4aa9ae7a322c1c9a08b35"
if [[ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
  curl --fail --location --retry 3 "$sdk_url" -o "$work/vitasdk.tar.bz2"
  printf '%s  %s\n' "$sdk_sha" "$work/vitasdk.tar.bz2" | sha256sum --check
  mkdir -p "$VITASDK"
  tar -xjf "$work/vitasdk.tar.bz2" -C "$VITASDK" --strip-components=1
fi
export PATH="$VITASDK/bin:$PATH"

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
