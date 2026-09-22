"""Generate a synthetic nested-effect fixture outside installed games.

Includes the mosaic regression plus three independently blurred inputs under
one changing parent. No commercial assets or game configuration are modified.
"""
from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'temp/node-cache-test/fixture'


def main():
    spec = importlib.util.spec_from_file_location('mosaic_fixture', ROOT / 'scripts/prepare-mosaic-perf-test.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.OUT = OUT
    module.main()
    source = (ROOT / 'tests/direct_gxm/fixtures/mosaic_source.iet').read_text()
    source = source[:source.index('[debugprint data="MOSAIC-PROBE READY"]')]
    source += '\n[lydel id=1]\n'
    weights = '0.2,0.15,0.10,0.06,0.04,0.025,0.015,0.01'
    for i in range(3):
        asset = 'bg.png' if i == 0 else 'fg.png'
        source += f'[lyc id=1.{i}.0.0 file="{asset}"]\n'
        position = '' if i == 0 else f' left={100 if i == 1 else 520} top=30'
        source += f'[lyprop id=1.{i}.0.0 intermediate_render=2 shader=blur_h shaderconstant="width,weights" width=0.004 weights="{weights}"{position}]\n'
        source += f'[lyprop id=1.{i}.0 intermediate_render=2 shader=blur_v shaderconstant="height,weights" height=0.004 weights="{weights}"]\n'
        source += f'[lyprop id=1.{i} intermediate_render=2]\n'
    source += '[lyprop id=1 intermediate_render=2]\n'
    source += '[trans time=0]\n[debugprint data="NODE-PROBE three-blur"]\n[wait time=6000 input=0]\n'
    source += '[takess]\n[savess file="three.png" width=960 height=544]\n'
    source += '[debugprint data="NODE-PROBE changing-outer"]\n'
    for _ in range(3):
        for alpha in [255,240,225,210,195,180,165,150,135,120]:
            source += f'[lyprop id=1 alpha={alpha}]\n[trans time=0]\n[wait time=100 input=0]\n'
    for name, change in [
        ('three-alpha', ''),
        ('three-move', '[lyprop id=1.1.0.0 left=300]\n[trans time=0]\n'),
        ('three-uniform', f'[lyprop id=1.2.0 shader=blur_v shaderconstant="height,weights" height=0.012 weights="{weights}"]\n[trans time=0]\n'),
    ]:
        source += change + f'[wait time=300 input=0]\n[takess]\n[savess file="{name}.png" width=960 height=544]\n'
    source += '[debugprint data="MOSAIC-PROBE READY"]\n[@]\n[stop]\n'
    (OUT / 'probe.iet').write_text(source)


if __name__ == '__main__':
    main()
