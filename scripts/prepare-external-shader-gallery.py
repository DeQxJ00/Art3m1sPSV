"""Create the interactive external-shader demo in a local temporary directory."""
from pathlib import Path
import json
import runpy
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'temp/shader-gallery/TEST_SHADERS_EXTERNAL'
source_module = runpy.run_path(str(ROOT / 'scripts/prepare-external-shader-test.py'))
source_module['main']()
source = source_module['OUT']
OUTPUT.mkdir(parents=True, exist_ok=True)
for folder in ('assets', 'system/shader'):
    shutil.copytree(source / folder, OUTPUT / folder, dirs_exist_ok=True)
for name in ('system.ini', 'platform.txt', 'manifest.json'):
    shutil.copyfile(source / name, OUTPUT / name)
cases = json.loads((OUTPUT / 'manifest.json').read_text())
text = runpy.run_path(str(ROOT / 'scripts/prepare-shader-gallery.py'))['text']
descriptions = [
    '双色映射：将明暗映射到蓝黄两种颜色',
    '色差偏移：红蓝通道沿相反方向移动',
    '暗角：中心明亮，边缘逐渐变暗',
    '双纹理混合：按羽化边界叠加第二张纹理',
    '数组参数：使用三个系数调整颜色曲线',
    '失败回退：samplerBack 暂不支持，预期显示原图',
]
lines = ['*top', '[debug mode=1 level=3]', '[lua]',
         'function getGameMode() return "adv" end',
         'function decide(e,p) e:overrideKey{key=124,status=32} end', '[/lua]',
         '[keyconfig role=0 keys=124]', '[setonpush key=13 handler=calllua function=decide]',
         '[fontdefault face="assets/probe.otf" size=24 color=ffffff show=none]']
# Register as one startup batch so the real compilation progress is visible.
for case in cases:
    lines.append(f'[lyshader id=external_{case["name"]} file="{case["file"]}"]')
lines += ['[lyc id=0 width=960 height=544 color=182334]']
text(lines, 'heading', 40, 52, 880, 40, '外置 Shader 演示 demo', 28)
intro = [
    '首次使用：在启动器 START 设置中开启 Shader 自动转换和自动编译。',
    '实机编译需要 ur0:/data/libshacccg.suprx。',
    '完成一次后可关闭两个开关，继续使用已有缓存。',
    '本演示不会修改全局开关；缺少缓存且未开启时只显示原图。',
    '包含五个新效果，以及一个预期失败的回退用例。',
]
for i, line in enumerate(intro):
    text(lines, f'intro{i}', 40, 128+i*48, 880, 40, line, 23)
text(lines, 'footer', 40, 494, 880, 40, '○ 开始演示    □ 菜单 / 退出游戏')
lines += ['[trans time=0]', '[debugprint data="EXTERNAL-GALLERY intro registrations=6"]', '[@]']
for i in range(len(intro)):
    lines += [f'[chgmsg id=intro{i} stack=0]', '[rp backlog=0]']
lines += [
          '[lyc id=900 file="assets/user.png"]', '[lyprop id=900 visible=0]',
          '[lyc id=1 file="assets/checker.png"]', '[lyprop id=1 left=40 top=140]',
          '[lyc id=2 file="assets/checker.png"]', '[lyprop id=2 left=520 top=140]',
          '[lyc id=10 file="assets/pattern.png"]', '[lyprop id=10 left=40 top=140]']
text(lines, 'left', 40, 104, 400, 32, '原图')
text(lines, 'right', 520, 104, 400, 32, '外置效果')
text(lines, 'footer', 40, 494, 880, 40, '○ 下一项（共 6 项，循环）    □ 菜单 / 退出游戏')
lines += ['*gallery']
for case, description in zip(cases, descriptions):
    name, index, params = case['name'], case['index'], case['params']
    lines += [f'*case_{index:02}', '[lydel id=11]', '[lyc id=11 file="assets/pattern.png"]']
    args = ' '.join(f'{k}="{v}"' for k, v in params.items())
    user = 'shadertexture=textureUser textureUser=900' if name == 'feather_mix' else ''
    lines += [f'[lyprop id=11 left=520 top=140 shader=external_{name} shaderconstant="{",".join(params)}" {args} {user}]']
    text(lines, 'heading', 40, 18, 880, 40, f'外置 Shader {index}/6  |  {name}', 26)
    text(lines, 'description', 40, 60, 880, 40, description, 23)
    text(lines, 'params', 40, 420, 880, 66,
         ' / '.join(f'{k}={v}' for k, v in params.items()) if params else '这是明确拒绝用例，不是成功编译的效果。', 22)
    lines += ['[trans time=0]', f'[debugprint data="EXTERNAL-GALLERY page={index:02} effect={name}"]', '[@]']
lines += ['[debugprint data="EXTERNAL-GALLERY complete=6"]', '[jump label=gallery]']
script = '\n'.join(lines) + '\n'
(OUTPUT / 'system/first.iet').write_text(script, encoding='utf-8')
(OUTPUT / 'title.txt').write_text('外置 Shader 演示 demo\n', encoding='utf-8')
glyphs = ROOT / 'temp/shader-gallery/external-glyphs.txt'
glyphs.parent.mkdir(parents=True, exist_ok=True)
glyphs.write_text(script, encoding='utf-8')
def wsl(path):
    path = path.resolve().as_posix()
    return '/mnt/' + path[0].lower() + path[2:]
subprocess.run(['wsl', '-d', 'Ubuntu-24.04', '--', 'python3', '-m', 'fontTools.subset',
                wsl(ROOT / 'backup/legacy-build/native-command-port/originals/otomeriron/sourcehansans-bold.otf'),
                '--text-file=' + wsl(glyphs), '--output-file=' + wsl(OUTPUT / 'assets/probe.otf')], check=True)
(OUTPUT / 'README.txt').write_text(
    '外置 Shader 演示 demo\n\n'
    'VPK 不附带 demo；将 TEST_SHADERS_EXTERNAL 文件夹复制到 ux0:data/art3m1s-gxm/games/ 后进入。○ 开始/下一项，六项后循环，□ 退出。\n'
    '首次在启动器 START 设置中开启 Shader 自动转换和自动编译；需要 ur0:/data/libshacccg.suprx。\n'
    '演示不修改全局设置。完成首次缓存后可以关闭两个开关；缺少可用程序时只显示原图。\n'
    '五个效果不匹配内置 51 项的源码哈希；第六项为不支持 samplerBack 的预期拒绝。\n'
    'HLSL 位于 ux0:data/art3m1s-gxm/games/TEST_SHADERS_EXTERNAL/system/shader/pc/custom/。\n'
    '缓存写入 ux0:data/art3m1s-gxm/games/TEST_SHADERS_EXTERNAL/shader-cache/system/shader/pc/custom/，包含 Cg、元数据、GXP 和 hash。\n'
    '首次转换编译有加载进度；后续根据设置复用缓存，不往应用目录写文件。\n', encoding='utf-8')
print(OUTPUT)
