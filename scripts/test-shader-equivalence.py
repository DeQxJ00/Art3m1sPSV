"""Guard semantic deduplication and original shader filenames."""
from pathlib import Path
import json
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]
tokens = runpy.run_path(str(ROOT / 'scripts/bundle-artemis-shaders.py'))['tokens']
schema = runpy.run_path(str(ROOT / 'scripts/shader-effect-schema.py'))


class ShaderEquivalence(unittest.TestCase):
    def test_equivalence_requires_formula_and_parameter_layout(self):
        uniforms = [dict(name='alpha', offset=0, count=1)]
        signature = schema['signature']
        self.assertEqual(signature('x *= alpha;', uniforms), signature('x /* comment */ *= alpha ;', uniforms))
        self.assertNotEqual(signature('x *= alpha;', uniforms), signature('x.a *= alpha;', uniforms))
        self.assertNotEqual(signature('x *= alpha;', uniforms), signature('x *= alpha;', [dict(name='alpha', offset=1, count=1)]))
        with self.assertRaises(ValueError):
            schema['parameters'](uniforms + [dict(name='gain', offset=0, count=1)])

    def test_equivalence_table_is_bound_to_current_implementation(self):
        directory = ROOT / 'shaders/psv'
        table = json.loads((directory / 'equivalences.json').read_text())
        descriptors = json.loads((directory / 'parameters.json').read_text())
        signatures = {d['implementation']: d['signature'] for d in descriptors['effects']}
        self.assertEqual(len(signatures), 33)
        for entry in table['matches']:
            self.assertEqual(entry['signature'], signatures[entry['implementation']])
        self.assertNotIn('8922fda15fc0d26d', {e['source_hash'] for e in table['matches']})

    def test_scalar_zero_initializes_all_vector_components(self):
        for size in (2, 3, 4):
            explicit = f'float{size} value = float{size}(' + ', '.join(['0.0'] * size) + ');'
            self.assertEqual(tokens(f'float{size} value = 0;'), tokens(explicit))
            self.assertEqual(tokens(f'float{size} value = 0.0;'), tokens(explicit))
            self.assertNotEqual(tokens(f'float{size} value = 1;'), tokens(explicit))

    def test_rgba_and_alpha_only_scaling_remain_distinct(self):
        self.assertNotEqual(tokens('fore *= alpha;'), tokens('fore.a *= alpha;'))
        self.assertNotEqual(tokens('x = a + +b;'), tokens('x = a++ + b;'))

    def test_radial_source_aliases_share_one_package(self):
        catalog = json.loads((ROOT / 'shaders/psv/catalog.json').read_text())
        entries = {e['source_hash']: e for e in catalog}
        first, second = entries['c722ff985d33cf48'], entries['19b3c54a5762960f']
        self.assertEqual(first['package'], 'radial.hlsl.agxp')
        for field in ('package', 'cg', 'canonical_hash', 'implementation'):
            self.assertEqual(first[field], second[field])

    def test_mobile_variants_are_not_mislabeled_as_exact_pc_matches(self):
        catalog = json.loads((ROOT / 'shaders/psv/catalog.json').read_text())
        hashes = {e['source_hash'] for e in catalog}
        for fingerprint in ('8922fda15fc0d26d', 'e3cc473c9c51a4e0',
                            '7acb774438a9f2a0', '10eb5f1441928ab8'):
            self.assertNotIn(fingerprint, hashes)
        fallback = json.loads((ROOT / 'shaders/psv/fallbacks.json').read_text())
        self.assertEqual(fallback['mb/gray.glsl'], 'gray')
        self.assertEqual(fallback['mb/sepia.glsl'], 'sepia')

    def test_original_basenames_and_unique_runtime_program_ids(self):
        catalog = json.loads((ROOT / 'shaders/psv/catalog.json').read_text())
        canonical = [e for e in catalog if e['source_hash'] == e['canonical_hash']]
        self.assertEqual(len(canonical), 33)
        self.assertEqual(len({e['implementation'] for e in canonical}), len(canonical))
        for e in canonical:
            self.assertEqual(Path(e['cg']).parent, Path('.'))
            self.assertEqual(Path(e['cg']).stem, e['effect'])
            self.assertEqual(Path(e['package']).name.split('.')[0], e['effect'])


if __name__ == '__main__':
    unittest.main()
