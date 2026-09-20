import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
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

    def test_changed_library_rejects_stale_package(self):
        package = self.package()
        self.library.write_bytes(b'new library')
        with self.assertRaisesRegex(ValueError, 'Core library'):
            version.archive(self.root, package, self.library)


if __name__ == '__main__':
    unittest.main()
