"""Exercise split cache accounting reports, including older unsplit logs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('summarize-image-cache-budget.py')
BASE = '[image-cache-budget] ready=80 idle=20 total=100 limit=128 idle_limit=32 ready_goal=0'
PARTS = ' parts_version=1 ready_decoded=60 ready_encoded=18 ready_proof=2 idle_decoded=2 idle_gpu_est=10 idle_encoded=7 idle_proof=1'

class BreakdownTests(unittest.TestCase):
    def parse(self, line):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / 'host.log'
            log.write_text(line + '\n', encoding='utf-8')
            result = subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True, text=True, check=False)
            return result.returncode, json.loads(result.stdout)

    def test_legacy_has_unknown_breakdown(self):
        code, data = self.parse(BASE)
        self.assertEqual(code, 0)
        self.assertIsNone(data['last_breakdown'])

    def test_mixed_pixels_backups_and_proofs_sum_once(self):
        code, data = self.parse(BASE + PARTS)
        self.assertEqual(code, 0)
        parts = data['last_breakdown']
        self.assertEqual([parts[k] for k in ('decoded_cpu_bytes','gpu_retained_est_bytes','encoded_bytes','alpha_proof_bytes')], [62,10,25,3])
        self.assertEqual(parts['total_bytes'], 100)

    def test_rejects_mismatch_missing_and_negative_parts(self):
        for changed in (PARTS.replace('ready_encoded=18','ready_encoded=19'),
                        PARTS.replace('idle_gpu_est=10','idle_gpu_est=9'),
                        PARTS.replace(' idle_proof=1',''),
                        PARTS.replace('idle_proof=1','idle_proof=-1'),
                        PARTS.replace('parts_version=1','parts_version=2')):
            with self.subTest(parts=changed):
                code, data = self.parse(BASE + changed)
                self.assertNotEqual(code, 0)
                self.assertTrue(data['errors'])

if __name__ == '__main__':
    unittest.main()
