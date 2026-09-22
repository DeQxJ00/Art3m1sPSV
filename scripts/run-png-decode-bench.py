"""Run the isolated PSV decoder comparison and restore the exact original eboot."""
import argparse,hashlib,json,socket,time
from pathlib import Path
from datetime import datetime
from ftplib import FTP,error_perm
from io import BytesIO
p=argparse.ArgumentParser();p.add_argument('--host',default='192.168.1.50');a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'backup/legacy-build/png-bench-runs'/datetime.now().strftime('%Y%m%d-%H%M%S');out.mkdir(parents=True)
remote='ux0:/app/ART3DIR01/eboot.bin';folder='ux0:/data/art3m1s-png-bench';log=folder+'/result.log'
def command(cmd):
    with socket.create_connection((a.host,1338),timeout=8) as s:
        s.settimeout(3);s.sendall((cmd+'\n').encode());data=[]
        try:
            while b:=s.recv(4096):data.append(b)
        except socket.timeout:pass
        return b''.join(data).decode(errors='replace')
def connect():
    f=FTP();f.connect(a.host,1337,timeout=20);f.login();return f
def fetch(f,path):
    data=BytesIO();f.retrbinary('RETR '+path,data.write);return data.getvalue()
def replace(data):
    with connect() as f:
        staged=remote+'.png-bench';f.storbinary('STOR '+staged,BytesIO(data))
        if fetch(f,staged)!=data:raise RuntimeError('staged mismatch')
        try:f.rename(staged,remote)
        except error_perm:
            # FTPVita can refuse deletion of a recently running SELF even after
            # exit; rename the verified previous file aside and retain rollback.
            prior=remote+'.png-prior-'+str(time.time_ns())
            f.rename(remote,prior)
            try:f.rename(staged,remote)
            except Exception:
                f.rename(prior,remote);raise
            if fetch(f,remote)!=data:raise RuntimeError('replacement mismatch')
            try:f.delete(prior)
            except error_perm:print('retained previous executable:',prior,flush=True)
        if fetch(f,remote)!=data:raise RuntimeError('installed mismatch')
print('health:',command('version'),flush=True)
with connect() as f:
    original=fetch(f,remote);(out/'original-eboot.bin').write_bytes(original)
    (out/'host.log').write_bytes(fetch(f,'ux0:/data/art3m1s-gxm/host.log'))
    try:f.mkd(folder)
    except error_perm as e:
        if not str(e).startswith('550'):raise
    try:
        previous=fetch(f,log);(out/'previous-result.log').write_bytes(previous);f.delete(log)
    except error_perm as e:
        if not str(e).startswith('550'):raise
    for asset in sorted((root/'backup/legacy-build/png-bench-assets').glob('*.png')):
        data=asset.read_bytes();dest=folder+'/'+asset.name
        try:existing=fetch(f,dest)
        except error_perm:existing=None
        if existing!=data:f.storbinary('STOR '+dest,BytesIO(data))
        if fetch(f,dest)!=data:raise RuntimeError('asset mismatch '+asset.name)
        print('asset:',asset.name,len(data),flush=True)
probe=(root/'build/png-bench-vita/png_bench.self').read_bytes()
manifest={'original_sha256':hashlib.sha256(original).hexdigest(),'probe_sha256':hashlib.sha256(probe).hexdigest(),'restored':False,'completed':False}
def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
save();print('backup:',out,flush=True)
try:
    print(command('kill ART3DIR01'),flush=True);time.sleep(1);replace(probe)
    print(command('launch ART3DIR01'),flush=True)
    started=time.monotonic();last=0
    while time.monotonic()-started<180:
        time.sleep(2)
        with connect() as f:
            try:data=fetch(f,log)
            except error_perm:continue
        (out/'result.log').write_bytes(data)
        if len(data)!=last:
            print(data[last:].decode(errors='replace'),end='',flush=True);last=len(data)
        if b'DONE failures=' in data:manifest['completed']=True;save();break
    else:raise RuntimeError('no completion within 180 seconds')
finally:
    print(command('kill ART3DIR01'),flush=True)
    # FTPVita may briefly reject STOR immediately after terminating the process.
    # Retrying uses the same immutable backup and verifies before replacement.
    for attempt in range(3):
        time.sleep(2*(attempt+1))
        try:
            replace(original);break
        except (OSError,error_perm) as e:
            manifest['restore_error']=str(e);save()
            if attempt==2:raise
            print('restore retry:',e,flush=True)
    manifest['restored']=True;save();print('restore:',command('launch ART3DIR01'),flush=True)
print('manifest:',out/'manifest.json',flush=True)
