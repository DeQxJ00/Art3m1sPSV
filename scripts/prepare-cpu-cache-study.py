"""Extract three layered character demos from user-owned PFS archives.

Only this harness is tracked; extracted PNGs/manifests belong in temp/.
Requires Pillow and a host pfs_upk library built from core/crates/pfs-upk-rust.
"""
import argparse
import ctypes as C
import hashlib
import io
import json
from pathlib import Path
import re
from PIL import Image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('game', type=Path)
    p.add_argument('--library', type=Path, required=True)
    p.add_argument('--group', action='append', required=True,
                   help='FG directory stem; repeat exactly three times')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if len(a.group) != 3 or (a.output.exists() and any(a.output.iterdir())):
        p.error('need three groups and an empty output directory')
    dll = C.CDLL(str(a.library.resolve()))
    def api(name, args, result):
        f = getattr(dll, name); f.argtypes = args; f.restype = result; return f
    op = api('pfs_open_single', [C.c_char_p, C.c_char_p], C.c_void_p)
    count = api('pfs_entry_count', [C.c_void_p], C.c_int)
    path = api('pfs_entry_path', [C.c_void_p,C.c_int,C.c_void_p,C.c_int], C.c_int)
    size = api('pfs_file_size', [C.c_void_p,C.c_char_p], C.c_int)
    read = api('pfs_read', [C.c_void_p,C.c_char_p,C.c_uint64,C.c_void_p,C.c_uint32], C.c_int)
    close = api('pfs_close', [C.c_void_p], None)
    archives = [f for f in a.game.iterdir() if f.is_file() and re.search(r'\.pfs(?:\.\d+)?$',f.name,re.I)]
    archives.sort(key=lambda f: (re.sub(r'\.\d+$','',f.name).lower(),
                                int(f.suffix[1:]) if f.suffix[1:].isdigit() else -1))
    handles = []; index = {}
    try:
        for archive in archives:
            h=op(str(archive.resolve()).encode(),b'auto')
            if not h: raise RuntimeError(f'Cannot open {archive}')
            handles.append(h)
            for i in range(count(h)):
                b=C.create_string_buffer(4096)
                if path(h,i,b,len(b)) < 0: raise RuntimeError('entry path')
                name=b.value.decode().replace('\\','/'); index[name.lower()]=(h,name,archive)
        a.output.mkdir(parents=True,exist_ok=True)
        rows=[]; records=[]
        for scene,group in enumerate(a.group):
            prefix=f'image/fg/{group}/{group}_'.lower()
            names=sorted(set(str(Path(n).with_suffix('.png')).replace('\\','/') for n in index
                             if n.startswith(prefix) and n.endswith(('.png','.dds'))))
            bodies=[n for n in names if n[len(prefix):].startswith('000')]
            eyes=[n for n in names if n[len(prefix):].startswith('001')]
            mouths=[n for n in names if n[len(prefix):].startswith('002')]
            if bodies and eyes and len(mouths)>=2:
                chosen=[bodies[0],eyes[0],mouths[0],mouths[1]]
            elif bodies and len(eyes)>=2 and mouths:
                chosen=[bodies[0],mouths[0],eyes[0],eyes[1]]
            else: raise RuntimeError(f'Cannot form body/eyes/two expressions: {group}')
            for role,key in enumerate(chosen):
                native_key=str(Path(key).with_suffix('.dds')).replace('\\','/')
                sample=f'scene{scene}_layer{role}'
                native_info=None
                if native_key in index:
                    nh,nn,na=index[native_key];ns=size(nh,nn.encode());nb=C.create_string_buffer(ns)
                    if ns<=0 or read(nh,nn.encode(),0,nb,ns)!=ns: raise RuntimeError(nn)
                    (a.output/(sample+'.dds')).write_bytes(nb.raw)
                    native_info={'archive':str(na.resolve()),'path':nn,'bytes':ns,'sha256':hashlib.sha256(nb.raw).hexdigest()}
                if key in index:
                    h,name,archive=index[key];n=size(h,name.encode());data=C.create_string_buffer(n)
                    if n<=0 or read(h,name.encode(),0,data,n)!=n: raise RuntimeError(name)
                    raw=data.raw
                else:
                    matches=list(a.game.glob('*.unpacked/'+key))
                    if len(matches)!=1: raise RuntimeError(f'Need one original PNG reference for {key}')
                    name=key;archive=matches[0];raw=archive.read_bytes();n=len(raw)
                if not raw.startswith(b'\x89PNG\r\n\x1a\n'): raise RuntimeError(f'Not PNG: {name}')
                with Image.open(io.BytesIO(raw)) as image:
                    w,ht=image.size
                    if w*ht*4>16*1024*1024: raise RuntimeError('sample exceeds bounded decoder')
                    alpha=image.convert('RGBA').getchannel('A'); hist=alpha.histogram()
                    meta={'mode':image.mode,'size':[w,ht],'alpha_bbox':alpha.getbbox(),
                          'transparent_fraction':hist[0]/(w*ht),'partial_fraction':sum(hist[1:255])/(w*ht)}
                (a.output/(sample+'.png')).write_bytes(raw)
                rows.append(f'{scene} {role} {sample} {w} {ht}')
                records.append({'sample':sample,'scene':scene,'role':role,'source':name,
                                'archive_or_png_reference':str(archive.resolve()),'native_dds':native_info,
                                'png_bytes':n,'sha256':hashlib.sha256(raw).hexdigest(),**meta})
        (a.output/'cache-study.scene').write_text(str(len(rows))+'\n'+'\n'.join(rows)+'\n')
        (a.output/'title.txt').write_text('CPU Cache Compression Demo\n')
        (a.output/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nBOOT=first.iet\n')
        (a.output/'first.iet').write_text('[wait time=3600000 input=0]\n')
        identity=hashlib.sha256()
        for f in sorted(a.output.iterdir()):
            if f.suffix in ('.png','.dds','.scene'):
                identity.update(f.name.encode());identity.update(hashlib.sha256(f.read_bytes()).digest())
        (a.output/'cache-study.key').write_text('rgba-cache-v1:'+identity.hexdigest()+'\n')
        (a.output/'sources.json').write_text(json.dumps({'archives':[str(f.resolve()) for f in archives],
                  'priority':'higher numeric PFS suffix replaces earlier same path','samples':records},indent=2),encoding='utf-8')
        print(json.dumps(records,indent=2))
    finally:
        for h in handles: close(h)


if __name__=='__main__': main()
