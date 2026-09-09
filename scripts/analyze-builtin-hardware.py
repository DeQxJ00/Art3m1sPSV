"""Summarize complete, stationary windows from profile-builtin-hardware.py."""
import json
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
result = {}

def kv(line):
    return {k: float(v) for k, v in re.findall(r'(\w+)=([0-9]+(?:\.[0-9]+)?)', line)}

for name in ['new1', 'old', 'new2']:
    rows = []
    current = {}
    route = {'generic': 0, 'at_us': 0}
    input_at = 0
    last_time = 0
    for line in (root / (name + '.log')).read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('[builtin-route]'):
            route = kv(line)
        if line.startswith('[input-trace]'):
            input_at = last_time
        if line.startswith('[builtin-perf]'):
            current = {'builtin': kv(line), 'route': route, 'input_at': input_at}
        for prefix, key in [('builtin-groups', 'groups'), ('gxm-perf', 'draw'), ('gxm-wait', 'wait')]:
            if line.startswith('[' + prefix + ']'):
                current[key] = kv(line)
        if line.startswith('[frame-perf]'):
            frame = kv(line)
            last_time = frame['at_us']
            if 'builtin' in current:
                current['frame'] = frame
                rows.append(current)
            current = {}
    expected = 1 if name == 'old' else 0
    # Tail windows must wholly follow the new state; discard transition windows.
    good = [r for r in rows if r['route']['generic'] == expected
            and r['builtin']['generic'] == expected
            and r['frame']['at_us'] - r['route']['at_us'] > 6000000
            and r['frame']['at_us'] - r['input_at'] > 10000000
            and all(k in r for k in ['draw', 'wait', 'groups'])][-3:]
    assert len(good) == 3, (name, 'insufficient stationary windows', len(good))
    frames = sum(r['frame']['frames'] for r in good)
    def avg(section, key):
        return sum(r[section][key] * r['frame']['frames'] for r in good) / frames
    frame_us = sum(avg('frame', k) for k in ['media_avg_us', 'logic_menu_avg_us', 'direct_present_avg_us', 'capture_avg_us'])
    result[name] = {'frames': frames, 'windows': len(good), 'estimated_host_fps': 1e6 / frame_us,
                    'frame_ms': frame_us / 1000, 'present_ms': avg('frame', 'direct_present_avg_us') / 1000,
                    'switch_ms': avg('builtin', 'switch_avg_us') / 1000,
                    'begin_wait_ms': avg('wait', 'begin_avg_us') / 1000,
                    'groups': avg('groups', 'requested_avg'), 'flattened': avg('groups', 'flattened_avg'),
                    'quads': avg('draw', 'quads_avg'), 'draws': avg('draw', 'draws_avg'), 'rows': good}
(root / 'analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: {key: value for key, value in v.items() if key != 'rows'} for k, v in result.items()}, indent=2))
