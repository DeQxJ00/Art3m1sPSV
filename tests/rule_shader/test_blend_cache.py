"""Compile the production blend-selection functions with a fake GXM patcher.

Checks CPU-side reuse and failure recovery, not GPU output or patcher behavior.
Run under WSL: python3 tests/rule_shader/test_blend_cache.py
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'vendor/borealis/library/include/borealis/extern/nanovg/nanovg_gxm.h').read_text()
functions = source[source.index('static int gxmnvg__sameBlend'):source.index('static void gxmnvg__setFragmentProgram')]
output = root / 'build/blend-cache-test'
output.mkdir(parents=True, exist_ok=True)
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int serial; } SceGxmFragmentProgram;
typedef int SceGxmShaderPatcherId;
typedef struct { int srcRGB,dstRGB,srcAlpha,dstAlpha; } GXMNVGblend;
typedef struct { SceGxmFragmentProgram *base,*program; GXMNVGblend blend; } GXMNVGblendVariant;
typedef struct { struct { SceGxmFragmentProgram *frag; int frag_id; void *vert_gxp; } prog; } GXMNVGshader;
typedef struct {
 GXMNVGshader shader,gradient_shader,img_texture_shader,text_texture_shader,rule_shader,image_quad_shader,depth_shader,depth_texture_shader;
 GXMNVGblend activeBlend; GXMNVGblendVariant *blendVariants;
 int nblendVariants,cblendVariants,blendFailureReported;
} GXMNVGcontext;
typedef struct { int colorMask,colorFunc,alphaFunc,colorSrc,colorDst,alphaSrc,alphaDst; } SceGxmBlendInfo;
enum { SCE_GXM_BLEND_FACTOR_ONE=1,SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA=2,
 SCE_GXM_BLEND_FUNC_ADD=3,SCE_GXM_COLOR_MASK_ALL=15,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4=4 };
static int creates,fail_patch,fail_memory;
static int gxmCreateFragmentProgram(int id,int format,const SceGxmBlendInfo *b,void *v,SceGxmFragmentProgram **p) {
 (void)id;(void)format;(void)v;
 assert(b->colorFunc==SCE_GXM_BLEND_FUNC_ADD && b->alphaFunc==SCE_GXM_BLEND_FUNC_ADD);
 if(fail_patch)return -1;
 *p=malloc(sizeof(**p));assert(*p);(*p)->serial=++creates;return 0;
}
static void *test_realloc(void *p,size_t n) { return fail_memory ? NULL : realloc(p,n); }
#define realloc test_realloc
#define sceClibPrintf printf
'''
suffix = r'''
int main(void) {
 GXMNVGcontext g={0}; SceGxmFragmentProgram base[3]={{0},{0},{0}};
 g.shader.prog.frag=&base[0];g.shader.prog.frag_id=1;
 g.rule_shader.prog.frag=&base[1];g.rule_shader.prog.frag_id=2;
 g.depth_shader.prog.frag=&base[2];
 g.activeBlend=(GXMNVGblend){1,2,1,2};
 assert(gxmnvg__blendProgram(&g,&base[0])==&base[0] && creates==0);
 g.activeBlend=(GXMNVGblend){1,1,1,1};
 fail_memory=1;assert(gxmnvg__blendProgram(&g,&base[0])==&base[0]);fail_memory=0;
 assert(g.nblendVariants==0);
 fail_patch=1;assert(gxmnvg__blendProgram(&g,&base[0])==&base[0]);fail_patch=0;
 assert(g.nblendVariants==0);
 SceGxmFragmentProgram *add=gxmnvg__blendProgram(&g,&base[0]);assert(add!=&base[0]);
 for(int i=0;i<500;i++)assert(gxmnvg__blendProgram(&g,&base[0])==add);
 assert(creates==1);
 assert(gxmnvg__blendProgram(&g,&base[1])!=add && creates==2);
 assert(gxmnvg__blendProgram(&g,&base[2])==&base[2] && creates==2);
 // Alpha factors are part of the key, even when RGB blending is identical.
 g.activeBlend.srcAlpha=0;
 assert(gxmnvg__blendProgram(&g,&base[0])!=add && creates==3);
 g.activeBlend=(GXMNVGblend){1,1,1,1};assert(gxmnvg__blendProgram(&g,&base[0])==add);
 for(int i=0;i<g.nblendVariants;i++)free(g.blendVariants[i].program);
 free(g.blendVariants);
 puts("PASS production blend cache: 500 hits, shader/alpha isolation, depth bypass, allocation/patch retry");
}
'''
test = output / 'test.c'
test.write_text(prefix + functions + suffix)
binary = output / 'test'
subprocess.run(['gcc', '-O1', '-g', '-fsanitize=address,undefined', str(test), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
