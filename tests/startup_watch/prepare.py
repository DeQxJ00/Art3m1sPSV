"""Create an isolated diagnostic host from the exact current build mirror.

Does not replace shaders, core archive, game assets, or the performance host.
Run from Windows, then run build/startup-watch/build.sh through WSL.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil

root=Path(__file__).resolve().parents[2]
out=root/'build/startup-watch'
source=out/'source'
source.mkdir(parents=True,exist_ok=True)
mirror=root/'build/async-loader-host-source'
for name in ('src','shaders'):
    shutil.copytree(mirror/name,source/name,dirs_exist_ok=True)
shutil.copy2(root/'tests/startup_watch/watch.hpp',source/'src/startup_watch.hpp')

def change(s,old,new):
    assert s.count(old)==1,(old,s.count(old))
    return s.replace(old,new)

s=(mirror/'src/main.cpp').read_text(encoding='utf-8')
s='#include "startup_watch.hpp"\n'+s
replacements={
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
cmake=change(cmake,'${ROOT}/host/video.c','${CMAKE_CURRENT_LIST_DIR}/video.c')
(source/'CMakeLists.txt').write_text(cmake,encoding='utf-8',newline='\n')
cache=(root/'build/async-loader-host/CMakeCache.txt').read_text()
options=['-D'+m.group(1)+'='+m.group(2) for m in re.finditer(r'^(DIRECT_\w+):BOOL=(ON|OFF)$',cache,re.M)]
options+=['-DART3_DIRECT_VERSION=01.10','-DART3_DIRECT_CORE_LIBRARY=/mnt/f/WorkSpaceAI2/art3m1s-psv-gxm/build/video-yuva-gxm/libart3m1s_core.a']
script='''#!/usr/bin/env bash
set -euo pipefail
export VITASDK=/home/qxj00/ae3-vitagl-build-20260830/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S build/startup-watch/source -B build/startup-watch/host '''+' '.join(options)+'''
cmake --build build/startup-watch/host --target art3m1s_direct.vpk-vpk -j2
'''
(out/'build.sh').write_text(script,encoding='utf-8',newline='\n')
hashes={name:hashlib.sha256((source/'src'/name).read_bytes()).hexdigest()
        for name in ('shaders.hpp','builtin_shader.hpp','video_yuva_shader.hpp','video_yuva.inl')}
for name in hashes:assert (source/'src'/name).read_bytes()==(mirror/'src'/name).read_bytes()
(out/'source-manifest.json').write_text(json.dumps(dict(options=options,unchanged=hashes),indent=2))
print(source)
