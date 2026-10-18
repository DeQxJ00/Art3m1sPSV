"""Package the three validated Vita GXP binaries, without recompiling shaders."""
from pathlib import Path
import hashlib
import json
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'shaders/psv'
EVIDENCE = ROOT / 'backup/legacy-build/three-game-shaders/device/cold'
fnv = runpy.run_path(str(ROOT / 'scripts/compile-external-shaders.py'))['fnv64']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    records = json.loads((OUT / "provenance.json").read_text(encoding="utf-8"))
    catalog = {e["source_hash"]:e for e in json.loads((OUT / "catalog.json").read_text(encoding="utf-8"))}
    for name in ['blend', 'blend2', 'radial']:
        pc = EVIDENCE / 'TEST_SHADER_sample03_PC_REAL/shader-cache/system/shader/pc'
        android = EVIDENCE / 'TEST_SHADER_sample03_ANDROID_REAL/shader-cache/system/shader/pc'
        parts = {ext: (pc / (name + '.hlsl' + ext)).read_bytes()
                 for ext in ['.cg', '.conversion.json', '.gxp', '.hash']}
        for ext, data in parts.items():
            assert data == (android / (name + '.hlsl' + ext)).read_bytes()
        source = ROOT / 'backup/legacy-build/three-game-shaders/originals/sample03_PC/system/shader/pc' / (name + '.hlsl')
        meta = json.loads(parts['.conversion.json'])
        assert meta['abi'] == 2 and meta['source_hash'] == fnv(source.read_bytes())
        assert meta['cg_hash'] == fnv(parts['.cg'])
        gxp = parts['.gxp']
        assert gxp[:4] == b'GXP\0' and parts['.hash'].decode().split()[1] == fnv(gxp)
        # AGX1 is the binary package ABI; conversion metadata ABI2 is separate.
        package_meta = dict(abi=1, source_hash=meta['source_hash'], uniforms=meta['uniforms'])
        encoded = json.dumps(package_meta, separators=(',', ':')).encode()
        entry = catalog[meta['source_hash']]
        implementation = entry['name']
        if entry['source_hash'] != entry['canonical_hash']:
            # Equivalent imported sources share the existing canonical package.
            # Validate the evidence without replacing it with an alias-header GXP.
            tokens = runpy.run_path(str(ROOT / 'scripts/bundle-artemis-shaders.py'))['tokens']
            assert tokens(parts['.cg'].decode('utf-8')) == tokens((OUT / entry['cg']).read_text(encoding='utf-8'))
            data = (OUT / entry['package']).read_bytes()
            length, = struct.unpack_from('<I', data, 4)
            canonical = json.loads(data[12:12+length])
            assert canonical['source_hash'] == entry['canonical_hash']
            assert canonical['uniforms'] == meta['uniforms']
            continue
        assert entry['source_hash'] == meta['source_hash']
        path = OUT / entry['package']
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'AGX1' + struct.pack('<II', len(encoded), len(gxp)) + encoded + gxp)
        cg = parts['.cg'].decode('utf-8')
        (OUT / entry['cg']).write_text(
            '\n'.join(line.rstrip() for line in cg.splitlines()) + '\n')
        records = [r for r in records if r['source_hash'] != meta['source_hash']]
        records.append(dict(name=implementation, package=entry['package'], source_path='system/shader/pc/' + name + '.hlsl',
                            source_hash=meta['source_hash'], source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            gxp_sha256=hashlib.sha256(gxp).hexdigest(), gxp_bytes=len(gxp),
                            compiler='libshacccg (PSV), verified 2026-09-19',
                            shared_by=['sample03_PC', 'sample03_Android']))
    (OUT / 'provenance.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print('Packaged three verified GXP programs; original shader bytes unchanged')


if __name__ == '__main__':
    main()
