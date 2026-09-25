"""Linux/WSL delayed GPU-read tests and targeted missing-guard controls.

Requires g++, VitaSDK headers and ASan/UBSan; never connects to a device.
"""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/deferred-finish'
out.mkdir(parents=True, exist_ok=True)
reports = root / 'temp/test-results/deferred-finish'
reports.mkdir(parents=True, exist_ok=True)
sdk = Path(os.environ['VITASDK']) / 'arm-vita-eabi/include'
gpu = (root / 'host-direct/src/gpu.cpp').read_text(encoding='utf-8')
fixture = (root / 'tests/direct_gxm/deferred_finish_test.cpp').read_text(encoding='utf-8')
results = []
for missing in (None, 'Begin', 'Update', 'Destroy', 'Readback'):
    name = 'complete' if missing is None else 'missing-' + missing.lower()
    source = gpu
    if missing:
        guard = f'finish_pending(WaitSite::{missing});'
        # Both RGBA and alpha-only updates now share the Update wait site.
        assert source.count(guard) >= 1
        source = source.replace(guard, '/* negative control: omitted wait */')
    path = out / (name + '-gpu.cpp')
    path.write_text(source, encoding='utf-8')
    test = out / (name + '.cpp')
    test.write_text(fixture.replace('../../host-direct/src/gpu.cpp', path.as_posix()), encoding='utf-8')
    binary = out / name
    command = ['g++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               '-ffunction-sections', '-fdata-sections', '-idirafter', str(sdk),
               '-I', str(root / 'host-direct/src'), '-I', str(root / 'host'),
               str(test), '-Wl,--gc-sections', '-o', str(binary)]
    with (reports / (name + '-build.log')).open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (reports / (name + '-run.log')).open('w') as log:
        result = subprocess.run([str(binary)], stdout=log, stderr=subprocess.STDOUT)
    text = (reports / (name + '-run.log')).read_text(encoding='utf-8')
    # A build/crash unrelated to the tested invariant must not count as success.
    passed = (result.returncode == 0 and 'DEFERRED_FINISH delayed_reads=' in text) if not missing else (
        result.returncode == -6 and 'Assertion' in text)
    results.append({'case': name, 'exit_code': result.returncode, 'passed': passed,
                    'output': text.strip()})
    print(json.dumps(results[-1]), flush=True)
    if not passed:
        break
(reports / 'mutation-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
assert len(results) == 5 and all(row['passed'] for row in results)
