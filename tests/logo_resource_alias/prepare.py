"""Create an isolated game; no proprietary assets or original game files changed."""
import argparse
from pathlib import Path
import struct
import zlib


def png(rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    # Deliberately not stage-sized, with a translucent border: preserve both size and alpha.
    w, h = 900, 520
    rows = b''.join(b'\0' + b''.join(bytes((*rgb, 139 if x == 0 or y == 0 else 255))
                                   for x in range(w)) for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('destination', type=Path, help='Dedicated TEST_LOGO_ALIAS game directory')
    args = ap.parse_args()
    dest = args.destination
    if dest.name != 'TEST_LOGO_ALIAS':
        ap.error('Use a dedicated TEST_LOGO_ALIAS directory, never a real game directory')
    (dest / 'image/bg').mkdir(parents=True, exist_ok=True)
    for name, rgb in [('black', (0, 0, 0)), ('white', (255, 255, 255))]:
        (dest / f'image/bg/{name}.png').write_bytes(png(rgb))
    (dest / 'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n')
    script = ['*top', '[lyc id=0 color=182334 width=960 height=544]',
              '[lyc id=1.80.button color=ff0000 width=240 height=70]',
              '[lyprop id=1.80.button left=600 top=430]']
    for i, name in enumerate([':bg/black', 'image/bg/black.png', ':bg/white', 'image/bg/white.png']):
        script += ['[lydel id=10]', f'[lyc id=10 file="{name}"]', '[trans time=0]',
                   f'[debugprint data="LOGO-ALIAS case={i} path={name}"]', '[wait time=3000 input=0]']
    script += ['[debugprint data="LOGO-ALIAS DONE"]', '[stop]']
    (dest / 'probe.iet').write_text('\n'.join(script) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
