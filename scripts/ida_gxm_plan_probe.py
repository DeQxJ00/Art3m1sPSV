"""Read selected GXM reference functions from a copied IDA database."""
import json
import traceback
from pathlib import Path

import ida_auto
import ida_funcs
import ida_hexrays
import ida_ida
import ida_pro
import idautils
import idc

output = Path(__file__).resolve().parents[1] / "research" / "ida-gxm"
output.mkdir(parents=True, exist_ok=True)
targets = [0x81031568, 0x8102F718, 0x81031CB8, 0x81031D64,
           0x81031EF4, 0x810328D8, 0x81033108]
try:
    ida_auto.auto_wait()
    ready = ida_hexrays.init_hexrays_plugin()
    result = {"input": idc.get_input_file_path(), "is_32bit": ida_ida.inf_is_32bit_exactly(),
              "hexrays": ready, "functions": []}
    for address in targets:
        function = ida_funcs.get_func(address)
        if not function:
            result["functions"].append({"address": hex(address), "error": "not found"})
            continue
        lines = [(hex(ea), idc.generate_disasm_line(ea, 0)) for ea in idautils.FuncItems(address)]
        calls = [line for line in lines if "sceGxm" in line[1] or "sceDisplay" in line[1]]
        result["functions"].append({"address": hex(address), "name": idc.get_func_name(address), "calls": calls})
        (output / f"{address:08x}.asm").write_text("\n".join(f"{ea} {line}" for ea, line in lines), encoding="utf-8")
        if ready:
            try:
                (output / f"{address:08x}.c").write_text(str(ida_hexrays.decompile(address)), encoding="utf-8")
            except Exception as error:
                result["functions"][-1]["decompile_error"] = str(error)
    (output / "probe.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
except Exception:
    (output / "error.txt").write_text(traceback.format_exc(), encoding="utf-8")
ida_pro.qexit(0)
