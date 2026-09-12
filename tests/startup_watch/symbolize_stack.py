"""Label raw stack words; this is NOT an unwound backtrace.

Use the matching deployed ELF and observer_code from startup-watch.log to
account for the executable's relocation. Stack words may be stale addresses,
function pointers or coincidental integers, so retain offsets and raw values.
"""
import argparse
import bisect
import json
from pathlib import Path
import struct
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('elf', type=Path)
p.add_argument('stack', type=Path)
p.add_argument('--nm', required=True)
p.add_argument('--observer', required=True, type=lambda x: int(x, 0))
p.add_argument('--stack-base', required=True, type=lambda x: int(x, 0))
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
rows = subprocess.check_output([a.nm, '-n', '-C', str(a.elf)], text=True, errors='replace')
symbols = []
for row in rows.splitlines():
    fields = row.split(' ', 2)
    if len(fields) == 3 and fields[1] in ('t', 'T', 'w', 'W'):
        try:
            symbols.append((int(fields[0], 16), fields[2]))
        except ValueError:
            pass
symbols.sort()
observers = [pc for pc, name in symbols if name.startswith('startupwatch::run(')]
if len(observers) != 1:
    raise ValueError('Need exactly one startupwatch::run symbol in the matching ELF')
slide = (a.observer & ~1) - observers[0]
addresses = [pc for pc, _ in symbols]
data = a.stack.read_bytes()
hits = []
for offset in range(0, len(data) - 3, 4):
    raw = struct.unpack_from('<I', data, offset)[0]
    pc = (raw & ~1) - slide
    index = bisect.bisect_right(addresses, pc) - 1
    if not raw & 1 or index < 0 or index == len(addresses) - 1:
        continue
    hits.append(dict(stack=hex(a.stack_base + offset), offset=offset, raw=hex(raw),
                     elf=hex(pc), symbol=symbols[index][1], plus=hex(pc-addresses[index])))
result = dict(kind='raw-stack-word-candidates-not-backtrace', elf=str(a.elf),
              stack=str(a.stack), slide=hex(slide), hits=hits)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(f'{len(hits)} candidates, slide={slide:#x}, output={a.output}')
