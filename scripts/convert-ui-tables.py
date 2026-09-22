"""Convert pixel fields in Artemis UI tables, preserving script values/commands.

Reads the unmodified extracted table and writes a loose override, never rescales
its own output. Game-wide image positions already handled by mulpos are untouched.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path


def convert(source, scale):
    active = False
    rows = 0
    output = []
    scalar = re.compile(r'(?<=[,{])(\s*)(x|y|w|h|cx|cy|cw|ch|p2)\s*=\s*("?)(-?\d+(?:\.\d+)?)\3(?=\s*[,}])')
    rectangles = re.compile(r'(?<=[,{])(\s*)(clip(?:_[acd])?|area)\s*=\s*"(-?[\d.,]+)"')
    for line in source.splitlines(keepends=True):
        if re.match(r'^\s*ui_\w+\s*=\s*\{\s*$', line):
            active = True
            output.append(line)
            continue
        elif active and re.match(r'^\s*},?\s*$', line):
            active = False
        if active and re.match(r'^\s*\w+\s*=\s*\{', line):
            command = re.search(r'\bcom\s*=\s*"([^"]+)"', line)
            command = command[1] if command else ''
            if not command:
                raise ValueError('Unrecognized UI row: ' + line[:100])

            def value(match):
                space, key, quote, number = match.groups()
                # obj2/obj3 encode percent scales in w/h, not pixel dimensions.
                if (key == 'w' and command in ('obj2', 'obj3')) or (key == 'h' and command == 'obj3'):
                    return match[0]
                # Slider thumb extent is the only p2 value in pixel units here.
                if key == 'p2' and command not in ('xslider', 'yslider'):
                    return match[0]
                return f'{space}{key}={quote}{math.floor(float(number) * scale)}{quote}'

            line = scalar.sub(value, line)

            def rectangle(match):
                numbers = match[3].split(',')
                if len(numbers) != 4:
                    raise ValueError('Unexpected rectangle: ' + match[0])
                values = ','.join(str(math.floor(float(n) * scale)) for n in numbers)
                return f'{match[1]}{match[2]}="{values}"'

            line = rectangles.sub(rectangle, line)
            rows += 1
        output.append(line)
    if not rows:
        raise ValueError('No UI rows found')
    return ''.join(output), rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('game', choices=['shuffle-steam', 'otomeriron'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    scale = .75 if args.game == 'shuffle-steam' else .5
    basename = 'root.pfs' if args.game == 'shuffle-steam' else 'otomeriron.pfs'
    source = root / 'temp/conversion-work' / args.game / basename / 'system/table/list_windows.tbl'
    raw = source.read_bytes()
    converted, rows = convert(raw.decode('utf-8-sig'), scale)
    target = root / 'temp/converted' / args.game / 'system/table/list_windows.tbl'
    target.parent.mkdir(parents=True, exist_ok=True)
    data = converted.encode('utf-8')
    target.write_bytes(data)
    report = dict(game=args.game, scale=scale, rows=rows, source=str(source),
                  sourceSha256=hashlib.sha256(raw).hexdigest(),
                  outputSha256=hashlib.sha256(data).hexdigest(), override='system/table/list_windows.tbl')
    (target.parents[2] / 'ui-conversion-manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
