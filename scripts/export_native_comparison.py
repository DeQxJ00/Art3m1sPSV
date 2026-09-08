"""Export selected native functions via read-only IDA MCP; no IDB edits."""
import concurrent.futures,json,re
from audit_native_instances import OUT,call

def export(port):
    data=json.loads((OUT/f'inventory-{port}.json').read_text(encoding='utf-8'))
    targets=set()
    for api in ['sceGxmInitialize','sceGxmCreateRenderTarget','sceGxmBeginScene','sceGxmEndScene',
        'sceGxmFinish','sceGxmDisplayQueueAddEntry','sceGxmDraw','sceDisplayWaitSetFrameBuf',
        'scePowerSetConfigurationMode']:
        for ref in data['apis'].get(api,{}).get('refs',[]):
            if ref['function']:targets.add(int(ref['function'],16))
    folder=OUT/str(port);folder.mkdir(exist_ok=True)
    code='output='+repr(str(folder/'functions.json'))+'\ntargets='+repr(sorted(targets))+'\n'+r'''
import json,idautils,idc,ida_funcs,ida_bytes,ida_hexrays,hashlib
rows=[]
for ea in targets:
    f=ida_funcs.get_func(ea)
    if not f: continue
    items=list(idautils.FuncItems(ea))
    mnemonics='|'.join(idc.print_insn_mnem(a) for a in items)
    row={'address':hex(ea),'size':f.end_ea-f.start_ea,'instructions':len(items),
        'mnemonic_sha256':hashlib.sha256(mnemonics.encode()).hexdigest(),
        'asm':'\n'.join(f'{a:08x} {idc.generate_disasm_line(a,0)}' for a in items)}
    try:row['c']=str(ida_hexrays.decompile(ea))
    except Exception as e:row['error']=str(e)
    rows.append(row)
from pathlib import Path
Path(output).write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print(len(rows))
'''
    raw=call(port,'py_eval',{'code':'exec('+repr(code)+', {})'})
    if raw.get('stderr'):raise RuntimeError(raw['stderr'])
    rows=json.loads((folder/'functions.json').read_text(encoding='utf-8'))
    names={address[2:].upper():name for name,address in data['imports'].items()}
    for row in rows:
        ea=row['address'][2:]
        for ext in ['asm','c']:
            if ext not in row:continue
            text=row[ext]
            # Annotate the exported text only; IDA symbols remain untouched.
            for address,name in names.items():
                text=re.sub(r'\bsub_'+address+r'\b',name,text,flags=re.IGNORECASE)
            (folder/f'{ea}.{ext}').write_text(text,encoding='utf-8')
    (folder/'functions.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'port':port,'functions':len(rows),'errors':[r for r in rows if 'error' in r]}
if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        for result in pool.map(export,range(13337,13342)):print(result)
