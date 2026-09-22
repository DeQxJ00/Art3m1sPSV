"""Finite real-device profiler A/B/A; restores the original diagnostic flag."""
import argparse, hashlib, json, socket, time
from datetime import datetime
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--host',default='192.168.1.50')
p.add_argument('--seconds',type=int,default=20)
a=p.parse_args()
if not 15 <= a.seconds <= 30:p.error('each phase must be 15-30 seconds')
flag='ux0:/data/art3m1s-gxm/trace-nextline.off'
log='ux0:/data/art3m1s-gxm/host.log'
marker=b'art3m1s bounded profile-state A/B test\n'
out=Path(__file__).resolve().parents[1] / Path('backup/legacy-build/hardware-logs')/datetime.now().strftime('%Y%m%d-%H%M%S-profile-ab')
out.mkdir(parents=True)
manifest={'host':a.host,'seconds_per_phase':a.seconds,'events':[],'restored':False}
def health():
 with socket.create_connection((a.host,1338),timeout=5) as s:
  s.settimeout(3);s.sendall(b'version\n');reply=s.recv(4096).decode(errors='replace')
  if 'vitacompanion' not in reply.lower():raise RuntimeError(reply)
def ftp():
 health();f=FTP();f.connect(a.host,1337,timeout=10);f.login();return f
def read(f,path,optional=False):
 b=BytesIO()
 try:f.retrbinary('RETR '+path,b.write)
 except error_perm as e:
  if optional and str(e).startswith('550'):return None
  raise
 return b.getvalue()
def save():
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
def snapshot(label):
 with ftp() as f:data=read(f,log)
 (out/(label+'.log')).write_bytes(data)
 manifest['events'].append({'label':label,'time':datetime.now().isoformat(),'sha256':hashlib.sha256(data).hexdigest()});save()
with ftp() as f:original=read(f,flag,True)
if original is not None:(out/'original-trace-nextline.off').write_bytes(original)
manifest['original_off_present']=original is not None
expected=original
save()
def set_flag(value):
 global expected
 with ftp() as f:
  if read(f,flag,True)!=expected:raise RuntimeError('Diagnostic flag changed externally; refusing to overwrite it')
  if value is None:
   if expected is not None:f.delete(flag)
  else:f.storbinary('STOR '+flag,BytesIO(value))
  expected=value
  if read(f,flag,True)!=value:raise RuntimeError('Flag verification failed')
try:
 for label,value in [('on',None),('off',marker),('on-again',None)]:
  set_flag(value);print(label,flush=True);snapshot(label+'-start')
  time.sleep(a.seconds);snapshot(label+'-end')
finally:
 try:set_flag(original);manifest['restored']=True
 finally:save();print(out,flush=True)
