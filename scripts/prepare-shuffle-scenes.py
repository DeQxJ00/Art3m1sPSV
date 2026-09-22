"""Audit all effective Steam AST scripts against the effective user PSV copy."""
import hashlib
import json
import math
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
number = re.compile(r'-?\d+(?:\.\d+)?')
shape = lambda s: re.sub(r'\s+', '', number.sub('#', s))

def effective(archives):
    result = {}
    for archive in archives:
        for path in archive.rglob('*.ast'):
            result[path.relative_to(archive).as_posix()] = path
    return result

work = root / 'temp/conversion-work/shuffle-steam'
source = effective(sorted(p for p in work.iterdir() if p.is_dir() and re.fullmatch(r'root\.pfs(?:\.\d{3})?', p.name)))
reference_archives = [root / 'temp/extracted' / name for name in (
    'shuffle-psv-reference', 'shuffle-psv-reference-010', 'shuffle-psv-reference-011')]
report = []
pending = []
for relative, path in sorted(source.items()):
    record = dict(path=relative, source=str(path))
    candidates = [archive / relative for archive in reversed(reference_archives) if (archive / relative).exists()]
    if not candidates:
        record['status'] = 'missing-reference'
        report.append(record)
        continue
    a = path.read_bytes().decode('utf-8-sig')
    matching = [(ref, ref.read_bytes().decode('utf-8-sig')) for ref in candidates]
    matching = [(ref, text) for ref, text in matching if shape(a) == shape(text)]
    if not matching:
        record['status'] = 'content-diff-review-required'
        report.append(record)
        continue
    ref, b = matching[0]
    record['reference'] = str(ref)
    if ref != candidates[0]:
        record['differentContentReferencePreserved'] = str(candidates[0])
    old, new = number.findall(a), number.findall(b)
    if len(old) != len(new):
        raise ValueError('Numeric field mismatch: ' + relative)
    changes = [(i, x, y) for i, (x, y) in enumerate(zip(old, new)) if x != y]
    unexpected = [(i, x, y) for i, x, y in changes if float(y) != math.trunc(float(x) * .75)]
    if unexpected:
        record.update(status='non-resize-diff-review-required', unexpected=unexpected)
        report.append(record)
        continue
    values = iter(new)
    converted = number.sub(lambda m: next(values), a)
    assert number.sub('#', converted) == number.sub('#', a)
    assert number.findall(converted) == new
    data = converted.encode('utf-8')
    record.update(status='converted' if changes else 'unchanged', changes=len(changes),
                  sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  referenceSha256=hashlib.sha256(ref.read_bytes()).hexdigest(),
                  outputSha256=hashlib.sha256(data).hexdigest())
    if changes:
        pending.append((root / 'temp/converted/shuffle-steam' / relative, data))
    report.append(record)
for target, data in pending:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
output = root / 'temp/converted/shuffle-steam/scene-conversion-manifest.json'
output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
counts = {status: sum(r['status'] == status for r in report) for status in sorted({r['status'] for r in report})}
print(json.dumps(counts))
print('Changed fields:', sum(r.get('changes', 0) for r in report))
for entry in report:
    if 'review' in entry['status'] or entry['status'] == 'missing-reference':
        print(entry['status'], entry['path'])
