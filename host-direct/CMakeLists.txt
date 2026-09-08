cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(art3m1s_direct LANGUAGES C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
option(DIRECT_UVDB "Enable opt-in real-device GDB debugging" OFF)
add_executable(art3m1s_direct src/main.cpp src/gpu.cpp src/bridge.cpp src/menu.cpp
  ${ROOT}/host-gxm/src/game_library.cpp ${ROOT}/host-gxm/src/media_gxm.cpp ${ROOT}/host-gxm/src/math_compat.c
  ${ROOT}/host/files.c ${ROOT}/host/audio.c ${ROOT}/host/audio_vorbis.c ${ROOT}/host/media_io.c
  ${ROOT}/host/video.c ${ROOT}/host/video_direct.c ${ROOT}/vendor/cJSON/cJSON.c)
target_include_directories(art3m1s_direct PRIVATE src ${ROOT}/host ${ROOT}/host-gxm/include
  ${ROOT}/build/media-sdk/include ${ROOT}/build/tremor ${ROOT}/vendor/tremor ${ROOT}/vendor/cJSON
  ${ROOT}/vendor/stb)
target_compile_definitions(art3m1s_direct PRIVATE ART3M1S_HOST_GXM=1 ART3M1S_EXPERIMENTAL_VITA_HW=1)
target_compile_options(art3m1s_direct PRIVATE -ffunction-sections -fdata-sections)
target_link_options(art3m1s_direct PRIVATE -Wl,--gc-sections -Wl,--defsym=__sce_headroom=0x10000)
target_link_libraries(art3m1s_direct PRIVATE
  ${ROOT}/build/rust-native/armv7-sony-vita-newlibeabihf/release/libart3m1s_core.a
  ${ROOT}/build/tremor/libvorbisidec.a
  ${ROOT}/build/media-sdk/lib/libavformat.a ${ROOT}/build/media-sdk/lib/libavcodec.a
  ${ROOT}/build/media-sdk/lib/libswresample.a ${ROOT}/build/media-sdk/lib/libswscale.a
  ${ROOT}/build/media-sdk/lib/libavutil.a
  SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceAppUtil_stub ScePower_stub
  SceAudio_stub SceAudiodec_stub SceVideodec_stub SceCodecEngine_stub
  -Wl,--whole-archive pthread -Wl,--no-whole-archive m c)
if(DIRECT_UVDB)
  set(UVDB_SOURCE "${ROOT}/refs/vita-uvdb")
  set(KUBRIDGE_SOURCE "${ROOT}/refs/kubridge-uvdb")
  set(KUBRIDGE_STUB "${ROOT}/build/uvdb/kubridge/libkubridge_stub.a")
  if(NOT EXISTS "${UVDB_SOURCE}/uvdb.c" OR NOT EXISTS "${KUBRIDGE_STUB}")
    message(FATAL_ERROR "Run scripts/build-direct-uvdb.sh to prepare uvdb and its kubridge stub")
  endif()
  add_library(art3_uvdb STATIC "${UVDB_SOURCE}/uvdb.c")
  target_include_directories(art3_uvdb PRIVATE "${UVDB_SOURCE}" "${KUBRIDGE_SOURCE}")
  target_compile_options(art3_uvdb PRIVATE -O0 -g3)
  target_sources(art3m1s_direct PRIVATE src/debug_uvdb.cpp)
  target_include_directories(art3m1s_direct PRIVATE "${UVDB_SOURCE}")
  target_compile_definitions(art3m1s_direct PRIVATE ART3_DIRECT_UVDB=1)
  target_compile_options(art3m1s_direct PRIVATE -g3)
  target_link_libraries(art3m1s_direct PRIVATE art3_uvdb "${KUBRIDGE_STUB}" SceNetPs_stub SceNet_stub)
endif()
vita_create_self(art3m1s_direct.self art3m1s_direct UNSAFE)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(art3m1s_direct.vpk ART3DIR01 art3m1s_direct.self
  VERSION "01.05" NAME "art3m1s Direct GXM"
  FILE ${ROOT}/host/assets/menu.ttf assets/menu.ttf
       ${ROOT}/host/assets/FONT-LICENSE.txt licenses/MENU-FONT.txt
       ${ROOT}/vendor/stb/LICENSE licenses/STB.txt
       ${ROOT}/vendor/tremor/COPYING licenses/TREMOR.txt
       ${ROOT}/vendor/ffmpeg/COPYING.LGPLv2.1 licenses/FFMPEG-LGPLv2.1.txt)

option(DIRECT_GXM_PROBE "Build standalone direct-renderer pixel probe" OFF)
if(DIRECT_GXM_PROBE)
  add_executable(direct_probe ${ROOT}/tests/direct_gxm/probe.cpp src/gpu.cpp)
  target_include_directories(direct_probe PRIVATE src)
  target_link_libraries(direct_probe PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub m c)
  vita_create_self(direct_probe.self direct_probe UNSAFE)
  vita_create_vpk(direct_probe.vpk ART3DPR01 direct_probe.self VERSION "01.00" NAME "Direct GXM pixel probe")
endif()

option(DIRECT_EFFECTS_PROBE "Build core built-in effects and bridge pixel probe" OFF)
if(DIRECT_EFFECTS_PROBE)
  add_executable(snapshot_probe ${ROOT}/tests/direct_effects/snapshot_probe.cpp src/gpu.cpp)
  target_include_directories(snapshot_probe PRIVATE src)
  target_link_libraries(snapshot_probe PRIVATE SceGxm_stub SceDisplay_stub m c)
  vita_create_self(snapshot_probe.self snapshot_probe UNSAFE)
  vita_create_vpk(snapshot_probe.vpk ART3SNP01 snapshot_probe.self VERSION "01.00" NAME "GXM Snapshot Probe")
  add_executable(effects_probe ${ROOT}/tests/direct_effects/probe.cpp src/gpu.cpp src/bridge.cpp)
  target_include_directories(effects_probe PRIVATE src ${ROOT}/host-gxm/include ${ROOT}/host)
  target_compile_options(effects_probe PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(effects_probe PRIVATE -Wl,--gc-sections)
  target_link_libraries(effects_probe PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub m c)
  vita_create_self(effects_probe.self effects_probe UNSAFE)
  vita_create_vpk(effects_probe.vpk ART3EFP01 effects_probe.self VERSION "01.00" NAME "GXM Builtin Effects Probe")
endif()
