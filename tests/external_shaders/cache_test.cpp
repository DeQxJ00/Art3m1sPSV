#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <sys/stat.h>
#include <unistd.h>
#include "../../host-direct/src/shader_settings.hpp"
#include "../../host-direct/src/shader_cache_path.hpp"
using direct::ShaderSettings;using direct::shader_cache_relative;
static bool active=false,failCompile=false;static int compiles=0,inits=0,ends=0;
struct SceGxmProgram {unsigned char bytes[156];};
using shark_log_level=int;
enum {SHARK_WARN_HIGH,SHARK_FRAGMENT_SHADER,SHARK_OPT_SAFE,SHARK_DISABLE};
void log(const char*,...){}
uint64_t sceKernelGetProcessTimeWide(){static uint64_t t=0;return ++t;}
int sceIoMkdir(const char*p,int m){return mkdir(p,m);}int sceIoRename(const char*a,const char*b){return rename(a,b);}int sceIoRemove(const char*p){return unlink(p);}
void shark_install_log_cb(void(*)(const char*,int,int)){} void shark_set_warnings_level(int){}
int shark_init(void*){++inits;return 0;}void shark_end(){++ends;}void shark_clear_output(){}
SceGxmProgram* shark_compile_shader_extended(const char*,uint32_t* n,int,int,int,int,int){++compiles;if(failCompile)return nullptr;*n=156;auto*p=(SceGxmProgram*)calloc(1,156);memcpy(p,"GXP",4);return p;}
unsigned external_register(const uint8_t*p,size_t n){return n==156&&!memcmp(p,"GXP",4)?1:0;}
#include "../../host-direct/src/external_shader_compiler.inl"
int main(){char base[]="/tmp/art3-shader-cache-XXXXXX";assert(mkdtemp(base));std::string a=std::string(base)+"/game-a",b=std::string(base)+"/game-b";
 assert(!external_conversion_enabled());assert(!ShaderSettings{}.compile);
 ShaderSettings missing{true,true};assert(direct::load_shader_settings(std::string(base)+"/missing-settings",missing)&&!missing.convert&&!missing.compile);
 external_shader_options({true,true});
 external_cache_root(a);assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==1);
 external_compiler_end();assert(ends==1);assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==1&&inits==1);
 assert(external_compile("blur_k","source-changed","cg-a"));assert(compiles==2);
 external_cache_root(b);assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==3);
 external_cache_root(a);const std::string identity="cg-front-v1|shark-safe-no-fast|sprite-abi1|source-a|missing|cg-a";
 auto file=a+"/blur_k.gxp";
 assert(shader_write(file,"broken",6));assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==4);
 failCompile=true;assert(!external_compile("blur_k","new-failed-source","cg-failed"));assert(compiles==5);
 failCompile=false;assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==5);
 external_compiler_end();external_shader_options({false,false});
 assert(!external_conversion_enabled());assert(external_compile("blur_k","source-a","cg-a"));assert(compiles==5);
 assert(!external_compile("blur_k","changed","cg-a"));assert(compiles==5);
 external_shader_options({true,false});assert(external_conversion_enabled());
 assert(!external_compile("system/shader/pc/blur_k.hlsl","a","cg"));assert(compiles==5);
 external_shader_options({false,true});assert(!external_conversion_enabled());
 assert(external_compile("system/shader/pc/blur_k.hlsl","a","cg"));assert(compiles==6);
 assert(!shader_read(a+"/system/shader/pc/blur_k.hlsl.gxp",1024).empty());
 for(auto path:{"../escape","/absolute","a/../../escape","ux0:/absolute","a/../b","a/evil."})assert(shader_cache_relative(path).empty());
 assert(shader_cache_relative("system\\shader\\pc\\gray.hlsl")=="system/shader/pc/gray.hlsl");
 assert(shader_cache_relative("system//shader/./pc/gray.hlsl")=="system/shader/pc/gray.hlsl");
 ShaderSettings setting;assert(direct::save_shader_settings(a+"/settings",{false,true}));
 assert(direct::load_shader_settings(a+"/settings",setting)&&!setting.convert&&setting.compile);
 assert(!direct::parse_shader_settings("1 1 0 junk",setting));assert(!direct::parse_shader_settings("1 2 0",setting));
 external_compiler_end();puts("PASS cache isolation/invalidation, all four toggle combinations, nested paths, traversal rejection, settings persistence");
}
