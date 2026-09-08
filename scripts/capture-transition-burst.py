"""Capture bounded MCP frames after one visible, explicitly selected input."""
import argparse
import base64
import json
import time
import urllib.request
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('session')
p.add_argument('output', type=Path)
p.add_argument('--button')
p.add_argument('--touch', nargs=2, type=float)
p.add_argument('--count', type=int, default=40)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)

def call(name, args):
    payload = json.dumps(dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name=name, arguments=args))).encode()
    req = urllib.request.Request('http://127.0.0.1:32560/mcp', data=payload,
        headers={'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'})
    with urllib.request.urlopen(req, timeout=45) as r:
        raw = r.read().decode()
    result = json.loads(next(line[6:] for line in raw.splitlines() if line.startswith('data: ')))
    if 'error' in result:
        raise RuntimeError(result['error'])
    return result['result']

call('session_status', {'sessionId': a.session})
t0 = time.monotonic()
if a.button:
    call('send_input', {'sessionId': a.session, 'buttons': [a.button], 'durationMs': 80})
if a.touch:
    call('touch', {'sessionId': a.session, 'port': 'front',
        'points': [{'x': a.touch[0], 'y': a.touch[1]}], 'durationMs': 80})
frames = []
for i in range(a.count):
    data = call('capture_screen', {'sessionId': a.session})
    block = next(b for b in data['content'] if b['type'] == 'image')
    filename = f'{i:03}.png'
    (a.output / filename).write_bytes(base64.b64decode(block['data']))
    frames.append({'file': filename, 'seconds': time.monotonic() - t0})
    time.sleep(0.04)
(a.output / 'frames.json').write_text(json.dumps(frames, indent=2), encoding='utf-8')
print(json.dumps({'frames': len(frames), 'elapsed': frames[-1]['seconds'], 'directory': str(a.output)}))
