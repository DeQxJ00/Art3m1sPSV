#!/usr/bin/env bash
# Package the already verified candidate; never invoke the release/tag target.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/recovered-host}"
recovery="${EMOTE_RECOVERY_ROOT:-$root/../emote-psv-test}"
export VITASDK="${VITASDK:-/home/qxj00/ae3-vitagl-build-20260830/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
check_inputs() {
  python3 "$root/scripts/recovered-build-manifest.py" --root "$root" --recovery "$recovery" \
    --core "$build/core-input.a" --mode ON --compare "$build/manifest.json" \
    --elf "$build/art3m1s_direct" --output "$build/package-inputs.json"
}
check_inputs
cmake --build "$build" --target art3m1s_direct.vpk-vpk -j2
check_inputs
python3 - "$root" "$build" <<'PY'
import hashlib,json,sys,zipfile
from pathlib import Path
root,build=map(Path,sys.argv[1:])
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
with zipfile.ZipFile(build/'art3m1s_direct.vpk') as z:
    assert z.testzip() is None
    assert z.read('eboot.bin')==(build/'art3m1s_direct.self').read_bytes()
record={'status':'packaged-only; not deployed or device accepted',
        'command':'scripts/package-recovered-host.sh',
        'package_script_sha256':digest(root/'scripts/package-recovered-host.sh'),
        'source_manifest_sha256':digest(build/'manifest.json'),
        'artifacts':{n:digest(build/n) for n in ['art3m1s_direct','art3m1s_direct.velf','art3m1s_direct.self','art3m1s_direct.vpk','build-info.json']}}
(build/'package-manifest.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
PY
