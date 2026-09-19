"""Run isolated shader fixtures on VitaCompanion; preserve user preferences.

Uploads resources only, never replaces the installed executable or game PFS.
Every write is read back. 'restore' restores launcher and shader settings.
"""
from pathlib import Path
from ftplib import FTP, error_perm
from io import BytesIO
import argparse
import hashlib
import json
import socket
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build/three-game-shaders'
BASE = 'ux0:/data/art3m1s-gxm/'
PREFS = ['last-game.txt','last-game.txt.bak','shader-settings.txt','shader-settings.txt.bak']


def main():
    p=argparse.ArgumentParser()
    p.add_argument('mode',choices=['upload','start','press','collect','restore','cg-only','refresh-fonts','reset-test-cache','verify-final','refresh-demo'])
    p.add_argument('--host',default='192.168.1.50')
    p.add_argument('--game',type=int,default=0,choices=range(3))
    p.add_argument('--round',default='cold',choices=['cold','warm','cg-only','progress'])
    p.add_argument('--evidence-root',type=Path,default=OUT/'device')
    a=p.parse_args()
    manifests=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'))
    m=manifests[a.game]; game=m['id']
    evidence=a.evidence_root; evidence.mkdir(parents=True,exist_ok=True)
    backup=evidence/'preferences'; backup.mkdir(exist_ok=True)
    def command(c):
        with socket.create_connection((a.host,1338),timeout=6) as s:
            s.settimeout(3);s.sendall((c+'\n').encode());parts=[]
            try:
                while True:
                    b=s.recv(4096)
                    if not b:break
                    parts.append(b)
            except socket.timeout: pass
            return b''.join(parts).decode(errors='replace')
    print('health:',command('version'),flush=True)
    def ftp():
        f=FTP();f.connect(a.host,1337,timeout=15);f.login();return f
    def get(f,path):
        b=BytesIO();f.retrbinary('RETR '+path,b.write);return b.getvalue()
    def optional(f,path):
        try:return get(f,path)
        except error_perm as e:
            if not str(e).startswith('550'):raise
            return None
    def put(f,path,data):
        f.storbinary('STOR '+path,BytesIO(data))
        assert get(f,path)==data,path
    def mkdirs(f,path):
        prefix='ux0:'
        for part in path.split('/')[1:]:
            prefix+='/'+part
            try:f.mkd(prefix)
            except error_perm as e:
                if not str(e).startswith('550'):raise
    if a.mode in ['upload','refresh-demo']:
        with ftp() as f:
            if not (backup/'manifest.json').exists():
                saved={}
                for name in PREFS:
                    data=optional(f,BASE+name);saved[name]=data is not None
                    if data is not None:(backup/name).write_bytes(data)
                (backup/'manifest.json').write_text(json.dumps(saved,indent=2))
            uploaded=[]
            for manifest in manifests:
                directory=OUT/'games'/manifest['id']
                # Dedicated test IDs must not reuse a previous run's cache.
                for row in (manifest['nonbuiltin'] if a.mode=='upload' else []):
                    if row['conversion']!='supported':continue
                    for prefix in [f'games/{manifest["id"]}/shader-cache/','shader-cache/']:
                        cached=optional(f,BASE+prefix+row['file']+'.gxp')
                        if cached is not None:raise RuntimeError('Cold run would reuse existing cache: '+prefix+row['file'])
                for file in sorted(directory.rglob('*')):
                    if not file.is_file():continue
                    rel=file.relative_to(directory).as_posix()
                    if a.mode=='refresh-demo' and rel not in ['assets/probe.otf','system/first.iet','README.txt']:continue
                    path=BASE+'games/'+manifest['id']+'/'+rel
                    mkdirs(f,path.rsplit('/',1)[0]);data=file.read_bytes();put(f,path,data)
                    uploaded.append(dict(path=path,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
                print('verified resources',manifest['id'],flush=True)
            (evidence/'upload.json').write_text(json.dumps(uploaded,indent=2))
    elif a.mode=='start':
        assert (backup/'manifest.json').exists(),'Backup preferences first'
        print(command('kill ART3DIR01'),flush=True)
        with ftp() as f:
            put(f,BASE+'last-game.txt',(game+'\n').encode())
            flags={'cold':'1 1 1\n','warm':'1 0 0\n','cg-only':'1 0 1\n','progress':'1 1 1\n'}
            put(f,BASE+'shader-settings.txt',flags[a.round].encode())
        print(command('nosleep on'),flush=True)
        print(command('launch ART3DIR01'),flush=True)
    elif a.mode=='press':
        print(command('press circle; wait 100ms; release circle'),flush=True)
    elif a.mode=='collect':
        dst=evidence/a.round/game;dst.mkdir(parents=True,exist_ok=True)
        with ftp() as f:
            data=get(f,BASE+'host.log');(dst/'host.log').write_bytes(data)
            log=data.decode(errors='replace')
            for line in log.splitlines():
                if any(k in line for k in ['REAL-SHADER DONE','REAL-SHADER CAPTURE','[shader-compiler]','[shader-cache] hit','startup_validation','shader-settings','boot platform','[shader-progress]']):
                    print(line[:700],flush=True)
            if f'REAL-SHADER DONE game={game}' not in log:
                raise SystemExit('INCOMPLETE: log preserved; not accepted')
            for c in m['cases']:
                data=get(f,BASE+f'saves/{game}/savedata/'+c['capture'])
                (dst/c['capture']).write_bytes(data)
            if 'REAL-SHADER MANUAL original-vs-psv' in log:
                data=get(f,BASE+f'saves/{game}/savedata/manual-first.png')
                (dst/'manual-first.png').write_bytes(data)
            cache=[]
            for row in m['nonbuiltin']:
                if row['conversion']!='supported':continue
                for ext in ['.cg','.conversion.json','.gxp','.hash']:
                    rel=row['file']+ext;data=optional(f,BASE+f'games/{game}/shader-cache/'+rel)
                    if data is not None:
                        target=dst/'shader-cache'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
                    cache.append(dict(path=rel,exists=data is not None,sha256=hashlib.sha256(data).hexdigest() if data else None))
            (dst/'cache.json').write_text(json.dumps(cache,indent=2))
            print('COLLECTED',len(m['cases']),'images',sum(c['exists'] for c in cache),'cache files',flush=True)
    elif a.mode=='refresh-fonts':
        print(command('kill ART3DIR01'),flush=True)
        refreshed=[]
        with ftp() as f:
            for item in manifests:
                path=BASE+'games/'+item['id']+'/assets/probe.otf'
                data=(OUT/'ascii.otf').read_bytes();put(f,path,data)
                refreshed.append(dict(path=path,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        (evidence/'font-refresh.json').write_text(json.dumps(refreshed,indent=2))
        print('Verified ASCII font for all three test fixtures',flush=True)
    elif a.mode in ['cg-only','reset-test-cache']:
        print(command('kill ART3DIR01'),flush=True)
        with ftp() as f:
            for row in m['nonbuiltin']:
                if row['conversion']!='supported':continue
                for ext in (['.cg','.conversion.json','.gxp','.hash'] if a.mode=='reset-test-cache' else ['.gxp','.hash']):
                    rel=row['file']+ext;path=BASE+f'games/{game}/shader-cache/'+rel
                    expected=evidence/('cold' if a.mode=='reset-test-cache' else 'warm')/game/'shader-cache'/rel
                    assert get(f,path)==expected.read_bytes(),path
                    f.delete(path)
        print('Removed only byte-verified artifacts in isolated test cache',flush=True)
    elif a.mode=='restore':
        print(command('kill ART3DIR01'),flush=True)
        saved=json.loads((backup/'manifest.json').read_text())
        with ftp() as f:
            for name,existed in saved.items():
                if existed:put(f,BASE+name,(backup/name).read_bytes())
                elif optional(f,BASE+name) is not None:f.delete(BASE+name)
        print(command('launch ART3DIR01'),flush=True)
        (evidence/'restored.json').write_text(json.dumps(dict(preferences_restored=True,time=time.time()),indent=2))
    elif a.mode=='verify-final':
        saved=json.loads((backup/'manifest.json').read_text())
        verified=[]
        with ftp() as f:
            for name,existed in saved.items():
                actual=optional(f,BASE+name)
                assert actual==((backup/name).read_bytes() if existed else None),name
            for item in manifests:
                for rel in ['assets/probe.otf','system/first.iet','platform.txt']:
                    path=BASE+'games/'+item['id']+'/'+rel
                    local=(OUT/'games'/item['id']/rel).read_bytes()
                    assert get(f,path)==local,path
                    verified.append(dict(path=path,sha256=hashlib.sha256(local).hexdigest()))
                readme=OUT/'games'/item['id']/'README.txt'
                put(f,BASE+'games/'+item['id']+'/README.txt',readme.read_bytes())
            log=get(f,BASE+'host.log');(evidence/'restored-startup.log').write_bytes(log)
            assert b'startup_validation=1' in log,'Restored startup validation missing'
            assert b'game loaded:' not in log,'Expected to leave launcher displayed'
        (evidence/'final-verification.json').write_text(json.dumps(dict(preferences_restored=True,
            launcher_startup_passed=True,resources=verified),indent=2))
        print('Verified restored preferences, test fonts/scripts/platform, and launcher startup; README files installed',flush=True)


if __name__=='__main__':main()
