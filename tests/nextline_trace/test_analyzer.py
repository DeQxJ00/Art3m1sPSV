import importlib.util
import json
from pathlib import Path
import unittest

script = Path(__file__).resolve().parents[2] / "scripts/analyze-nextline-log.py"
spec = importlib.util.spec_from_file_location("analyzer", script)
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)

HEADER = "08:00:01 [INFO] [nextline-trace] logic_max_us=20000 atlas_prepare_max_us=1000 tick_over16ms=1"
ATLAS = "08:00:01 [INFO] [nextline-atlas] updates=1 bytes=400 wait_us=500 wait_max_us=500 copy_us=30"
CORE = "08:00:01 [INFO] [nextline-core] " + json.dumps({
    "enabled": True, "sample_window_ms": 10000, "sample_count": 1200,
    "one_percent": {"event_text_ms": 2.5}, "maximum": {"event_text_ms": 999}})
END = "08:00:01 [INFO] [nextline-trace] report_cost_us=350; exclude reporting from gameplay cost"


class AnalyzerTests(unittest.TestCase):
    def test_units_tail_semantics_and_missing_fields(self):
        data = analyzer.parse_log("\n".join((HEADER, ATLAS, CORE, END)), "hardware")
        self.assertTrue(data["reports"][0]["complete"])
        self.assertEqual(data["reports"][0]["core"]["one_percent_ms"]["event_text_ms"], 2.5)
        self.assertIsNone(data["reports"][0]["core"]["one_percent_ms"]["interpreter_ms"])
        report = analyzer.markdown(data)
        self.assertIn("20.000 | 1.000 | 0.500 | 2.500 | —", report)
        self.assertNotIn("999", report)

    def test_truncated_report_does_not_inherit_prior_core(self):
        data = analyzer.parse_log("\n".join((HEADER, ATLAS, CORE, END, HEADER, ATLAS,
                                              '[nextline-core] {"enabled":')), "emulator")
        self.assertEqual([r["complete"] for r in data["reports"]], [True, False])
        self.assertNotIn("core", data["reports"][1])
        self.assertEqual(len(data["issues"]), 1)

    def test_orphan_and_invalid_schema(self):
        for fragment in (CORE, HEADER + '\n[nextline-core] []'):
            data = analyzer.parse_log(fragment, "unknown")
            self.assertEqual(len(data["issues"]), 1)

    def test_restarted_session_has_no_cross_session_attachment(self):
        data = analyzer.parse_log(HEADER + '\n[nextline-trace] enabled; diagnostic\n' + CORE, "emulator")
        self.assertFalse(data["reports"][0]["complete"])
        self.assertEqual(len(data["issues"]), 1)

    def test_no_diagnostic_data_is_not_a_pass(self):
        data = analyzer.parse_log("normal host log", "unknown")
        self.assertEqual(data["reports"], [])
        self.assertIn("报告 0 份", analyzer.markdown(data))


if __name__ == "__main__":
    unittest.main()
