"""Measure profiling or optI layout caching on/off/on in one manually selected, untouched scene.

Only the selected temporary .off control is changed; restore its original
bytes/absence even on failure. Saves, executable, clocks and inputs are untouched.
"""
import argparse
from datetime import datetime
from ftplib import FTP
from io import BytesIO
import json
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--mode', choices=['profile', 'layout', 'commands', 'keys'], default='profile')
    args = parser.parse_args()
    if not 15 <= args.seconds <= 60:
        parser.error('--seconds must be between 15 and 60')
    control, marker, banner = {
        'profile': ('trace-nextline.off', '[profile-state]', b'optH profile gate'),
        'layout': ('text-layout-cache.off', '[layout-cache-state]', b'optI layout cache'),
        'commands': ('text-command-cache.off', '[command-cache-state]', b'optJ command cache'),
        'keys': ('gxm-keyless.off', '[keyless-state]', b'optK keyless'),
    }[args.mode]
    out = Path(__file__).resolve().parents[1] / 'build/profile-ab' / datetime.now().strftime('%Y%m%d-%H%M%S')
    out.mkdir(parents=True)
    report = {'host': args.host, 'mode': args.mode, 'control': control, 'seconds_per_phase': args.seconds, 'phases': [], 'restored': False}
    base = 'ux0:/data/art3m1s-gxm/'

    def connection():
        ftp = FTP()
        ftp.connect(args.host, 1337, timeout=10)
        ftp.login()
        return ftp

    def read(ftp, name):
        data = BytesIO()
        ftp.retrbinary('RETR ' + base + name, data.write)
        return data.getvalue()

    def read_optional(ftp, name):
        # Enumerate first: a generic FTP 550 can also mean permission failure.
        names = []
        ftp.retrlines('NLST ' + base, names.append)
        if name not in [n.rstrip('/').split('/')[-1] for n in names]:
            return None
        return read(ftp, name)

    def set_pause(data):
        with connection() as ftp:
            if data is None:
                if read_optional(ftp, control) is not None:
                    ftp.delete(base + control)
            else:
                ftp.storbinary('STOR ' + base + control, BytesIO(data))
            if read_optional(ftp, control) != data:
                raise RuntimeError('Profile control verification failed')

    with socket.create_connection((args.host, 1338), timeout=5) as client:
        client.settimeout(3)
        client.sendall(b'version\n')
        report['health'] = client.recv(4096).decode(errors='replace')
    with connection() as ftp:
        before = read(ftp, 'host.log')
        (out / 'before.log').write_bytes(before)
        if banner not in before[:512] or b'game loaded:' not in before:
            raise RuntimeError('Matching candidate must be running with a game loaded')
        if (args.mode == 'profile' and read_optional(ftp, 'trace-nextline.flag') is None) or marker.encode() not in before:
            raise RuntimeError('Requested diagnostic control was not armed at game boot')
        original = read_optional(ftp, control)
        report['original_pause_present'] = original is not None
        if original is not None:
            (out / ('original-' + control)).write_bytes(original)
    try:
        for name, data, enabled in [('on-a', None, 1), ('off', b'bounded Direct A/B pause\n', 0), ('on-b', None, 1)]:
            set_pause(data)
            print(f'{name}: sampling {args.seconds}s; keep the same scene untouched', flush=True)
            time.sleep(args.seconds)
            with connection() as ftp:
                raw = read(ftp, 'host.log')
            (out / (name + '.log')).write_bytes(raw)
            states = [line for line in raw.decode(errors='replace').splitlines() if marker in line]
            if not states or f'enabled={enabled} ' not in states[-1]:
                raise RuntimeError('Profile state not confirmed: ' + name)
            report['phases'].append({'name': name, 'state': states[-1], 'log': name + '.log'})
    finally:
        try:
            set_pause(original)
            report['restored'] = True
        except Exception as error:
            report['restore_error'] = str(error)
        (out / 'manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print('manifest:', out / 'manifest.json', flush=True)
        if not report['restored']:
            raise RuntimeError('Failed to restore profile control; see manifest')


if __name__ == '__main__':
    main()
