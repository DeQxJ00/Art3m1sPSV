"""Audit only the isolated effects build, never the main host artifacts."""
import hashlib,json,re,subprocess,sys,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
build=Path(sys.argv[3]) if len(sys.argv)>3 else root/'build/shader-completion/host'
title_id=sys.argv[4] if len(sys.argv)>4 else 'ART3EFX01'
report_dir=Path(sys.argv[5]) if len(sys.argv)>5 else build.parent
symbols=subprocess.check_output([sys.argv[1],'-C',str(build/'art3m1s_direct')],text=True)
(report_dir/'symbols.txt').write_text(symbols,encoding='utf-8')
bad=re.compile(r'brls::|\bnvg[A-Z]|\bnvgxm|nanovg|borealis|vitagl|\bvgl[A-Z]',re.I)
assert not bad.search(symbols)
for symbol in ['art3m1s_gxm_draw_effect','art3m1s_gxm_group_begin','art3m1s_gxm_group_mask_begin','art3m1s_gxm_group_end']:
    assert re.search(r'\bT '+symbol+r'\b',symbols),symbol
assert 'native_effects::render' in symbols
link=(build/'CMakeFiles/art3m1s_direct.dir/link.txt').read_text()
assert not re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',link,re.I)
deps=list((build/'CMakeFiles/art3m1s_direct.dir').rglob('*.obj.d'))
assert all(not re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',p.read_text(),re.I) for p in deps)
sha=lambda b:hashlib.sha256(b).hexdigest().upper()
vpk=build/'art3m1s_direct.vpk'
with zipfile.ZipFile(vpk) as z:
    assert title_id.encode() in z.read('sce_sys/param.sfo')
    expected=sha(z.read('eboot.bin'))
    assert all(not re.search(r'borealis|nanovg|vendor[/\\]vitagl|(?:libvitagl\.a|vitagl\.h)|-lvitagl\b',p,re.I) for p in z.namelist())
    if len(sys.argv)>2:assert sha(Path(sys.argv[2]).read_bytes())==expected
    report=dict(vpk_sha256=sha(vpk.read_bytes()),eboot_sha256=expected,
        installed_matches=len(sys.argv)>2,linked_native_effects=True,no_borealis_or_nanovg=True,
        dependencies_checked=len(deps),entries=z.namelist())
(report_dir/'package-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
