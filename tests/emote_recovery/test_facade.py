from pathlib import Path
import subprocess,shutil,json,hashlib
root=Path(__file__).resolve().parents[2];recovery=root.parent/'emote-psv-test';b=root/'build/recovery-facade-test';b.mkdir(parents=True,exist_ok=True)
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
native=['model_view','scene_runtime','timeline','model_controls','render_packet']
recovered=['curve','psb','mesh','mesh_child','motion_events','motion_clock','parameter','controls','physics_schedule','physics_lookup','wind','material_colors']
for folder in ['native','recovered']:
 for p in (recovery/folder).glob('*.h'):shutil.copyfile(p,b/p.name)
for folder,names in [('native',native),('recovered',recovered)]:
 for n in names:shutil.copyfile(recovery/folder/(n+'.c'),b/(n+'.c'))
for name in ['psb.h','controls.h']:
 p=b/name;p.write_text('\n'.join(x for x in p.read_text().splitlines() if not x.startswith('_Static_assert')))
shutil.copyfile(recovery/'tools/host_gpu.h',b/'gpu.h')
p=b/'psp2/kernel/threadmgr/thread.h';p.parent.mkdir(parents=True,exist_ok=True);p.write_text('typedef long long SceInt64; SceInt64 sceKernelGetSystemTimeWide(void);')
for n in ['emote_recovery_facade.cpp','emote_recovery_facade.h']:shutil.copyfile(root/'host-direct/src'/n,b/n)
shutil.copyfile(root/'tests/emote_recovery/facade_test.cpp',b/'test.cpp')
flags=['-O2','-g','-fno-builtin-fmodf','-ffp-contract=off','-fsanitize=address,undefined','-I'+linux(b)]
objects=[]
for n in native+recovered:
 o=b/(n+'.o');subprocess.run(['wsl','-e','gcc',*flags,'-c',linux(b/(n+'.c')),'-o',linux(o)],check=True);objects.append(linux(o))
subprocess.run(['wsl','-e','g++',*flags,'-pthread',linux(b/'test.cpp'),linux(b/'emote_recovery_facade.cpp'),*objects,'-lm','-o',linux(b/'test')],check=True)
asset=recovery/'assets/sample01-kog_12.psb'
r=subprocess.run(['wsl','-e',linux(b/'test'),linux(asset)],capture_output=True,text=True);print(r.stdout,r.stderr);r.check_returncode();assert not r.stderr
(root/'tests/emote_recovery/facade-verification.json').write_text(json.dumps({'status':'pass','scope':r.stdout.strip(),'asset_sha256':hashlib.sha256(asset.read_bytes()).hexdigest(),'facade_sha256':hashlib.sha256((root/'host-direct/src/emote_recovery_facade.cpp').read_bytes()).hexdigest()},indent=2)+'\n')
