"""Compare the legacy and quad halves of a settled 960x544 probe capture."""
from pathlib import Path
import argparse
import json
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('capture', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
image = np.asarray(Image.open(args.capture).convert('RGB'), dtype=np.int16)
assert image.shape == (544, 960, 3), image.shape
diff = np.abs(image[:, :480] - image[:, 480:])
background = np.array([20, 80, 180], dtype=np.int16)
assert np.abs(image[0, 0] - background).max() <= 1, 'Probe background is not ready'
cells = []
for cell in range(12):
    left = (cell % 3) * 160
    top = (cell // 3) * 136
    part = diff[top:top+136, left:left+160]
    legacy = image[top:top+136, left:left+160]
    visible = int((np.abs(legacy - background).max(axis=2) > 3).sum())
    cells.append({'cell': cell, 'max_channel_error': int(part.max()),
                  'mean_channel_error': float(part.mean()),
                  'pixels_over_3': int((part.max(axis=2) > 3).sum()),
                  'visible_reference_pixels': visible})
report = {'capture': str(args.capture), 'checks': cells,
          'passed': all(c['pixels_over_3'] == 0 and c['visible_reference_pixels'] > 200 for c in cells),
          'scope': 'Screenshot RGB comparison, not framebuffer alpha or physical Vita performance'}
print(json.dumps(report, indent=2))
if args.output:
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report['passed'], 'Legacy/quad pixels differ beyond RGB tolerance 3'
