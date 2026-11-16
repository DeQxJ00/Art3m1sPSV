import contextlib
import importlib.util
import io
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('release_version', ROOT / 'scripts/release-version.py')
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)


def sfo(app_version):
    keys = b'APP_VER\0TITLE_ID\0'
    data = app_version.encode() + b'\0ART3DIR01\0'
    start = 20 + 16 * 2
    entries = struct.pack('<HHIII', 0, 0x0204, len(app_version) + 1, len(app_version) + 1, 0)
    entries += struct.pack('<HHIII', 8, 0x0204, 10, 10, len(app_version) + 1)
    return struct.pack('<5I', 0x46535000, 0x101, start, start + len(keys), 2) + entries + keys + data


class ReleaseVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Version Test')
        self.git('config', 'user.email', 'test@example.invalid')
        (self.root / '.gitignore').write_text('build/\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'fixture')
        self.git('tag', 'v1.2.14')
        self.output = self.root / 'build'
        self.output.mkdir()
        self.library = self.output / 'core.a'
        self.library.write_bytes(b'core fixture')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.STDOUT)

    def package(self, app_version='12.14'):
        version.prepare(self.root, self.output, self.library)
        package = self.output / 'test.vpk'
        with zipfile.ZipFile(package, 'w') as z:
            z.writestr('build-info.json', (self.output / 'build-info.json').read_bytes())
            z.writestr('sce_sys/param.sfo', sfo(app_version))
            z.writestr('eboot.bin', b'test executable')
        return package

    def test_version_mapping_and_limits(self):
        self.assertEqual(version.sfo_version('v1.2.14'), '12.14')
        self.assertEqual(version.sfo_version('v1.3.0'), '13.00')
        for bad in ('v1.20', 'v1.2.14-core', 'v10.0.0', 'v1.10.0', 'v1.2.100'):
            with self.assertRaises(ValueError):
                version.sfo_version(bad)

    def test_clean_tag_and_development_identity(self):
        self.assertEqual(version.identity(self.root)['version'], 'v1.2.14')
        self.git('commit', '--allow-empty', '-qm', 'next')
        info = version.identity(self.root)
        self.assertEqual(info['commits_since_tag'], 1)
        self.assertTrue(info['version'].startswith('v1.2.14-dev.1+g'))
        (self.root / 'local.txt').write_text('not committed')
        self.assertTrue(version.identity(self.root)['version'].endswith('.dirty'))

    def test_ignore_core_old_and_unreachable_tags(self):
        self.git('tag', 'v1.20')
        self.git('tag', 'v9.9.99-core')
        self.git('checkout', '-qb', 'other')
        self.git('commit', '--allow-empty', '-qm', 'other branch')
        self.git('tag', 'v9.9.99')
        self.git('checkout', '-q', 'main')
        self.assertEqual(version.identity(self.root)['tag'], 'v1.2.14')

    def test_no_release_tag_fails(self):
        self.git('tag', '-d', 'v1.2.14')
        with self.assertRaises(ValueError):
            version.identity(self.root)

    def test_archive_and_sfo_verification(self):
        package = self.package()
        with contextlib.redirect_stdout(io.StringIO()):
            version.archive(self.root, package, self.library)
            version.archive(self.root, package, self.library)
        self.assertEqual(len(list((self.output / 'releases').glob('*.vpk'))), 2)
        record = json.loads((self.output / 'releases/latest.json').read_text())
        self.assertEqual(record['sfo_version'], '12.14')
        with self.assertRaisesRegex(ValueError, 'APP_VER'):
            version.archive(self.root, self.package('01.10'), self.library)

    def test_changed_source_state_rejects_stale_package(self):
        package = self.package()
        self.git('commit', '--allow-empty', '-qm', 'changed')
        with self.assertRaisesRegex(ValueError, 'Source/tag'):
            version.archive(self.root, package, self.library)

    def test_no_demo_artifact_preserves_app_and_updates_build_record(self):
        demo = 'demos/STARWIND_DEMO/root.pfs'
        payload = b'demo fixture'
        manifest = self.root / 'host-direct/assets/demo-manifest.json'
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({'files': {demo: {
            'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}}}))
        package = self.package()
        with zipfile.ZipFile(package, 'a') as z:
            z.writestr('demos/', b'')
            z.writestr(demo, payload)
            z.writestr('assets/menu.ttf', b'font fixture')
        with contextlib.redirect_stdout(io.StringIO()):
            version.archive(self.root, package, self.library)
        releases = self.output / 'releases'
        latest = (releases / 'latest.json').read_bytes()
        output = self.output / 'release-no-demo'
        run = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/release-version.py'), 'without-demos',
             '--root', str(self.root), '--output', str(output)],
            capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        variant, = output.glob('*.vpk')
        record = json.loads(variant.with_suffix('.json').read_text())
        self.assertEqual(record['package'], variant.name)
        self.assertEqual(record['sha256'], hashlib.sha256(variant.read_bytes()).hexdigest())
        self.assertEqual(record['bundled_demos'], {})
        self.assertEqual((output / 'SHA256SUMS').read_text(), f"{record['sha256']}  {variant.name}\n")
        with zipfile.ZipFile(variant) as z:
            self.assertIsNone(z.testzip())
            self.assertEqual(set(z.namelist()), {
                'build-info.json', 'sce_sys/param.sfo', 'eboot.bin', 'assets/menu.ttf'})
            self.assertEqual(z.read('eboot.bin'), b'test executable')
            self.assertEqual(z.read('sce_sys/param.sfo'), sfo('12.14'))
            self.assertEqual(z.read('assets/menu.ttf'), b'font fixture')
            embedded = json.loads(z.read('build-info.json'))
            self.assertEqual(embedded['bundled_demos'], {})
            self.assertEqual(embedded['commit'], json.loads(latest)['commit'])
        self.assertEqual((releases / 'latest.json').read_bytes(), latest)
        with zipfile.ZipFile(releases / json.loads(latest)['package']) as z:
            self.assertEqual(z.read(demo), payload)

    def test_changed_library_rejects_stale_package(self):
        package = self.package()
        self.library.write_bytes(b'new library')
        with self.assertRaisesRegex(ValueError, 'Core library'):
            version.archive(self.root, package, self.library)

    def test_unapproved_bundled_demos_rejected(self):
        package = self.package()
        with zipfile.ZipFile(package, 'a') as z:
            z.writestr('demos/example/system.ini', b'test')
        with self.assertRaisesRegex(ValueError, 'approved manifest'):
            version.archive(self.root, package, self.library)

    def test_approved_bundled_demo_verified_and_missing_or_changed_rejected(self):
        data = b'pf8-demo-fixture'
        name = 'demos/STARWIND_DEMO/root.pfs'
        manifest = self.root / 'host-direct/assets/demo-manifest.json'
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps({'files': {name: {
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}}}))
        for payload, succeeds in [(data, True), (b'wrong', False), (None, False)]:
            package = self.package()
            if payload is not None:
                with zipfile.ZipFile(package, 'a') as z: z.writestr(name, payload)
            with zipfile.ZipFile(package) as z:
                if succeeds: version.verify_demos(self.root, z)
                else:
                    with self.assertRaises(ValueError): version.verify_demos(self.root, z)


if __name__ == '__main__':
    unittest.main()
