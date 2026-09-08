"""Read-only VitaCompanion connection check and timestamped log retrieval."""
import argparse
from datetime import datetime
import ftplib
import hashlib
import json
from pathlib import Path
import socket

parser = argparse.ArgumentParser()
parser.add_argument('--host', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'hardware-logs' / datetime.now().strftime('%Y%m%d-%H%M%S-companion')
out.mkdir(parents=True, exist_ok=False)
report = {'host': args.host, 'output': str(out), 'read_only': True}

def command(text):
    with socket.create_connection((args.host, 1338), timeout=5) as client:
        client.settimeout(2)
        client.sendall((text+'\n').encode())
        chunks = []
        try:
            while sum(map(len, chunks)) < 32768:
                data = client.recv(4096)
                if not data:
                    break
                chunks.append(data)
        except socket.timeout:
            pass
        return b''.join(chunks).decode(errors='replace')

try:
    report['command_help'] = command('help')
    if 'version' in report['command_help']:
        report['command_version'] = command('version')
except Exception as error:
    report['command_error'] = str(error)

try:
    with ftplib.FTP() as ftp:
        report['ftp_banner'] = ftp.connect(args.host, 1337, timeout=8)
        report['ftp_login'] = ftp.login()
        entries = []
        ftp.retrlines('LIST ux0:/data/art3m1s-gxm/', entries.append)
        report['data_directory'] = entries
        report['files'] = {}
        for remote, name in [
            ('ux0:/data/art3m1s-gxm/host.log', 'host.log'),
            ('ux0:/data/art3m1s-gxm/host.previous.log', 'host.previous.log'),
            ('ux0:/app/ART3DIR01/sce_sys/param.sfo', 'installed-param.sfo'),
        ]:
            temp = out/(name+'.partial')
            try:
                with temp.open('wb') as stream:
                    ftp.retrbinary('RETR '+remote, stream.write)
                temp.rename(out/name)
                raw = (out/name).read_bytes()
                report['files'][remote] = {'local': str(out/name), 'bytes': len(raw),
                    'sha256': hashlib.sha256(raw).hexdigest().upper()}
                if name.endswith('.log'):
                    report['files'][remote]['header'] = raw.decode(errors='replace').splitlines()[:2]
            except Exception as error:
                report['files'][remote] = {'error': str(error)}
except Exception as error:
    report['ftp_error'] = str(error)

(out/'manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
