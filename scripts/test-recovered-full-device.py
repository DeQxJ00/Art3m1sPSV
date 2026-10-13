"""Run the full packaged host and external three-person fixture; restore executable/preferences."""
from pathlib import Path
import sys,json,hashlib,io,time,zipfile,argparse,uuid
from ftplib import error_perm
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root.parent/'emote-psv-test/tools'))
import hardware_demo as h
parser=argparse.ArgumentParser();parser.add_argument('--run',default='run2');parser.add_argument('--reuse-models',action='store_true');parser.add_argument('--pixels-only',action='store_true');parser.add_argument('--lifecycle',action='store_true');parser.add_argument('--phase-ms',type=int,default=60000);parser.add_argument('--transition-ms',type=int,default=0);parser.add_argument('--build',type=Path,default=root/'build/recovered-host');args=parser.parse_args()
assert 15000 <= args.phase_ms <= 60000
assert 0 <= args.transition_ms <= 1000
assert not args.pixels_only or args.transition_ms == 0
assert not args.pixels_only or not args.lifecycle
fixture=root/'build/recovered-full-device/game'
assert fixture.is_dir(), 'Prepare the licensed local PSB fixture folder first'
run_token=uuid.uuid4().hex
source=(root/'tests/emote_recovery/three-person.iet').read_text(encoding='utf-8')
source=source.replace('[wait time=60000 input=0]',f'[wait time={args.phase_ms} input=0]')
source=source.replace('[trans time=0]',f'[trans time={args.transition_ms}]')
if args.lifecycle:
 cycle=(root/'tests/emote_recovery/lifecycle.iet').read_text(encoding='utf-8')
 extra=''.join(f'[debugprint data="RECOVERED-LIFECYCLE round={i+1} BEGIN"]\n'+cycle+f'\n[debugprint data="RECOVERED-LIFECYCLE round={i+1} END"]\n' for i in range(3))
 extra+='[takess]\n[savess file="lifecycle-three.png" width=960 height=544]\n[debugprint data="RECOVERED-LIFECYCLE END DONE"]\n'
 source=source.replace('END DONE','END').replace('[wait time=3600000 input=0]',extra+'[wait time=3600000 input=0]')
if args.pixels_only:
 assert 'DIRECT_RECOVERED_FREEZE_PROBE:BOOL=ON' in (args.build/'CMakeCache.txt').read_text()
 source=source.split('[lyprop id=10.1 intermediate_render=0]',1)[0]+'\n[debugprint data="RECOVERED-PIXELS frozen=1 END DONE"]\n[wait time=3600000 input=0]\n'
else:
 assert 'DIRECT_RECOVERED_FREEZE_PROBE:BOOL=ON' not in (args.build/'CMakeCache.txt').read_text(), 'Frozen build cannot measure performance'
