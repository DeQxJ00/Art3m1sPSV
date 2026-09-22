"""Build a small local regression fixture; never unpack into an installed game.

Usage: python scripts/prepare-intermediate-coverage-test.py PATH_TO_OTOMERIRON_PFS_DIR
Output: temp/otomeriron-gray-background/fixture/TEST_GRAY_COVERAGE
No EXE execution, emulator deployment, PFS writes, or automatic gameplay.
"""
from pathlib import Path
import json
import runpy
import shutil
import sys

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).resolve()
out = root / "temp/otomeriron-gray-background/fixture/TEST_GRAY_COVERAGE"
if source == out.resolve() or source in out.resolve().parents:
    raise SystemExit("The source must be outside the fixture output tree")
helpers = runpy.run_path(str(root / "scripts/prepare-native-command-tests.py"))
entries = helpers["index"](source)
out.mkdir(parents=True, exist_ok=True)
manifest = []
for name, dest in [
    ("image/bg/bg01fs_s.png", "bg.png"),
    ("image/bg/bg02as_w.png", "bg2.png"),
    ("image/fg/asa/z1/asa_z1a0500.png", "fg.png"),
    ("image/cg/回想_白枠.png", "vignette.png"),
]:
    manifest.append(helpers["extract"](entries, name, out / dest))
(out / "provenance.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf8")
shutil.copyfile(root / "tests/direct_gxm/fixtures/intermediate_coverage.iet", out / "probe.iet")
(out / "title.txt").write_text("Temporary grayscale coverage regression\n", encoding="utf8")
section = "WIDTH=960\nHEIGHT=540\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n"
(out / "system.ini").write_text("[VITA]\n" + section + "[WINDOWS]\n" + section, encoding="utf8")
print(out)
