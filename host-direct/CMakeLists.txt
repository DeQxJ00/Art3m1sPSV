cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(art3m1s_direct LANGUAGES C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(ART3_DIRECT_CORE_LIBRARY "${ROOT}/build/direct-opt2/baseline/libart3m1s_core.a" CACHE FILEPATH "Core archive; defaults to the verified 01.02 baseline")
if(NOT EXISTS "${ART3_DIRECT_CORE_LIBRARY}")
  message(FATAL_ERROR "Missing core archive. See baselines/01.02/README.zh-CN.md")
endif()
if(ART3_DIRECT_CORE_LIBRARY STREQUAL "${ROOT}/build/direct-opt2/baseline/libart3m1s_core.a")
  file(SHA256 "${ART3_DIRECT_CORE_LIBRARY}" CORE_BASELINE_SHA256)
  if(NOT CORE_BASELINE_SHA256 STREQUAL "a93e5a7ec538cfe1e41cdaf7b952b99f83d745e47d35f2923b492150a31c6dbb")
    message(FATAL_ERROR "01.02 baseline core hash mismatch")
  endif()
endif()
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
  ${ART3_DIRECT_CORE_LIBRARY}
  ${ROOT}/build/tremor/libvorbisidec.a
  ${ROOT}/build/media-sdk/lib/libavformat.a ${ROOT}/build/media-sdk/lib/libavcodec.a
  ${ROOT}/build/media-sdk/lib/libswresample.a ${ROOT}/build/media-sdk/lib/libswscale.a
  ${ROOT}/build/media-sdk/lib/libavutil.a
  SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub SceAppUtil_stub ScePower_stub
  SceAudio_stub SceAudiodec_stub SceVideodec_stub SceCodecEngine_stub
  -Wl,--whole-archive pthread -Wl,--no-whole-archive m c)
vita_create_self(art3m1s_direct.self art3m1s_direct UNSAFE)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_vpk(art3m1s_direct.vpk ART3DIR01 art3m1s_direct.self
  VERSION "01.02" NAME "art3m1s Direct GXM"
  FILE ${ROOT}/host/assets/menu.ttf assets/menu.ttf
       ${ROOT}/host/assets/FONT-LICENSE.txt licenses/MENU-FONT.txt
       ${ROOT}/vendor/stb/LICENSE licenses/STB.txt
       ${ROOT}/vendor/tremor/COPYING licenses/TREMOR.txt
       ${ROOT}/vendor/ffmpeg/COPYING.LGPLv2.1 licenses/FFMPEG-LGPLv2.1.txt)

option(DIRECT_GXM_PROBE "Build standalone direct-renderer pixel probe" OFF)
if(DIRECT_GXM_PROBE)
  add_executable(upload_bench ${ROOT}/tests/direct_gxm/upload_bench.cpp)
  target_include_directories(upload_bench PRIVATE src)
  target_link_libraries(upload_bench PRIVATE m c)
  vita_create_self(upload_bench.self upload_bench UNSAFE)
  vita_create_vpk(upload_bench.vpk ART3BEN01 upload_bench.self VERSION "01.00" NAME "art3m1s upload bench")
  add_executable(direct_probe ${ROOT}/tests/direct_gxm/probe.cpp src/gpu.cpp)
  target_include_directories(direct_probe PRIVATE src)
  target_link_libraries(direct_probe PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub m c)
  vita_create_self(direct_probe.self direct_probe UNSAFE)
  vita_create_vpk(direct_probe.vpk ART3DPR01 direct_probe.self VERSION "01.00" NAME "Direct GXM pixel probe")
endif()
