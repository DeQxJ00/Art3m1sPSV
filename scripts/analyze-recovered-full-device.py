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
log = (folder / 'host.log').read_text(encoding='utf-8', errors='replace')
for line in log.splitlines():
    match = re.search(r'RECOVERED-BENCH phase=(\S+) (BEGIN|END)', line)
    if match:
        phase = match[1] if match[2] == 'BEGIN' else None
        last = None
    if phase and '[frame-perf]' in line:
        values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)}
        if last is not None:
            samples.setdefault(phase, []).append((values['frames'], values['at_us'] - last, values['max_us']))
        last = values['at_us']
result = {'phases': {}, 'log_sha256': hashlib.sha256((folder / 'host.log').read_bytes()).hexdigest(),
          'method': 'Weighted frames/time; discard first window crossing each BEGIN; max is observed window max, not percentile.',
          'complete': 'RECOVERED-BENCH phase=negative-alpha END DONE' in log, 'neutral_pixels': [],
          'pixel_caveat': 'Equality requires frozen identical poses; animated captures cannot isolate group color error.'}
for phase, rows in samples.items():
    frames = sum(r[0] for r in rows)
    seconds = sum(r[1] for r in rows) / 1e6
    result['phases'][phase] = dict(fps=frames / seconds, samples=len(rows), frames=frames,
                                 seconds=seconds, max_ms=max(r[2] for r in rows) / 1000)
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
(folder / 'performance.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
