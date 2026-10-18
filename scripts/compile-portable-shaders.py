"""Compile fixed GLSL/DX9/DX11 effects with the runtime translator, without source edits.
Build scripts/translate-portable-shader.rs with rustc --edition=2024 first.
Outputs AGX1 files keyed by original source bytes, not by effect filename.
"""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path

def compile_sources(source, output, translator, compiler, include_hlsl=False):
    records=[]
    paths=list(source.rglob('*.glsl'))
    if include_hlsl:paths+=list(source.rglob('*.hlsl'))
    for path in sorted(paths):
        data=path.read_bytes()
        relative=path.relative_to(source)
        target=output/relative.with_suffix(relative.suffix+'.agxp')
        target.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run([str(translator),str(path),str(target)],check=True)
        gxp=Path(str(target)+'.gxp')
        subprocess.run([str(compiler),'-profile','sce_fp_psp2','-O1','-nofastmath','-bestprecision','-o',str(gxp),str(target)+'.cg'],check=True,capture_output=True)
        binary=gxp.read_bytes()
        n=0xcbf29ce484222325
        for b in data:n=((n^b)*0x100000001b3)&0xffffffffffffffff
        meta={'abi':1,'source_hash':f'{n:016x}','uniforms':json.loads(Path(str(target)+'.uniforms.json').read_text())}
        encoded=json.dumps(meta,separators=(',',':')).encode()
        target.write_bytes(b'AGX1'+struct.pack('<II',len(encoded),len(binary))+encoded+binary)
        records.append({'source':relative.as_posix(),'source_hash':meta['source_hash'],
            'source_sha256':hashlib.sha256(data).hexdigest(),'gxp_sha256':hashlib.sha256(binary).hexdigest(),'gxp_bytes':len(binary)})
    (output/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
    return records

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for name in ['source','output','translator','compiler']:parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--include-hlsl',action='store_true')
    args=parser.parse_args()
    if args.output.resolve()==args.source.resolve() or args.source.resolve() in args.output.resolve().parents:
        parser.error('output must be outside source directory')
    args.output.mkdir(parents=True,exist_ok=True)
    print('Compiled',len(compile_sources(args.source,args.output,args.translator,args.compiler,args.include_hlsl)),'portable effects')
