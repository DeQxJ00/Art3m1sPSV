"""Generate the grayscale cache-admission regression without commercial assets."""
from pathlib import Path
import importlib.util
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gray-cache-test/fixture'


def main():
    spec = importlib.util.spec_from_file_location('mosaic_fixture', ROOT / 'scripts/prepare-mosaic-perf-test.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.OUT = OUT
    module.main()
    shutil.copyfile(ROOT / 'tests/direct_gxm/fixtures/grayscale_entry.iet', OUT / 'probe.iet')
    shutil.copyfile(ROOT / 'host-direct/assets/TEST_SHADERS_51/sources/shared/system/shader/pc/gray.hlsl', OUT / 'gray.hlsl')
    (OUT / 'title.txt').write_text('TEST GRAY CACHE\n')


if __name__ == '__main__':
    main()
