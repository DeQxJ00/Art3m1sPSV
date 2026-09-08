import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('conversion', Path(__file__).with_name('convert-ui-tables.py'))
conversion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(conversion)


class ConversionTests(unittest.TestCase):
    def test_pixel_fields_and_script_values(self):
        prefix = 'init={\ngame_scale={1280,720},\n},\n'
        source = prefix + 'ui_title={\n[1]="",\nstart={x=557,y=596,w=111,h=112,com="btn",p2="200",clip_a="0,114,111,112",time=200},\n},\n'
        result, rows = conversion.convert(source, .75)
        self.assertTrue(result.startswith(prefix))
        self.assertIn('x=417,y=447,w=83,h=84', result)
        self.assertIn('p2="200",clip_a="0,85,83,84",time=200', result)
        self.assertEqual(rows, 1)

    def test_scale_percent_and_slider_extent(self):
        source = 'ui_test={\na={x=100,y=100,w=100,h=100,com="obj3"},\nb={w=200,p2="20",com="xslider",area="0,0,200,40"},\n},\n'
        result, _ = conversion.convert(source, .5)
        self.assertIn('x=50,y=50,w=100,h=100', result)
        self.assertIn('w=100,p2="10",com="xslider",area="0,0,100,20"', result)

    def test_reject_unknown_layout(self):
        with self.assertRaises(ValueError):
            conversion.convert('init={x=12}', .5)


if __name__ == '__main__':
    unittest.main()
