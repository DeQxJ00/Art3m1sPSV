"""Bounded render-only reassociation: compile and execute the actual ARM kernels."""
from pathlib import Path
import sys,subprocess,random,struct,json,hashlib,math
import argparse
p=argparse.ArgumentParser(description='Compare bounded render-grid sampling with the exact PSV ARM kernel.')
p.add_argument('--recovery-root',type=Path,required=True,help='Local recovery tools checkout containing tools/arm_oracle.py')
p.add_argument('--sdk-bin',type=Path,required=True,help='VitaSDK bin directory')
a=p.parse_args()
sys.path.insert(0,str(a.recovery_root/'tools'))
from arm_oracle import *
root=Path(__file__).resolve().parents[1]; src=root/'vendor/emote-recovery/recovered/mesh.c'; out=root/'build/grid-factor';out.mkdir(exist_ok=True)
sdk=a.sdk_bin
elf=out/'grid.elf'
subprocess.run([str(next(x for x in [sdk/'arm-vita-eabi-gcc',sdk/'arm-vita-eabi-gcc.exe'] if x.exists())),'-O2','-ffp-contract=off','-mthumb','-mfpu=neon','-mfloat-abi=hard','-nostdlib','-Wl,-e,em_mesh_world_grid',str(src),'-o',str(elf)],check=True)
u=machine(elf); sy=symbols(elf.read_bytes()); rng=random.Random(91634);worst=0.;points=0;fallback=0
for trial in range(180):
 n=[0,1,2,3,4,5,7,8,15,17,79,80,81][trial%13]
 scale=8192 if trial%3==0 else 1000
 cp=[rng.uniform(-scale,scale) for _ in range(32)]
 inv=[1,0,0,1];off=[0,0]
 xy=[rng.uniform(-.25,1.25) for _ in range(n*2)]
 exact=trial%5==0
 if exact:xy=[rng.uniform(2,4) for _ in xy]
 if trial%7==0:cp[0]=10000;exact=True
 u.mem_write(SCRATCH,struct.pack('<38f',*(cp+inv+off)));raw=struct.pack('<%df'%(n*2),*xy)
 for dest in [DATA,DATA+0x1000,DATA+0x2000]:u.mem_write(dest,raw+b'\xa5'*32)
 call(u,sy['em_mesh_world_many'],SCRATCH,DATA,n);call(u,sy['em_mesh_world_grid'],SCRATCH,DATA+0x1000,n)
 bounded=call(u,sy['em_mesh_world_grid_bounded'],SCRATCH)
 call(u,sy['em_mesh_world_grid_prepared'],SCRATCH,DATA+0x2000,n,bounded)
 assert u.mem_read(DATA+0x2000,n*8+32)==u.mem_read(DATA+0x1000,n*8+32),(trial,'prepared')
 a=bytes(u.mem_read(DATA,n*8+32));b=bytes(u.mem_read(DATA+0x1000,n*8+32));assert a[n*8:]==b[n*8:]==b'\xa5'*32
 if exact:assert a==b,(trial,'fallback');fallback+=n
 aa=struct.unpack('<%df'%(n*2),a[:n*8]);bb=struct.unpack('<%df'%(n*2),b[:n*8])
 err=max([abs(x-y) for x,y in zip(aa,bb)]+[0]);worst=max(worst,err);assert err<.02,(trial,err);points+=n
# Nonfinite controls and inverse maps must preserve exact-kernel output bits.
nonfinite_cases=0
for bits in [0x7fc00001,0x7f800000,0xff800000,0x7fa00001]:
 for lane in [0,32,36]:
  params=[float(i%4) for i in range(32)]+[1.,0.,0.,1.,0.,0.]
  raw=bytearray(struct.pack('<38f',*params));struct.pack_into('<I',raw,lane*4,bits);u.mem_write(SCRATCH,bytes(raw))
  points_raw=struct.pack('<16f',*[.25]*16)
  for dest in [DATA,DATA+0x1000,DATA+0x2000]:u.mem_write(dest,points_raw+b'\xa5'*32)
  call(u,sy['em_mesh_world_many'],SCRATCH,DATA,8);call(u,sy['em_mesh_world_grid'],SCRATCH,DATA+0x1000,8)
  bounded=call(u,sy['em_mesh_world_grid_bounded'],SCRATCH)
  call(u,sy['em_mesh_world_grid_prepared'],SCRATCH,DATA+0x2000,8,bounded)
  assert u.mem_read(DATA,96)==u.mem_read(DATA+0x1000,96)==u.mem_read(DATA+0x2000,96),(bits,lane)
  nonfinite_cases+=1
# Smooth world-space patches in a six-stage chain; track accumulated pixel error.
chain_worst=0.;chain_points=0
for trial in range(40):
 xy=[rng.uniform(0,1000) for _ in range(162)];raw=struct.pack('<162f',*xy)
 for dest in [DATA,DATA+0x1000,DATA+0x2000]:u.mem_write(dest,raw)
 for depth in range(6):
  cp=[]
  for y in range(4):
   for x in range(4):cp += [1000*x/3+rng.uniform(-15,15),1000*y/3+rng.uniform(-15,15)]
  u.mem_write(SCRATCH,struct.pack('<38f',*(cp+[.001,0,0,.001,0,0])))
  call(u,sy['em_mesh_world_many'],SCRATCH,DATA,81);call(u,sy['em_mesh_world_grid'],SCRATCH,DATA+0x1000,81)
  bounded=call(u,sy['em_mesh_world_grid_bounded'],SCRATCH)
  call(u,sy['em_mesh_world_grid_prepared'],SCRATCH,DATA+0x2000,81,bounded)
  assert u.mem_read(DATA+0x1000,648)==u.mem_read(DATA+0x2000,648),(trial,depth,'prepared_chain')
  a=struct.unpack('<162f',u.mem_read(DATA,648));b=struct.unpack('<162f',u.mem_read(DATA+0x1000,648));err=max(abs(x-y) for x,y in zip(a,b));chain_worst=max(chain_worst,err);assert err<.02;chain_points+=81
r={'status':'pass','single_stage_points':points,'exact_fallback_points':fallback,'nonfinite_exact_fallback_cases':nonfinite_cases,'maximum_world_coordinate_error':worst,'chain_transformations':chain_points,'chain_depth':6,'maximum_chain_world_coordinate_error':chain_worst,'prepared_route_cases':180,'prepared_nonfinite_cases':nonfinite_cases,'prepared_chain_stages':240,'scope':'Compiled PSV ARM comparison against retained exact kernel. Prepared control-bound verdict versus direct route. Random bounded controls, extrapolation fallback, large controls fallback, SIMD tails and guard bytes, smooth six-stage chains. Not a universal error bound or physical benchmark.','source_sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
(root/'tests/emote_recovery/grid-factor-verification.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
