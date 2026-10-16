"""Verify same-stage old/prebound GPU pixel pairs, independently of parent equivalence.

The ordinary probe oracle compares different parent depths and remains separate:
passing this check does not resolve existing transparent soft-mask parent errors.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device', type=Path)
    args = parser.parse_args()
    folder = args.device.resolve()
    raw = (folder / 'result.log').read_bytes()
    log = raw.decode('utf-8', errors='replace')
    manifest = json.loads((folder.parent / 'build-verification.json').read_text())
    assert manifest.get('prebound_pixel_pairs') is True
    pairs = [(int(a), int(b)) for a, b in re.findall(
        r'^prebound-pixels stage=(\d+) different_bytes=(\d+)$', log, re.M)]
    assert pairs == [(stage, 0) for stage in range(12)], pairs
    assert 'COMPLETE ok=1 stages=13 GPU-drained=1' in log
    assert 'unsupported-opacity-recovery=1' in log
    captures = {}
    for stage in range(13):
        path = folder / f'stage-{stage:02}.rgba'
        data = path.read_bytes()
        assert len(data) == 960 * 544 * 4, path
        captures[path.name] = hashlib.sha256(data).hexdigest()
    oracle_path = folder / 'pixel-verification.json'
    oracle = json.loads(oracle_path.read_text()) if oracle_path.exists() else None
    report = {
        'status': 'pass_fenced_vs_queued_packets' if manifest.get('queued_pixel_pairs') else 'pass_same_stage_old_vs_prebound_only',
        'scope': '12 synthetic affine/soft/parent-depth pairs; RGBA byte equality on device outside the explicitly recorded overlay exclusion; queued variant includes repeated masked body/main portrait handoffs' if manifest.get('queued_pixel_pairs') else '12 synthetic affine/soft/parent-depth pairs; RGBA byte equality on device outside the explicitly recorded overlay exclusion',
        'not_covered': 'Real PSB raster equivalence; different parent depths' if manifest.get('queued_pixel_pairs') else 'Real PSB raster equivalence; full-host queued mode; different parent depths',
        'excluded_pixel_rect': manifest.get('excluded_pixel_rect'),
        'pair_different_bytes': dict(pairs),
        'probe_build': manifest,
        'log_sha256': hashlib.sha256(raw).hexdigest(),
        'capture_sha256': captures,
        'separate_parent_oracle': oracle,
    }
    destination = folder / 'prebound-pixel-verification.json'
    destination.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(report['status'])


if __name__ == '__main__':
    main()