(fixture/'first.iet').write_text(source.replace('[debug mode=1 level=3]', '[debug mode=1 level=3]\n[debugprint data="RECOVERED-RUN '+run_token+'"]', 1),encoding='utf-8')
out=root/'build/recovered-full-device'/args.run;out.mkdir(parents=True,exist_ok=True);backup=out/'backup';backup.mkdir(parents=True,exist_ok=True)
base='ux0:/data/art3m1s-gxm/';game='TEST_RECOVERED_THREE'
prefs={'last-game.txt':(game+'\n').encode(),'cpu-clock.conf':b'5 444 444 222 222 444 222 444 222\n','debug-settings.txt':b'1 0\n','log-settings.txt':b'1 1\n','cache-hud.txt':b'1 0\n','trace-nextline.off':b'1\n'}
paths={'eboot.bin':'ux0:/app/ART3DIR01/eboot.bin',**{n:base+n for n in prefs}}
saved={};report={'status':'running','run_token':run_token,'pixels_only':args.pixels_only,'lifecycle':args.lifecycle,'phase_ms':args.phase_ms,'transition_ms':args.transition_ms,'fixture_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in fixture.iterdir() if p.is_file()}}
def put(f,path,data):
 f.storbinary('STOR '+path,io.BytesIO(data),blocksize=65536);assert h.read(f,path)==data,path
with h.ftp() as f:
 for name,path in paths.items():
  try:data=h.read(f,path)
  except error_perm as e:
   if not str(e).startswith('550'):raise
   data=None
  saved[name]=data
  if data is not None:(backup/name).write_bytes(data)
 (backup/'manifest.json').write_text(json.dumps({n:{'remote':paths[n],'sha256':hashlib.sha256(d).hexdigest() if d is not None else None} for n,d in saved.items()},indent=2))
 assert saved['eboot.bin']
try:
 h.command('kill EORC00001');h.command('kill ART3DIR01');time.sleep(1)
 with h.ftp() as f:
  folder=base+'games/'+game
  try:f.mkd(folder)
  except error_perm:pass
  for source in sorted(fixture.iterdir()):
   data=source.read_bytes();path=folder+'/'+source.name
   if args.reuse_models and source.suffix=='.psb':
    known=json.loads((fixture.parent/'fixture.json').read_text())
    assert hashlib.sha256(data).hexdigest()==known[source.name]
    f.voidcmd('TYPE I');assert f.size(path)==len(data)
    print('Reusing previously verified model',source.name,flush=True);continue
   try:existing=h.read(f,path)
   except error_perm:existing=None
   if existing!=data:put(f,path,data)
   print('Verified fixture',source.name,len(data),flush=True)
  with zipfile.ZipFile(args.build/'art3m1s_direct.vpk') as z:
   assert b'ART3DIR01' in z.read('sce_sys/param.sfo');new=z.read('eboot.bin');put(f,paths['eboot.bin'],new);report['eboot_sha256']=hashlib.sha256(new).hexdigest()
  for name,data in prefs.items():put(f,paths[name],data)
 h.command('nosleep on');h.command('launch ART3DIR01');time.sleep(7)
 h.command('press circle; wait 100ms; release circle')
 start=time.monotonic();previous='';seen=set()
 while time.monotonic()-start<320:
  time.sleep(5)
  with h.ftp() as f:log=h.read(f,base+'host.log')
  (out/'host.log').write_bytes(log);text=log.decode('utf-8',errors='replace')
  if 'RECOVERED-RUN '+run_token not in text:continue
  for line in text.splitlines():
   if ('RECOVERED-BENCH' in line or 'RECOVERED-LIFECYCLE' in line or '[game-load-failed]' in line or '[recovered-emote]' in line) and line not in seen:print(line,flush=True);seen.add(line)
  if 'recovered packet replay failed' in text or '[game-load-failed]' in text or '[core:E]' in text:raise RuntimeError('Host render/load/script failure; see saved log')
  if 'END DONE' in text:break
 else:raise RuntimeError('Full-host benchmark timed out; saved log')
 time.sleep(6)
 with h.ftp() as f:
  (out/'host.log').write_bytes(h.read(f,base+'host.log'))
  captures=['neutral-direct.png','neutral-group.png','neutral-nested.png']
  if not args.pixels_only:captures+=['three-neutral.png','three-gray-alpha.png','three-negative-alpha.png']
  if args.lifecycle:captures+=['lifecycle-three.png']
  for name in captures:(out/name).write_bytes(h.read(f,base+'saves/'+game+'/save/'+name))
 report['status']='completed_requires_analysis'
except Exception as error:
 report.update(status='failed',error=str(error));raise
finally:
 h.command('kill ART3DIR01');time.sleep(1)
 with h.ftp() as f:
  for name,data in saved.items():
   if data is not None:put(f,paths[name],data)
   else:
    try:f.delete(paths[name])
    except error_perm:pass
 report['restored']=True;(out/'device.json').write_text(json.dumps(report,indent=2));h.command('launch ART3DIR01')
