cmake_minimum_required(VERSION 3.10)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/library)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(art3m1s_gxm LANGUAGES C CXX)
option(ART3M1S_NATIVE_RENDERER "Isolated native-reference renderer experiment" OFF)
set(ART3M1S_CORE_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../build/rust/armv7-sony-vita-newlibeabihf/release/libart3m1s_core.a" CACHE FILEPATH "Core static library")

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(VERSION_BUILD "0")
set(PACKAGE_NAME "io.art3m1s.psv.gxm")
set(PSN_TITLE_ID "ART3GXM01")
set(ART3M1S_APP_NAME "art3m1s GXM")
if(ART3M1S_NATIVE_RENDERER)
    set(PSN_TITLE_ID "ART3NAT01")
    set(ART3M1S_APP_NAME "art3m1s Native v1")
endif()
set(PSN_VERSION "01.00")

add_subdirectory(${BOREALIS_LIBRARY} ${CMAKE_BINARY_DIR}/borealis)

program_target(${PROJECT_NAME}
    "src/main.cpp;src/library_activity.cpp;src/game_library.cpp;src/game_surface.cpp;src/gxm_bridge.cpp;src/math_compat.c;src/media_gxm.cpp;src/video_gxm.cpp;../host/files.c;../host/audio.c;../host/audio_vorbis.c;../host/media_io.c;../host/video.c;../host/video_direct.c;../vendor/cJSON/cJSON.c")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(ART3M1S_NATIVE_RENDERER)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ART3M1S_NATIVE_RENDERER=1)
endif()
option(ART3M1S_IMAGE_QUAD_AB "Diagnostic only: alternate legacy and image-quad frames" OFF)
option(ART3M1S_SAFE_PRESENT "Hardware flicker candidate: finish GPU before display submission" OFF)
option(ART3M1S_ASYNC_PRESENT "Hardware candidate: per-frame fragment notification in display callback" OFF)
if(ART3M1S_SAFE_PRESENT AND ART3M1S_ASYNC_PRESENT)
    message(FATAL_ERROR "Choose one present candidate")
endif()
if(ART3M1S_NATIVE_RENDERER AND ART3M1S_SAFE_PRESENT)
    message(FATAL_ERROR "Native reference and safe-present have different submission orders; choose one")
endif()
if(ART3M1S_ASYNC_PRESENT)
    message(FATAL_ERROR "ART3M1S_ASYNC_PRESENT is withdrawn: physical Vita black-screens at startup. Use ART3M1S_SAFE_PRESENT for the verified display path.")
endif()
if(ART3M1S_SAFE_PRESENT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ART3M1S_SAFE_PRESENT=1)
endif()
if(ART3M1S_IMAGE_QUAD_AB)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ART3M1S_IMAGE_QUAD_AB=1)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/../host
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/include
    ${CMAKE_CURRENT_LIST_DIR}/../build/tremor
    ${CMAKE_CURRENT_LIST_DIR}/../vendor/tremor
    ${CMAKE_CURRENT_LIST_DIR}/../vendor/cJSON
    ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
target_compile_definitions(${PROJECT_NAME} PRIVATE ART3M1S_HOST_GXM=1 ART3M1S_EXPERIMENTAL_VITA_HW=1)
target_link_options(${PROJECT_NAME} PRIVATE
    -Wl,--gc-sections
    -Wl,--defsym=__sce_headroom=0x10000)
target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    ${ART3M1S_CORE_LIBRARY}
    ${APP_PLATFORM_LIB}
    ${CMAKE_CURRENT_LIST_DIR}/../build/tremor/libvorbisidec.a
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libavformat.a
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libavcodec.a
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libswresample.a
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libswscale.a
    ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libavutil.a
    SceAudio_stub SceAudiodec_stub SceVideodec_stub SceCodecEngine_stub
    m c)

if(PLATFORM_PSV)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
    vita_create_vpk(${PROJECT_NAME}.vpk ${PSN_TITLE_ID} ${PROJECT_NAME}.self
        VERSION ${PSN_VERSION}
        NAME "${ART3M1S_APP_NAME}"
        FILE
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/psv/sce_sys sce_sys
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/resources resources
            ${CMAKE_CURRENT_LIST_DIR}/../host/assets/menu.ttf resources/font/font.ttf
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/resources/font/switch_icons.ttf resources/font/icon.ttf
            ${CMAKE_CURRENT_LIST_DIR}/../host/assets/FONT-LICENSE.txt licenses/MENU-FONT.txt
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/tremor/COPYING licenses/TREMOR.txt
            ${CMAKE_CURRENT_LIST_DIR}/../vendor/ffmpeg/COPYING.LGPLv2.1 licenses/FFMPEG-LGPLv2.1.txt
    )
