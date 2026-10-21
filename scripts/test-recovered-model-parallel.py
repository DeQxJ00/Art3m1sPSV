"""Actual facade batch evaluation versus serial packets using caller-owned PSB fixtures."""
from pathlib import Path
import argparse,subprocess,shutil,json,hashlib
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--recovery-root',type=Path,default=root.parent/'emote-psv-test');p.add_argument('--assets',type=Path,nargs=3,required=True);p.add_argument('--tsan',action='store_true');p.add_argument('--tsan-no-aslr',action='store_true',help='Disable ASLR for this Linux test process when TSan cannot reserve its shadow mapping');p.add_argument('--fail-worker',action='store_true');a=p.parse_args();recovery=a.recovery_root
build=root/'build/model-parallel-test';build.mkdir(parents=True,exist_ok=True)
def linux(p):return '/mnt/'+str(p.resolve())[0].lower()+str(p.resolve())[2:].replace('\\','/')
sources=[]
for folder,names in [('native',['model_view','render_packet','render_packet_gpu','scene_runtime','timeline','model_controls']),('recovered',['curve','psb','mesh','mesh_child','motion_events','motion_clock','parameter','controls','physics_schedule','physics_lookup','wind','material_colors'])]:
 for name in names:
  for ext in ['.c','.h']:
   src=recovery/folder/(name+ext)
   if src.exists():shutil.copyfile(src,build/src.name)
  sources.append(build/(name+'.c'))
for relative in ['native/texture_host.h','native/render_packet_internal.h','native/append_log.h','native/exact_bytes.h','native/patch_ops.h','recovered/pose_update.h','recovered/timeline_commands.h','tools/host_gpu.h','tools/view_instance_test_main.c']:
 src=recovery/relative;shutil.copyfile(src,build/('gpu.h' if src.name=='host_gpu.h' else src.name))
for name in ['psb.h','controls.h']:
 dst=build/name;dst.write_text('\n'.join(x for x in dst.read_text().splitlines() if not x.startswith('_Static_assert')))
dst=build/'psp2/kernel/threadmgr/thread.h';dst.parent.mkdir(parents=True,exist_ok=True);dst.write_text('typedef long long SceInt64; SceInt64 sceKernelGetSystemTimeWide(void);')
flags=['-O3','-g','-ffp-contract=off','-fno-builtin-fmodf','-DEM_VERTEX_THREADS_HOST=1','-DEM_LOG_DISABLED=1','-pthread','-fno-omit-frame-pointer','-fsanitize='+('thread' if a.tsan else 'address,undefined'),'-I'+linux(build),'-I'+linux(root/'host-direct/src')]
if a.fail_worker:
 shutil.copyfile(root/'host-direct/src/emote_recovery_facade.cpp',build/'emote_recovery_facade.cpp')
 text=(root/'host-direct/src/recovered_eval_pool.hpp').read_text()
 marker='for(unsigned n=0;n<2;++n){Worker &w=workers[n];'
 assert marker in text;text=text.replace(marker,marker+'if(n==1){shutdown();attempted=true;return false;}')
 (build/'recovered_eval_pool.hpp').write_text(text);flags.append('-DEM_TEST_EVAL_FAIL=1')
else:
 for name in ['emote_recovery_facade.cpp','recovered_eval_pool.hpp']:
  local=build/name
  if local.exists():local.unlink()
objects=[]
for src in sources+[build/'view_instance_test_main.c']:
 obj=src.with_suffix('.o');extra=['-Dmain=unused_fixture_main'] if src.name=='view_instance_test_main.c' else []
 subprocess.run(['wsl','-e','gcc',*flags,*extra,'-c',linux(src),'-o',linux(obj)],check=True);objects.append(linux(obj))
exe=build/'test';subprocess.run(['wsl','-e','g++','-std=c++17','-no-pie',*flags,linux(root/'tests/emote_recovery/model_parallel.cpp'),*objects,'-lm','-o',linux(exe)],check=True)
r=subprocess.run(['wsl','-e',*(['setarch','x86_64','-R'] if a.tsan and a.tsan_no_aslr else []),linux(exe),*[linux(x) for x in a.assets]],capture_output=True,text=True);print(r.stdout,r.stderr);r.check_returncode()
report={'status':'pass','scope':'Actual facade and isolated CPU adapter, grid1/2/4/6/8 plus cache transitions, serial/batch packet comparisons including late mouth writes, physics metadata rebinding without view destruction, owner packet construction while other model jobs may run, and Step/Skip during pending capture; single, dual, triple, four-model and mixed valid/unknown batches; deferred explicit/implicit join, pending shutdown and cross-thread retirement; 5 shutdown/reloads, duplicate IDs and retained packets after models destroyed. Host texture callbacks assert owner thread. No GPU pixel or physical scheduling assertion.','worker_start_failure_injected':a.fail_worker,'tsan_process_aslr_disabled':a.tsan and a.tsan_no_aslr,'sanitizer':'TSan' if a.tsan else 'ASan + UBSan','result':r.stdout.strip(),'fixtures':[{'case':'sample%02d'%(n+1),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for n,path in enumerate(a.assets)],'sources':{}}
for repo,paths in [(root,['host-direct/src/emote_recovery_facade.cpp','host-direct/src/recovered_eval_pool.hpp','core/src/runtime/emote.rs','core/src/runtime/emote/recovery.rs','tests/emote_recovery/model_parallel.cpp']),(recovery,['native/model_view.c','native/model_view.h','native/patch_ops.h','native/timeline.c','native/model_controls.c','native/model_controls.h','recovered/mesh.c','recovered/mesh.h'])]:
 for name in paths:report['sources'][('recovery/' if repo==recovery else '')+name]=hashlib.sha256((repo/name).read_bytes()).hexdigest()
(root/'tests/emote_recovery'/('model-parallel-'+('fallback' if a.fail_worker else 'tsan' if a.tsan else 'regression')+'.json')).write_text(json.dumps(report,indent=2)+'\n')
