import sys
from pathlib import Path
from PIL import Image, ImageDraw

p = Path(sys.argv[1])
files = sorted(p.glob('[0-9]*.png'))[::int(sys.argv[2]) if len(sys.argv) > 2 else 3]
out = Image.new('RGB', (960, 156 * ((len(files)+3)//4)))
d = ImageDraw.Draw(out)
for i, f in enumerate(files):
    x, y = (i%4)*240, (i//4)*156
    out.paste(Image.open(f).resize((240,136)), (x,y))
    d.text((x,y+136), f.stem)
out.save(p/'sheet.jpg')