endif()

option(ART3M1S_BUILD_GXM_NV12_PROBE "Build the native GXM video lifetime probe" OFF)
option(ART3M1S_BUILD_IMAGE_QUAD_PROBE "Build the image quad pixel comparison" OFF)
if(PLATFORM_PSV AND ART3M1S_BUILD_IMAGE_QUAD_PROBE)
    add_executable(art3_image_quad_probe ../tests/image_quad/render.cpp src/math_compat.c)
    if(ART3M1S_NATIVE_RENDERER)
        target_compile_definitions(art3_image_quad_probe PRIVATE ART3M1S_NATIVE_RENDERER=1)
    endif()
    set_target_properties(art3_image_quad_probe PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(art3_image_quad_probe PRIVATE ${APP_PLATFORM_OPTION})
    target_include_directories(art3_image_quad_probe PRIVATE ${APP_PLATFORM_INCLUDE})
    target_link_libraries(art3_image_quad_probe PRIVATE borealis ${APP_PLATFORM_LIB} m c)
    vita_create_self(art3_image_quad_probe.self art3_image_quad_probe UNSAFE)
    set(VITA_PACK_VPK_FLAGS "")
    vita_create_vpk(art3_image_quad_probe.vpk ART3GQP01 art3_image_quad_probe.self
        VERSION "01.00" NAME "art3m1s Image Quad Probe"
        FILE ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/resources resources
             ${CMAKE_CURRENT_LIST_DIR}/../host/assets/menu.ttf resources/font/font.ttf)
endif()
option(ART3M1S_BUILD_RULE_RENDER_PROBE "Build the native rule shader pixel probe" OFF)
if(PLATFORM_PSV AND ART3M1S_BUILD_RULE_RENDER_PROBE)
    add_executable(art3_rule_render ../tests/rule_shader/render.cpp src/math_compat.c)
    set_target_properties(art3_rule_render PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(art3_rule_render PRIVATE ${APP_PLATFORM_OPTION})
    target_include_directories(art3_rule_render PRIVATE ${APP_PLATFORM_INCLUDE})
    target_link_libraries(art3_rule_render PRIVATE borealis ${APP_PLATFORM_LIB} m c)
    vita_create_self(art3_rule_render.self art3_rule_render UNSAFE)
    set(VITA_PACK_VPK_FLAGS "")
    vita_create_vpk(art3_rule_render.vpk ART3GRP01 art3_rule_render.self
        VERSION "01.00" NAME "art3m1s Rule GPU Probe"
        FILE ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/resources resources
             ${CMAKE_CURRENT_LIST_DIR}/../host/assets/menu.ttf resources/font/font.ttf
             ${CMAKE_CURRENT_LIST_DIR}/../host/assets/FONT-LICENSE.txt licenses/MENU-FONT.txt)
endif()
if(PLATFORM_PSV AND ART3M1S_BUILD_GXM_NV12_PROBE)
    add_executable(gxm_nv12_probe ../tests/video_gxm/probe.cpp src/video_gxm.cpp src/math_compat.c ../host/video_direct.c)
    set_target_properties(gxm_nv12_probe PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(gxm_nv12_probe PRIVATE include ../host ../build/media-sdk/include ${APP_PLATFORM_INCLUDE})
    target_compile_options(gxm_nv12_probe PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
    target_compile_definitions(gxm_nv12_probe PRIVATE ART3M1S_HOST_GXM=1)
    target_link_options(gxm_nv12_probe PRIVATE -Wl,--gc-sections)
    target_link_libraries(gxm_nv12_probe PRIVATE borealis ${CMAKE_CURRENT_LIST_DIR}/../build/media-sdk/lib/libavutil.a ${APP_PLATFORM_LIB} m c)
    vita_create_self(gxm_nv12_probe.self gxm_nv12_probe UNSAFE)
    # VitaSDK's packaging macro appends FILE arguments to this caller variable.
    set(VITA_PACK_VPK_FLAGS "")
    vita_create_vpk(gxm_nv12_probe.vpk ART3GN012 gxm_nv12_probe.self
        VERSION "01.00" NAME "GXM NV12 probe"
        FILE ${CMAKE_CURRENT_LIST_DIR}/../vendor/borealis/resources resources
             ${CMAKE_CURRENT_LIST_DIR}/../host/assets/menu.ttf resources/font/font.ttf)
endif()
