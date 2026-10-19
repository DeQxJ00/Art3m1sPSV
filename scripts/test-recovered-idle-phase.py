"""Synthetic idle phase and authored timeline timing regression (ASan/UBSan)."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, sys

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--recovery-root', type=Path, default=root.parent / 'emote-psv-test')
a = p.parse_args()
sys.path.insert(0, str(a.recovery_root / 'tools'))
from scene_fixture import psb_fixture

b = root / 'build/idle-phase-test'
b.mkdir(parents=True, exist_ok=True)
def linux(path):
    return '/mnt/' + str(path.resolve())[0].lower() + str(path.resolve())[2:].replace('\\', '/')

for folder, names in [('native', ['timeline.c', 'timeline.h']),
                      ('recovered', ['psb.c', 'psb.h', 'controls.c', 'controls.h', 'timeline_commands.h'])]:
    for name in names:
        shutil.copyfile(root / 'vendor/emote-recovery' / folder / name, b / name)
for name in ['psb.h', 'controls.h']:
    f = b / name
    f.write_text('\n'.join(s for s in f.read_text().splitlines() if not s.startswith('_Static_assert')))
tracks = []
for name, begin, instant in [('idle', 0, False), ('intro', 10, False), ('gesture', -1, False), ('face', 0, True)]:
    t = {'label': name, 'diff': 1, 'lastTime': 120, 'variableList': [
        {'label': 'face' if instant else 'body', 'frameList': [
            {'time': 0, 'type': 1, 'content': {'value': 0}},
            {'time': 60, 'type': 1, 'content': {'value': 1}},
            {'time': 120, 'type': 0}]}]}
    if begin >= 0:
        t.update(loopBegin=begin, loopEnd=120)
    tracks.append(t)
for spec in ['win', 'vita']:
    (b / (spec + '.psb')).write_bytes(psb_fixture({'spec': spec, 'metadata': {
        'instantVariableList': ['face'], 'timelineControl': tracks}}))
shutil.copyfile(root / 'tests/emote_recovery/idle_phase.c', b / 'test.c')
exe = b / 'test'
subprocess.run(['wsl', '-e', 'gcc', '-O2', '-g', '-ffp-contract=off',
                '-fsanitize=address,undefined', '-no-pie', '-I', linux(b),
                *[linux(b / (n + '.c')) for n in ['test', 'psb', 'controls']],
                '-lm', '-o', linux(exe)], check=True)
r = subprocess.run(['wsl', '-e', linux(exe), linux(b / 'win.psb'), linux(b / 'vita.psb')], capture_output=True, text=True)
print(r.stdout, r.stderr)
r.check_returncode()
assert not r.stderr
paths = ['vendor/emote-recovery/native/timeline.c', 'vendor/emote-recovery/native/timeline.h',
         'tests/emote_recovery/idle_phase.c', 'scripts/test-recovered-idle-phase.py']
(root / 'tests/emote_recovery/idle-phase-verification.json').write_text(json.dumps({
    'status': 'pass', 'scope': 'Synthetic Win/Vita timing and context isolation; no device visual acceptance.',
    'result': r.stdout.strip(), 'sha256': {n: hashlib.sha256((root/n).read_bytes()).hexdigest() for n in paths}
}, indent=2) + '\n')
