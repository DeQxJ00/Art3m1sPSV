"""Offline validation of release provenance and package rejection paths."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('publish_vpk', ROOT / 'scripts/ci/publish-vpk.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
SHA = 'a' * 40
REPO = 'owner/project'


def fixture(change=None, wrong_hash=False, demos=False):
    info = dict(version='v1.2.16', tag='v1.2.16', commit=SHA, tag_commit=SHA,
                dirty=False, commits_since_tag=0, core_library_sha256='b' * 64,
                sfo_version='12.16')
    info.update(change or {})
    payload = io.BytesIO()
    with zipfile.ZipFile(payload, 'w') as z:
        z.writestr('eboot.bin', b'fixture')
        z.writestr('build-info.json', json.dumps(info))
        if demos:
            z.writestr('demos/unwanted.txt', b'fixture')
    data = payload.getvalue()
    record = dict(info, package='build.vpk', sha256=hashlib.sha256(data).hexdigest())
    if wrong_hash:
        record['sha256'] = '0' * 64
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w') as z:
        z.writestr('build.vpk', data)
        z.writestr('build.json', json.dumps(record))
    archive.seek(0)
    return archive


class PublishTests(unittest.TestCase):
    def test_clean_release_and_beta(self):
        for tag in ('v1.2.16', 'beta1.2.0'):
            record, data = publisher.verify_package(fixture(), tag, SHA)
            self.assertEqual(record['version'], 'v1.2.16')
            self.assertTrue(data)

    def test_wrong_provenance_rejected(self):
        for change in ({'dirty': True}, {'commit': 'c' * 40},
                       {'tag_commit': 'c' * 40}, {'commits_since_tag': 1},
                       {'version': 'v1.2.16-dev.1'}, {'tag': 'v1.2.15'}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                publisher.verify_package(fixture(change), 'beta1.2.0', SHA)
        with self.assertRaises(ValueError):
            publisher.verify_package(fixture(), 'v1.2.17', SHA)

    def test_corrupt_or_bundled_demo_rejected(self):
        for options in ({'wrong_hash': True}, {'demos': True}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                publisher.verify_package(fixture(**options), 'v1.2.16', SHA)

    def test_only_successful_local_tag_builds(self):
        run = dict(head_branch='v1.2.16', status='completed', conclusion='success',
                   event='push', path='.github/workflows/build-vpk.yml',
                   head_repository={'full_name': REPO}, repository={'full_name': REPO},
                   head_sha=SHA)
        publisher.validate_run(run, REPO, SHA)
        for change in ({'head_branch': 'main'}, {'event': 'pull_request'},
                       {'conclusion': 'failure'}, {'status': 'in_progress'},
                       {'head_repository': {'full_name': 'fork/project'}},
                       {'path': '.github/workflows/unrelated.yml'}, {'head_sha': 'c' * 40}):
            bad = copy.deepcopy(run)
            bad.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                publisher.validate_run(bad, REPO, SHA)


if __name__ == '__main__':
    unittest.main()
