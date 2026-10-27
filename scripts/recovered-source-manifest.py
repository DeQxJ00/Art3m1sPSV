"""Verify the pinned recovery runtime used by release builds."""
import argparse
import hashlib
import json
from pathlib import Path


def verify(root):
    root = Path(root)
    manifest = root / 'source-manifest.json'
    data = json.loads(manifest.read_text(encoding='utf-8'))
    if len(data['source_commit']) != 40 or not data['files']:
        raise ValueError('Invalid recovery source manifest')
    actual = {p.relative_to(root).as_posix() for folder in ('native', 'recovered')
              for p in (root / folder).rglob('*') if p.is_file()}
    if actual != set(data['files']):
        raise ValueError('Recovery runtime file set differs from manifest')
    for name, digest in data['files'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Recovery source checksum mismatch: {name}')
    device_hash = data['validated_device_self_sha256']
    device_status = data.get('device_validation', 'validated' if device_hash else 'not_recorded')
    if device_status not in ('validated', 'not_recorded'):
        raise ValueError('Unknown recovery device validation status; put descriptions in device_validation_notes')
    if device_status == 'validated':
        if len(device_hash) != 64 or any(c not in '0123456789abcdef' for c in device_hash):
            raise ValueError('Missing recovery provenance: validated_device_self_sha256')
    elif device_hash != '':
        raise ValueError('Unvalidated recovery runtime must not claim a device hash')
    return dict(source_commit=data['source_commit'],
                manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                validated_device_self_sha256=device_hash,
                device_validation=device_status)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.root), indent=2))
