"""Compare the production raw-GXM probe with the captured no-MSAA reference.

Requires numpy and Pillow. Arguments: direct.png old-reference.png result.json.
The rotated NanoVG scissor cell is explicitly excluded: the core bridge supplies
an axis-aligned rectangle in stage space. Its hard clip is checked separately.
"""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image

actual = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
reference = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(int)
assert actual.shape == reference.shape == (544, 960, 3)
results = []
for cell in range(12):
    if cell == 10:
        continue
    x, y = cell % 3 * 160, cell // 3 * 136
    a = actual[y:y+136, x:x+160]
    # Right half is the previous native-v1 explicit-UV four-vertex path.
    b = reference[y:y+136, x+480:x+640]
    diff = abs(a-b)
    results.append(dict(case=f'reference_{cell}', maximum=int(diff.max()),
                        mean=float(diff.mean()), failed_pixels=int(np.any(diff > 3, axis=2).sum()),
                        passed=bool(diff.max() <= 3)))

background = np.array([20, 80, 180.])
source = np.array([200, 100, 50.])
def point(name, x, y, expected, tolerance=3):
    got = actual[y, x]
    error = float(np.max(abs(got-expected)))
    results.append(dict(case=name, point=[x, y], actual=got.tolist(),
                        expected=np.asarray(expected).tolist(), maximum=error, passed=error<=tolerance))
point('alpha_over', 550, 60, source*128/255+background*(1-128/255))
point('additive', 650, 60, np.minimum(255, source*128/255+background))
point('clip_inside', 780, 50, source*128/255+background*(1-128/255))
for x,y in [(750,50),(810,50),(780,30),(780,80)]:
    point(f'clip_outside_{x}_{y}',x,y,background)
for progress in [0,.5,1]:
    y=175+int(progress*2)*90
    for x in [510,550,600,650,700,750,790]:
        tex=(x+.5-500)/300*3-.5
        lo=int(np.floor(tex)); fraction=tex-lo
        mask=[0,128/255,1]
        sample=mask[max(0,min(2,lo))]*(1-fraction)+mask[max(0,min(2,lo+1))]*fraction
        threshold=progress*1.2-.2
        s=float(np.clip((sample-threshold)/.2,0,1)); alpha=s*s*(3-2*s)
        point(f'rule_{progress}_{x}',x,y,255*alpha+background*(1-alpha))
point('odd_stride_original',515,445,[240,0,0])
point('partial_update',565,465,[0,210,0])
point('partial_update_other_row',565,435,[240,0,0])
point('index_65535_last_quad',856,126,[130,130,190])
point('index_next_batch',896,126,[130,130,190])
for x,y in [(680,460),(910,460),(850,435),(850,495)]:
    point(f'rule_clip_outside_{x}_{y}',x,y,background)
for x in [710,795,850,890]:
    tex=(x+.5-660)/270*3-.5
    lo=int(np.floor(tex));fraction=tex-lo;mask=[0,128/255,1]
    sample=mask[max(0,min(2,lo))]*(1-fraction)+mask[max(0,min(2,lo+1))]*fraction
    s=float(np.clip((sample-.4)/.2,0,1));alpha=s*s*(3-2*s)
    point(f'rule_clip_inside_{x}',x,460,255*alpha+background*(1-alpha))
for comparison in range(4):
    x=500+comparison*110
    optimized=actual[514:538,x:x+46]
    blended=actual[514:538,x+50:x+96]
    diff=abs(optimized-blended)
    results.append(dict(case=f'opaque_vs_blended_{comparison}', maximum=int(diff.max()),
                        mean=float(diff.mean()), passed=bool(diff.max()<=3)))
passed=all(r['passed'] for r in results)
report=dict(passed=passed, checks=results, excluded_reference_cells=[10])
Path(sys.argv[3]).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
sys.exit(0 if passed else 1)
