"""Apply the user's verified PSV layout to the matching Steam table.

Fail closed if the reference changes anything beyond the observed 0.75 resize.
Keep every nonnumeric byte of the Steam text; this does not copy game scripts.
"""
import hashlib
import json
import math
import re
import sys
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
if len(sys.argv) == 1:
    for table in ('list_windows.tbl', 'list_windows_ja.tbl', 'list_windows_cn.tbl', 'list_windows_tw.tbl', 'list_windows_en.tbl'):
        subprocess.run([sys.executable, str(Path(__file__).resolve()), table], check=True)
    sys.exit(0)
name = sys.argv[1] if len(sys.argv) > 1 else 'list_windows.tbl'
is_scene = re.fullmatch(r'[A-Za-z0-9_]+\.ast', name) is not None
if not is_scene and not re.fullmatch(r'list_windows(?:_(?:ja|cn|en|tw))?\.tbl', name):
    raise ValueError('Unsupported reference table')
relative = Path('script' if is_scene else 'system/table') / name
source = root / 'build/conversion-work/shuffle-steam/root.pfs' / relative
reference = root / 'build/extracted/shuffle-psv-reference' / relative
target = root / 'build/converted/shuffle-steam' / relative
a = source.read_bytes().decode('utf-8-sig')
b = reference.read_bytes().decode('utf-8-sig')
number = re.compile(r'-?\d+(?:\.\d+)?')
shape = lambda value: re.sub(r'\s+', '', number.sub('#', value))
if shape(a) != shape(b):
    raise ValueError('Reference changes nonnumeric structure: manual review required')
original = number.findall(a)
expected = number.findall(b)
if len(original) != len(expected):
    raise ValueError('Different numeric field count')
changes = []
for i, (old, new) in enumerate(zip(original, expected)):
    if old == new:
        continue
    if float(new) != math.trunc(float(old) * .75):
        raise ValueError(f'Unreviewed reference change #{i}: {old} -> {new}')
    changes.append(dict(index=i, before=old, after=new))
values = iter(expected)
result = number.sub(lambda match: next(values), a)
assert number.sub('#', result) == number.sub('#', a)
assert number.findall(result) == expected
target.parent.mkdir(parents=True, exist_ok=True)
data = result.encode('utf-8')
target.write_bytes(data)
report = dict(method='verified-user-psv-reference', source=str(source), reference=str(reference),
              sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              referenceSha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
              outputSha256=hashlib.sha256(data).hexdigest(), numericFields=len(original),
              changedFields=len(changes), changes=changes)
(root / 'build/converted/shuffle-steam' / (name + '.conversion-manifest.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
print(f'Applied {len(changes)} verified resize changes across {len(original)} fields: {target}')
