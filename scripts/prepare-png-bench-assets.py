"""Generate synthetic PNG comparison cases outside installed game directories.
Run the Rust extract utility first for background.png and portrait.png.
"""
from pathlib import Path
import struct,zlib,shutil,hashlib,json
root=Path(__file__).resolve().parents[1];out=root/'build/png-bench-assets';out.mkdir(exist_ok=True)
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
def save(name,w,h,depth,color,raw,extra=b''):
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,depth,color,0,0,0))+extra+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    (out/name).write_bytes(data)
for name in ['pal8-opaque.png','pal8-trns.png']:
    shutil.copy2(root/'core/src/image_decode_testdata'/name,out/name)
for name,w,h,depth in [('rgba32.png',641,257,8),('rgba16.png',67,39,16)]:
    rows=[]
    for y in range(h):
        values=[v for x in range(w) for v in ((x*37+y*13)%256,(y*71+x*3)%256,(255-x)%256,[0,1,127,128,254,255][(x+y)%6])]
        row=bytes(values) if depth==8 else b''.join(struct.pack('>H',(v*257+y*11)%65536) for v in values)
        rows.append(b'\0'+row)
    save(name,w,h,depth,6,b''.join(rows))
palette=bytes(v for i in range(256) for v in ((i*37)%256,(i*71+3)%256,255-i))
raw=b''.join(b'\0'+bytes(((x*3+y*7)^(x//17))%256 for x in range(960)) for y in range(540))
save('pal8-large.png',960,540,8,3,raw,chunk(b'PLTE',palette)+chunk(b'tRNS',bytes([0,1,127,128,254,255])))
manifest={}
for p in sorted(out.glob('*.png')):
    b=p.read_bytes();w,h,depth,color=struct.unpack('>IIBB',b[16:26])
    manifest[p.name]={'size':len(b),'sha256':hashlib.sha256(b).hexdigest(),'width':w,'height':h,'depth':depth,'color_type':color,'synthetic':p.name not in ['background.png','portrait.png','rgba32-game.png']}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest,indent=2))
