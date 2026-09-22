"""Read-only inventory of the five already-open native Artemis IDA databases.

No rename, type edit, patch, save, instance switch or debugger action is issued.
Each response is tied to its own endpoint and input path.
"""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request
import re

OUT = Path(__file__).resolve().parents[1] / 'temp/native-five-audit'
OUT.mkdir(parents=True, exist_ok=True)

def call(port, name, args):
    payload = json.dumps(dict(jsonrpc='2.0', id=1, method='tools/call',
        params=dict(name=name, arguments=args))).encode()
    req = urllib.request.Request(f'http://127.0.0.1:{port}/mcp', payload,
        headers={'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'})
    with urllib.request.urlopen(req, timeout=55) as response:
        result = json.load(response)
    if 'error' in result:
        raise RuntimeError(result['error'])
    result = result['result']
    if result.get('isError'):
        raise RuntimeError(result)
    return result.get('structuredContent') or result

CODE = r'''
import json, idautils, idc, ida_funcs, ida_nalt, ida_bytes
apis = ['sceGxmInitialize','sceGxmBeginScene','sceGxmEndScene','sceGxmFinish',
 'sceGxmDisplayQueueAddEntry','sceGxmCreateRenderTarget','sceGxmColorSurfaceInit',
 'sceGxmSetVertexStream','sceGxmDraw','sceGxmSetFragmentTexture','sceGxmTransferCopy',
 'sceGxmTransferDownscale','sceDisplaySetFrameBuf','sceDisplayWaitVblankStart',
 'sceDisplayWaitSetFrameBuf','sceKernelAllocMemBlock','scePowerSetConfigurationMode',
 'scePowerSetArmClockFrequency','scePowerSetGpuClockFrequency']
result={'input':idc.get_input_file_path(),'apis':{},'strings':[]}
# Resolve 0x34-byte Vita import entries without renaming anything in the IDB.
# libname +20; function count +6; NID/entry pointer arrays +28/+32.
result['imports']={}
for s in idautils.Strings():
    if str(s) not in ['SceGxm','SceDisplay','ScePower','SceSysmem','SceThreadmgr']: continue
    for x in idautils.XrefsTo(s.ea):
        base=x.frm-20
        if ida_bytes.get_word(base)!=0x34: continue
        count=ida_bytes.get_word(base+6)
        nids=ida_bytes.get_dword(base+28);entries=ida_bytes.get_dword(base+32)
        if not 0<count<2048: continue
        for i in range(count):
            nid=ida_bytes.get_dword(nids+i*4)
            name=NID_NAMES.get(str(nid))
            if not name: continue
            addr=ida_bytes.get_dword(entries+i*4)&~1
            result['imports'][name]=hex(addr)
            if name not in apis: continue
            refs=[]
            for ref in idautils.XrefsTo(addr):
                f=ida_funcs.get_func(ref.frm)
                refs.append({'at':hex(ref.frm),'function':hex(f.start_ea) if f else None})
            result['apis'][name]={'at':hex(addr),'nid':hex(nid),'library':str(s),'refs':refs}
for ea,name in idautils.Names():
    if name not in apis: continue
    refs=[]
    for x in idautils.XrefsTo(ea):
        f=ida_funcs.get_func(x.frm)
        refs.append({'at':hex(x.frm),'function':hex(f.start_ea) if f else None})
    result['apis'][name]={'at':hex(ea),'refs':refs}
for s in idautils.Strings():
    t=str(s)
    if any(k in t.lower() for k in ['fontcachesize','font_cache_size','imagecachesize',
        'cfont','cgpusurface','cgpudraw','conech','coneline','coneblock','ctextlayer','csurfacecache','cimagecache']):
        refs=[]
        for x in idautils.XrefsTo(s.ea):
            f=ida_funcs.get_func(x.frm)
            refs.append({'at':hex(x.frm),'function':hex(f.start_ea) if f else None})
        result['strings'].append({'at':hex(s.ea),'text':t,'refs':refs})
print(json.dumps(result))
'''

def inventory(port):
    health = call(port, 'server_health', {})
    db=Path('F:/WorkSpaceAI2/art3m1s-psv/.tools/vitasdk/sdk-2026.08/share/vita-headers/db/360')
    nid_names={str(int(nid,16)):name for path in db.glob('*.yml')
        for name,nid in re.findall(r'^\s+(sce\w+):\s+(0x[0-9A-Fa-f]+)',path.read_text(encoding='utf-8'),re.MULTILINE)}
    code='NID_NAMES='+repr(nid_names)+'\n'+CODE
    raw = call(port, 'py_eval', {'code': 'exec(' + repr(code) + ', {})'})
    (OUT / f'inventory-raw-{port}.json').write_text(json.dumps(raw, ensure_ascii=False, indent=2), encoding='utf-8')
    if raw.get('stderr'): raise RuntimeError(raw['stderr'])
    evidence = json.loads(raw['stdout'])
    evidence['health'] = health
    path = Path(health['input_path'])
    evidence['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
    (OUT / f'inventory-{port}.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    return dict(port=port, input=health['input_path'], sha256=evidence['sha256'],
        apis={name:len(value['refs']) for name,value in evidence['apis'].items()},
        strings=[s['text'] for s in evidence['strings']])

if __name__ == '__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futures = {pool.submit(inventory, port):port for port in range(13337,13342)}
        rows=[]
        for future in concurrent.futures.as_completed(futures):
            try: rows.append(future.result())
            except Exception as error: rows.append(dict(port=futures[future], error=str(error)))
        rows.sort(key=lambda row:row['port'])
        (OUT/'summary.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(rows,ensure_ascii=False,indent=2))
