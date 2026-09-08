"""Temporarily run a one-shot benchmark through the existing test title, then restore it."""
import argparse
from datetime import datetime
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path
import hashlib
import json
import socket
import time

p=argparse.ArgumentParser()
p.add_argument('--host',required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'build/upload-bench'/datetime.now().strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True)
remote='ux0:/app/ART3DIR01/eboot.bin'
log='ux0:/data/art3m1s-upload-bench/result.log'
def command(text):
    with socket.create_connection((a.host,1338),timeout=5) as s:
        s.settimeout(2);s.sendall((text+'\n').encode());chunks=[]
        try:
            while b:=s.recv(4096):chunks.append(b)
        except socket.timeout:pass
        return b''.join(chunks).decode(errors='replace')
def connect():
    f=FTP();f.connect(a.host,1337,timeout=12);f.login();return f
def fetch(f,path):
    b=BytesIO();f.retrbinary('RETR '+path,b.write);return b.getvalue()
def replace(data):
    with connect() as f:
        staged=remote+'.upload-bench'
        f.storbinary('STOR '+staged,BytesIO(data))
        if fetch(f,staged)!=data:raise RuntimeError('staging verification failed')
        try:f.rename(staged,remote)
        except error_perm:
            f.delete(remote);f.rename(staged,remote)
        if fetch(f,remote)!=data:raise RuntimeError('installed verification failed')
print('health:',command('version'),flush=True)
with connect() as f:
    original=fetch(f,remote);(out/'original-eboot.bin').write_bytes(original)
    (out/'host.log').write_bytes(fetch(f,'ux0:/data/art3m1s-gxm/host.log'))
    try:
        previous=fetch(f,log);(out/'previous-result.log').write_bytes(previous);f.delete(log)
    except error_perm as e:
        if not str(e).startswith('550'):raise
probe=(root/'build/direct-01.02-host/upload_bench.self').read_bytes()
manifest={'host':a.host,'original_sha256':hashlib.sha256(original).hexdigest(),
          'probe_sha256':hashlib.sha256(probe).hexdigest(),'restored':False}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('backup:',out,flush=True)
print(command('kill ART3DIR01'),flush=True)
try:
    replace(probe)
    print(command('launch ART3DIR01'),flush=True)
    for attempt in range(15):
        time.sleep(1)
        with connect() as f:
            try:data=fetch(f,log)
            except error_perm:continue
        (out/'result.log').write_bytes(data)
        if b'DONE checksum=' in data:
            print(data.decode(errors='replace'),flush=True);break
    else:raise RuntimeError('benchmark did not complete')
finally:
    print(command('kill ART3DIR01'),flush=True)
    replace(original);manifest['restored']=True
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print('restored:',command('launch ART3DIR01'),flush=True)
