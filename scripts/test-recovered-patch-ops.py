"""Compare compiled ARM patch SIMD with retained scalar operation order."""
from pathlib import Path
import argparse, subprocess, sys, struct, random, json, hashlib
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--oracle-tools',type=Path,required=True)
p.add_argument('--compiler',type=Path,required=True)
a=p.parse_args();sys.path.insert(0,str(a.oracle_tools))
from arm_oracle import machine, symbols, call, DATA, SCRATCH
build=root/'build/patch-ops-test';build.mkdir(parents=True,exist_ok=True);elf=build/'test.elf'
subprocess.run([str(a.compiler),'-O2','-fno-tree-vectorize','-ffp-contract=off','-mthumb','-mfpu=neon','-mfloat-abi=hard','-nostdlib','-Wl,-e,vector_patch','-I'+str(root/'vendor/emote-recovery/native'),str(root/'tests/emote_recovery/patch_ops.c'),'-o',str(elf)],check=True)
u=machine(elf);sy=symbols(elf.read_bytes());rng=random.Random(20261021);checks=0
for trial in range(512):
 arrays=[[rng.uniform(-8192,8192) for _ in range(32)] for _ in range(3)]
 if trial%5==0:arrays=[[rng.choice([0.,-0.,1.,-1.,1/3,2/3]) for _ in range(32)] for _ in range(3)]
 params=[rng.choice([-.5,0.,1.,1.5,.5,rng.random()]),float(trial%2),float((trial//2)%2),rng.uniform(-2,2),rng.uniform(-1000,1000),rng.uniform(-1000,1000)]
 for mode in range(3):
  u.mem_write(SCRATCH,struct.pack('<32f',*arrays[0]));u.mem_write(SCRATCH+256,struct.pack('<32f',*arrays[1]));u.mem_write(SCRATCH+512,struct.pack('<6f',*params))
  results=[]
  for entry in ['scalar_patch','vector_patch']:
   u.mem_write(DATA,b'\xa5'*16+struct.pack('<32f',*arrays[2])+b'\x5a'*16)
   call(u,sy[entry],mode,DATA+16,SCRATCH,SCRATCH+256,SCRATCH+512)
   result=bytes(u.mem_read(DATA,160));assert result[:16]==b'\xa5'*16 and result[-16:]==b'\x5a'*16;results.append(result)
  assert results[0]==results[1],(trial,mode);checks+=1
paths=['vendor/emote-recovery/native/patch_ops.h','tests/emote_recovery/patch_ops.c','scripts/test-recovered-patch-ops.py']
r={'status':'pass','comparisons':checks,'scope':'Compiled Cortex-A9 ARM SIMD versus scalar patch blend/add/affine transform, exact/extrapolation inputs, signed zeros and guard bytes; byte equality. Not a device FPS test.','sources':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in paths}}
(root/'tests/emote_recovery/patch-ops-verification.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
