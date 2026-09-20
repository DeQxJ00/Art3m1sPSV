"""Compare the fixture captures; only the observed host cache HUD is excluded."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--prefix', default='candidate')
a = p.parse_args()
results = {}
for mode in ['plain', 'mask']:
    images = [Image.open(a.directory/f'{a.prefix}-{mode}-{fmt}.png').convert('RGBA') for fmt in ['rgba','gray']]
    assert images[0].size == images[1].size == (960,544)
    diff = ImageChops.difference(*images)
    # The host HUD in this fixture occupies the upper right and its cache hit
    # counts necessarily change between captures. Compare all remaining pixels.
    regions = [diff.crop((0,0,650,250)), diff.crop((0,250,960,544))]
    delta = max(hi for region in regions for lo,hi in region.getextrema())
    results[mode] = {'max_delta_outside_hud':delta,
                     'all_differences_bbox':diff.convert('RGB').getbbox(),
                     'excluded_hud_rect':[650,0,960,250]}
    assert delta == 0, (mode, results[mode])
print(json.dumps(results, indent=2))
