"""Verify promoted effects bypass caches/compiler and preserve device pixels."""
from pathlib import Path
import hashlib
import json
import re
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'backup/legacy-build/stella-builtin'


def main():
    results = []
    for suffix in ['PC', 'ANDROID']:
        game = 'TEST_SHADER_STELLA_' + suffix + '_REAL'
        directory = OUT / 'device/builtin' / game
        log = (directory / 'host.log').read_text(encoding='utf-8', errors='replace')
        rows = re.findall(r'\[shader-builtin\] id=(\S+) implementation=(\S+)', log)
        mapping = dict(rows)
        captures = []
        for i in range(1, 10):
            name = f'real-{i:02}.png'
            baseline = ROOT / 'backup/legacy-build/shader-progress-hlsl/device/progress' / game / name
            with Image.open(directory / name) as actual, Image.open(baseline) as expected:
                equal = bool(np.array_equal(np.asarray(actual.convert('RGB')), np.asarray(expected.convert('RGB'))))
            captures.append(dict(file=name, full_pixels_equal_to_external=equal))
        cache = json.loads((directory / 'cache.json').read_text())
        old_cache = json.loads((ROOT / 'backup/legacy-build/shader-progress-hlsl/device/progress' / game / 'cache.json').read_text())
        result = dict(game=game, builtin=mapping,
                      shader_settings_disabled=(directory / 'shader-settings.txt').read_text().split()==['1','0','0'],
                      startup_passed='startup_validation=1' in log,
                      completed=f'REAL-SHADER DONE game={game} pages=9' in log,
                      compiler_calls=log.count('[shader-compiler]'),
                      cache_hits=log.count('[shader-cache] hit'),
                      old_cache_files_unchanged=cache == old_cache,
                      images=captures, log_sha256=hashlib.sha256((directory / 'host.log').read_bytes()).hexdigest())
        result['passed'] = (result['shader_settings_disabled'] and result['startup_passed'] and result['completed']
                            and result['compiler_calls'] == 0 and result['cache_hits'] == 0
                            and result['old_cache_files_unchanged']
                            and mapping.get('real_blend') == 'blend' and mapping.get('real_blend2') == 'blend2'
                            and mapping.get('real_radial') == 'radial_stella'
                            and all(c['full_pixels_equal_to_external'] for c in captures))
        results.append(result)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    for result in results:
        print(json.dumps(result))
    assert all(result['passed'] for result in results)


if __name__ == '__main__':
    main()
