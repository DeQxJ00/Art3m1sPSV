"""Build an external E-mote motion demo from a user-supplied embedded-texture PSB.

Only the harness is tracked. The model stays in the output directory, outside
the VPK and source tree. Edit the harness timelines for other model exports.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=0,
                        help='automatic page duration; 0 waits for Circle on each page')
    parser.add_argument('--scale', type=float, default=0.6)
    parser.add_argument('--y', type=float, default=400)
    parser.add_argument('--mouth-max', type=float, default=5,
                        help='face_talk rangeEnd from the model motion parameter (default: 5)')
    args = parser.parse_args()
    if not args.model.is_file() or args.seconds < 0:
        parser.error('model must exist and seconds must be non-negative')
    if not math.isfinite(args.mouth_max) or args.mouth_max <= 0:
        parser.error('mouth-max must be finite and positive')
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('output must be empty; preserve existing demo resources')
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.model, out / 'model.psb')
    shutil.copyfile(root / 'host/assets/menu.ttf', out / 'font.ttf')
    source = (root / 'tests/emote_motion/first.iet').read_text(encoding='utf-8')
    wait = f'[wait time={round(args.seconds * 1000)} input=1]' if args.seconds else '[@]'
    source = source.replace('[wait time=DEMO_SECONDS input=1]', wait)
    source = source.replace('DEMO_SCALE', str(args.scale)).replace('DEMO_Y', str(args.y))
    source = source.replace('DEMO_MOUTH_MAX', str(args.mouth_max))
    (out / 'first.iet').write_text(source, encoding='utf-8')
    (out / 'title.txt').write_text('E-mote Motion Demo\n', encoding='utf-8')
    (out / 'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=first.iet\n', encoding='utf-8')
    (out / 'demo-manifest.json').write_text(json.dumps({
        'model_sha256': hashlib.sha256(args.model.read_bytes()).hexdigest(),
        'scale': args.scale, 'y': args.y, 'seconds': args.seconds,
        'mouth_max': args.mouth_max,
        'package': 'external game directory; no assets bundled in VPK',
    }, indent=2), encoding='utf-8')
    print(out)


if __name__ == '__main__':
    main()
