"""Repair already resized Otomeriron tables using original PFS table references.

The current resource pack also halves AST coordinates, so retain its 640x360
logical canvas and 960x540 physical canvas. Never scale the adapted input again.
Original tables must be extracted outside the installed game directory.
"""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    'ui_conversion', Path(__file__).with_name('convert-ui-tables.py'))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def repair(adapted, original, common):
    if common:
        for pattern in (r'game_width\s*=\s*960\b', r'game_height\s*=\s*540\b',
                        r'game_scale\s*=\s*\{640,\s*360\}'):
            if not re.search(pattern, adapted):
                raise ValueError('Expected the existing half-size resource pack')
        # Rotation is dimensionless: the resize utility incorrectly halves r.
        rotations = re.findall(r'\br\s*=\s*(-?\d+)', original)
        if len(rotations) != len(re.findall(r'\br\s*=\s*(-?\d+)', adapted)):
            raise ValueError('Rotation entries differ; review table revision')
        values = iter(rotations)
        text = re.sub(r'\br\s*=\s*(-?\d+)', lambda m: 'r=' + next(values), adapted)
        return text, dict(uiRows=0, rotationFields=len(rotations))

    # Rebuild physical UI rows from originals: this also fixes omitted slider
    # hit areas and thumb extents, while preserving obj2/obj3 percentage values.
    row = re.compile(r'\bcom\s*=')
    # game_lang/game_cm are also UI tables although their names lack ui_.
    original_rows = [line for line in original.splitlines() if row.search(line)]
    converted, count = ui.convert('ui_repair = {\n' + '\n'.join(original_rows) + '\n}\n', .5)
    expected = [line for line in converted.splitlines() if row.search(line)]
    current = [line for line in adapted.splitlines() if row.search(line)]
    identity = lambda line: re.match(r'\s*(\w+)\s*=', line)[1]
    if list(map(identity, expected)) != list(map(identity, current)):
        raise ValueError('UI row order differs; review table revision')
    rows = iter(expected)
    text = '\n'.join(next(rows) if row.search(line) else line
                     for line in adapted.splitlines()) + '\n'
    return text, dict(uiRows=count, correctedRows=sum(a != b for a, b in zip(current, expected)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adapted', required=True, type=Path)
    parser.add_argument('--original', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.resolve() in (args.adapted.resolve(), args.original.resolve()):
        raise ValueError('Output must be separate from both source directories')
    pending, reports = [], []
    for suffix in ('', '_ja', '_cn', '_en', '_tw'):
        name = 'list_windows' + suffix + '.tbl'
        source = (args.adapted / name).read_bytes()
        original = (args.original / name).read_bytes()
        result, stats = repair(source.decode('utf-8-sig'), original.decode('utf-8-sig'), not suffix)
        data = result.encode('utf-8')
        for target in (name, 'list_vita' + suffix + '.tbl'):
            pending.append((args.output / target, data))
        reports.append(dict(table=name, sourceSha256=hashlib.sha256(source).hexdigest(),
                            originalSha256=hashlib.sha256(original).hexdigest(),
                            outputSha256=hashlib.sha256(data).hexdigest(), **stats))
    args.output.mkdir(parents=True, exist_ok=True)
    for path, data in pending:
        path.write_bytes(data)
    (args.output / 'manifest.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
