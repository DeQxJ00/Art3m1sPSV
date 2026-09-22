"""Export bounded local renderer evidence from a copied IDA database."""
import json
from pathlib import Path
import ida_auto, ida_bytes, ida_funcs, ida_hexrays, ida_pro, idautils, idc
out = Path(__file__).resolve().parents[1] / 'temp/renderer-comparison-20260908'
out.mkdir(parents=True, exist_ok=True)
ida_auto.auto_wait()
ida_hexrays.init_hexrays_plugin()
targets = {0x81031568, 0x81031234, 0x81031FC8, 0x81003AFC, 0x81003E2C,
           0x81014C00, 0x81216EB4, 0x81215C10, 0x81215F4C, 0x810328D8,
           0x8102F718, 0x81031CB8, 0x81031D64, 0x81031EF4, 0x8102FFB4}
names = list(idautils.Names())
report = {'input': idc.get_input_file_path(), 'api_xrefs': {}, 'tables': {}, 'strings': [], 'xrefs': {}}
apis = ['sceGxmInitialize', 'sceGxmTransferCopy', 'sceGxmTransferDownscale',
        'sceGxmSetFragmentTexture', 'sceGxmBeginScene', 'sceGxmEndScene',
        'sceGxmFinish', 'sceDisplaySetFrameBuf', 'sceKernelCreateThread']
for ea, name in names:
    if name in apis:
        refs = []
        for x in idautils.XrefsTo(ea):
            f = ida_funcs.get_func(x.frm)
            refs.append({'at': hex(x.frm), 'function': hex(f.start_ea) if f else None})
            if f and name != 'sceKernelCreateThread': targets.add(f.start_ea)
        report['api_xrefs'][name] = refs
tables = [(0x813A97C4, 20), (0x813A4388, 8)]
for rtti in [0x813A64A0, 0x813A64AC, 0x813A7AA4, 0x813A4474]:
    for x in idautils.XrefsTo(rtti):
        if ida_funcs.get_func(ida_bytes.get_dword(x.frm+4) & ~1):
            tables.append((x.frm+4, 48))
for table, count in tables:
    rows=[]
    for i in range(count):
        target = ida_bytes.get_dword(table + i*4)
        f=ida_funcs.get_func(target & ~1)
        if not f: break
        rows.append({'offset': i*4, 'value': hex(target), 'name': idc.get_name(target & ~1)})
        if f: targets.add(f.start_ea)
    report['tables'][hex(table)] = rows
for s in idautils.Strings():
    text=str(s)
    if any(k.lower() in text.lower() for k in ['fontcachesize', 'font_cache_size', 'imagecachesize',
              'CFontRenderer', 'CGpuSurface', 'CGpuDraw', 'CText', 'CImageCache', 'CSurfaceCache']):
        refs=[]
        for x in idautils.XrefsTo(s.ea):
            f=ida_funcs.get_func(x.frm)
            refs.append({'at':hex(x.frm), 'function':hex(f.start_ea) if f else None})
            if f: targets.add(f.start_ea)
        report['strings'].append({'at':hex(s.ea), 'text':text, 'refs':refs})
# Data references expose virtual methods/constructors which call-only graphs miss.
for ea in sorted(targets):
    report['xrefs'][hex(ea)] = [{'at':hex(x.frm), 'type':x.type,
       'function':hex(ida_funcs.get_func(x.frm).start_ea) if ida_funcs.get_func(x.frm) else None}
       for x in idautils.XrefsTo(ea)]
    if (out/f'{ea:08x}.c').exists(): continue
    (out/f'{ea:08x}.asm').write_text('\n'.join(f'{a:08x} {idc.generate_disasm_line(a,0)}'
        for a in idautils.FuncItems(ea)), encoding='utf-8')
    try: (out/f'{ea:08x}.c').write_text(str(ida_hexrays.decompile(ea)), encoding='utf-8')
    except Exception as e: report.setdefault('errors',[]).append([hex(ea),str(e)])
(out/'evidence.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
ida_pro.qexit(0)
