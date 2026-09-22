"""Private local fixture: prefetch, exceed GPU residency, revisit the same PNG.

Extracts one user-owned image into temp/ only. No game archive is changed.
Run from the repository root; install the resulting TEST_DIALOGUE_CACHE as a
separate temporary test game, never overlay it onto the source game.
"""
from pathlib import Path
import argparse, json, runpy, struct, zlib

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--game', type=Path, required=True)
p.add_argument('--asset', default='image/bg/bg01fs_s.png')
a = p.parse_args()
out = ROOT/'temp/dialogue-image-cache/TEST_DIALOGUE_CACHE'
out.mkdir(parents=True, exist_ok=True)
lib = runpy.run_path(str(ROOT/'scripts/prepare-native-command-tests.py'))
meta = lib['extract'](lib['index'](a.game), a.asset, out/'assets/first.png')
(out.parent/'source.json').write_text(json.dumps(meta, indent=2))

def chunk(kind, data):
    return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))

for n in range(18):
    raw = (b'\0'+bytes([n*13 % 256,80,150,255])*960)*540
    png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',960,540,8,6,0,0,0))
    png += chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    (out/f'assets/fill{n}.png').write_bytes(png)
(out/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n')
(out/'platform.txt').write_text('VITA\n')
(out/'title.txt').write_text('TEST DIALOGUE CACHE\n')
rows = ['*top','[debug mode=1 level=3]','[lyc id=0 color=182334 width=960 height=544]',
        '[lua]\nfunction cache_probe_prepare(e) e:bindSurfaceAsync("assets/first.png") end\n[/lua]',
        '[calllua function="cache_probe_prepare"]','[wait time=3000 input=0]']

def show(file, marker):
    rows.extend([f'[debugprint data="CACHE-PROBE {marker}"]','[lydel id=10]',
                 f'[lyc id=10 file="assets/{file}.png"]','[trans time=0]','[wait time=800 input=0]'])

show('first','FIRST')
rows += ['[takess]','[savess file="first.png" width=960 height=544]']
for n in range(18):
    show(f'fill{n}',f'FILL{n}')
show('first','RETURN')
rows += ['[takess]','[savess file="return.png" width=960 height=544]',
         '[debugprint data="CACHE-PROBE DONE"]','[stop]']
(out/'probe.iet').write_text('\n'.join(rows),encoding='utf-8')
print(out)
