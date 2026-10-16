"""Serial PSV probe deployment with exact pre-test eboot restoration."""
from pathlib import Path
import sys,json,hashlib,io,time,subprocess,argparse
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root.parent/'emote-psv-test/tools'))
import hardware_demo as h
parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,default=root/'build/recovered-packet-device');parser.add_argument('--title',choices=['EORC00001','ART3DIR01'],default='EORC00001');args=parser.parse_args()
build=args.build.resolve();out=build/'device';out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((build/'build-verification.json').read_text())
for name,digest in manifest['sources'].items():assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
new=(build/'eboot.bin').read_bytes();assert hashlib.sha256(new).hexdigest()==manifest['eboot_sha256']
title=args.title;app=f'ux0:/app/{title}/eboot.bin';folder='ux0:/data/art3m1s-recovered-probe/'
with h.ftp() as f:
 assert title.encode() in h.read(f,f'ux0:/app/{title}/sce_sys/param.sfo')
 old=h.read(f,app)
(out/'before-eboot.bin').write_bytes(old)
report={'status':'running','build':manifest,'device_title':title,'before_eboot_sha256':hashlib.sha256(old).hexdigest()}
try:
 h.command('kill ART3DIR01');h.command('kill EORC00001');time.sleep(1)
 with h.ftp() as f:
  try:f.delete(folder+'result.log')
  except Exception:pass
  f.storbinary('STOR '+app,io.BytesIO(new),blocksize=65536);assert h.read(f,app)==new
 h.command('launch '+title);deadline=time.monotonic()+120
 while True:
  time.sleep(2)
  with h.ftp() as f:
   try:log=h.read(f,folder+'result.log')
   except Exception:log=b''
  if b'COMPLETE' in log or b'FAIL' in log:break
  if time.monotonic()>deadline:raise RuntimeError('Probe timeout: '+log[-1200:].decode(errors='replace'))
 (out/'result.log').write_bytes(log);print('\n'.join(line for line in log.decode(errors='replace').splitlines() if line.startswith(('stage=','COMPLETE','FAIL','unsupported-opacity'))),flush=True)
 count=manifest.get('stage_count',13)
 assert f'COMPLETE ok=1 stages={count} GPU-drained=1'.encode() in log,log
 with h.ftp() as f:
  for stage in range(count):
   name=f'stage-{stage:02}.rgba';data=h.read(f,folder+name);assert len(data)==960*544*4;(out/name).write_bytes(data)
 verifier='verify-recovered-prebound.py' if manifest.get('queued_pixel_pairs') else 'verify-recovered-probe.py'
 subprocess.run([sys.executable,str(root/'scripts'/verifier),str(out)],check=True)
 evidence='prebound-pixel-verification.json' if manifest.get('queued_pixel_pairs') else 'pixel-verification.json'
 report['pixels']=json.loads((out/evidence).read_text());report['status']=report['pixels']['status'];report['execution_status']='pass'
except Exception as e:
 report.update(status='failed',error=str(e));raise
finally:
 try:
  h.command('kill '+title);time.sleep(1)
  with h.ftp() as f:
   f.storbinary('STOR '+app,io.BytesIO(old),blocksize=65536);assert h.read(f,app)==old
  report['original_eboot_restored']=True;h.command('launch '+title)
 finally:(out/'device-verification.json').write_text(json.dumps(report,indent=2)+'\n')
