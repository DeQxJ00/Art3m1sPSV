import hashlib, json, re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
base=root/'temp/conversion-work/otomeriron'
archives=sorted(p for p in base.iterdir() if p.is_dir() and re.fullmatch(r'otomeriron\.pfs(?:\.\d{3})?',p.name))
effective={}
for archive in archives:
    for path in archive.rglob('*'):
        if path.is_file(): effective[path.relative_to(archive).as_posix().lower()]=path
shaders={}
for name,path in effective.items():
    if re.fullmatch(r'system/shader/pc/[^/]+\.hlsl',name):
        source=path.read_text(encoding='cp932',errors='replace')
        shaders[path.stem]={'source':str(path.relative_to(root)), 'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'uniforms':re.findall(r'^\s*(float[234]?(?:x[234])?|int[234]?|bool)\s+(\w+)\s*;',source,re.M),
            'samplers':re.findall(r'^\s*sampler\s+(\w+)',source,re.M),'scriptReferences':[],'runtimeStatus':'not verified'}
refs=[]
for name,path in effective.items():
    if not name.startswith('script/') or not name.endswith('.ast'): continue
    for number,line in enumerate(path.read_text(encoding='utf-8',errors='replace').splitlines(),1):
        for field,value in re.findall(r'\b(style|rule|shader)\s*=\s*"([^"]+)"',line):
            if value in shaders or value in ('shader001','shader002'):
                entry={'path':name,'archive':path.relative_to(base).parts[0],'line':number,'field':field,'value':value,'command':line.strip()}
                refs.append(entry)
                effect='radial' if value in ('shader001','shader002') else value
                shaders[effect]['scriptReferences'].append(entry)
out=root/'temp/otomeriron-shader-audit.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps({'scope':'Static inventory of effective archives. References are candidates, not executed effects or visual verification.','shaders':shaders,'references':refs},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'shaderCount':len(shaders),'referenceCount':len(refs),'counts':{k:len(v['scriptReferences']) for k,v in shaders.items()},'output':str(out)},ensure_ascii=False))
