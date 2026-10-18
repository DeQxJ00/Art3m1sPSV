"""Self-contained synthetic runtime compilation/cache probe (no game assets)."""
import argparse
import json
import struct
import zlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    raw = b''.join(b'\0' + bytes((80, 120, 160, 255)) * 128 for _ in range(128))
    (out / 'input.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 128, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
    sources = {
        'custom/array.hlsl': 'float alpha; float gains[3]; void vs(){resultPosition=position;resultTexCoord0=texCoord0;resultTexCoord1=texCoord1;} void ps(){float4 fore=tex2D(samplerFore,texCoord1); fore.rgb*=float3(gains[0],gains[1],gains[2]); fore.a*=alpha; result=fore;}',
        'custom/gray.glsl': 'precision mediump float; varying vec2 resultCoord1; uniform sampler2D textureFore; uniform float alpha; void main(){vec4 fore=texture2D(textureFore,resultCoord1); float l=dot(fore.rgb,vec3(0.3,0.6,0.1)); fore.rgb=vec3(l+0.2); fore.a*=alpha; gl_FragColor=fore;}',
        'custom/gain.hlsl': 'struct SVertexShaderResult{float4 position:SV_POSITION;float2 texCoord0:TEXCOORD0;float2 texCoord1:TEXCOORD1;};cbuffer ConstantBuffer{float alpha; float gain;};SamplerState samplerBack:register(s0);Texture2D textureFore:register(t1);float4 main(SVertexShaderResult vertexShaderResult):SV_TARGET{float4 fore=textureFore.Sample(samplerBack,vertexShaderResult.texCoord1); fore.rgb*=gain; fore.a*=alpha; return fore;}',
    }
    lines = ['*top', '[debug mode=1 level=3]', '[lyc id=0 width=960 height=544 color=182334]']
    for index, (name, source) in enumerate(sources.items(), 1):
        path = out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding='utf-8')
        params = {1:'shaderconstant="gains" gains="0.5,0.75,1"', 2:'', 3:'shaderconstant="gain" gain=0.5'}[index]
        lines += [f'[lyshader id=probe{index} file="{name}"]', f'[lyc id={index} file="input.png"]',
                  f'[lyprop id={index} left={80 + (index-1)*260} top=180 shader=probe{index} {params}]']
    lines += ['[trans time=0]', '[wait time=1500 input=0]', '[takess]', '[savess file="cache-probe.png" width=960 height=544]', '[debugprint data="SHADER-CACHE-PROBE DONE"]', '[@]']
    (out / 'first.iet').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (out / 'system.ini').write_text('[WINDOWS]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=first.iet\n[VITA]\nWIDTH=960\nHEIGHT=544\nCHARSET=UTF-8\nBOOT=first.iet\n', encoding='utf-8')
    (out / 'title.txt').write_text('TEST SHADER CACHE\n', encoding='utf-8')
    (out / 'platform.txt').write_text('psvita\n', encoding='utf-8')
    (out / 'expected.json').write_text(json.dumps(dict(center_pixels=[[40,90,160],[163,163,163],[40,60,80]], tolerance=3), indent=2)+'\n', encoding='utf-8')
    print(out)


if __name__ == '__main__':
    main()
