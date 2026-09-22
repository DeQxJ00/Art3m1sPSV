"""Upload only the generated test games, verifying every byte via FTP.
Never writes an executable, normal game resource or save directory.
"""
import argparse, hashlib, json
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path

p=argparse.ArgumentParser(); p.add_argument('--host', default='192.168.1.50'); a=p.parse_args()
root=Path(__file__).resolve().parents[1]/'temp/native-command-port'
games=['TEST_TOSHIUE_CMD','TEST_OTOMERIRON_CMD']
for game in games:
    if not (root/'games'/game/'system/first.iet').is_file():
        raise RuntimeError('Test resources missing; run scripts/prepare-native-command-tests.py first')
manifest=[]
with FTP() as f:
    f.connect(a.host,1337,timeout=25); f.login()
    for game in games:
        local=root/'games'/game
        for source in sorted(local.rglob('*')):
            if not source.is_file(): continue
            relative=source.relative_to(local).as_posix()
            remote='ux0:/data/art3m1s-gxm/games/'+game+'/'+relative
            directory=remote.rsplit('/',1)[0]; built='ux0:'
            for part in directory.split('/')[1:]:
                built+='/'+part
                try: f.mkd(built)
                except error_perm: pass
            data=source.read_bytes()
            f.storbinary('STOR '+remote,BytesIO(data))
            got=BytesIO(); f.retrbinary('RETR '+remote,got.write)
            if got.getvalue()!=data: raise RuntimeError('verification failed: '+remote)
            manifest.append(dict(remote=remote,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
        print(game,'verified',flush=True)
(root/'psv-resources-deployed.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
