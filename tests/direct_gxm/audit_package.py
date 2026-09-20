"""Audit the final linked host, dependency files and installed eboot identity."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile

root=Path(__file__).resolve().parents[2]
build=root/'build/direct-host'
nm=sys.argv[1]
symbols=subprocess.check_output([nm,'-C',str(build/'art3m1s_direct')],text=True)
(build/'symbols.txt').write_text(symbols)
forbidden=re.compile(r'brls::|\bnvg[A-Z]|\bnvgxm|nanovg|borealis|vitagl|\bvgl[A-Z]',re.I)
symbol_matches=[s for s in symbols.splitlines() if forbidden.search(s)]
deps=list((build/'CMakeFiles/art3m1s_direct.dir').rglob('*.obj.d'))
dependency_matches=[str(p) for p in deps if re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',p.read_text(),re.I)]
link=(build/'CMakeFiles/art3m1s_direct.dir/link.txt').read_text()
assert not symbol_matches and not dependency_matches
assert not re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',link,re.I)
digest=lambda b:hashlib.sha256(b).hexdigest().upper()
vpk=build/'art3m1s_direct.vpk'
with zipfile.ZipFile(vpk) as z:
    entries=z.namelist()
    assert not any(re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',p,re.I) for p in entries)
    eboot=digest(z.read('eboot.bin'))
    assert 'licenses/STB.txt' in entries and 'assets/menu.ttf' in entries
    installed=Path(sys.argv[2]) if len(sys.argv)>2 else None
    if installed:assert digest(installed.read_bytes())==eboot
report=dict(vpk_sha256=digest(vpk.read_bytes()),eboot_sha256=eboot,
            installed_eboot_matches=bool(installed),forbidden_symbols=symbol_matches,
            forbidden_dependencies=dependency_matches,dependency_files_checked=len(deps),
            package_entries=entries)
(build/'package-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
