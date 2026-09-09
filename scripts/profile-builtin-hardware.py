"""Run a stationary-scene PSV new/old/new comparison; never send game input."""
import argparse
from datetime import datetime
from ftplib import FTP
from io import BytesIO
import hashlib
import json
from pathlib import Path
import socket
import time

p = argparse.ArgumentParser()
p.add_argument('--host', default='192.168.1.50')
args = p.parse_args()
directory = 'ux0:/data/art3m1s-gxm/'
flag = directory + 'builtin-generic.on'
out = Path('build/hardware-builtin-ab') / datetime.now().strftime('%Y%m%d-%H%M%S')
out.mkdir(parents=True)

def connect():
    ftp = FTP()
    ftp.connect(args.host, 1337, timeout=12)
    ftp.login()
    return ftp

def fetch(ftp):
    stream = BytesIO()
    ftp.retrbinary('RETR ' + directory + 'host.log', stream.write)
    return stream.getvalue()

def has_flag(ftp):
    return any(n.replace('\\', '/').rstrip('/').split('/')[-1] == 'builtin-generic.on'
               for n in ftp.nlst(directory))

with socket.create_connection((args.host, 1338), timeout=5) as sock:
    sock.settimeout(3)
    sock.sendall(b'version\n')
    print(sock.recv(4096).decode().strip(), flush=True)
with connect() as ftp:
    assert not has_flag(ftp), 'Existing override: preserve it and stop'
    initial = fetch(ftp)
    assert any(v in initial.splitlines()[0] for v in [b'builtin-v3', b'builtin-v4']), 'Unexpected installed version'
    assert b'[clock-readonly] arm=333 bus=222 gpu=111 xbar=111' in initial, 'Unexpected clocks'
    (out / 'initial.log').write_bytes(initial)

manifest = {'host': args.host, 'scope': 'same executable, new/old/new builtins and group routes; no input or clock changes', 'phases': []}
owned = False
try:
    for name in ['new1', 'old', 'new2']:
        with connect() as ftp:
            if name == 'old':
                owned = True
                ftp.storbinary('STOR ' + flag, BytesIO(b'controlled builtins comparison\n'))
            elif name == 'new2':
                ftp.delete(flag)
                owned = False
        started = datetime.now().isoformat()
        time.sleep(30)
        with connect() as ftp:
            data = fetch(ftp)
        (out / (name + '.log')).write_bytes(data)
        manifest['phases'].append({'name': name, 'started': started, 'fetched': datetime.now().isoformat(), 'sha256': hashlib.sha256(data).hexdigest()})
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        print(str(out), name, 'captured', flush=True)
finally:
    with connect() as ftp:
        if owned and has_flag(ftp):
            ftp.delete(flag)
        assert not has_flag(ftp), 'Could not restore fast mode'
    manifest['restored_fast'] = True
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('Restored fast mode', flush=True)
