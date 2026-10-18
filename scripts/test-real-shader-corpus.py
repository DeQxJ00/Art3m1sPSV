"""Verify private real-source samples without copying source assets into git."""
import argparse
import json
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-dir', type=Path, required=True, help='private HASH.hlsl/glsl/sb files')
    p.add_argument('--translator', type=Path, required=True)
    p.add_argument('--compiler', type=Path)
    a = p.parse_args()
    matrix = json.loads((ROOT/'tests/external_shaders/real_source_cases.json').read_text(encoding='utf-8'))
    schema = runpy.run_path(str(ROOT/'scripts/shader-effect-schema.py'))
    catalog = json.loads((ROOT/'shaders/psv/catalog.json').read_text(encoding='utf-8'))
    canonical = {schema['describe'](ROOT/'shaders/psv', e)['signature']: e['implementation'] for e in catalog}
    signatures = {}
    with tempfile.TemporaryDirectory(prefix='shader-corpus-') as tmp:
        for c in matrix['cases']:
            source = a.source_dir/(c['source_hash']+c['suffix'])
            data = source.read_bytes()
            fingerprint = 0xcbf29ce484222325
            for byte in data:
                fingerprint = ((fingerprint ^ byte)*0x100000001b3) & 0xffffffffffffffff
            assert f'{fingerprint:016x}' == c['source_hash'], c['case']+' source changed'
            prefix = Path(tmp)/c['case']
            result = subprocess.run([str(a.translator), str(source), str(prefix)], capture_output=True)
            assert (result.returncode == 0) == c['expected_translation'], c['case']+' translation outcome changed'
            if result.returncode:
                continue
            cg = Path(str(prefix)+'.cg').read_text(encoding='utf-8')
            uniforms = json.loads(Path(str(prefix)+'.uniforms.json').read_text())
            signature = schema['signature'](cg, uniforms)
            assert signature == c['signature'], c['case']+' code or parameter ABI changed'
            assert canonical.get(signature) == c['expected_implementation'], c['case']+' equivalence changed'
            signatures[c['case']] = signature
            if a.compiler:
                result = subprocess.run([str(a.compiler), '-profile', 'sce_fp_psp2', '-O1', '-nofastmath', '-bestprecision', '-o', str(prefix)+'.gxp', str(prefix)+'.cg'], capture_output=True)
                assert result.returncode == 0, c['case']+' compiler rejected translated source'
        for left, right in matrix['equal_pairs']:
            assert signatures[left] == signatures[right], (left, right)
        for left, right in matrix['different_pairs']:
            assert signatures[left] != signatures[right], (left, right)
    print(f"PASS {len(matrix['cases'])} source cases, equivalence and difference pairs")


if __name__ == '__main__':
    main()
