"""Analyze stable five-second device windows; require exact neutral group pixels."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from PIL import Image, ImageChops

parser = argparse.ArgumentParser()
parser.add_argument('run', type=Path)
args = parser.parse_args()
folder = args.run
phase = None
last = None
samples = {}
timing = None
timings = {}
log = (folder / 'host.log').read_text(encoding='utf-8', errors='replace')
for line in log.splitlines():
    match = re.search(r'RECOVERED-BENCH phase=(\S+) (BEGIN|END)', line)
    if match:
        phase = match[1] if match[2] == 'BEGIN' else None
        last = None
        timing = None
    if phase and '[recovered-perf]' in line:
        timing = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
    if phase and '[frame-perf]' in line:
        values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
        if last is not None:
            samples.setdefault(phase, []).append((values['frames'], values['at_us'] - last, values['max_us']))
            if timing is not None:
                timings.setdefault(phase, []).append((values['frames'], timing))
        last = values['at_us']
        timing = None
result = {'phases': {}, 'log_sha256': hashlib.sha256((folder / 'host.log').read_bytes()).hexdigest(),
          'script_error_count': sum('[core:E]' in line for line in log.splitlines()),
          'method': 'Weighted frames/time; discard first window crossing each BEGIN; max is observed window max, not percentile.',
          'complete': any(marker in log for marker in ('RECOVERED-BENCH phase=negative-alpha END DONE', 'RECOVERED-PIXELS frozen=1 END DONE', 'RECOVERED-LIFECYCLE END DONE')), 'neutral_pixels': [],
          'frozen_probe': 'RECOVERED-PIXELS frozen=1 END DONE' in log,
          'pixel_caveat': 'Equality requires frozen identical poses; animated captures cannot isolate group color error.'}
for phase, rows in samples.items():
    frames = sum(r[0] for r in rows)
    seconds = sum(r[1] for r in rows) / 1e6
    result['phases'][phase] = dict(fps=frames / seconds, samples=len(rows), frames=frames,
                                 seconds=seconds, max_ms=max(r[2] for r in rows) / 1000)
    if phase in timings:
        items = timings[phase]
        count = sum(n for n, _ in items)
        result['phases'][phase]['recovered_per_frame'] = {
            key: sum(t[key] for _, t in items) / count
            for key in ('capture_us', 'replay_us', 'prepare_us', 'advance_us', 'captures', 'replays')}
for name in ('neutral-group.png', 'neutral-nested.png'):
    if not (folder / name).exists():
        continue
    reference = Image.open(folder / 'neutral-direct.png').convert('RGBA')
    candidate = Image.open(folder / name).convert('RGBA')
    assert reference.size == candidate.size
    delta = ImageChops.difference(reference, candidate)
    differences = sum(any(p) for p in delta.getdata())
    result['neutral_pixels'].append(dict(candidate=name, differing_pixels=differences,
                                        max_channel_delta=max(v[1] for v in delta.getextrema()), exact=differences == 0))
result['pixel_acceptance'] = ('pass' if result['frozen_probe'] and len(result['neutral_pixels']) == 2 and all(p['exact'] for p in result['neutral_pixels'])
                              else 'fail' if result['frozen_probe'] else 'not_validated_poses_not_frozen')
(folder / 'performance.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
