"""Exercise production NanoVG quad geometry and GXM queue with CPU textures.

No GPU is mocked as passed: pixel equivalence is covered by the Vita probe.
Run under WSL: python3 tests/image_quad/test_queue.py
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'vendor/borealis/library/include/borealis/extern/nanovg/nanovg_gxm.h').read_text()

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    pos = brace + 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos] + '\n'

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define NVG_NO_STB
#include "nanovg.c"
typedef NVGcompositeOperationState GXMNVGblend;
typedef struct { int id, type, flags; } GXMNVGtexture;
enum { NVG_ANTIALIAS=1, GXMNVG_TRIANGLES=4, GXMNVG_NATIVE_QUAD=7, GXMNVG_CONVEXFILL=2,
 NSVG_SHADER_FILLIMG=2, NSVG_SHADER_FILLGRAD=1, NSVG_SHADER_FILLCOLOR=0,
 NSVG_SHADER_RGBAIMG=6 };
'''
types = source[source.index('struct GXMNVGcall {'):source.index('struct GXMNVGcontext {')]
prefix += types + r'''
typedef struct {
 struct { struct { void *frag; } prog; } image_quad_shader;
 int flags, ncalls, ccalls, nverts, cverts, nuniforms, cuniforms, fragSize;
 int nativeSprites, nativeTextureSlots[256], ntextures;
 struct { uint64_t textureLookups, textureComparisons; } stats;
 GXMNVGcall *calls; NVGvertex *verts; unsigned char *uniforms;
 GXMNVGtexture textures[3];
} GXMNVGcontext;
static int nvg_gxm_vertex_buffer_size=1024*1024;
static int fail_allocation;
static void *checked_realloc(void *p, size_t size) {
 if (fail_allocation && --fail_allocation==0) return NULL;
 return realloc(p,size);
}
#define realloc checked_realloc
static int gxmnvg__maxi(int a,int b) { return a>b?a:b; }
static GXMNVGblend gxmnvg__blendCompositeOperation(NVGcompositeOperationState b) { return b; }
'''
production = ''.join(function(name) for name in [
    'static GXMNVGtexture *gxmnvg__findTexture(',
    'static int gxmnvg__sameBlend(',
    'static NVGcolor gxmnvg__premulColor(',
    'static void gxmnvg__xformToMat3x4(',
    'static int gxmnvg__convertPaint(',
    'static GXMNVGcall *gxmnvg__allocCall(',
    'static int gxmnvg__allocVerts(',
    'static int gxmnvg__allocFragUniforms(',
    # Skip the forward declaration by including its opening brace in the search.
    'static GXMNVGfragUniforms *nvg__fragUniformPtr(GXMNVGcontext *gxm, int i) {',
    'static int gxmnvg__renderImageTriangles(',
])
suffix = r'''
static void reset(GXMNVGcontext *g) {
 free(g->calls);free(g->verts);free(g->uniforms); memset(g,0,sizeof(*g));
 g->fragSize=sizeof(GXMNVGfragUniforms);g->image_quad_shader.prog.frag=g;
 g->ntextures=3;
 g->textures[0]=(GXMNVGtexture){1,NVG_TEXTURE_RGBA,0};
 g->textures[1]=(GXMNVGtexture){2,NVG_TEXTURE_RGBA,0};
 g->textures[2]=(GXMNVGtexture){3,NVG_TEXTURE_ALPHA,0};
}
static int quad(NVGcontext *n,int image,float x) {
 return nvgImageQuad(n,image,x,4,10,20,.2f,.3f,.4f,.5f,nvgRGBAf(.8f,.6f,.4f,1));
}
static void closef(float a,float b) { assert(fabsf(a-b)<0.0001f); }
static void vertex(NVGvertex v,float x,float y,float u,float t) {
 closef(v.x,x);closef(v.y,y);closef(v.u,u);closef(v.v,t);
}
int main(void) {
 GXMNVGcontext g={0};reset(&g);
 NVGcontext n={0};n.nstates=1;n.fringeWidth=1;n.params.userPtr=&g;
 n.params.renderImageTriangles=gxmnvg__renderImageTriangles;nvgReset(&n);
 n.ncommands=17;
 assert(quad(&n,1,2));
 vertex(g.verts[0],2,4,.2f,.3f);vertex(g.verts[1],12,24,.6f,.8f);
 vertex(g.verts[2],12,4,.6f,.3f);
 for(int i=1;i<5;i++) assert(quad(&n,1,(float)i));
 assert(g.ncalls==1 && g.nverts==30 && g.calls[0].triangleCount==30 && g.nuniforms==1);
 assert(n.ncommands==17); // No path mutation.
 for(int i=1;i<5;i++) vertex(g.verts[i*6],i,4,.2f,.3f); // Original order.
 nvgGlobalAlpha(&n,.5f);assert(quad(&n,1,2));assert(g.ncalls==2);
 GXMNVGfragUniforms *frag=nvg__fragUniformPtr(&g,g.calls[1].uniformOffset);
 closef(frag->innerCol.r,.4f);closef(frag->innerCol.a,.5f);
 nvgScissor(&n,0,0,5,6);assert(quad(&n,1,2));assert(g.ncalls==3);
 nvgGlobalCompositeOperation(&n,NVG_LIGHTER);assert(quad(&n,1,2));assert(g.ncalls==4);
 assert(quad(&n,2,2));assert(g.ncalls==5);
 assert(nvgImageQuad(&n,2,2,4,10,20,.2f,.3f,.4f,.5f,nvgRGBAf(1,0,0,1)));
 assert(g.ncalls==6); // Tint changes cannot merge.
 GXMNVGcall *barrier=gxmnvg__allocCall(&g);barrier->type=GXMNVG_CONVEXFILL;
 assert(quad(&n,2,2));assert(g.ncalls==8); // Never cross a fill/stencil/rule call.
 int count=g.ncalls, verts=g.nverts;
 assert(!quad(&n,99,2) && !quad(&n,3,2));
 g.textures[0].flags=NVG_IMAGE_PREMULTIPLIED;assert(!quad(&n,1,2));
 g.textures[0].flags=NVG_IMAGE_FLIPY;assert(!quad(&n,1,2));g.textures[0].flags=0;
 g.flags=NVG_ANTIALIAS;assert(!quad(&n,1,2));g.flags=0;
 g.image_quad_shader.prog.frag=NULL;assert(!quad(&n,1,2));g.image_quad_shader.prog.frag=&g;
 assert(g.ncalls==count && g.nverts==verts);
 // CPU transformed geometry keeps winding even for a reflected/rotated layer.
 reset(&g);nvgReset(&n);nvgTranslate(&n,100,200);nvgScale(&n,-2,3);
 assert(quad(&n,1,2));vertex(g.verts[0],96,272,.2f,.8f);
 vertex(g.verts[1],76,212,.6f,.3f);
 float area=(g.verts[1].x-g.verts[0].x)*(g.verts[2].y-g.verts[0].y)-
            (g.verts[1].y-g.verts[0].y)*(g.verts[2].x-g.verts[0].x);
 assert(area<0);
 reset(&g);nvgReset(&n);nvgRotate(&n,NVG_PI/2);
 assert(quad(&n,1,2));vertex(g.verts[0],-4,2,.2f,.3f);
 reset(&g);nvgReset(&n);
 assert(nvgImageQuad(&n,1,2,4,10,20,.6f,.8f,-.4f,-.5f,nvgRGBAf(1,1,1,1)));
 vertex(g.verts[0],2,4,.6f,.8f);vertex(g.verts[1],12,24,.2f,.3f);
 // GPU U16 index limit: split rather than silently discard an oversized draw.
 reset(&g);nvgReset(&n);
 for(int i=0;i<10923;i++)assert(quad(&n,1,2));
 assert(g.ncalls==2 && g.calls[0].triangleCount==65532 && g.calls[1].triangleCount==6);
 // Exercise production call/vertex/uniform allocation rollback independently.
 for(int failure=1;failure<=3;failure++) {
  reset(&g);fail_allocation=failure;assert(!quad(&n,1,2));fail_allocation=0;
  assert(g.ncalls==0 && g.nverts==0 && g.nuniforms==0);
  assert(quad(&n,1,2));assert(g.ncalls==1 && g.nverts==6);
 }
 reset(&g);assert(quad(&n,1,2));g.cverts=6;fail_allocation=1;
 assert(!quad(&n,1,3));fail_allocation=0;
 assert(g.nverts==6 && g.calls[0].triangleCount==6 && g.ncalls==1);
 reset(&g);nvgReset(&n);g.nativeSprites=1;
 assert(quad(&n,1,2));assert(quad(&n,1,20));
 assert(g.ncalls==2 && g.nverts==8 && g.calls[0].type==GXMNVG_NATIVE_QUAD);
 vertex(g.verts[0],12,4,.6f,.3f);vertex(g.verts[1],2,4,.2f,.3f);
 vertex(g.verts[2],12,24,.6f,.8f);vertex(g.verts[3],2,24,.2f,.8f);
 uint64_t comparisons=g.stats.textureComparisons;
 assert(gxmnvg__findTexture(&g,1)==&g.textures[0]);
 assert(g.stats.textureComparisons==comparisons+1);
 g.textures[1].id=257;
 assert(gxmnvg__findTexture(&g,257)==&g.textures[1]);
 assert(gxmnvg__findTexture(&g,1)==&g.textures[0]);
 g.textures[0].id=513;assert(gxmnvg__findTexture(&g,1)==NULL);
 g.textures[2].id=1;assert(gxmnvg__findTexture(&g,1)==&g.textures[2]);
 for(int failure=1;failure<=3;failure++) {
  reset(&g);g.nativeSprites=1;fail_allocation=failure;
  assert(!quad(&n,1,2));fail_allocation=0;
  assert(g.ncalls==0 && g.nverts==0 && g.nuniforms==0);
  assert(quad(&n,1,2));assert(g.nverts==4);
 }
 // Cost model: 40 visible glyphs, black shadow + four black outline offsets
 // and white body. Production queue only; these are counts, not GPU timings.
 for(int mode=0;mode<2;mode++) {
  reset(&g);nvgReset(&n);g.nativeSprites=mode;
  for(int glyph=0;glyph<40;glyph++) {
   for(int effect=0;effect<6;effect++) {
    float color=effect==5 ? 1.0f : 0.0f;
    assert(nvgImageQuad(&n,1,glyph*12.0f+effect,4,10,20,.2f,.3f,.4f,.5f,
      nvgRGBAf(color,color,color,1)));
   }
  }
  printf("QUEUE COUNTS mode=%s glyphs=40 commands=240 calls=%d vertices=%d uniforms=%d\n",
    mode ? "native4" : "merged6",g.ncalls,g.nverts,g.nuniforms);
  assert(g.ncalls==(mode ? 240 : 80));
 }
 n.params.renderImageTriangles=NULL;assert(!quad(&n,1,2));
 reset(&g);
 puts("PASS production image quad: UV/transform/reflection, tint/alpha/scissor/blend barriers, ordered merging, U16 split, allocation fallback");
}
'''
out = root / 'build/image-quad-tests'
out.mkdir(parents=True, exist_ok=True)
test = out / 'test.c'
test.write_text(prefix + production + suffix)
binary = out / 'test'
subprocess.run(['gcc', '-O1', '-g', '-fsanitize=address,undefined',
               '-I'+str(root/'vendor/borealis/library/include/borealis/extern/nanovg'),
               '-I'+str(root/'vendor/borealis/library/lib/extern/nanovg'),
               str(test), '-lm', '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
