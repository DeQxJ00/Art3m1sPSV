"""Generate an isolated, deduplicated 31-effect shader gallery; never modify real games."""
from pathlib import Path
import hashlib
import json
import math
import shutil
import struct
import subprocess
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/shader-gallery'
GAME = OUT / 'TEST_SHADERS_51'
DESCRIPTIONS = {
    'add': '加算混合', 'blur_h': '水平高斯模糊', 'blur_v': '垂直高斯模糊',
    'blur_k': '双向 Kawase 模糊', 'blur_kx': '水平 Kawase 模糊', 'blur_ky': '垂直 Kawase 模糊',
    'cadd': '颜色加算', 'cmul': '颜色乘算', 'compbr': '取亮混合', 'compbrc': '取亮混合变体',
    'compdk': '取暗混合', 'compdkc': '取暗混合变体', 'dimhole': '圆形挖空与扭曲',
    'dimover': '圆形扭曲与暗化', 'dimring': '圆环高亮', 'gray': '灰度', 'mosaic': '马赛克',
    'mul': '乘算混合', 'nega': '反色', 'noise': '噪声位移', 'radial': '径向模糊',
    'raster': '波浪位移', 'reset': '基础拷贝，预期与原图相同', 'rgb': '分通道调整',
    'screen': '滤色混合', 'sepia': '褐色调', 'sepia2': '褐色调变体',
    'trapezoid_dw': '向下梯形变换', 'trapezoid_lt': '向左梯形变换',
    'trapezoid_rt': '向右梯形变换', 'trapezoid_up': '向上梯形变换',
}
DEFAULTS = dict(param='0.7', weights='0.3,0.05,0.05,0.05,0.05,0.05,0.05,0.05',
                width='0.008', height='0.012', offset='1', size='0.04', ratio='0.65',
                centerx='0.5', centery='0.5', radius='0.2', thick='0.15', dark='0.6',
                ax='0.5', ay='0.5', fade='0.8', angle='30', inter='1440', steps='0.6',
                dost='0.01', red='0.2', green='-0.1', blue='0.1')
OVERRIDES = dict(cmul=dict(red='0.7', green='0.5', blue='1.1'),
                 rgb=dict(red='-0.2', green='0.1', blue='0.2'),
                 sepia=dict(red='1', green='0.8', blue='0.6'),
                 sepia2=dict(red='0.1', green='0.8', blue='0.6'),
                 mosaic=dict(size='16', ratio='1.53846'), radial=dict(size='0.5'),
                 raster=dict(size='0.08'))
MIX = {'add', 'mul', 'screen', 'compbr', 'compbrc', 'compdk', 'compdkc'}


