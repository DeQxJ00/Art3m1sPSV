"""Check a T01 sky-scene save thumbnail copied from Vita or Vita3K."""
import argparse
import json
from pathlib import Path

from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("png", type=Path)
args = parser.parse_args()
with Image.open(args.png) as source:
    assert source.format == "PNG", "not a PNG"
    image = source.convert("RGBA")
    extrema = image.getextrema()
    colors = len(set(image.getdata()))
    result = {"size": image.size, "extrema": extrema, "colors": colors,
              "bytes": args.png.stat().st_size}
    print(json.dumps(result))
    assert image.size == (120, 67), "unexpected T01 thumbnail dimensions"
    assert extrema[3] == (255, 255), "thumbnail contains transparent pixels"
    assert colors > 64, "expected a rendered scene, got a blank/flat thumbnail"
