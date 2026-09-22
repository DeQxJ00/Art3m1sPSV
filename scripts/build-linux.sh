#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${VITASDK:?Set VITASDK to an installed VitaSDK}"
export PATH="$VITASDK/bin:$HOME/.cargo/bin:$PATH"
toolchain="${RUST_TOOLCHAIN:-nightly-2026-08-28}"
[[ -f "$root/core/Cargo.toml" ]] || {
  echo 'Run git submodule update --init --recursive first.' >&2
  exit 1
}
export CARGO_TARGET_DIR="$root/build/rust-native"
export CARGO_TARGET_ARMV7_SONY_VITA_NEWLIBEABIHF_LINKER="$VITASDK/bin/arm-vita-eabi-gcc"
export CC_armv7_sony_vita_newlibeabihf="$VITASDK/bin/arm-vita-eabi-gcc"
export AR_armv7_sony_vita_newlibeabihf="$VITASDK/bin/arm-vita-eabi-ar"
bash "$root/scripts/build-tremor.sh"
bash "$root/scripts/build-ffmpeg.sh"
cargo "+$toolchain" rustc --locked --manifest-path "$root/core/Cargo.toml" \
  --lib --release --no-default-features -Z build-std=std,panic_abort \
  --target armv7-sony-vita-newlibeabihf --crate-type staticlib \
  --features gl-backend,gxm-native-renderer,gxm-text-epoch,gxm-menu-key-alias,gxm-builtin-effects
mkdir -p "$root/build/native-command-port"
cp "$CARGO_TARGET_DIR/armv7-sony-vita-newlibeabihf/release/libart3m1s_core.a" \
  "$root/build/native-command-port/libart3m1s_core.a"
bash "$root/scripts/build-native-commands-host.sh"
