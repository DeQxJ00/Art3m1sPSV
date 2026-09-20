"""Compare physical Vita captures before/after the alpha atlas change."""
import argparse
import json
from PIL import Image, ImageChops
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
p.add_argument('--reference', default='baseline')
p.add_argument('--candidate', default='candidate')
p.add_argument('--max-delta', type=int, default=0,
               help='Explicit accepted per-channel tolerance; default requires exact pixels')
a = p.parse_args()
report = {}
passed = True
for i in range(3):
    old = Image.open(a.directory/f'{a.reference}-font{i}.png').convert('RGBA')
    new = Image.open(a.directory/f'{a.candidate}-font{i}.png').convert('RGBA')
    assert old.size == new.size == (960,544)
    diff = ImageChops.difference(old,new)
    # The active host cache HUD has changing counters. All text to its left
    # and the complete lower area are compared, including transparent gutters.
    regions = [diff.crop((0,0,650,250)),diff.crop((0,250,960,544))]
    delta = max(hi for r in regions for lo,hi in r.getextrema())
    report[str(i)] = {'max_delta_outside_hud':delta,
                      'different_pixels_outside_hud':sum(any(px) for r in regions for px in r.getdata()),
                      'accepted_max_delta':a.max_delta,
                      'all_difference_bbox':diff.convert('RGB').getbbox(),
                      'excluded_hud_rect':[650,0,960,250]}
    passed = passed and delta <= a.max_delta
print(json.dumps(report,indent=2))
raise SystemExit(0 if passed else 1)
