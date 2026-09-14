"""Convert the fixed Artemis DX9 pixel-effect wrapper to external Vita GXP.
Outputs only to the explicitly selected output directory; never edits sources.
No game executables are launched. DX11, custom vertex stages and backbuffer
sampling are rejected instead of silently emulated.
"""
from pathlib import Path
import argparse, json, re, struct, subprocess, hashlib

MAX_VALUES = 128

def fnv64(data):
    n=0xcbf29ce484222325
    for b in data:n=((n^b)*0x100000001b3)&0xffffffffffffffff
    return f"{n:016x}"

def strip_comments(s):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", s, flags=re.S)

def body(s, name):
    m=re.search(r"\bvoid\s+"+name+r"\s*\([^)]*\)\s*\{",s)
    if not m:raise ValueError("requires fixed DX9 void "+name+"() wrapper")
    start=m.end();depth=1
    for i in range(start,len(s)):
        if s[i]=='{':depth+=1
        if s[i]=='}':depth-=1
        if not depth:return s[start:i],m.start()
    raise ValueError("unbalanced function body")

def convert(data):
    s=strip_comments(data.decode('utf-8',errors='replace')).replace('\r','')
    ps,ps_at=body(s,'ps');vs,vs_at=body(s,'vs')
    assignments=re.sub(r"\s+","",vs)
    if assignments not in ("resultPosition=position;resultTexCoord0=texCoord0;resultTexCoord1=texCoord1;",):
        raise ValueError("custom vertex stage requires a separate port")
    if re.search(r"\bsamplerBack\b",ps):raise ValueError("backbuffer reads require an explicit snapshot implementation")
    globals_s=s[:min(vs_at,ps_at)]
    globals_s=re.sub(r"\btexture\s+\w+\s*;",'',globals_s)
    globals_s=re.sub(r"\bsampler\s+\w+\s*=\s*sampler_state\s*\{[^}]*\}\s*;",'',globals_s)
    declarations=[];uniforms=[];offset=0
    for item in globals_s.split(';'):
        item=item.strip()
        if not item:continue
        if item.startswith('const float'):
            if '#' in item:raise ValueError('preprocessor unsupported')
            # Cg globals are uniform unless explicitly static: a plain const
            # initializer can become an unset uniform on Vita (black grayscale).
            declarations.append('static '+item+';');continue
        m=re.fullmatch(r"(float[1-4]?)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?",item)
        if not m:raise ValueError('unsupported global: '+item[:120])
        typ,name,array=m.groups();count=int(typ[5:] or 1)*int(array or 1)
        if name.startswith('art_') or not 0<count<=MAX_VALUES or offset+count>MAX_VALUES:raise ValueError('uniform bounds/name')
        uniforms.append(dict(name=name,offset=offset,count=count));offset+=count
        declarations.append('uniform '+item+';')
    if re.search(r"\b(?:sampler|texture)\w*\b",ps) and any(n not in ['samplerFore','samplerMask','samplerUser'] for n in re.findall(r"\bsampler\w+",ps)):
        raise ValueError('unsupported sampler')
    cg='\n'.join(['uniform sampler2D samplerFore : TEXUNIT0;','uniform sampler2D samplerMask : TEXUNIT1;',
        'uniform sampler2D samplerUser : TEXUNIT3;','uniform float4 art_clip;']+declarations)+"\n"
    cg+='float4 main(float2 texCoord1:TEXCOORD0,float4 tint:COLOR0,float2 pixel:TEXCOORD1):COLOR {\n'
    cg+='float2 texCoord0=texCoord1; float4 result=float4(0,0,0,0);\n'+ps+'\n'
    cg+='float cover=step(art_clip.x,pixel.x)*step(art_clip.y,pixel.y)*(1-step(art_clip.z,pixel.x))*(1-step(art_clip.w,pixel.y));\nreturn result*cover;\n}\n'
    return cg,dict(abi=1,source_hash=fnv64(data),uniforms=uniforms)

def compile_one(src,out,compiler):
    data=src.read_bytes();cg,meta=convert(data)
    out.parent.mkdir(parents=True,exist_ok=True)
    cgpath=out.with_suffix(out.suffix+'.cg');gxp=out.with_suffix(out.suffix+'.gxp')
    cgpath.write_text(cg,encoding='utf-8')
    r=subprocess.run([str(compiler),'-profile','sce_fp_psp2','-O1','-nofastmath','-bestprecision','-o',str(gxp),str(cgpath)],capture_output=True,text=True)
    if r.returncode:raise ValueError(r.stdout+r.stderr)
    binary=gxp.read_bytes();metadata=json.dumps(meta,separators=(',',':')).encode()
    out.write_bytes(b'AGX1'+struct.pack('<II',len(metadata),len(binary))+metadata+binary)
    return dict(source=str(src),output=str(out),gxp_bytes=len(binary),source_sha256=hashlib.sha256(data).hexdigest(),uniforms=meta['uniforms'])

def main():
    a=argparse.ArgumentParser();a.add_argument('--source',type=Path,required=True);a.add_argument('--out',type=Path,required=True)
    a.add_argument('--compiler',type=Path,default=Path(__file__).resolve().parents[1]/'.tools/sony-shader-3.570/sdk/host_tools/bin/psp2cgc.exe')
    args=a.parse_args();results=[];bad=[]
    if args.out.resolve()==args.source.resolve() or args.source.resolve() in args.out.resolve().parents:raise SystemExit('output must be outside source directory')
    for src in sorted(args.source.rglob('*.hlsl')):
        try:results.append(compile_one(src,args.out/src.relative_to(args.source).with_suffix('.hlsl.agxp'),args.compiler))
        except ValueError as e:bad.append(dict(source=str(src),error=str(e)))
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/'manifest.json').write_text(json.dumps(dict(compiled=results,unsupported=bad),ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(compiled=len(results),unsupported=bad),ensure_ascii=False))
    return bool(bad)
if __name__=='__main__':raise SystemExit(main())
