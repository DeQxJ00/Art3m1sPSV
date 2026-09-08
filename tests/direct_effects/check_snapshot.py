"""Check production GPU snapshot composition through the emulator's final image."""
import json
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGB')
assert im.size == (960,544), im.size
checks = []
for x,y,color in [(120,120,(128,0,127)),(840,120,(0,128,127)),
                  (120,400,(0,0,255)),(840,400,(0,255,0))]:
    for dx in range(-30,31,10):
        for dy in range(-30,31,10):
            got = im.getpixel((x+dx,y+dy))
            assert max(abs(a-b) for a,b in zip(got,color)) <= 1, ((x+dx,y+dy),got,color)
            checks.append({'xy':[x+dx,y+dy],'actual':got,'expected':color})
for x in range(400,561,5):
    if abs(x-480) <= 1: continue
    rule = min(1,max(0,2*(x+.5)/960-.5))
    t = min(1,max(0,(rule-.4)/.2))
    alpha = t*t*(3-2*t)
    expected = (round(255*alpha) if x<480 else 0,
                round(255*alpha) if x>480 else 0,round(255*(1-alpha)))
    got=im.getpixel((x,400))
    assert max(abs(a-b) for a,b in zip(got,expected)) <= 3,(x,got,expected)
    checks.append({'xy':[x,400],'actual':got,'expected':expected})
print(json.dumps({'passed':len(checks),'checks':checks},indent=2))
