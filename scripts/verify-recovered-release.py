"""Reject release builds that omit the tested recovered backend or enable probes."""
import argparse
import json
from pathlib import Path
import subprocess


def validate_metadata(info):
    if info.get('emote_backend') != 'recovered' or info.get('emote_default_backend') != 2:
        raise ValueError('Release must default to the recovered E-mote backend')
    runtime = info.get('recovered_runtime', {})
    for key, size in [('source_commit', 40), ('manifest_sha256', 64)]:
        value = runtime.get(key, '')
        if len(value) != size or any(c not in '0123456789abcdef' for c in value):
            raise ValueError(f'Missing recovery provenance: {key}')
    # Device acceptance is separate from reproducible source provenance.
    # Legacy records with a real device hash remain valid; an empty hash must
    # explicitly state that no device validation was recorded.
    device_hash = runtime.get('validated_device_self_sha256', '')
    device_status = runtime.get('device_validation', 'validated')
    if device_status == 'not_recorded':
        if device_hash != '':
            raise ValueError('Unvalidated recovery runtime must not claim a device hash')
    elif device_status == 'validated':
        if len(device_hash) != 64 or any(c not in '0123456789abcdef' for c in device_hash):
            raise ValueError('Missing recovery provenance: validated_device_self_sha256')
    else:
        raise ValueError('Unknown recovery device validation status')
    if len(info.get('core_commit', '')) != 40:
        raise ValueError('Missing core revision')


def verify(build, sdk):
    info = json.loads((build / 'build-info.json').read_text())
    validate_metadata(info)
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            cache[key.split(':')[0]] = value
    expected = dict(CMAKE_BUILD_TYPE='Release', DIRECT_RECOVERED_PACKET='ON',
                    DIRECT_RECOVERED_FREEZE_PROBE='OFF', DIRECT_BUILTIN_PROBE='OFF',
                    DIRECT_GXM_PROBE='OFF', DIRECT_BUILTIN_EFFECTS='ON',
                    DIRECT_CAPUNLOCKER='ON', DIRECT_DEFERRED_FINISH_CANDIDATE='ON',
                    DIRECT_RESOURCE_LEDGER='ON', DIRECT_SEMANTIC_CONTROLS='ON',
                    DIRECT_TEXT_EPOCH_CANDIDATE='ON', DIRECT_HEAP_DIAGNOSTICS='ON')
    for key, value in expected.items():
        if cache.get(key) != value:
            raise ValueError(f'Untested release configuration: {key}={cache.get(key)}')
    symbols = subprocess.check_output([str(sdk / 'bin/arm-vita-eabi-nm'), '--defined-only', str(build / 'art3m1s_direct')], text=True)
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {'art3m1s_recovery_create_prepared', 'art3m1s_recovery_capture_begin',
                'art3m1s_recovery_command', 'model_controls_seed', 'timeline_finish_playback'}
    if not required.issubset(names):
        raise ValueError('Recovered runtime is absent from linked ELF: ' + ', '.join(sorted(required - names)))
    print('Verified recovered default, release configuration and linked runtime symbols')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    args = parser.parse_args()
    verify(args.build, args.sdk)
