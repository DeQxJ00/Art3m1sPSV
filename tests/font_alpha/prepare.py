"""Prepare a standalone font fixture; never extract into an installed game."""
import argparse
from pathlib import Path
import shutil

p = argparse.ArgumentParser()
p.add_argument('--font', required=True, type=Path, help='Local TTF or OTF containing the test glyphs')
p.add_argument('--output', type=Path, default=Path('build/font-alpha/fixture'))
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
shutil.copyfile(a.font, a.output/'font.otf')
shutil.copyfile(Path(__file__).with_name('probe.iet'), a.output/'probe.iet')
(a.output/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=probe.iet\nSAVEPATH=savedata\n')
(a.output/'platform.txt').write_text('VITA\n')
(a.output/'title.txt').write_text('TEST ALPHA FONT ATLAS\n')
print(a.output.resolve())
