"""Release gates for the pinned device-tested recovered runtime."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sources = load('sources', ROOT / 'scripts/recovered-source-manifest.py')
gate = load('gate', ROOT / 'scripts/verify-recovered-release.py')
publisher_tests = load('publisher_tests', Path(__file__).with_name('publish_test.py'))
publisher = publisher_tests.publisher


class RecoveredReleaseTests(unittest.TestCase):
    def metadata(self):
        return dict(version='v1.4.0', tag='v1.4.0', emote_backend='recovered',
                    emote_default_backend=2, core_commit='c' * 40,
                    recovered_runtime=sources.verify(ROOT / 'vendor/emote-recovery'))

    def test_pinned_sources_and_tampering(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'runtime'
            shutil.copytree(ROOT / 'vendor/emote-recovery', root)
            sources.verify(root)
            target = root / 'native/model_controls.c'
            original = target.read_bytes()
            target.write_bytes(original + b'\n')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                sources.verify(root)
            target.write_bytes(original)
            target.unlink()
            with self.assertRaisesRegex(ValueError, 'file set'):
                sources.verify(root)
            target.write_bytes(original)
            (root / 'native/unexpected.h').write_text('extra')
            with self.assertRaisesRegex(ValueError, 'file set'):
                sources.verify(root)

    def test_recovered_release_and_wrong_default(self):
        info = self.metadata()
        gate.validate_metadata(info)
        for change in ({'emote_backend': 'builtin'}, {'emote_default_backend': 0},
                       {'recovered_runtime': {}}, {'core_commit': ''}):
            bad = copy.deepcopy(info)
            bad.update(change)
            with self.assertRaises(ValueError):
                gate.validate_metadata(bad)

    def test_device_validation_is_explicit_and_consistent(self):
        info = self.metadata()
        runtime = info['recovered_runtime']
        runtime.update(device_validation='not_recorded', validated_device_self_sha256='')
        gate.validate_metadata(info)
        for status, digest in [('validated', ''), ('not_recorded', 'a' * 64),
                               ('unknown', ''), ('validated', 'z' * 64)]:
            runtime.update(device_validation=status, validated_device_self_sha256=digest)
            with self.assertRaises(ValueError):
                gate.validate_metadata(info)
        runtime.update(device_validation='validated', validated_device_self_sha256='a' * 64)
        gate.validate_metadata(info)
        del runtime['device_validation']
        gate.validate_metadata(info)
        runtime['validated_device_self_sha256'] = ''
        with self.assertRaises(ValueError):
            gate.validate_metadata(info)

    def test_publisher_rejects_legacy_backend_for_new_release(self):
        info = self.metadata()
        publisher.verify_package(publisher_tests.fixture(info), 'v1.4.0', publisher_tests.SHA)
        info['emote_default_backend'] = 0
        with self.assertRaisesRegex(ValueError, 'default'):
            publisher.verify_package(publisher_tests.fixture(info), 'v1.4.0', publisher_tests.SHA)
        with self.assertRaises(ValueError):
            publisher.verify_package(publisher_tests.fixture({'version': 'v1.4.0', 'tag': 'v1.4.0'}), 'v1.4.0', publisher_tests.SHA)

    def test_replacement_requires_explicit_opt_in(self):
        self.assertFalse(publisher.replacement_required('same', 'same', False))
        with self.assertRaisesRegex(ValueError, 'explicit'):
            publisher.replacement_required('old', 'new', False)
        self.assertTrue(publisher.replacement_required('old', 'new', True))

    def test_changelog_extracts_exact_version(self):
        text = '## v1.4.0 — date\n\n- Faster.\n\n## v1.3.13 — date\n\n- Older.\n'
        self.assertEqual(publisher.changelog_section(text, 'v1.4.0'), '- Faster.')
        with self.assertRaisesRegex(ValueError, 'Missing changelog'):
            publisher.changelog_section(text, 'v1.4.1')
        for filename in ('CHANGELOG.md', 'CHANGELOG.en.md'):
            entry = publisher.changelog_section((ROOT / filename).read_text(encoding='utf-8'), 'v1.4.0')
            self.assertIn('E-mote', entry)
            self.assertIn('444', entry)


if __name__ == '__main__':
    unittest.main()
