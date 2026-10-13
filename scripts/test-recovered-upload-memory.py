"""Exercise the production upload allocator, with OS allocation/map failures."""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[1];out=root/'build/recovered-upload-memory';out.mkdir(parents=True,exist_ok=True)
p=root/'host-direct/src/gpu.cpp';text=p.read_text(encoding='utf-8')
code=text[text.index('Memory allocate_upload('):text.index('void release(Memory m)')]
fixture=r'''#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstdlib>
constexpr int SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE=2,SCE_GXM_MEMORY_ATTRIB_RW=3;
struct Charge{size_t bytes=0;unsigned region=0,owner=0;bool retired=false;};
struct Memory{int uid=-1;void* p=nullptr;int usse=0;Charge charge;};
static int mode=0,fallbacks=0,blocks=0;static int64_t live=0,reserved=0;static size_t requested=0;static char* memory=nullptr;
void resource_event(unsigned r,unsigned owner,int64_t l,int64_t s){assert(r==2&&owner==5);live+=l;reserved+=s;}
void resource_free(Charge c){assert(c.region==2&&c.owner==5);live-=c.bytes;}
void log(const char*,...){} bool check(int r,const char*){return r>=0;}
int sceKernelAllocMemBlock(const char*,int type,size_t n,void*){assert(type==2);requested=n;if(mode==1)return -10;memory=(char*)std::malloc(n);++blocks;return 1;}
int sceKernelGetMemBlockBase(int,void** p){if(mode==2)return -20;*p=memory;return 0;}
int sceGxmMapMemory(void* p,size_t n,int flags){assert(p==memory&&n==requested&&flags==3);return mode==3?-30:0;}
int sceKernelFreeMemBlock(int){std::free(memory);memory=nullptr;--blocks;return 0;}
Memory allocate(size_t n,int usse,unsigned owner){assert(n==4097&&usse==0&&owner==5);++fallbacks;return {};}
'''
main=r'''int main(){
 for(mode=0;mode<4;++mode){auto m=allocate_upload(4097,5);assert(requested==8192&&reserved==0);
  if(mode==0){assert(m.p==memory&&m.charge.bytes==8192&&live==8192&&blocks==1);sceKernelFreeMemBlock(m.uid);resource_free(m.charge);}
  else assert(!m.p&&live==0&&blocks==0);
 }
 assert(fallbacks==1);assert(!allocate_upload(0,5).p);assert(!allocate_upload(SIZE_MAX,5).p);assert(live==0&&reserved==0&&blocks==0);
 std::puts("PASS upload allocator: uncached/alignment/charge, allocation fallback, base/map failure cleanup, zero/overflow");
}'''
(out/'test.cpp').write_text(fixture+code+main,encoding='utf-8')
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
subprocess.run(['wsl','-e','g++','-O2','-g','-fsanitize=address,undefined','-no-pie',linux(out/'test.cpp'),'-o',linux(out/'test')],check=True)
r=subprocess.run(['wsl','-e',linux(out/'test')],text=True,capture_output=True);print(r.stdout,r.stderr);r.check_returncode();assert not r.stderr
(root/'tests/direct_gxm/recovered-upload-memory-verification.json').write_text(json.dumps({'status':'pass','output':r.stdout,'gpu_cpp_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'scope':'Production upload allocator; OS calls mocked; physical GPU performance is separate.'},indent=2)+'\n',encoding='utf-8')
