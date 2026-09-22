"""New, non-bundled effects for real-device compiler/cache acceptance."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'temp/external-shaders/custom-physical/game'
CASES = [
    ('duotone', 'float3 lowColor; float3 highColor; float amount;',
     'float l=dot(fore.rgb,float3(0.299,0.587,0.114)); fore.rgb=lerp(fore.rgb,lerp(lowColor,highColor,l),amount);',
     dict(lowColor='0.05,0.1,0.35', highColor='1,0.8,0.25', amount='0.85')),
    ('chromatic', 'float shift;',
     'fore.r=tex2D(samplerFore,texCoord1+float2(shift,0)).r; fore.b=tex2D(samplerFore,texCoord1-float2(shift,0)).b;',
     dict(shift='0.045')),
    ('vignette', 'float strength; float innerRadius; float outerRadius;',
     'float d=length((texCoord1-float2(0.5,0.5))*float2(1,0.65)); fore.rgb*=1-strength*smoothstep(innerRadius,outerRadius,d);',
     dict(strength='0.9', innerRadius='0.15', outerRadius='0.5')),
    ('feather_mix', 'float split; float feather;',
     'float4 user=tex2D(samplerUser,texCoord1); float t=smoothstep(split-feather,split+feather,texCoord1.x)*user.a; fore.rgb=lerp(fore.rgb,user.rgb,t);',
     dict(split='0.5', feather='0.25')),
    ('array_curve', 'float gains[3];',
     'fore.rgb=saturate(fore.rgb*gains[0]+fore.rgb*fore.rgb*gains[1]+gains[2]);',
     dict(gains='0.5,0.3,0.15')),
    # This is an expected rejection, not a supported render case.
    ('unsupported_back', '', 'fore=tex2D(samplerBack,texCoord1);', {}),
]


def main():
    (OUT / 'system/shader/pc/custom').mkdir(parents=True, exist_ok=True)
    (OUT / 'assets').mkdir(exist_ok=True)
    for file in ['pattern.png', 'checker.png', 'user.png']:
        shutil.copyfile(ROOT / 'temp/shader-gallery/TEST_SHADERS_51/assets' / file, OUT / 'assets' / file)
    shutil.copyfile(ROOT / 'temp/external-shaders/game/assets/probe.otf', OUT / 'assets/probe.otf')
    template = (ROOT / 'backup/legacy-build/external-shaders/resources/toshiue/system/shader/pc/gray.hlsl').read_text()
    prefix = template.split('void ps(', 1)[0]
    technique = 'technique technique0' + template.split('technique technique0', 1)[1]
    known = {e['source_hash'] for e in json.loads((ROOT / 'shaders/psv/manifest.json').read_text())}
    lines = ['*top', '[debug mode=1 level=3]', '[fontdefault face="assets/probe.otf" size=26 color=ffffff show=none]',
             '[lyc id=0 width=960 height=544 color=182334]',
             '[lyc id=1 file="assets/checker.png"]', '[lyprop id=1 left=40 top=140]',
             '[lyc id=2 file="assets/checker.png"]', '[lyprop id=2 left=520 top=140]',
             '[lyc id=10 file="assets/pattern.png"]', '[lyprop id=10 left=40 top=140]',
             '[lyc id=900 file="assets/user.png"]', '[lyprop id=900 visible=0]']
    def text(layer, x, y, value):
        lines.extend([f'[chgmsg id={layer} stack=0]', f'[font left={x} top={y} width=880 height=60]', '[rp backlog=0]', f'[print data="{value}"]'])
    text('left', 40, 100, 'ORIGINAL')
    text('right', 520, 100, 'EXTERNAL')
    manifest = []
    for index, (name, uniforms, body, params) in enumerate(CASES, 1):
        path = f'system/shader/pc/custom/{name}.hlsl'
        extra = 'texture textureUser; sampler samplerUser = sampler_state { texture = <textureUser>; MinFilter = LINEAR; MagFilter = LINEAR; AddressU = Clamp; AddressV = Clamp; };\n'
        # The supported Artemis wrapper declares globals before vs()/ps().
        declarations, vertex = prefix.split('void vs(', 1)
        source = (declarations + extra + uniforms + '\nvoid vs(' + vertex
                  + '\nvoid ps(float2 texCoord0:TEXCOORD0,float2 texCoord1:TEXCOORD1,out float4 result:COLOR0) {\n'
                  'float4 fore=tex2D(samplerFore,texCoord1);\n' + body + '\n'
                  'fore.a*=tex2D(samplerMask,texCoord1).a*alpha; result=fore;\n}\n' + technique).encode()
        fnv = 0xcbf29ce484222325
        for byte in source:
            fnv = ((fnv ^ byte) * 0x100000001b3) & 0xffffffffffffffff
        assert f'{fnv:016x}' not in known
        (OUT / path).write_bytes(source)
        lines.extend([f'[lyshader id=external_{name} file="{path}"]', '[lydel id=11]', '[lyc id=11 file="assets/pattern.png"]'])
        args = ' '.join(f'{k}="{v}"' for k, v in params.items())
        user = 'shadertexture=textureUser textureUser=900' if name == 'feather_mix' else ''
        lines.append(f'[lyprop id=11 left=520 top=140 shader=external_{name} shaderconstant="{",".join(params)}" {args} {user}]')
        text('heading', 40, 24, f'EXTERNAL {index}/6 | {name}')
        text('hint', 40, 420, 'EXPECTED: REJECT UNSUPPORTED / SHOW ORIGINAL' if index == 6 else 'NEW SOURCE / NOT IN THE 51 BUNDLED FILES')
        lines.extend(['[trans time=0]', '[wait time=1100 input=0]', '[takess]',
                      f'[savess file="external-{index:02}.png" width=960 height=544]',
                      f'[debugprint data="EXTERNAL-CUSTOM rendered={index} name={name}"]'])
        manifest.append(dict(index=index, name=name, file=path, params=params, builtin=False,
                             expected='reject' if index == 6 else 'compile', sha256=hashlib.sha256(source).hexdigest()))
    lines.extend(['[debugprint data="EXTERNAL-CUSTOM DONE"]', '[@]'])
    (OUT / 'system/first.iet').write_text('\n'.join(lines) + '\n', encoding='utf8')
    (OUT / 'title.txt').write_text('TEST EXTERNAL SHADERS\n')
    (OUT / 'platform.txt').write_text('psvita\n')
    (OUT / 'system.ini').write_text('[WINDOWS]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=system/first.iet\nSAVEPATH=savedata\n')
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(OUT)


if __name__ == '__main__':
    main()
