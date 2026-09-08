"""Summarize saved Direct logs; upload timings are not whole-game FPS."""
import argparse
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('logs', nargs='+', type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()

def fields(line):
    return {k: int(v) for k, v in re.findall(r'(\w+)=([0-9]+)(?=\s|;|$)', line)}

def distribution(values):
    return {'count': len(values), 'min': min(values), 'median': statistics.median(values),
            'max': max(values)} if values else None

rows = []
for path in a.logs:
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    groups = {}
    frames = []
    for line in lines:
        if '[gxm-upload]' in line:
            size = re.search(r'size=(\d+x\d+)', line).group(1)
            groups.setdefault(size, []).append(fields(line))
        if '[frame-perf]' in line:
            frames.append(fields(line))
    rows.append({'path': str(path.resolve()), 'header': lines[:3],
        'uploads_by_size': {size: {key: distribution([d[key] for d in items])
            for key in ['alloc_us', 'clear_us', 'copy_us', 'bounds_us']} for size, items in groups.items()},
        'last_frame_windows': frames[-12:],
        'note': 'Per-upload timings are measured components, not whole-frame latency. Last windows may be idle; inspect scene and input evidence before comparing FPS.'})
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
for row in rows:
    print(row['path'])
    print(json.dumps(row['uploads_by_size'].get('960x540'), ensure_ascii=False))
