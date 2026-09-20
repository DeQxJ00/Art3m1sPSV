"""Derive PSV package identity from release tags and archive every built VPK."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile

TAG = re.compile(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')


def sfo_version(tag):
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError('Release tag must be vMAJOR.MINOR.PATCH')
    major, minor, patch = map(int, match.groups())
    if major > 9 or minor > 9 or patch > 99:
        raise ValueError('PSV version mapping requires major/minor <= 9 and patch <= 99; do not truncate')
    return f'{major * 10 + minor:02d}.{patch:02d}'


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True, encoding='utf-8').strip()


def identity(root):
    tags = [t for t in git(root, 'tag', '--list').splitlines() if TAG.fullmatch(t)]
    if not tags:
        raise ValueError('No vMAJOR.MINOR.PATCH release tag; package version cannot be guessed')
    # Match only actual release tags, not the paired core tag or old two-part labels.
    patterns = [arg for tag in tags for arg in ('--match', tag)]
    tag = git(root, 'describe', '--first-parent', '--tags', '--abbrev=0', *patterns, 'HEAD')
    head = git(root, 'rev-parse', 'HEAD')
    distance = int(git(root, 'rev-list', '--count', f'{tag}..HEAD'))
    dirty = bool(git(root, 'status', '--porcelain', '--untracked-files=normal'))
    version = tag
    if distance or dirty:
        version += f'-dev.{distance}+g{head[:8]}' + ('.dirty' if dirty else '')
    return dict(tag=tag, version=version, sfo_version=sfo_version(tag), commit=head,
                tag_commit=git(root, 'rev-parse', f'{tag}^{{commit}}'),
                commits_since_tag=distance, dirty=dirty)


def sfo_string(data, name):
    magic, _, keys, values, count = struct.unpack_from('<5I', data)
    if magic != 0x46535000:
        raise ValueError('Invalid PARAM.SFO')
    for i in range(count):
        key, fmt, length, _, offset = struct.unpack_from('<HHIII', data, 20 + i * 16)
        k = data[keys + key:].split(b'\0', 1)[0].decode()
        if k == name:
            if fmt != 0x0204:
                raise ValueError(f'{name} is not an SFO string')
            return data[values + offset:values + offset + length].rstrip(b'\0').decode()
    raise ValueError(f'Missing {name}')


def prepare(root, output, library):
    info = identity(root)
    info['core_library_sha256'] = hashlib.sha256(library.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=True)
    # Keep deterministic embedded metadata; timestamp belongs to the external build record.
    (output / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    values = dict(ART3_DIRECT_VERSION=info['version'], ART3_DIRECT_TAG=info['tag'],
                  ART3_DIRECT_SFO_VERSION=info['sfo_version'])
    (output / 'release-version.cmake').write_text(
        ''.join(f'set({key} "{value}")\n' for key, value in values.items()), encoding='utf-8')


def archive(root, package, library):
    # Verify identity before publishing the artifact, rather than renaming an old VPK.
    with zipfile.ZipFile(package) as z:
        info = json.loads(z.read('build-info.json'))
        if any(info.get(k) != v for k, v in identity(root).items()):
            raise ValueError('Source/tag state changed during build; rerun the build')
        if hashlib.sha256(library.read_bytes()).hexdigest() != info['core_library_sha256']:
            raise ValueError('Core library changed during build; rerun CMake configuration')
        if sfo_string(z.read('sce_sys/param.sfo'), 'APP_VER') != info['sfo_version']:
            raise ValueError('VPK APP_VER does not match Git release tag')
        if sfo_string(z.read('sce_sys/param.sfo'), 'TITLE_ID') != 'ART3DIR01':
            raise ValueError('Unexpected VPK application identity')
        if 'eboot.bin' not in z.namelist():
            raise ValueError('VPK has no executable')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-%fZ')
    destination = root / 'build/releases' / f'art3m1s-direct-{info["version"]}-{stamp}.vpk'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with package.open('rb') as source, destination.open('xb') as output:
        shutil.copyfileobj(source, output)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    if digest != hashlib.sha256(package.read_bytes()).hexdigest():
        raise ValueError('VPK archive copy verification failed')
    record = dict(info, built_at_utc=stamp, package=destination.name, sha256=digest)
    destination.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    (destination.parent / 'latest.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('prepare', 'archive'))
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--library', type=Path)
    parser.add_argument('--package', type=Path)
    args = parser.parse_args()
    if args.command == 'prepare':
        if not args.output or not args.library:
            parser.error('prepare requires --output and --library')
        prepare(args.root, args.output, args.library)
    else:
        if not args.package or not args.library:
            parser.error('archive requires --package and --library')
        archive(args.root, args.package, args.library)


if __name__ == '__main__':
    main()
