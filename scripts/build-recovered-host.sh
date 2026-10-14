#!/usr/bin/env bash
# Build only; never installs or deploys. Pass an explicit already-built core.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
core="${1:?Usage: build-recovered-host.sh /absolute/libart3m1s_core.a [ON|OFF]}"
mode="${2:-ON}"
probe="${3:-live}"
[[ "$probe" == live || "$probe" == frozen ]] || { echo 'Probe must be live or frozen' >&2; exit 2; }
[[ "$probe" != frozen || "$mode" == ON ]] || { echo 'Frozen requires ON' >&2; exit 2; }
[[ "$mode" == ON || "$mode" == OFF ]] || { echo 'Mode must be ON or OFF' >&2; exit 2; }
recovery="${EMOTE_RECOVERY_ROOT:-$(dirname "$root")/emote-psv-test}"
build="$root/build/recovered-host"
[[ "$mode" == ON ]] || build="$root/build/recovered-host-off"
freeze=OFF
if [[ "$probe" == frozen ]]; then build="$root/build/recovered-host-frozen"; freeze=ON; fi
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
mkdir -p "$build"
# Freeze the supplied archive so another cargo build cannot change the linker input.
before="$(sha256sum "$core" | cut -d' ' -f1)"
cp "$core" "$build/core-input.a"
[[ "$before" == "$(sha256sum "$build/core-input.a" | cut -d' ' -f1)" ]] || { echo 'Core changed during copy' >&2; exit 1; }
if [[ "$mode" == ON ]]; then
  "$VITASDK/bin/arm-vita-eabi-nm" -u "$build/core-input.a" > "$build/core-undefined-symbols.txt" 2> "$build/core-nm-diagnostics.txt"
  if ! grep -Eq '[[:space:]]art3m1s_recovery_create(_prepared)?$' "$build/core-undefined-symbols.txt"; then
    echo 'Refusing ON build: archive has no recovered-backend create reference; rebuild core with emote-recovery.' >&2
    exit 1
  fi
fi
python3 "$root/scripts/recovered-build-manifest.py" --root "$root" --recovery "$recovery" --core "$build/core-input.a" --mode "$mode" --output "$build/inputs.json"
cmake -S "$root/host-direct" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DDIRECT_RECOVERED_PACKET="$mode" -DEMOTE_RECOVERY_ROOT="$recovery" -DART3_DIRECT_CORE_LIBRARY="$build/core-input.a" \
  -DDIRECT_RECOVERED_FREEZE_PROBE="$freeze" \
  -DDIRECT_BUILTIN_EFFECTS=ON -DDIRECT_CAPUNLOCKER=ON -DDIRECT_DEFERRED_FINISH_CANDIDATE=ON \
  -DDIRECT_RESOURCE_LEDGER=ON -DDIRECT_SEMANTIC_CONTROLS=ON -DDIRECT_TEXT_EPOCH_CANDIDATE=ON \
  -DDIRECT_HEAP_DIAGNOSTICS=ON -DDIRECT_BUILTIN_PROBE=OFF -DDIRECT_GXM_PROBE=OFF
cmake --build "$build" --target art3m1s_direct -j2
python3 "$root/scripts/recovered-build-manifest.py" --root "$root" --recovery "$recovery" --core "$build/core-input.a" --mode "$mode" --compare "$build/inputs.json" --elf "$build/art3m1s_direct" --output "$build/manifest.json"
"$VITASDK/bin/arm-vita-eabi-g++" --version > "$build/compiler-version.txt"
printf 'Host ELF ready (%s): %s\n' "$mode" "$build/art3m1s_direct"
