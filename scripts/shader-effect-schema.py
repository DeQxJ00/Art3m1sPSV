"""Conservative effect/parameter descriptors and verified source equivalences.

Only token-identical translated programs with identical parameter layouts share
an implementation. Algebraic similarity and effect filenames are not proofs.
"""
import hashlib
import json
import runpy
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def parameters(uniforms):
    occupied = set()
    names = set()
    result = []
    for u in uniforms:
        name, offset, count = u['name'], u['offset'], u['count']
        slots = set(range(offset, offset + count))
        if not name or name in names or offset < 0 or count < 1 or offset + count > 128 or occupied & slots:
            raise ValueError('invalid parameter layout')
        names.add(name)
        occupied |= slots
        result.append(dict(name=name, scalar='float32', offset=offset, count=count,
                           binding={'alpha': 'draw-opacity', 'colorMultiply': 'draw-color'}.get(name, 'script'),
                           default=[1.0 if name in ('alpha', 'colorMultiply') else 0.0] * count,
                           script_override=True))
    return result


def signature(cg, uniforms):
    tokens = runpy.run_path(str(ROOT / 'scripts/bundle-artemis-shaders.py'))['tokens']
    payload = dict(version=1, tokens=tokens(cg), parameters=parameters(uniforms))
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def describe(directory, entry):
    package = (directory / entry['package']).read_bytes()
    if package[:4] != b'AGX1':
        raise ValueError('not AGX1')
    size = struct.unpack_from('<I', package, 4)[0]
    meta = json.loads(package[12:12 + size])
    cg = (directory / entry['cg']).read_text(encoding='utf-8')
    return dict(implementation=entry['implementation'], signature=signature(cg, meta['uniforms']),
                parameters=parameters(meta['uniforms']),
                textures={'samplerFore': 0, 'samplerMask': 1, 'samplerUser': 3},
                vertex='sprite-abi1', clip='art_clip', background_read=False)


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidates', type=Path, required=True, help='directory containing catalog.json and translated AGX1/Cg files')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    directory = ROOT / 'shaders/psv'
    catalog = json.loads((directory / 'catalog.json').read_text(encoding='utf-8'))
    canonical = {describe(directory, e)['signature']: e for e in catalog if e['source_hash'] == e['canonical_hash']}
    matches = {}
    different = 0
    for entry in json.loads((args.candidates / 'catalog.json').read_text(encoding='utf-8')):
        desc = describe(args.candidates, entry)
        target = canonical.get(desc['signature'])
        if target is None:
            different += 1
            continue
        matches[entry['source_hash']] = dict(source_hash=entry['source_hash'], implementation=target['implementation'], signature=desc['signature'])
    args.output.write_text(json.dumps(dict(version=1, matches=sorted(matches.values(), key=lambda e:e['source_hash'])), indent=2) + '\n', encoding='utf-8')
    print(f'{len(matches)} verified aliases; {different} different sources kept separate')


if __name__ == '__main__':
    main()
