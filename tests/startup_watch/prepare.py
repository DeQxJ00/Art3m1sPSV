"""Create an isolated diagnostic host from the exact current build mirror.

Does not replace shaders, core archive, game assets, or the performance host.
Run from Windows, then run build/startup-watch/build.sh through WSL.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import argparse
import shlex

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--output',default='build/startup-watch')
p.add_argument('--current-video-gpu',action='store_true')
p.add_argument('--current-host',action='store_true',help='Use current host-direct source and CMake rather than the historical mirror')
p.add_argument('--core-library',default='build/video-yuva-gxm/libart3m1s_core.a')
args=p.parse_args()
out=(root/args.output).resolve()
assert out.is_relative_to(root/'build')
relative=out.relative_to(root).as_posix()
library=(root/args.core_library).resolve()
assert library.is_relative_to(root/'build') and library.is_file()
library_wsl='/mnt/'+library.drive[0].lower()+library.as_posix()[2:]
source=out/'source'
source.mkdir(parents=True,exist_ok=True)
mirror=root/'build/async-loader-host-source'
if args.current_host:mirror=root/'host-direct'
for name in ('src','shaders'):
    shutil.copytree(mirror/name,source/name,dirs_exist_ok=True)
if args.current_video_gpu:
    for name in ('gpu.cpp','gpu.hpp','bridge.cpp','video_yuva.inl'):
        shutil.copy2(root/'host-direct/src'/name,source/'src'/name)
shutil.copy2(root/'tests/startup_watch/watch.hpp',source/'src/startup_watch.hpp')

def change(s,old,new):
    assert s.count(old)==1,(old,s.count(old))
    return s.replace(old,new)

s=(mirror/'src/main.cpp').read_text(encoding='utf-8')
s='#include "startup_watch.hpp"\n'+s
replacements={
 '    art3m1s_register_worker_init_callback(host_background_thread_enter);':'    art3m1s_register_worker_init_callback([](const char* role){startupwatch::worker_started(role);host_background_thread_enter(role);});',
 'int main(){':'int main(){startupwatch::start();',
 '        art3m1s_runtime_advance_without_render(runtime,delta);':'        startupwatch::mark("runtime-advance");art3m1s_runtime_advance_without_render(runtime,delta);',
 '        art3m1s_runtime_prepare_gxm_textures(runtime);':'        startupwatch::mark("texture-prepare");art3m1s_runtime_prepare_gxm_textures(runtime);',
 '        input(pad,touch);':'        startupwatch::mark("game-input");input(pad,touch);',
 'const bool rendered=art3m1s_runtime_present_gxm(runtime)!=0;':'startupwatch::mark("runtime-present");const bool rendered=art3m1s_runtime_present_gxm(runtime)!=0;',
 'previousTouch=touch.reportNum>0;gxm_media_pump();':'previousTouch=touch.reportNum>0;startupwatch::mark("media-pump");gxm_media_pump();',
 '        if(game){game->tick(pad,touch);':'        startupwatch::mark("game-tick");if(game){game->tick(pad,touch);',
 '        if(game)game->prepare();':'        startupwatch::mark("game-prepare");if(game)game->prepare();',
 't2=sceKernelGetProcessTimeWide();direct::begin();':'t2=sceKernelGetProcessTimeWide();startupwatch::mark("gxm-begin");direct::begin();',
 '        direct::end();const uint64_t t3=':'        startupwatch::mark("gxm-end");direct::end();startupwatch::mark("frame-finish");const uint64_t t3=',
 '        if(now-heartbeat>5000000){heartbeat=now;':'        startupwatch::mark("heartbeat-log");if(now-heartbeat>5000000){heartbeat=now;',
 '            report_log_timing();flush_log();}':'            startupwatch::mark("log-stats");report_log_timing();startupwatch::mark("log-flush");flush_log();}',
 '    game.reset();direct::menu_release();':'    startupwatch::stop();game.reset();direct::menu_release();',
}
for a,b in replacements.items():s=change(s,a,b)
(source/'src/main.cpp').write_text(s,encoding='utf-8',newline='\n')

v=(root/'host/video.c').read_text(encoding='utf-8')
v='extern void art3_startup_mark(const char*);\n'+v
for a,b in {
 '    host_video_close();const char *name=str(j,"id");':'    art3_startup_mark("video-open-close-old");host_video_close();art3_startup_mark("video-resolve");const char *name=str(j,"id");',
 '    int r=host_media_input_open(&input,path);':'    art3_startup_mark("video-input-open");int r=host_media_input_open(&input,path);',
 '    if(!decoder && (r=create_video_decoder(codec,0,0))<0)return r;':'    art3_startup_mark("video-codec-open");if(!decoder && (r=create_video_decoder(codec,0,0))<0)return r;',
 '    if(*id && (r=open_mask(path))<0)return r;':'    art3_startup_mark("video-mask-open");if(*id && (r=open_mask(path))<0)return r;art3_startup_mark("video-preload");',
 '    draining=0;frames_uploaded=0;origin_pts=AV_NOPTS_VALUE;':'    art3_startup_mark("video-activate");draining=0;frames_uploaded=0;origin_pts=AV_NOPTS_VALUE;',
}.items():v=change(v,a,b)
(source/'video.c').write_text(v,encoding='utf-8',newline='\n')
cmake=(mirror/'CMakeLists.txt').read_text(encoding='utf-8')
if args.current_host:
    root_wsl='/mnt/'+root.drive[0].lower()+root.as_posix()[2:]
    cmake=change(cmake,'set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")','set(ROOT "'+root_wsl+'")')
cmake=change(cmake,'${ROOT}/host/video.c','${CMAKE_CURRENT_LIST_DIR}/video.c')
(source/'CMakeLists.txt').write_text(cmake,encoding='utf-8',newline='\n')
cache=(root/'build/async-loader-host/CMakeCache.txt').read_text()
options=['-D'+m.group(1)+'='+m.group(2) for m in re.finditer(r'^(DIRECT_\w+):BOOL=(ON|OFF)$',cache,re.M)]
options+=['-DART3_DIRECT_VERSION=01.10','-DART3_DIRECT_CORE_LIBRARY='+library_wsl]
script='''#!/usr/bin/env bash
set -euo pipefail
export VITASDK=/home/qxj00/ae3-vitagl-build-20260830/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S '''+shlex.quote(relative+'/source')+' -B '+shlex.quote(relative+'/host')+' '+' '.join(map(shlex.quote,options))+'''
cmake --build '''+shlex.quote(relative+'/host')+''' --target art3m1s_direct.vpk-vpk -j2
'''
(out/'build.sh').write_text(script,encoding='utf-8',newline='\n')
hashes={name:hashlib.sha256((source/'src'/name).read_bytes()).hexdigest()
        for name in ('shaders.hpp','builtin_shader.hpp','video_yuva_shader.hpp','video_yuva.inl')}
for name in hashes:
    reference=root/'host-direct/src' if args.current_video_gpu and name=='video_yuva.inl' else mirror/'src'
    assert (source/'src'/name).read_bytes()==(reference/name).read_bytes()
(out/'source-manifest.json').write_text(json.dumps(dict(options=options,hashes=hashes,current_host=args.current_host,current_video_gpu=args.current_video_gpu,core=str(library),core_sha256=hashlib.sha256(library.read_bytes()).hexdigest()),indent=2))
print(source)
