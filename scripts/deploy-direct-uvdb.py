"""Deploy an opt-in uvdb build after backing up the installed app files."""
import argparse
from datetime import datetime
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path
import hashlib
import json
import socket
import time
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--host', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root/'backup/deploy-uvdb'/datetime.now().strftime('deploy-%Y%m%d-%H%M%S')
out.mkdir(parents=True)
def command(cmd):
    with socket.create_connection((a.host,1338),timeout=8) as s:
        s.settimeout(3);s.sendall((cmd+'\n').encode());data=[]
        try:
            while True:
                b=s.recv(4096)
                if not b:break
                data.append(b)
        except socket.timeout:pass
        return b''.join(data).decode(errors='replace')
print('health:',command('version'),flush=True)
manifest={'host':a.host,'backup':str(out),'files':{}}
with FTP() as f:
    f.connect(a.host,1337,timeout=20);f.login()
    def fetch(remote):
        b=BytesIO();f.retrbinary('RETR '+remote,b.write);return b.getvalue()
    # Complete and record every backup before stopping or changing the app.
    paths={'eboot.bin':'ux0:/app/ART3DIR01/eboot.bin',
           'param.sfo':'ux0:/app/ART3DIR01/sce_sys/param.sfo',
           'host.log':'ux0:/data/art3m1s-gxm/host.log'}
    for name,remote in paths.items():
        data=fetch(remote);(out/name).write_bytes(data)
        manifest['files'][name]={'remote':remote,'old_sha256':hashlib.sha256(data).hexdigest()}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print('backups complete:',out,flush=True)
    print('kill ART3DIR01:',command('kill ART3DIR01'),flush=True)
    time.sleep(1)
    with zipfile.ZipFile(root/'build/uvdb/host/art3m1s_direct.vpk') as z:
        for member,name in [('eboot.bin','eboot.bin'),('sce_sys/param.sfo','param.sfo')]:
            remote=paths[name];data=z.read(member)
            # Stage beside the destination and verify before replacing it.
            staged=remote+'.uvdb-upload'
            f.storbinary('STOR '+staged,BytesIO(data))
            if fetch(staged)!=data:raise RuntimeError('staged verification failed: '+name)
            try:
                f.rename(staged,remote)
            except error_perm:
                # This FTPVita build refuses rename-over-existing. The old file
                # is already saved locally and the staged replacement verified.
                f.delete(remote)
                try:
                    f.rename(staged,remote)
                except Exception:
                    f.storbinary('STOR '+remote,BytesIO((out/name).read_bytes()))
                    raise
            if fetch(remote)!=data:raise RuntimeError('installed verification failed: '+name)
            manifest['files'][name]['new_sha256']=hashlib.sha256(data).hexdigest()
    f.storbinary('STOR ux0:/data/art3m1s-gxm/uvdb-start.flag',BytesIO(b'one-shot startup GDB test\n'))
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('launch ART3DIR01:',command('launch ART3DIR01'),flush=True)
print('deploy manifest:',out/'manifest.json',flush=True)
