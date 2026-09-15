#!/usr/bin/env python3
"""Prepare isolated probes; never launch either game's EXE or deploy to games/."""
import argparse
from pathlib import Path
import math
import shutil
import struct
import wave
import zlib


def png(path):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
    raw = b''.join(b'\0' + bytes((x + y) % 256 for x in range(1024 * 4))
                   for y in range(1024))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', 1024, 1024, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    repo = Path(__file__).resolve().parent.parent
    fixtures = repo / 'tests/direct_gxm/fixtures/native_semantics'
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--font', type=Path, required=True, help='Existing CJK OTF/TTF; copied as font.otf')
    ap.add_argument('--otomeriron-exe', type=Path, help='Optional original otomeriron.exe copy; never executed')
    ap.add_argument('--video', type=Path, help='Optional synthetic three-second MP4 for video probes')
    ap.add_argument('--case', action='append', dest='cases', help='Fixture name without .iet; repeatable')
    args = ap.parse_args()
    dest = args.output.resolve()
    if not dest.is_relative_to((repo / 'build').resolve()) or dest == (repo / 'build').resolve():
        ap.error('--output must be a new subdirectory of this repository build/')
    if dest.exists():
        ap.error('--output already exists; use a fresh directory to avoid saved-state contamination')
    if not args.font.is_file():
        ap.error('--font must exist')
    if args.otomeriron_exe and (not args.otomeriron_exe.is_file()
                               or args.otomeriron_exe.name.lower() != 'otomeriron.exe'):
        ap.error('only an explicitly supplied otomeriron.exe is accepted; do not use Toshiue EXE')
    selected = sorted(fixtures.glob('*.iet'))
    if args.cases:
        unknown = set(args.cases) - {p.stem for p in selected}
        if unknown:
            ap.error(f'unknown cases: {sorted(unknown)}')
        selected = [p for p in selected if p.stem in args.cases]
    if any(p.stem.startswith('video-') for p in selected) and (not args.video or not args.video.is_file()):
        ap.error('video cases require --video with a synthetic MP4')
    dest.mkdir(parents=True)
    for script in selected:
        out = dest / script.stem
        out.mkdir()
        (out / 'savedata').mkdir()
        shutil.copy2(script, out / 'probe.iet')
        shutil.copy2(args.font, out / 'font.otf')
        if args.otomeriron_exe:
            shutil.copy2(args.otomeriron_exe, out / 'otomeriron.exe')
        if script.stem.startswith('video-'):
            shutil.copy2(args.video, out / 'test.mp4')
        settings = 'WIDTH=640\nHEIGHT=360\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n'
        (out / 'system.ini').write_text('[WINDOWS]\n' + settings + '[VITA]\n' + settings, encoding='utf8')
        png(out / 'image.png')
        with wave.open(str(out / 'tone.wav'), 'wb') as sound:
            sound.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
            sound.writeframes(b''.join(struct.pack('<h', int(800 * math.sin(2 * math.pi * 330 * i / 22050)))
                                       for i in range(22050)))
    print(f'Prepared {len(selected)} probes under {dest}; nothing was launched or deployed.')


if __name__ == '__main__':
    main()
