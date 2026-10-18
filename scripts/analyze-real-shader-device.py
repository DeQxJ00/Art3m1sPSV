"""Check cold/warm real-source probe logs and pixels. Requires Pillow."""
import argparse
import json
from pathlib import Path
import re
from PIL import Image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    logs = {phase:(a.evidence/(phase+'.log')).read_text(encoding='utf-8', errors='replace') for phase in ('cold', 'warm')}
    cold, warm = logs['cold'], logs['warm']
    assert cold.count('[shader-compiler] id=') == 7
    assert cold.count('saved=1 elapsed_us=') == 7
    assert '[shader-compiler] id=' not in warm
    assert warm.count('[shader-cache] hit id=') == 7
    for log in logs.values():
        assert log.count('[shader-builtin] id=') == 5
        assert 'done=17 total=17 failed=5 skipped=0 stage=10' in log
        assert '[shader-fallback]' not in log
        for case in ('sample11', 'sample12'):
            assert f'id={case} implementation=radial ' in log
    stages = sorted({int(s) for s in re.findall(r'\[shader-progress\].*?stage=(\d+)', cold)})
    assert {4, 5, 6, 10, 11} <= set(stages)
    pixels = {}
    for mode in ('opaque', 'half', 'mask'):
        first = Image.open(a.evidence/('cold-'+mode+'.png')).convert('RGBA')
        second = Image.open(a.evidence/('warm-'+mode+'.png')).convert('RGBA')
        assert first.size == second.size == (960, 544)
        assert first.tobytes() == second.tobytes(), mode+' cache output changed'
        pixels[mode] = [first.getpixel((100+224*(i%4), 88+156*(i//4)))[:3] for i in range(12)]
        assert pixels[mode][10] == pixels[mode][11], mode+' radial aliases differ'
    opaque, half, mask = (pixels[k] for k in ('opaque', 'half', 'mask'))
    assert opaque[0] == opaque[1] == opaque[3] == (113, 113, 113)
    assert opaque[2] == (164, 164, 164)
    assert opaque[4] == opaque[5] == (135, 90, 56)
    assert opaque[6] == (122, 116, 113)
    assert opaque[7] == opaque[8] == opaque[9]
    assert half[7] == half[8] != half[9]
    assert mask[0] != mask[1] and mask[4] != mask[5] and mask[7] != mask[8]
    report = dict(cases=17, builtin=5, generated=7, expected_rejections=5,
                  warm_cache_hits=7, warm_compilations=0, cold_warm_pixels_identical=True,
                  startup_stages=stages, pixels=pixels,
                  cold_compile_us=[int(v) for v in re.findall(r'\[shader-compiler\] id=.*?elapsed_us=(\d+)', cold)],
                  warm_cache_us=[int(v) for v in re.findall(r'\[shader-cache\] hit id=.*?elapsed_us=(\d+)', warm)])
    a.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS: cold generation, warm reuse, expected rejects, progress stages and all pixel comparisons')


if __name__ == '__main__':
    main()
