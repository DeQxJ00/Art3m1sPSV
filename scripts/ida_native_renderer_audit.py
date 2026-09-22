"""Read-only source audit in a copied SHUF00002 database; output stays in build."""
import json
from pathlib import Path
import ida_auto, ida_funcs, ida_hexrays, ida_pro, idautils, idc
out = Path(__file__).resolve().parents[1] / 'temp/native-renderer-audit'
out.mkdir(parents=True, exist_ok=True)
ida_auto.auto_wait()
ready = ida_hexrays.init_hexrays_plugin()
targets = [0x81031CB8, 0x81031D64, 0x81031EF4, 0x8102F718,
           0x810328D8, 0x81031234, 0x81003E2C, 0x81003AFC]
result = {'input': idc.get_input_file_path(), 'hexrays': ready, 'functions': []}
for ea in targets:
    callers = sorted({ida_funcs.get_func(x.frm).start_ea for x in idautils.XrefsTo(ea)
                      if ida_funcs.get_func(x.frm)})
    result['functions'].append({'address': hex(ea), 'callers': [hex(x) for x in callers]})
    for address in [ea] + callers[:12]:
        path = out / f'{address:08x}.asm'
        if path.exists():
            continue
        path.write_text('\n'.join(f'{a:08x} {idc.generate_disasm_line(a,0)}'
                                  for a in idautils.FuncItems(address)), encoding='utf-8')
        if ready:
            try:
                (out / f'{address:08x}.c').write_text(str(ida_hexrays.decompile(address)), encoding='utf-8')
            except Exception as error:
                result.setdefault('errors', []).append({'address': hex(address), 'error': str(error)})
(out / 'audit.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
ida_pro.qexit(0)
