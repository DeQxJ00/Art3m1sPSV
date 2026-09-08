"""Resize physical UI/font coordinates; retain mulpos-based scene coordinates."""
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('ui_conversion', Path(__file__).with_name('convert-ui-tables.py'))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)
archives = sorted(p for p in (root / 'build/conversion-work/otomeriron').iterdir()
                  if p.is_dir() and re.fullmatch(r'otomeriron\.pfs(?:\.\d{3})?', p.name))
effective = {}
for archive in archives:
    for path in (archive / 'system/table').glob('list_windows*.tbl'):
        effective[path.name] = path

font_number = re.compile(r'(?<=[,{])(\s*)(size|left|top|width|height|spacetop|spacemiddle|spacebottom|kerning|rubysize)\s*=\s*(-?\d+(?:\.\d+)?)(?=\s*[,}])')
reports = []
pending = []
for name, source in sorted(effective.items()):
    raw = source.read_bytes()
    original = raw.decode('utf-8-sig')
    has_ui = re.search(r'^\s*ui_\w+\s*=\s*\{', original, re.M)
    text, rows = ui.convert(original, .5) if has_ui else (original, 0)
    fonts = 0
    lines = []
    for line in text.splitlines(keepends=True):
        if re.search(r'\bface\s*=\s*"', line):
            line, count = font_number.subn(lambda m: f'{m[1]}{m[2]}={math.trunc(float(m[3])*.5)}', line)
            if count:
                fonts += 1
        # Physical source canvas is 1920x1080. game_scale=1280x720 is
        # intentionally retained: mulpos(640) must become the new center 480.
        line = re.sub(r'\bgame_width\s*=\s*1920\b', 'game_width=960', line)
        line = re.sub(r'\bgame_height\s*=\s*1080\b', 'game_height=540', line)
        lines.append(line)
    result = ''.join(lines)
    if name != 'list_windows.tbl' and not rows:
        raise ValueError(f'No UI rows in language table {name}')
    target = root / 'build/converted/otomeriron/system/table' / name
    data = result.encode('utf-8')
    pending.append((target, data))
    reports.append(dict(table=name, source=str(source), uiRows=rows, fontRows=fonts,
                        sourceSha256=hashlib.sha256(raw).hexdigest(), outputSha256=hashlib.sha256(data).hexdigest()))
if not pending:
    raise ValueError('No tables found')
for target, data in pending:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
(root / 'build/converted/otomeriron/layout-manifest.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
for report in reports:
    print(report['table'], 'UI', report['uiRows'], 'fonts', report['fontRows'], report['source'])
