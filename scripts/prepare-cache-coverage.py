"""Prepare a diverse external PNG benchmark corpus from user-supplied PFS files.

Archive precedence is full relative path, with higher numeric suffix winning.
Copyrighted inputs and provenance stay in the requested local output directory.
"""
import argparse
import ctypes as C
import hashlib
import io
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from PIL import Image, ImageChops

CATEGORIES = ['background', 'scene_art', 'character_body', 'portrait_fragment', 'sparse_layer', 'interface', 'grayscale']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',type=Path,action='append',required=True)
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--exclude',type=Path)
    p.add_argument('--per-category',type=int,default=8)
    a=p.parse_args()
    if a.output.exists() and any(a.output.iterdir()):p.error('output must be empty')
    dll=C.CDLL(str(a.library.resolve()))
    def api(name,args,result):
        f=getattr(dll,name);f.argtypes=args;f.restype=result;return f
    op=api('pfs_open_single',[C.c_char_p,C.c_char_p],C.c_void_p)
    count=api('pfs_entry_count',[C.c_void_p],C.c_int)
    path=api('pfs_entry_path',[C.c_void_p,C.c_int,C.c_void_p,C.c_int],C.c_int)
    size=api('pfs_file_size',[C.c_void_p,C.c_char_p],C.c_int)
    read=api('pfs_read',[C.c_void_p,C.c_char_p,C.c_uint64,C.c_void_p,C.c_uint32],C.c_int)
    close=api('pfs_close',[C.c_void_p],None)
    excluded=set()
    if a.exclude:
        excluded={r['sha256'] for r in json.loads(a.exclude.read_text())['samples']}
    a.output.mkdir(parents=True,exist_ok=True)
    candidates=[];inventory=[];handles=[];skipped=[]
    def fetch(h,name,n):
        b=C.create_string_buffer(n)
        if read(h,name.encode(),0,b,n)!=n:raise RuntimeError('read failed: '+name)
        return b.raw
    try:
        for game_id,game in enumerate(a.game):
            archives=sorted((f for f in game.iterdir() if f.is_file() and re.search(r'\.pfs(?:\.\d+)?$',f.name,re.I)),
                            key=lambda f:(re.sub(r'\.\d+$','',f.name).lower(),int(f.suffix[1:]) if f.suffix[1:].isdigit() else -1))
            index={}
            for archive in archives:
                h=op(str(archive.resolve()).encode(),b'auto')
                if not h:raise RuntimeError('cannot open '+str(archive))
                handles.append(h)
                for i in range(count(h)):
                    b=C.create_string_buffer(4096)
                    if path(h,i,b,len(b))<0:raise RuntimeError('entry path')
                    name=b.value.decode().replace('\\','/');index[name.lower()]=(h,name,archive)
            rough=defaultdict(list)
            for key,(h,name,archive) in index.items():
                if not key.endswith('.png'):continue
                n=size(h,name.encode())
                if n<33 or n>16*1024*1024:continue
                header=fetch(h,name,33)
                if not header.startswith(b'\x89PNG\r\n\x1a\n'):continue
                w=int.from_bytes(header[16:20],'big');height=int.from_bytes(header[20:24],'big')
                if not (w and height and max(w,height)<=4096 and 256<=w*height<=4*1024*1024):continue
                typ=header[25]
                parts=set(key.split('/'))
                category=('grayscale' if typ in (0,4) else 'fg' if 'fg' in parts else
                          'background' if 'bg' in parts else 'scene_art' if parts & {'ev','cg','event'} else 'interface')
                rough[(category,typ)].append((w*height,h,name,archive,n,w,height))
            inventory.append({'game':str(game),'effective_pngs':sum(map(len,rough.values())),
                              'buckets':{str(k):len(v) for k,v in rough.items()}})
            for (category,typ),items in rough.items():
                items.sort(key=lambda r:(r[0],hashlib.sha256(r[2].encode()).hexdigest()))
                # Area strata plus deterministic name sampling retain small UI and large portraits.
                picks={round(i*(len(items)-1)/min(39,len(items)-1)) for i in range(min(40,len(items)))} if len(items)>1 else {0}
                picks.update(sorted(range(len(items)),key=lambda i:hashlib.sha256(items[i][2].encode()).digest())[:24])
                for i in sorted(picks):
                    _,h,name,archive,n,w,height=items[i]
                    raw=fetch(h,name,n);digest=hashlib.sha256(raw).hexdigest()
                    if digest in excluded:continue
                    try:
                        with Image.open(io.BytesIO(raw)) as im:
                            mode=im.mode;rgba=im.convert('RGBA');red,green,blue,alpha=rgba.split();hist=alpha.histogram()
                            gray=not ImageChops.difference(red,green).getbbox() and not ImageChops.difference(red,blue).getbbox()
                            transparent=hist[0]/(w*height);graded=sum(hist[1:255])/(w*height)
                            actual='grayscale' if gray else ('sparse_layer' if transparent>=.95 else 'character_body' if w*height>=65536 and height>=256 else 'portrait_fragment') if category=='fg' else category
                    except (OSError,ValueError) as error:
                        skipped.append({"source":name,"archive":str(archive),"error":str(error)})
                        continue
                    candidates.append(dict(category=actual,game_id=game_id,source=name,archive=str(archive),png_bytes=n,
                                           width=w,height=height,mode=mode,png_color_type=typ,transparent_fraction=transparent,
                                           partial_fraction=graded,sha256=digest,handle=h))
            print('indexed',game.name,inventory[-1]['effective_pngs'],'PNG;',len(candidates),'inspected',flush=True)
        selected=[];seen=set()
        for category in CATEGORIES:
            pool=[x for x in candidates if x['category']==category]
            chosen=[]
            while pool and len(chosen)<a.per_category:
                games=Counter(x['game_id'] for x in chosen);modes=Counter(x['mode'] for x in chosen)
                # Alternate the smallest and largest remaining candidates while spreading sources/modes.
                large=len(chosen)%2==1
                pool.sort(key=lambda x:(games[x['game_id']],modes[x['mode']],(-1 if large else 1)*x['width']*x['height'],x['sha256']))
                x=pool.pop(0)
                if x['sha256'] in seen:continue
                seen.add(x['sha256']);chosen.append(x)
            selected.extend(chosen)
        rows=[]
        for i,x in enumerate(selected):
            sample=f'coverage_{i:03d}';raw=fetch(x.pop('handle'),x['source'],x['png_bytes'])
            (a.output/(sample+'.png')).write_bytes(raw);x['sample']=sample;x['scene']=i;x['role']=0
            rows.append(f"{i} 0 {sample} {x['width']} {x['height']}")
        (a.output/'cache-study.scene').write_text(str(len(rows))+'\n'+'\n'.join(rows)+'\n')
        (a.output/'cache-study.profile').write_text('coverage-v2-lzw\n')
        (a.output/'title.txt').write_text('CPU Cache Coverage Benchmark\n')
        (a.output/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nBOOT=first.iet\n')
        (a.output/'first.iet').write_text('[wait time=3600000 input=0]\n')
        identity=hashlib.sha256()
        for f in sorted(a.output.iterdir()):
            if f.suffix in ('.png','.scene','.profile'):
                identity.update(f.name.encode());identity.update(hashlib.sha256(f.read_bytes()).digest())
        (a.output/'cache-study.key').write_text('rgba-coverage-v2-lzw:'+identity.hexdigest()+'\n')
        (a.output/'sources.json').write_text(json.dumps({'inventory':inventory,'skipped':skipped,'samples':selected},indent=2),encoding='utf-8')
        print('selected',dict(Counter(x['category'] for x in selected)),flush=True)
    finally:
        for h in handles:close(h)


if __name__=='__main__':main()
