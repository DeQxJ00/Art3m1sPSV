"""Check HLSL-only device progress and original/effect demo (numpy/Pillow)."""
from pathlib import Path
import hashlib
import json
import re
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'backup/legacy-build/shader-progress-hlsl'
FIXTURES = ROOT / 'backup/legacy-build/three-game-shaders'
REGIONS = [(40, 140, 440, 400), (520, 140, 920, 400)]


def pixels(path, box):
    with Image.open(path) as im:
        return np.asarray(im.convert('RGB').crop(box)).copy()


def main():
    results = []
    manifests = json.loads((FIXTURES / 'manifest.json').read_text(encoding='utf-8'))
    for m in manifests:
        directory = OUT / 'device/progress' / m['id']
        log = (directory / 'host.log').read_text(encoding='utf-8', errors='replace')
        batches = re.findall(r'\[shader-progress\] done=(\d+) total=(\d+) failed=(\d+) skipped=(\d+) stage=10', log)
        expected = [m['expected_hlsl_total'], m['expected_hlsl_total'],
                    m['expected_hlsl_failures'], m['expected_skipped']]
        errors = [line for line in log.splitlines() if '[shader]' in line and '[core:E]' in line]
        error_paths = set(re.findall(r'file=([^ ]+):', '\n'.join(errors)))
        skipped = re.findall(r'\[shader\] skipped non-HLSL id=\S+ file=(\S+)', log)
        expected_errors = {r['file'] for r in m['nonbuiltin']
                           if r['file'].lower().endswith('.hlsl') and r['conversion'] != 'supported'}
        expected_skips = {r['file'] for r in m['nonbuiltin'] if not r['file'].lower().endswith('.hlsl')}
        images = []
        for case in m['cases']:
            baseline = FIXTURES / 'device/cold' / m['id'] / case['capture']
            equal = all(np.array_equal(pixels(directory / case['capture'], box), pixels(baseline, box)) for box in REGIONS)
            images.append({'capture': case['capture'], 'effect_regions_equal_to_baseline': equal})
        caches = json.loads((directory / 'cache.json').read_text())
        old_caches = json.loads((FIXTURES / 'device/cold' / m['id'] / 'cache.json').read_text())
        result = dict(id=m['id'], final_batch=list(map(int, batches[-1])) if batches else None,
                      expected_batch=expected, rejected_hlsl_paths_match=error_paths == expected_errors,
                      errors=len(errors), skipped_paths_match=set(skipped) == expected_skips,
                      skipped=len(skipped), cache_hits=log.count('[shader-cache] hit id='),
                      cache_artifacts_unchanged=caches == old_caches, images=images,
                      startup_passed='startup_validation=1' in log,
                      platform_vita=f'id={m["id"]} platform=VITA' in log,
                      log_sha256=hashlib.sha256((directory / 'host.log').read_bytes()).hexdigest())
        result['passed'] = (result['final_batch'] == expected and result['rejected_hlsl_paths_match']
                            and result['errors'] == m['expected_hlsl_failures'] and result['skipped_paths_match']
                            and result['skipped'] == m['expected_skipped']
                            and result['cache_hits'] == m['expected_compiles'] and result['cache_artifacts_unchanged']
                            and result['startup_passed'] and result['platform_vita']
                            and all(i['effect_regions_equal_to_baseline'] for i in images))
        manual = directory / 'manual-first.png'
        if 'STELLA_PC' in m['id']:
            assert 'REAL-SHADER MANUAL original-vs-psv' in log and manual.exists()
            left, right = [pixels(manual, box) for box in REGIONS]
            original = pixels(directory / 'real-01.png', REGIONS[0])
            effect = pixels(directory / 'real-02.png', REGIONS[1])
            result['manual'] = dict(left_matches_original=bool(np.array_equal(left, original)),
                                    right_matches_shader=bool(np.array_equal(right, effect)),
                                    changed_pixels=int(np.any(left != right, axis=2).sum()))
            result['passed'] &= (result['manual']['left_matches_original'] and result['manual']['right_matches_shader']
                                 and result['manual']['changed_pixels'] > 1000)
        results.append(result)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    for result in results:
        print(json.dumps({k: v for k, v in result.items() if k != 'images'}))
    assert len(results) == 3 and all(r['passed'] for r in results)


if __name__ == '__main__':
    main()
