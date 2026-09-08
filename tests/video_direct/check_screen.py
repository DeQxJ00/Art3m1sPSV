"""Check a screenshot of the probe's final, stationary color bars."""
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
assert im.size == (960, 544), im.size
colors = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
          (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
for i, expected in enumerate(colors):
    for y in (0, 272, 542, 543):
        actual = im.getpixel((i * 120 + 60, y))
        assert max(abs(a-b) for a, b in zip(actual, expected)) <= 8, (i, y, actual, expected)
print("PASS: eight NV12 color bars, top/center/bottom; no padded-row color bleed")