def png(path, pixel):
    width, height = 400, 260
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    raw = b''.join(b'\0' + bytes(v for x in range(width) for v in pixel(x, y)) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def pattern(x, y):
    color = (int(x * 255 / 399), int(y * 255 / 259), 224 if (x // 16 + y // 16) % 2 else 32, 255)
    if y < 38:
        color = [(240, 40, 40, 255), (40, 230, 70, 255), (40, 80, 245, 255),
                 (245, 230, 50, 255), (235, 50, 220, 255), (50, 225, 230, 255)][min(x // 67, 5)]
    r = math.hypot(x - 200, y - 138)
    if abs(r - 74) < 3 or abs(x - 200) < 2 or abs(y - 138) < 2:
        color = (255, 255, 255, 255)
    if x < 32 and y > 220:
        color = color[:3] + (96,)
    return color


def text(lines, layer, x, y, width, height, content, size=24):
    lines.extend([f'[chgmsg id={layer} stack=0]', '[fontinit]',
                  f'[font left={x} top={y} width={width} height={height} size={size}]',
                  '[rp backlog=0]', f'[print data="{content}"]'])


def main():
    # Only clear this generated workspace output, never an installed game.
    assert GAME.resolve() == ROOT.resolve() / 'build/shader-gallery/TEST_SHADERS_51'
    if GAME.exists():
        shutil.rmtree(GAME)
    (GAME / 'assets').mkdir(parents=True, exist_ok=True)
    (GAME / 'system').mkdir(exist_ok=True)
    manifest = {e['name']: e for e in json.loads((ROOT / 'shaders/artemis-pc/manifest.json').read_text())}
    cases = []
    by_hash = {}
    source_count = 0
    for source_group, game, count in [('otomeriron', 'game1', 20), ('toshiue', 'game2', 31)]:
        files = sorted((ROOT / f'build/external-shaders/resources/{source_group}/system/shader/pc').glob('*.hlsl'))
        assert len(files) == count, (game, len(files))
        for source in files:
            data = source.read_bytes()
            fnv = 0xcbf29ce484222325
            for byte in data:
                fnv = ((fnv ^ byte) * 0x100000001b3) & 0xffffffffffffffff
            assert f'{fnv:016x}' == manifest[source.stem]['source_hash']
            source_count += 1
            digest = hashlib.sha256(data).hexdigest()
            origin = dict(game=game, file=f'system/shader/pc/{source.name}')
            if digest in by_hash:
                existing = by_hash[digest]
                assert (GAME / existing['file']).read_bytes() == data
                existing['origins'].append(origin)
                continue
            relative = f'sources/shared/system/shader/pc/{source.name}'
            dest = GAME / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            values = DEFAULTS | OVERRIDES.get(source.stem, {})
            params = {u['name']: values[u['name']] for u in manifest[source.stem]['uniforms']
                      if u['name'] not in ('alpha', 'colorMultiply', 'maskTransitionVague', 'maskTransitionStep')}
            cases.append(dict(index=len(cases) + 1, origins=[origin], name=source.stem,
                              shader_id=f'builtin_{source.stem}', file=relative, params=params,
                              sha256=digest))
            by_hash[digest] = cases[-1]
    assert source_count == 51 and len(cases) == 31 and sum(len(c['origins']) for c in cases) == 51
    png(GAME / 'assets/pattern.png', pattern)
    png(GAME / 'assets/checker.png', lambda x, y: ((68, 79, 96, 255) if (x // 16 + y // 16) % 2 else (39, 50, 65, 255)))
    png(GAME / 'assets/user.png', lambda x, y: (235 if x < 200 else 32, 70 if y < 130 else 220, 170, 160))
    lines = ['*top', '[debug mode=1 level=3]', '[lua]',
             'function getGameMode() return "adv" end',
             'function decide(e,p) e:overrideKey{key=124,status=32} end', '[/lua]',
             '[keyconfig role=0 keys=124]', '[setonpush key=13 handler=calllua function=decide]',
             '[fontdefault face="assets/probe.otf" size=24 color=ffffff show=none]',
             '[lyc id=0 width=960 height=544 color=182334]']
    for case in cases:
        lines.append(f'[lyshader id={case["shader_id"]} file="{case["file"]}"]')
    lines += ['[debugprint data="SHADER-GALLERY registrations=31 sources=51 unique=31"]',
              '[lyc id=900 file="assets/user.png"]', '[lyprop id=900 visible=0]',
              '[lyc id=1 file="assets/checker.png"]', '[lyprop id=1 left=40 top=140]',
              '[lyc id=2 file="assets/checker.png"]', '[lyprop id=2 left=520 top=140]',
              '[lyc id=10 file="assets/pattern.png"]', '[lyprop id=10 left=40 top=140]']
    text(lines, 'label_left', 40, 104, 400, 32, '原图')
    text(lines, 'label_right', 520, 104, 400, 32, '效果图')
    text(lines, 'footer', 40, 494, 880, 40, '○ 下一项（共 31 项，循环）    □ 菜单 / 退出游戏')
    lines += ['*gallery']
    for case in cases:
        name, params, index = case['name'], case['params'], case['index']
        lines.extend([f'*case_{index:02}', '[lydel id=11]', '[lyc id=11 file="assets/pattern.png"]'])
        args = ' '.join(f'{k}="{v}"' for k, v in params.items())
        user = ' shadertexture=textureUser textureUser=900' if name in MIX else ''
        lines.append(f'[lyprop id=11 left=520 top=140 shader={case["shader_id"]} shaderconstant="{",".join(params)}" {args}{user}]')
        origin = '/'.join(o['game'] for o in case['origins'])
        text(lines, 'heading', 40, 18, 880, 40, f'Shader {index:02}/31  |  {origin}  |  {name}', 26)
        text(lines, 'description', 40, 60, 880, 40, DESCRIPTIONS[name] + ('；混合输入为半透明四色图' if name in MIX else ''))
        shown = ['weights=0.3 + 14 x 0.05' if k == 'weights' else f'{k}={v}' for k, v in params.items()]
        text(lines, 'params', 40, 420, 880, 66, ' / '.join(shown) if shown else '无额外参数', 22)
        lines.extend(['[trans time=0]', f'[debugprint data="SHADER-GALLERY page={index:02} game={origin} effect={name}"]', '[@]'])
    lines += ['[debugprint data="SHADER-GALLERY complete=31"]', '[jump label=gallery]']
    script = '\n'.join(lines) + '\n'
    (GAME / 'system/first.iet').write_text(script, encoding='utf-8')
    (GAME / 'title.txt').write_text('内置 Shader 演示 demo\n', encoding='utf-8')
    (GAME / 'platform.txt').write_text('psvita\n', encoding='utf-8')
    (GAME / 'system.ini').write_text('[WINDOWS]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n')
    (GAME / 'manifest.json').write_text(json.dumps(cases, ensure_ascii=False, indent=2), encoding='utf-8')
    glyphs = OUT / 'glyphs.txt'
    glyphs.write_text(script, encoding='utf-8')
    def wsl(p):
        p = p.resolve().as_posix()
        return '/mnt/' + p[0].lower() + p[2:]
    subprocess.run(['wsl', '-d', 'Ubuntu-24.04', '--', 'python3', '-m', 'fontTools.subset',
                    wsl(ROOT / 'build/native-command-port/originals/otomeriron/sourcehansans-bold.otf'),
                    '--text-file=' + wsl(glyphs), '--output-file=' + wsl(GAME / 'assets/probe.otf')], check=True)
    readme = ('内置 Shader 演示 demo（31 项，51 个来源文件按完整字节去重）\n\n'
              'Demo 已预置在 VPK 内，在启动器选择“内置 Shader 演示 demo” 即可；无需另外复制资源。\n'
              '独立 ZIP 仍可将 TEST_SHADERS_51 复制到 ux0:data/art3m1s-gxm/games/；内置版优先，不重复显示。\n'
              '使用已内置 31 种效果的最新版安装包；自动转换、自动编译可以全部关闭。\n'
              '左侧原图，右侧效果；按 ○ 下一页，31 页后循环。按 □ 菜单中的退出游戏返回启动器。\n'
              '每页标注 game1/game2 来源；manifest.json 保留全部 51 个原始文件的对应关系。\n'
              '这是固定参数的视觉演示，不是性能基准或原版所有参数的像素一致性验收。\n'
              'reset 预期与原图一致；混合类额外使用半透明四色纹理；挖空后可见棋盘底。\n'
              '资源只包含原始 HLSL、小型字体子集与程序生成的测试图，不包含剧情及 EXE。\n'
              'Shader 路径、缓存文件及开关使用方法请见同目录 SHADER_PLACEMENT.txt。\n')
    (GAME / 'README.txt').write_text(readme, encoding='utf-8')
    (GAME / 'SHADER_PLACEMENT.txt').write_text(
        (ROOT / 'SHADER_PLACEMENT.zh-CN.md').read_text(encoding='utf-8'), encoding='utf-8')
    bundled = ROOT / 'host-direct/assets/TEST_SHADERS_51'
    assert bundled.resolve() == ROOT.resolve() / 'host-direct/assets/TEST_SHADERS_51'
    if bundled.exists():
        shutil.rmtree(bundled)
    shutil.copytree(GAME, bundled)
    archive = OUT / 'TEST_SHADERS_51.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(GAME.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(OUT))
    print(json.dumps(dict(directory=str(GAME), archive=str(archive), pages=31,
                          unique=31, bytes=archive.stat().st_size)))


if __name__ == '__main__':
    main()
