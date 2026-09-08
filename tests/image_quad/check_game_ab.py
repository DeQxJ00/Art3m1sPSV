"""Compare stationary T01 frames from the same run and atlas (A/B build).

Excludes only the path marker and known animated click-wait icon. Loading,
revealing text, moving characters, or different dialogue invalidate this test.
"""
from pathlib import Path
import argparse
import json
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
old = np.asarray(Image.open(args.directory/'legacy.png').convert('RGB'), dtype=np.int16)
new = np.asarray(Image.open(args.directory/'quad.png').convert('RGB'), dtype=np.int16)
assert old.shape == new.shape == (544, 960, 3)
assert (old[2, 2] == [255, 0, 0]).all(), 'Legacy marker missing'
assert (new[2, 2] == [0, 255, 0]).all(), 'Quad marker missing'
diff = np.abs(old-new)
mask = np.ones(diff.shape[:2], dtype=bool)
mask[:6, :6] = False
mask[466:522, 880:936] = False
report = {
    'max_error': int(diff[mask].max()),
    'mean_error': float(diff[mask].mean()),
    'pixels_over_3': int((diff.max(axis=2)[mask] > 3).sum()),
    'excluded': '6x6 diagnostic marker and animated wait icon x880..936 y466..522',
    'scope': 'Same-run screenshot RGB; no physical performance or framebuffer-alpha conclusion',
}
report['passed'] = report['pixels_over_3'] == 0
(args.directory/'pixels.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
assert report['passed'], 'Game pixels differ; first verify the scene and reveal state are unchanged'
