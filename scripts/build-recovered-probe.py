from pathlib import Path
import subprocess,hashlib,json,zipfile,shutil,argparse
parser=argparse.ArgumentParser();parser.add_argument('--upload',action='store_true');parser.add_argument('--prebound',action='store_true');parser.add_argument('--queued',action='store_true');parser.add_argument('--exclude-system-hud',action='store_true');args=parser.parse_args();assert not(args.upload and (args.prebound or args.queued))
if args.queued:args.prebound=True
root=Path(__file__).resolve().parents[1];recovery=root.parent/'emote-psv-test';out=root/('build/recovered-queue-device' if args.queued else 'build/recovered-upload-device' if args.upload else 'build/recovered-parent-prebind-device' if args.prebound else 'build/recovered-packet-device');out.mkdir(parents=True,exist_ok=True)
sdk='/home/qxj00/ae3-vitagl-build-20260830/vitasdk'
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
def run(*args):subprocess.run(['wsl','-e',*args],check=True)
incs=[root/'host-direct/src',root/'host',root/'vendor/stb',root/'build/media-sdk/include',root/'build/update-network/install/include',recovery/'native',recovery/'recovered']
flags=['-O2','-ffp-contract=off','-ffunction-sections','-fdata-sections','-DDIRECT_RECOVERED_PACKET=1','-DDIRECT_RECOVERED_PROBE_DIAGNOSTICS=1',*['-I'+linux(p) for p in incs]]
if args.exclude_system_hud:
 assert args.prebound, 'HUD exclusion requires a paired probe'
 flags.append('-DRECOVERED_PROBE_EXCLUDE_SYSTEM_HUD=1')
if args.prebound:flags.append('-DRECOVERED_PREBOUND_PROBE=1')
if args.queued:flags+=['-DRECOVERED_QUEUE_PROBE=1','-DDIRECT_DEFERRED_FINISH_PROBE=1']
sources=[root/'host-direct/src/gpu.cpp',root/('tests/direct_gxm/recovered_upload_probe.cpp' if args.upload else 'tests/direct_gxm/recovered_probe.cpp'),recovery/'native/render_packet.c',recovery/'recovered/material.c'];objects=[]
if args.upload:sources.append(root/'host-direct/src/recovered_emote_bridge.cpp')
inputs=sources+[root/'host-direct/src/gpu.hpp',root/'host-direct/src/recovered_clip_region.hpp',root/'host-direct/src/recovered_packet.inl',root/'host-direct/src/recovered_original_shaders.h',root/'host-direct/src/recovered_original_mask_shaders.h',recovery/'native/render_packet.h',recovery/'native/render_packet_internal.h',Path(__file__)]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
for i,p in enumerate(sources):
 obj=out/(str(i)+'.o');objects.append(obj);run(sdk+'/bin/arm-vita-eabi-'+('g++' if p.suffix=='.cpp' else 'gcc'),*flags,*(['-std=c++17'] if p.suffix=='.cpp' else []),'-c',linux(p),'-o',linux(obj))
run(sdk+'/bin/arm-vita-eabi-g++',*[linux(p) for p in objects],'-Wl,-q','-Wl,--gc-sections','-Wl,--defsym=__sce_headroom=0x10000','-L'+sdk+'/arm-vita-eabi/lib','-lvitashark','-lSceShaccCg_stub_weak','-lSceKernelModulemgr_stub','-lSceGxm_stub','-lSceDisplay_stub','-lSceCtrl_stub','-lScePower_stub','-Wl,--whole-archive','-lpthread','-Wl,--no-whole-archive','-lm','-lc','-o',linux(out/'probe.elf'))
run(sdk+'/bin/vita-elf-create',linux(out/'probe.elf'),linux(out/'probe.velf'))
run(sdk+'/bin/vita-make-fself',linux(out/'probe.velf'),linux(out/'eboot.bin'))
run(sdk+'/bin/vita-mksfoex','-s','TITLE_ID=EORC00001','-s','APP_VER=01.00','-d','ATTRIBUTE2=12','Recovered host probe',linux(out/'param.sfo'))
vpk=out/'recovered-host-probe.vpk'
with zipfile.ZipFile(vpk,'w',zipfile.ZIP_DEFLATED) as z:z.write(out/'eboot.bin','eboot.bin');z.write(out/'param.sfo','sce_sys/param.sfo')
files=sources+[root/'host-direct/src/gpu.hpp',root/'host-direct/src/recovered_clip_region.hpp',root/'host-direct/src/recovered_packet.inl',root/'host-direct/src/recovered_original_shaders.h',root/'host-direct/src/recovered_original_mask_shaders.h',recovery/'native/render_packet.h',recovery/'native/render_packet_internal.h',Path(__file__)]
report={'status':'built_not_device_tested','excluded_pixel_rect':[0,0,320,148] if args.exclude_system_hud else None,'prebound_pixel_pairs':args.prebound,'queued_pixel_pairs':args.queued,'title_id':'EORC00001','stage_count':29 if args.upload else 13,'vpk_sha256':hashlib.sha256(vpk.read_bytes()).hexdigest(),'eboot_sha256':hashlib.sha256((out/'eboot.bin').read_bytes()).hexdigest(),'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'stages':('29 stages: opacity, consecutive packet state, original transparent groups, actual upload/defer/token resolution and in-scene retirement, linear/swizzled BC3, opaque nested parent controls' if args.upload else '0..11 mapped0/1 x soft0/1 x groupDepth0/1/2; stage12 unsupported opacity recovery; all textures released inside scene through host retirement')}
assert report['sources']==before, 'Build source changed; do not deploy this artifact'
(out/'build-verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
