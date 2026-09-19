"""Package the three validated Vita GXP binaries, without recompiling shaders."""
from pathlib import Path
import hashlib
import json
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'shaders/artemis-pc/supplemental'
EVIDENCE = ROOT / 'build/three-game-shaders/device/cold'
fnv = runpy.run_path(str(ROOT / 'scripts/compile-external-shaders.py'))['fnv64']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    records = []
    for name in ['blend', 'blend2', 'radial']:
        pc = EVIDENCE / 'TEST_SHADER_STELLA_PC_REAL/shader-cache/system/shader/pc'
        android = EVIDENCE / 'TEST_SHADER_STELLA_ANDROID_REAL/shader-cache/system/shader/pc'
        parts = {ext: (pc / (name + '.hlsl' + ext)).read_bytes()
                 for ext in ['.cg', '.conversion.json', '.gxp', '.hash']}
        for ext, data in parts.items():
            assert data == (android / (name + '.hlsl' + ext)).read_bytes()
        source = ROOT / 'build/three-game-shaders/originals/Stella_of_the_End_PC/system/shader/pc' / (name + '.hlsl')
        meta = json.loads(parts['.conversion.json'])
        assert meta['abi'] == 2 and meta['source_hash'] == fnv(source.read_bytes())
        assert meta['cg_hash'] == fnv(parts['.cg'])
        gxp = parts['.gxp']
        assert gxp[:4] == b'GXP\0' and parts['.hash'].decode().split()[1] == fnv(gxp)
        # AGX1 is the binary package ABI; conversion metadata ABI2 is separate.
        package_meta = dict(abi=1, source_hash=meta['source_hash'], uniforms=meta['uniforms'])
        encoded = json.dumps(package_meta, separators=(',', ':')).encode()
        implementation = 'radial_stella' if name == 'radial' else name
        path = OUT / (implementation + '.hlsl.agxp')
        path.write_bytes(b'AGX1' + struct.pack('<II', len(encoded), len(gxp)) + encoded + gxp)
        path.with_suffix(path.suffix + '.cg').write_bytes(parts['.cg'])
        records.append(dict(name=implementation, source_path='system/shader/pc/' + name + '.hlsl',
                            source_hash=meta['source_hash'], source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            gxp_sha256=hashlib.sha256(gxp).hexdigest(), gxp_bytes=len(gxp),
                            compiler='libshacccg (PSV), verified 2026-09-19',
                            shared_by=['Stella_of_the_End_PC', 'Stella_of_the_End_Android']))
    (OUT / 'provenance.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print('Packaged three verified GXP programs; original shader bytes unchanged')


if __name__ == '__main__':
    main()
