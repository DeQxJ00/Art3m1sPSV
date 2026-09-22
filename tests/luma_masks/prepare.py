"""Generate synthetic Gray8/RGBA mask comparisons outside installed games."""
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'temp/mask-format-audit/fixture'


def png(path, width, height, color, pixels, extra=b''):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    channels = {0: 1, 6: 4}[color]
    raw = b''.join(b'\0' + pixels[y*width*channels:(y+1)*width*channels] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, color, 0, 0, 0))
                    + extra + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for w, h, name in [(17, 9, 'odd'), (960, 540, 'full')]:
        gray = bytes(((x*255//max(1,w-1)) ^ (y*19 & 255)) for y in range(h) for x in range(w))
        png(OUT/f'{name}-gray.png', w, h, 0, gray)
        png(OUT/f'{name}-rgba.png', w, h, 6, b''.join(bytes([v,v,v,255]) for v in gray))
    (OUT/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n')
    (OUT/'platform.txt').write_text('VITA\n')
    (OUT/'title.txt').write_text('TEST SINGLE CHANNEL MASKS\n')
    s = ['*top', '[debug mode=1 level=3]', '[lua]',
         'function prepare_luma(e) e:bindSurfaceAsync("full-gray.png"); e:bindSurfaceAsync("full-rgba.png") end',
         '[/lua]', '[calllua function="prepare_luma"]', '[wait time=2000 input=0]',
         '[lyc id=0 color=204060 width=960 height=544]']
    for mode in ['plain', 'mask']:
        for fmt in ['rgba', 'gray']:
            s += ['[lydel id=10]']
            if mode == 'plain':
                s += [f'[lyc id=10 file="full-{fmt}.png"]', '[lyprop id=10 alpha=180]']
            else:
                s += ['[lyc id=10.0 color=e06080 width=960 height=540]',
                      f'[lyprop id=10 intermediate_render=2 intermediate_render_mask="full-{fmt}.png"]']
            s += ['[trans time=0]', '[wait time=1200 input=0]', '[takess]',
                  f'[savess file="{mode}-{fmt}.png" width=960 height=544]',
                  f'[debugprint data="LUMA-PROBE {mode}-{fmt}"]']
    s += ['[debugprint data="LUMA-PROBE DONE"]', '[stop]']
    (OUT/'probe.iet').write_text('\n'.join(s), encoding='utf8')
    print(OUT)


if __name__ == '__main__':
    main()
