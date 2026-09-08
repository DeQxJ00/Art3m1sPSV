"""Read-only cache/offscreen call-site audit on a workspace copy of the IDB."""
import json
from pathlib import Path
import ida_auto
import ida_funcs
import ida_hexrays
import ida_pro
import idautils
import idc

out = Path(__file__).resolve().parents[1] / "build/recording-review/native-cache"
out.mkdir(parents=True, exist_ok=True)
ida_auto.auto_wait()
ida_hexrays.init_hexrays_plugin()
seeds = [0x810328D8, 0x81031EF4, 0x8102F718, 0x81031FC8]
records = []
targets = set(seeds)
for address in seeds:
    callers = []
    for ref in idautils.XrefsTo(address):
        fn = ida_funcs.get_func(ref.frm)
        if fn:
            callers.append({"site": hex(ref.frm), "function": hex(fn.start_ea)})
            targets.add(fn.start_ea)
    records.append({"address": hex(address), "callers": callers})
for address in sorted(targets)[:30]:
    try:
        text = str(ida_hexrays.decompile(address))
        (out / f"{address:08x}.c").write_text(text, encoding="utf-8")
    except Exception as error:
        records.append({"address": hex(address), "error": str(error)})
(out / "callers.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
ida_pro.qexit(0)
