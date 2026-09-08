"""Attach rolling core/audio/texture evidence to steady host A/B windows.

Core averages include tick-only samples: normalize by sample_count / rendered_frames.
These rolling windows are not synchronized with the host's five-second windows;
the values describe workload components and must not be summed as exact frames.
Run analyze-direct-ab.py first. No device access or mutations.
"""
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
comparison = json.loads((root / 'comparison.json').read_text(encoding='utf-8'))
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
fields = ('frame_build_ms', 'frame_backlog_ms', 'frame_text_ms',
          'frame_scene_ms', 'frame_retain_ms', 'gpu_submit_ms')
optional_fields = ('frame_history_sync_ms', 'frame_message_sync_ms', 'frame_text_metrics_ms')


def last_frame_time(path):
    return max(int(re.search(r'at_us=(\d+)', line)[1])
               for line in path.read_text(encoding='utf-8', errors='replace').splitlines()
               if '[frame-perf]' in line)


boundary = last_frame_time(root / 'before.log')
output = []
for phase, summary in zip(manifest['phases'], comparison, strict=True):
    assert phase['name'] == summary['phase']
    eligible = {row['at_us'] for row in summary.get('windows_raw', [])}
    marker = int(re.search(r'at_us=(\d+)', phase['state'])[1])
    boundary = max(boundary, marker) + 1000000
    snapshots = []
    core = None
    audio = texture = None
    for line in (root / phase['log']).read_text(encoding='utf-8', errors='replace').splitlines():
        if '[nextline-core]' in line:
            core = json.loads(line.split('[nextline-core]', 1)[1].strip())
        elif '[audio-detail]' in line:
            audio = line
        elif 'GXM texture-perf' in line:
            texture = line
        elif '[frame-perf]' in line and core:
            end = int(re.search(r'at_us=(\d+)', line)[1])
            # Leave a full core rolling window after the phase boundary.
            if end not in eligible or end - core['sample_window_ms'] * 1000 < boundary:
                continue
            rendered = core['rendered_frames']
            if not rendered:
                continue
            factor = core['sample_count'] / rendered
            snapshots.append({
                'end_us': end, 'audio': audio, 'texture': texture,
                'core_samples': core['sample_count'], 'core_rendered': rendered,
                'per_render_ms': {key: round(core['average'][key] * factor, 4)
                                  for key in (*fields, *(key for key in optional_fields
                                                       if key in core['average']))},
            })
    output.append({'phase': phase['name'], 'snapshots': snapshots})
    boundary = last_frame_time(root / phase['log'])

(root / 'interpretation.json').write_text(json.dumps(output, indent=2), encoding='utf-8')
for phase in output:
    print(json.dumps({'phase': phase['phase'], 'rolling_snapshots': len(phase['snapshots']),
                      'last': phase['snapshots'][-1] if phase['snapshots'] else None}))
