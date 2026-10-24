"""Prepare a four-model residency stress fixture using external licensed PSBs.

The output (including captures and model files) belongs outside the repository.
This exercises the normal provider and model lifecycle; the separate opt-in
startup allocator probe measures actual fragmented allocation failures.
"""
import argparse
import hashlib
import json
import random
import struct
import zlib
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--models', type=Path, nargs=4, required=True)
p.add_argument('--pose', required=True)
p.add_argument('--idle', required=True)
p.add_argument('--rounds', type=int, default=12)
p.add_argument('--seed', type=int, default=104729)
p.add_argument('--resident-images', type=int, default=3,
               help='Live image barriers retained while all four models load (3..24).')
p.add_argument('--unique-images', action='store_true',
               help='Use new image resources each round so GPU cache hits cannot hide fragmentation.')
p.add_argument('--staged-models', action='store_true',
               help='Keep two models live during the second half of image churn, then create four.')
a = p.parse_args()
if not 1 <= a.rounds <= 48:
    p.error('--rounds must be between 1 and 48')
if not 3 <= a.resident_images <= 24:
    p.error('--resident-images must be between 3 and 24')
if a.output.resolve().is_relative_to(Path(__file__).resolve().parents[1]):
    p.error('--output must be outside the repository')
a.output.mkdir(parents=True, exist_ok=True)
models = []
for i, source in enumerate(a.models, 1):
    data = source.read_bytes()
    name = f'actor{i}_0.psb'
    (a.output/name).write_bytes(data)
    models.append({'file': name, 'sha256': hashlib.sha256(data).hexdigest()})

# Fully occupied images avoid sparse/cropped upload optimizations. Their
# repeating colors compress on disk, but use normal RGBA surfaces on the GPU.
sizes = [(256, 256), (512, 256), (512, 512), (768, 512), (1024, 512), (1024, 768)]
def image_name(i, r):
    return (f'r{r+1:02}_' if a.unique_images else '')+f'churn{i:02}.png'
for r in range(a.rounds if a.unique_images else 1):
    for i in range(24):
        w, h = sizes[i % len(sizes)]
        row = bytes(v for x in range(w) for v in ((x*17+i*23+r*11)%256, (x*7+i*61+r*13)%256, (x*29+i*13+r*17)%256, 255))
        def chunk(kind, data):
            return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress((b'\0'+row)*h))+chunk(b'IEND', b'')
        (a.output/image_name(i,r)).write_bytes(png)

q = lambda s: json.dumps(s, ensure_ascii=False)
script = ['*top', '[debug mode=1 level=3]', '[lyc id=0 width=960 height=544 color=203040]', '[lua]',
          'function getGameMode() return "adv" end', 'bench={models={}}',
          'function atlas_remove(e,p) bench.models={} end',
          'function atlas_create(e,p)',
          ' local count=(p and tonumber(p.count)) or 4',
          ' for i=1,count do',
          '  local em=__engine:createEmoteLayer{id="10."..i,files={"actor"..i.."_0.psb"},width=960,height=544}',
          '  em:setScale(0.30,0,0)', '  em:setCoord((i-2.5)*220,220,0,0)',
          f'  em:playTimeline({q(a.pose)},1)', f'  em:playTimeline({q(a.idle)},1)',
          '  bench.models[i]=em', ' end', 'end', '[/lua]',
          f'[debugprint data="ATLAS-FOUR seed={a.seed} rounds={a.rounds} BEGIN"]']
rng = random.Random(a.seed)
for r in range(a.rounds):
    script += [f'[debugprint data="ATLAS-FOUR round={r+1} phase=churn"]',
               '[calllua function=atlas_remove]', '[lydel id=10]', '[lydel id=5]', '[trans time=150]', '[wait time=600 input=0]']
    order = list(range(24)); rng.shuffle(order)
    # Feed the real GPU cache, then leave a few live images between idle ones.
    for j, i in enumerate(order):
        if a.staged_models and j == 12:
            script += ['[calllua function=atlas_create count=2]', '[trans time=150]', '[wait time=600 input=0]']
        script += [f'[lyc id=5.{i+1} file="{image_name(i,r)}"]',
                   f'[lyprop id=5.{i+1} left={(j%8)*120} top={(j//8)*60} zoom=10]',
                   '[trans time=0]', '[wait time=80 input=0]']
        if j >= 5 and j % 3 != r % 3:
            script += [f'[lydel id=5.{order[j-5]+1}]', '[trans time=0]']
    # Recreate the chosen live barriers, including layers removed during churn.
    # A small visible tile keeps each whole texture genuinely in use.
    for j, i in enumerate(order[-a.resident_images:]):
        script += [f'[lyc id=5.{i+1} file="{image_name(i,r)}"]',
                   f'[lyprop id=5.{i+1} left={(j%8)*120} top={(j//8)*60} zoom=10]',
                   '[trans time=0]', '[wait time=80 input=0]']
    for i in order[:-a.resident_images]:
        script += [f'[lydel id=5.{i+1}]']
    script += ['[trans time=150]', '[wait time=300 input=0]',
               f'[debugprint data="ATLAS-FOUR round={r+1} phase=create models=4"]',
               '[calllua function=atlas_create]', '[trans time=300]', '[wait time=7000 input=0]',
               '[takess]', f'[savess file="four-{r+1:02}.png" width=960 height=544]',
               f'[debugprint data="ATLAS-FOUR round={r+1} phase=resident models=4"]', '[wait time=2000 input=0]']
script += ['[debugprint data="ATLAS-FOUR END DONE"]', '[wait time=3600000 input=0]']
(a.output/'first.iet').write_text('\n'.join(script)+'\n', encoding='utf-8')
(a.output/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nBOOT=first.iet\n', encoding='utf-8')
(a.output/'title.txt').write_text('Four-model memory stress', encoding='utf-8')
(a.output.parent/'fixture.json').write_text(json.dumps({'seed': a.seed, 'rounds': a.rounds, 'resident_images': a.resident_images, 'unique_images': a.unique_images, 'staged_models': a.staged_models, 'models': models}, indent=2), encoding='utf-8')
image_count = 24 * (a.rounds if a.unique_images else 1)
print(f'Prepared {a.rounds} rounds with four models and {image_count} RGBA cache textures in {a.output}')
