"""Small standalone registration/draw fixture. Never writes into a real game."""
from pathlib import Path
import shutil,json,struct,zlib,subprocess
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/external-shaders/game'
(out/'system/shader/pc').mkdir(parents=True,exist_ok=True)
(out/'assets').mkdir(exist_ok=True)
for p in ['system.ini','platform.txt']:shutil.copyfile(ROOT/'build/toshiue-click-test/game'/p,out/p)
def wsl(p):
    p=p.resolve().as_posix();return '/mnt/'+p[0].lower()+p[2:]
subprocess.run(['wsl','-d','Ubuntu-24.04','--','python3','-m','fontTools.subset',
    wsl(ROOT/'build/native-command-port/originals/otomeriron/sourcehansans-bold.otf'),
    '--unicodes=U+0020-007E','--output-file='+wsl(out/'assets/probe.otf')],check=True)
(out/'title.txt').write_text('TEST SHADERS 31\n')
def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
raw=b''.join(b'\0'+bytes(v for x in range(640)for v in (int(x*255/639),int(y*255/319),224 if (x//24+y//24)%2 else 32,255))for y in range(320))
(out/'assets/grid.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',640,320,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
manifest=json.loads((ROOT/'shaders/psv/manifest.json').read_text())
lines=['*top','[debug mode=1 level=3]','[fontdefault face="assets/probe.otf" size=24 color=ffffff show=none]','[lyc id=0 width=960 height=544 color=182334]']
# Register all 51 source files, retaining the shared 20 under the same IDs.
for game in ('otomeriron','toshiue'):
    for p in sorted((ROOT/f'build/external-shaders/resources/{game}/system/shader/pc').glob('*.hlsl')):
        dest=out/'system/shader/pc'/p.name;shutil.copyfile(p,dest)
        lines+=[f'[lyshader id={p.stem} file="system/shader/pc/{p.name}"]']
lines+=['[debugprint data="BUNDLED-FIXTURE 51 registrations complete"]']
for name,args in [('gray',''),('nega',''),('sepia','red=1 green=0.8 blue=0.6'),('blur_k','offset=1 size=0.03'),('raster','angle=30 inter=1440 size=0.1'),('trapezoid_up','centerx=0.5 centery=0.5 steps=0.6 dost=0.01')]:
    keys=','.join(p.split('=')[0]for p in args.split())
    lines += ['[lydel id=1]','[lyc id=1 file="assets/grid.png"]',f'[lyprop id=1 left=160 top=110 shader={name} shaderconstant="{keys}" {args}]',
        '[chgmsg id=label stack=0]','[font left=40 top=60 width=880 height=40]','[rp backlog=0]',f'[print data="BUNDLED SHADER: {name}"]',
        '[trans time=0]','[wait time=1200 input=0]', '[takess]',f'[savess file="bundle-{name}.png" width=960 height=544]',f'[debugprint data="BUNDLED-FIXTURE rendered {name}"]']
lines+=['[debugprint data="BUNDLED-FIXTURE DONE"]','[@]']
(out/'system/first.iet').write_text('\n'.join(lines)+'\n',encoding='utf8')
print(out)
cache=out.parent/'cache-game'
(cache/'system/shader/pc').mkdir(parents=True,exist_ok=True);(cache/'assets').mkdir(exist_ok=True)
for name in ['system.ini','platform.txt','assets/grid.png','assets/probe.otf']:shutil.copyfile(out/name,cache/name)
(cache/'title.txt').write_text('TEST SHADER CACHE\n')
(cache/'system/shader/pc/gray_runtime.hlsl').write_bytes((out/'system/shader/pc/gray.hlsl').read_bytes()+b'\n// Runtime cache validation variant.\n')
(cache/'system/first.iet').write_text('\n'.join([
    '*top','[debug mode=1 level=3]','[fontdefault face="assets/probe.otf" size=24 color=ffffff show=none]',
    '[lyshader id=gray_runtime file="system/shader/pc/gray_runtime.hlsl"]',
    '[lyc id=0 width=960 height=544 color=182334]','[lyc id=1 file="assets/grid.png"]',
    '[lyprop id=1 left=160 top=110 shader=gray_runtime]',
    '[chgmsg id=label stack=0]','[font left=40 top=60 width=880 height=40]','[print data="EXTERNAL CG CACHE: GRAYSCALE"]',
    '[trans time=0]','[wait time=1200 input=0]','[takess]','[savess file="cache-gray.png" width=960 height=544]',
    '[debugprint data="EXTERNAL-CACHE-FIXTURE DONE"]','[@]'])+'\n',encoding='utf8')
