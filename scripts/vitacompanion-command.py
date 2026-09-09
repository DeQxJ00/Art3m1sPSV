"""Send a user-authorized VitaCompanion command after a connection check."""
import argparse
import socket

parser = argparse.ArgumentParser()
parser.add_argument('--host', default='192.168.1.50')
parser.add_argument('command')
args = parser.parse_args()

def send(command):
    with socket.create_connection((args.host, 1338), timeout=5) as client:
        client.settimeout(3)
        client.sendall((command + '\n').encode())
        chunks = []
        try:
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
        except socket.timeout:
            pass
        return b''.join(chunks).decode(errors='replace')

print('health:', send('version'), flush=True)
print('command:', args.command, flush=True)
print(send(args.command), flush=True)
