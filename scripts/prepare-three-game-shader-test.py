"""Audit original PFS shaders and generate isolated Vita acceptance fixtures.

Never writes to a source game, executes a game EXE, or changes shader bytes.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import runpy
import shutil
import math
import zipfile
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/three-game-shaders'
NAMES = ['toshiue-kanojo2', 'Stella_of_the_End_PC', 'Stella_of_the_End_Android']
archive = runpy.run_path(str(ROOT / 'scripts/prepare-native-command-tests.py'))
converter = runpy.run_path(str(ROOT / 'scripts/compile-external-shaders.py'))
gallery = runpy.run_path(str(ROOT / 'scripts/prepare-shader-gallery.py'))
IDS = ['TEST_SHADER_TOSHIUE_REAL', 'TEST_SHADER_STELLA_PC_REAL', 'TEST_SHADER_STELLA_ANDROID_REAL']


def audit(source):
    known = {r['source_hash'] for r in json.loads((ROOT / 'shaders/artemis-pc/manifest.json').read_text())}
    rows = []
    for game in NAMES:
        entries = archive['index'](source / game)
        for name in sorted(entries):
            if not name.startswith('system/shader/') and not re.fullmatch(r'system/table/list_[^/]+\.tbl', name):
                continue
            dest = OUT / 'originals' / game / name
            assert dest.resolve().is_relative_to((OUT/'originals'/game).resolve()), name
            item = archive['extract'](entries, name, dest)
            if not name.startswith('system/shader/'):
                continue
            data = dest.read_bytes()
            digest = converter['fnv64'](data)
            row = dict(game=game, file=name, **{k: v for k, v in item.items() if k != 'path'},
                       source_hash=digest, builtin=digest in known)
            try:
                cg, meta = converter['convert'](data)
                row.update(conversion='supported', uniforms=meta['uniforms'])
            except (ValueError, UnicodeError) as e:
                row.update(conversion='unsupported', reason=str(e))
            rows.append(row)
        group = [r for r in rows if r['game'] == game]
        print(game, json.dumps(dict(files=len(group), builtin=sum(r['builtin'] for r in group),
              nonbuiltin=sum(not r['builtin'] for r in group),
              convertible_nonbuiltin=sum(not r['builtin'] and r['conversion'] == 'supported' for r in group))), flush=True)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'inventory.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
    return rows


def reference(name, params):
    pattern = gallery['pattern']
    def sample(x, y):
        x, y = max(0., min(399., x)), max(0., min(259., y))
        a, b = int(x), int(y)
        tx, ty = x-a, y-b
        pixels = [pattern(a, b), pattern(min(a+1,399), b),
                  pattern(a, min(b+1,259)), pattern(min(a+1,399), min(b+1,259))]
        return [(p[0]*(1-tx)+p[1]*tx)*(1-ty)+(p[2]*(1-tx)+p[3]*tx)*ty for p in zip(*pixels)]
    def pixel(x, y):
        p = list(pattern(x,y))
        if name == 'blend': p[3] = sum(p[:3])/3
        elif name == 'blend2': p[3] = 255-p[0] if p[0]/255 > .78 else 255
        elif name == 'radial':
            ax, ay, size, fade = [float(params[k]) for k in ('ax','ay','size','fade')]
            u, v = (x+.5)/400, (y+.5)/260
            dx, dy = u-ax, v-ay
            factor = size/8*math.hypot(dx,dy)
            p = [c*(1/8+7/8*(1-fade)) for c in p]
            for i in range(1,8):
                value = sample((dx*(1-factor*i)+ax)*400-.5, (dy*(1-factor*i)+ay)*260-.5)
                p = [a+b*fade/8 for a,b in zip(p,value)]
        return tuple(max(0,min(255,round(c))) for c in p)
    return pixel


def generate(rows):
    font = ROOT/'build/native-command-port/originals/otomeriron/sourcehansans-bold.otf'
    def wsl(path):
        path=path.resolve().as_posix()
        return '/mnt/'+path[0].lower()+path[2:]
    subprocess.run(['wsl','-d','Ubuntu-24.04','--','python3','-m','fontTools.subset',
                    wsl(font),'--unicodes=U+0020-007E',
                    '--text=左：CPU参考效果右：PSV着色器效果两边一致表示通过，不是原图与效果图对比原图左右对比观察变化圆圈下一项本页为无变化对照',
                    '--output-file='+wsl(OUT/'ascii.otf')],check=True)
    manifests = []
    for game, game_id in zip(NAMES, IDS):
        dest = OUT / 'games' / game_id
        (dest / 'assets').mkdir(parents=True, exist_ok=True)
        (dest / 'system').mkdir(exist_ok=True)
        subset = [r for r in rows if r['game'] == game and not r['builtin']]
        controls = [r for r in rows if r['game'] == game and r['file'] == 'system/shader/pc/reset.hlsl']
        for r in subset+controls:
            target = dest / r['file']; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(OUT/'originals'/game/r['file'], target)
            assert hashlib.sha256(target.read_bytes()).hexdigest() == r['sha256']
        gallery['png'](dest/'assets/pattern.png', gallery['pattern'])
        gallery['png'](dest/'assets/checker.png', lambda x,y: (68,79,96,255) if (x//16+y//16)%2 else (39,50,65,255))
        shutil.copyfile(OUT/'ascii.otf', dest/'assets/probe.otf')
        cases = [dict(name='reset', shader='control_reset', file=controls[0]['file'], params={}, alpha=255, builtin=True)]
        for r in subset:
            if r['conversion'] != 'supported': continue
            name = Path(r['file']).stem
            assert name in {'blend','blend2','radial'}, 'Add independent reference/parameter cases for '+r['file']
            r['test_shader_id'] = 'real_'+name
            variants = [dict(ax='0.5',ay='0.5',size='0',fade='1'),
                        dict(ax='0.35',ay='0.65',size='0.7',fade='0.8')] if name == 'radial' else [{}]
            for params in variants:
                for alpha in [255,128]:
                    cases.append(dict(name=name,shader='real_'+name,file=r['file'],params=params,alpha=alpha,builtin=False))
        lines = ['*top','[debug mode=1 level=3]', '[lua]',
                 'function getGameMode() return "adv" end',
                 'function decide(e,p) e:overrideKey{key=124,status=32} end', '[/lua]',
                 '[keyconfig role=0 keys=124]', '[setonpush key=13 handler=calllua function=decide]',
                 '[fontdefault face="assets/probe.otf" size=22 color=ffffff show=none]',
                 '[debugprint data="REAL-SHADER BEGIN platform=VITA"]']
        for i,r in enumerate(subset):
            sid = r.get('test_shader_id',f'reject_{i:03}')
            lines += [f'[debugprint data="REAL-SHADER REQUEST id={sid} file={r["file"]}"]',
                      f'[lyshader id={sid} file="{r["file"]}"]']
        lines += ['[lyshader id=control_reset file="system/shader/pc/reset.hlsl"]',
                  '[lyc id=0 width=960 height=544 color=182334]',
                  '[lyc id=1 file="assets/checker.png"]', '[lyprop id=1 left=40 top=140]',
                  '[lyc id=2 file="assets/checker.png"]', '[lyprop id=2 left=520 top=140]']
        text = gallery['text']
        text(lines,'label_left',40,100,400,32,'左：CPU参考效果')
        text(lines,'label_right',520,100,400,32,'右：PSV着色器效果')
        text(lines,'footer',40,465,880,70,'两边一致表示通过，不是原图与效果图对比',20)
        pages=[]
        for i,c in enumerate(cases,1):
            c['index']=i
            c['capture']=f'real-{i:02}.png'
            ref = f'assets/reference-{i:02}.png'
            gallery['png'](dest/ref,reference(c['name'],c['params']))
            page=['[lydel id=10]', '[lydel id=11]', f'[lyc id=10 file="{ref}"]',
                  f'[lyprop id=10 left=40 top=140 alpha={c["alpha"]}]',
                  '[lyc id=11 file="assets/pattern.png"]']
            args=' '.join(f'{k}="{v}"' for k,v in c['params'].items())
            page += [f'[lyprop id=11 left=520 top=140 alpha={c["alpha"]} shader={c["shader"]} shaderconstant="{",".join(c["params"])}" {args}]']
            text(page,'heading',40,24,880,60,f'{game_id} {i}/{len(cases)} {c["name"]}',22)
            text(page,'params',40,415,880,40,f'alpha={c["alpha"]} '+args.replace('"',''),20)
            page += ['[trans time=0]']
            manual=page[:-1].copy()
            manual[2]='[lyc id=10 file="assets/pattern.png"]'
            text(manual,'label_left',40,100,400,32,'左：原图')
            text(manual,'footer',40,465,880,70,
                 '本页为reset无变化对照，圆圈下一项' if c['builtin'] else '左右对比观察效果变化，圆圈下一项',20)
            manual+=['[trans time=0]']
            pages.append(manual)
            lines += page + ['[wait time=700 input=0]', '[takess]',
                             f'[savess file="{c["capture"]}" width=960 height=544]',
                             f'[debugprint data="REAL-SHADER CAPTURE page={i} effect={c["name"]} alpha={c["alpha"]}"]']
        lines += [f'[debugprint data="REAL-SHADER DONE game={game_id} pages={len(cases)}"]', '*manual']
        # Enter the first nontrivial effect immediately after automatic checks.
        # CPU reference captures remain separate from the user-facing original.
        display_pages=pages[1:]+pages[:1] if len(pages)>1 else pages
        for i,page in enumerate(display_pages):
            lines += page
            if i==0:
                lines += ['[wait time=500 input=0]','[takess]',
                          '[savess file="manual-first.png" width=960 height=544]',
                          '[debugprint data="REAL-SHADER MANUAL original-vs-psv"]']
            lines += ['[@]']
        lines += ['[jump label=manual]']
        (dest/'system/first.iet').write_text('\n'.join(lines)+'\n',encoding='utf-8')
        (dest/'platform.txt').write_text('vita\n')
        (dest/'title.txt').write_text(game_id+'\n')
        (dest/'system.ini').write_text('[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n')
        manifest=dict(game=game,id=game_id,platform='VITA',nonbuiltin=subset,cases=cases,
                      expected_compiles=sum(r['conversion']=='supported' for r in subset),
                      expected_rejects=sum(r['conversion']!='supported' for r in subset),
                      expected_hlsl_failures=sum(r['conversion']!='supported' and r['file'].lower().endswith('.hlsl') for r in subset),
                      expected_skipped=sum(not r['file'].lower().endswith('.hlsl') for r in subset),
                      expected_hlsl_total=1+sum(r['file'].lower().endswith('.hlsl') for r in subset),
                      builtin_control=controls)
        (dest/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
        (dest/'README.txt').write_text(
            f'{game_id}\nSource: {game}\nPlatform: VITA\n'
            f'Non-builtin DX9 shaders eligible for compilation: {manifest["expected_compiles"]}\n'
            f'Unsupported HLSL failures: {manifest["expected_hlsl_failures"]}; other formats counted as skipped: {manifest["expected_skipped"]}.\n'
            'Automatic checks: left is CPU reference, right is Vita shader; matching is expected.\n'
            'Manual display: left is ORIGINAL IMAGE, right is Vita shader. Reset is a BUILTIN no-change control.\n'
            'After the automatic captures, the first nontrivial original/effect page appears. Circle advances. Square opens the exit menu.\n'
            'For a fresh installation, enable automatic shader conversion and compilation in launcher settings.\n'
            'After generating valid caches on the Vita, both options can be disabled.\n'
            'Requires libshacccg for compilation. GLSL is skipped; unsupported DX11 HLSL remains a failure. Neither is a successful effect.\n'
            'Caches belong to this TEST directory, under shader-cache plus the original virtual source path.\n'
            'Do not replace original game resources with this demo.\n',encoding='utf-8')
        manifests.append(manifest)
        print(game_id, 'pages',len(cases),'compiles',manifest['expected_compiles'],
              'HLSL failures',manifest['expected_hlsl_failures'],'skipped',manifest['expected_skipped'],flush=True)
    (OUT/'manifest.json').write_text(json.dumps(manifests,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(OUT/'three-game-shader-demo.zip','w',zipfile.ZIP_DEFLATED) as z:
        for f in sorted((OUT/'games').rglob('*')):
            if f.is_file(): z.write(f,'ux0/data/art3m1s-gxm/games/'+f.relative_to(OUT/'games').as_posix())


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--games', type=Path, default=Path('E:/EmuGame/vita3k_data/ux0/data/art3m1s-gxm/games'))
    a = p.parse_args()
    generate(audit(a.games))
