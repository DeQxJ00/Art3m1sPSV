"""Prepare a startup batch (five new shaders + one expected rejection)."""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-external-shader-test.py')], check=True)
source = ROOT / 'temp/external-shaders/custom-physical/game'
output = ROOT / 'temp/shader-progress/game'
shutil.copytree(source, output, dirs_exist_ok=True)
script = output / 'system/first.iet'
lines = script.read_text(encoding='utf-8').splitlines()
shaders = [line for line in lines if line.startswith('[lyshader ')]
assert len(shaders) == 6
lines = [line for line in lines if not line.startswith('[lyshader ')]
# No waits/transitions before this batch: every registration is observable
# during startup, including the deliberately unsupported sixth request.
lines[2:2] = shaders
script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
(output / 'title.txt').write_text('TEST SHADER PROGRESS\n', encoding='utf-8')
print(output)
