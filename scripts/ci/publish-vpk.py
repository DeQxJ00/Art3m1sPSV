"""Publish a verified, successful tag build without rebuilding or moving tags."""
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

RELEASE_TAG = re.compile(r'(?:v|beta)(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
VERSION = re.compile(r'v[0-9]+\.[0-9]+\.[0-9]+\Z')


def api(repo, path):
    return json.loads(subprocess.check_output(['gh', 'api', f'repos/{repo}/{path}']))


def validate_run(run, repo, tag_sha):
    tag = run['head_branch']
    if not RELEASE_TAG.fullmatch(tag):
        raise ValueError('Only semantic release tags can be published')
    if (run['status'] != 'completed' or run['conclusion'] != 'success'
            or run['event'] != 'push'
            or run['path'] != '.github/workflows/build-vpk.yml'
            or run['head_repository']['full_name'] != repo
            or run['repository']['full_name'] != repo
            or run['head_sha'] != tag_sha):
        raise ValueError('Build must be a successful local tag build at the current tag commit')


def verify_package(archive, tag, sha):
    with zipfile.ZipFile(archive) as artifact:
        names = artifact.namelist()
        if len(names) != len(set(names)):
            raise ValueError('Duplicate artifact members')
        packages = [n for n in names if n.endswith('.vpk')]
        if len(packages) != 1:
            raise ValueError('Expected exactly one VPK')
        package = packages[0]
        record_name = package[:-4] + '.json'
        record = json.loads(artifact.read(record_name))
        version = record['version']
        if (not VERSION.fullmatch(version) or record['tag'] != version
                or record['commit'] != sha or record['tag_commit'] != sha
                or record['dirty'] is not False or record['commits_since_tag'] != 0
                or record['package'] != Path(package).name
                or (tag.startswith('v') and version != tag)):
            raise ValueError('Package identity differs from the clean tagged source')
        data = artifact.read(package)
    if hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError('VPK checksum mismatch')
    with zipfile.ZipFile(io.BytesIO(data)) as vpk:
        if vpk.testzip() or 'eboot.bin' not in vpk.namelist():
            raise ValueError('Invalid VPK')
        if any(n.lower().startswith('demos/') for n in vpk.namelist()):
            raise ValueError('Bundled demos are not allowed')
        embedded = json.loads(vpk.read('build-info.json'))
        for key in ('version', 'tag', 'commit', 'tag_commit', 'dirty',
                    'commits_since_tag', 'core_library_sha256', 'sfo_version'):
            if embedded[key] != record[key]:
                raise ValueError(f'Embedded build identity mismatch: {key}')
    return record, data


def publish(run_id):
    if not run_id.isdecimal():
        raise ValueError('Run ID must be numeric')
    repo = os.environ['GITHUB_REPOSITORY']
    run = api(repo, f'actions/runs/{run_id}')
    tag = run['head_branch']
    if not RELEASE_TAG.fullmatch(tag):
        raise ValueError('Not a release tag')
    ref = api(repo, f'git/ref/tags/{tag}')['object']
    while ref['type'] == 'tag':
        ref = api(repo, f'git/tags/{ref["sha"]}')['object']
    if ref['type'] != 'commit':
        raise ValueError('Tag does not resolve to a commit')
    validate_run(run, repo, ref['sha'])
    artifacts = api(repo, f'actions/runs/{run_id}/artifacts?per_page=100')['artifacts']
    candidates = [a for a in artifacts if a['name'].startswith('Art3m1sPSV-VPK-') and not a['expired']]
    if len(candidates) != 1:
        raise ValueError('Expected one unexpired VPK artifact')
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        archive = root / 'artifact.zip'
        with archive.open('wb') as output:
            subprocess.run(['gh', 'api', f'repos/{repo}/actions/artifacts/{candidates[0]["id"]}/zip'],
                           stdout=output, check=True)
        digest = candidates[0].get('digest')
        if digest and digest != 'sha256:' + hashlib.sha256(archive.read_bytes()).hexdigest():
            raise ValueError('Artifact digest mismatch')
        record, data = verify_package(archive, tag, ref['sha'])
        package = root / f'art3m1s-direct-{record["version"]}.vpk'
        package.write_bytes(data)
        metadata = package.with_suffix('.json')
        metadata.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        sums = root / 'SHA256SUMS'
        sums.write_text(f'{record["sha256"]}  {package.name}\n', encoding='utf-8')
        releases = api(repo, 'releases?per_page=100')
        release = next((r for r in releases if r['tag_name'] == tag), None)
        if release is None:
            notes = root / 'notes.md'
            notes.write_text(f'PSV VPK built from `{ref["sha"]}`.\n\n'
                             f'Package version: `{record["version"]}`.\n\n'
                             f'[Verified build]({run["html_url"]}). Includes SHA256SUMS; no bundled demos.\n',
                             encoding='utf-8')
            args = ['gh', 'release', 'create', tag, '--repo', repo, '--verify-tag',
                    '--title', tag, '--notes-file', str(notes)]
            if tag.startswith('beta'):
                args += ['--prerelease', '--latest=false']
            subprocess.run(args, check=True)
            release = api(repo, f'releases/tags/{tag}')
        for path in (package, metadata, sums):
            existing = next((a for a in release['assets'] if a['name'] == path.name), None)
            if existing:
                expected = 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()
                if existing.get('digest') != expected:
                    raise ValueError(f'Existing asset differs; refusing to overwrite {path.name}')
                print(f'Already verified: {path.name}')
                continue
            subprocess.run(['gh', 'release', 'upload', tag, str(path), '--repo', repo], check=True)
        final = api(repo, f'releases/tags/{tag}')
        for path in (package, metadata, sums):
            asset = next(a for a in final['assets'] if a['name'] == path.name)
            if asset['size'] != path.stat().st_size or asset.get('digest') != 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError(f'Published asset verification failed: {path.name}')
        print(f'Published and verified: {final["html_url"]}')


if __name__ == '__main__':
    publish(sys.argv[1])
