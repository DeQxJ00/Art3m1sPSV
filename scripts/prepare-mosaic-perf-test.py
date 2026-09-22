"""Generate a synthetic Vita mosaic regression fixture outside installed games.

No commercial game assets are extracted. Copy the generated fixture to an
independent test game only when deploying; preserve the user's game selection.
"""
from pathlib import Path
import shutil
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'temp/mosaic-source-test/fixture'


def png(name, width, height, pixel):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + bytes(v for x in range(width) for v in pixel(x, y)) for y in range(height))
    (OUT / name).write_bytes(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ['mosaic', 'blur_h', 'blur_v']:
        shutil.copyfile(ROOT / 'host-direct/assets/TEST_SHADERS_51/sources/shared/system/shader/pc' / (name + '.hlsl'),
                        OUT / (name + '.hlsl'))
    shutil.copyfile(ROOT / 'tests/direct_gxm/fixtures/mosaic_source.iet', OUT / 'probe.iet')
    (OUT / 'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n')
    (OUT / 'platform.txt').write_text('vita\n')
    png('bg.png', 960, 544, lambda x, y: (x*255//960, y*255//544, 220 if (x//17+y//17)%2 else 40, 255))
    png('fg.png', 320, 480, lambda x, y: (220, 80, 120, 192 if (x-160)**2/120**2+(y-240)**2/230**2 < 1 else 0))
    print(OUT)


if __name__ == '__main__':
    main()
