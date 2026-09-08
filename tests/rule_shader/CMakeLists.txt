cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
endif()
project(art3_rule_compile C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
add_executable(art3_rule_compile compile.c)
target_compile_options(art3_rule_compile PRIVATE -Wall -Wextra)
target_link_libraries(art3_rule_compile vitashark SceShaccCg_stub SceKernelModulemgr_stub SceIofilemgr_stub SceGxm_stub)
vita_create_self(art3_rule_compile.self art3_rule_compile UNSAFE)
vita_create_vpk(art3_rule_compile.vpk ART3GRC01 art3_rule_compile.self
    VERSION "01.00" NAME "art3m1s Rule Compiler"
    FILE ../../host-gxm/shaders/rule_transition_f.cg rule_transition_f.cg
         VITASHARK-LICENSE.txt licenses/VITASHARK.txt)
