"""Linux/WSL: exercise the shipped facade/evaluator with a caller-supplied PSB."""
from pathlib import Path
import argparse
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--asset', type=Path, required=True)
p.add_argument('--sdk-include', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
source = root/'vendor/emote-recovery'
build = root/'build/mesh-ratio-regression'
build.mkdir(parents=True, exist_ok=True)
groups = {
    'native': ['model_view', 'scene_runtime', 'timeline', 'model_controls', 'render_packet'],
    'recovered': ['psb', 'curve', 'mesh', 'mesh_child', 'motion_events', 'motion_clock',
                  'parameter', 'controls', 'physics_schedule', 'physics_lookup', 'wind',
                  'material', 'material_colors'],
}
for folder in groups:
    for path in (source/folder).glob('*.h'):
        shutil.copyfile(path, build/path.name)
# Only these recovery headers assert the 32-bit target's pointer layout.
for name in ['psb.h', 'controls.h']:
    path = build/name
    path.write_text('\n'.join(line for line in path.read_text().splitlines()
                              if not line.startswith('_Static_assert')))
header = build/'psp2/kernel/threadmgr/thread.h'
header.parent.mkdir(parents=True, exist_ok=True)
header.write_text('#include <psp2/types.h>\nSceInt64 sceKernelGetSystemTimeWide(void);')
flags = ['-O1', '-g', '-DEM_LOG_DISABLED=1', '-ffp-contract=off', '-fno-builtin-fmodf', '-fsanitize=address,undefined',
         '-I'+str(build), '-I'+str(root/'host-direct/src'), '-idirafter', str(a.sdk_include)]
objects = []
for folder, names in groups.items():
    for name in names:
        # Use the copied headers even for quoted includes in the C source.
        local = build/(name+'.c')
        shutil.copyfile(source/folder/local.name, local)
        obj = build/(name+'.o')
        subprocess.run(['gcc', *flags, '-c', str(local), '-o', str(obj)], check=True)
        objects.append(str(obj))
exe = build/'mesh-ratio-test'
subprocess.run(['g++', '-std=c++17', *flags, '-pthread',
                str(root/'tests/emote_recovery/mesh_ratio.cpp'), *objects, '-lm', '-o', str(exe)], check=True)
subprocess.run([str(exe), str(a.asset.resolve())], check=True)
