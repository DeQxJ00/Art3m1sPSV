/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis 'TREMOR' CODEC SOURCE CODE.   *
 *                                                                  *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis 'TREMOR' SOURCE CODE IS (C) COPYRIGHT 1994-2002    *
 * BY THE Xiph.Org FOUNDATION http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: arm7 and later wide math functions

 ********************************************************************/

#ifdef _ARM_ASSEM_

#if !defined(_V_WIDE_MATH) && !defined(_LOW_ACCURACY_)
#define _V_WIDE_MATH

static inline ogg_int32_t MULT32(ogg_int32_t x, ogg_int32_t y) {
  int lo,hi;
  asm volatile("smull\t%0, %1, %2, %3"
               : "=&r"(lo),"=&r"(hi)
               : "%r"(x),"r"(y)
	       : "cc");
  return(hi);
}

static inline ogg_int32_t MULT31(ogg_int32_t x, ogg_int32_t y) {
  return MULT32(x,y)<<1;
}

static inline ogg_int32_t MULT31_SHIFT15(ogg_int32_t x, ogg_int32_t y) {
  int lo,hi;
  asm volatile("smull	%0, %1, %2, %3\n\t"
	       "movs	%0, %0, lsr #15\n\t"
	       "adc	%1, %0, %1, lsl #17\n\t"
               : "=&r"(lo),"=&r"(hi)
               : "%r"(x),"r"(y)
	       : "cc");
  return(hi);
}

#define MB() asm volatile ("" : : : "memory")

static inline void XPROD32(ogg_int32_t  a, ogg_int32_t  b,
			   ogg_int32_t  t, ogg_int32_t  v,
			   ogg_int32_t *x, ogg_int32_t *y)
{
  int x1, y1, l;
  asm(	"smull	%0, %1, %4, %6\n\t"
	"smlal	%0, %1, %5, %7\n\t"
	"rsb	%3, %4, #0\n\t"
	"smull	%0, %2, %5, %6\n\t"
	"smlal	%0, %2, %3, %7"
	: "=&r" (l), "=&r" (x1), "=&r" (y1), "=r" (a)
	: "3" (a), "r" (b), "r" (t), "r" (v)
	: "cc" );
  *x = x1;
  MB();
  *y = y1;
}

static inline void XPROD31(ogg_int32_t  a, ogg_int32_t  b,
			   ogg_int32_t  t, ogg_int32_t  v,
			   ogg_int32_t *x, ogg_int32_t *y)
{
  int x1, y1, l;
  asm(	"smull	%0, %1, %4, %6\n\t"
	"smlal	%0, %1, %5, %7\n\t"
	"rsb	%3, %4, #0\n\t"
	"smull	%0, %2, %5, %6\n\t"
	"smlal	%0, %2, %3, %7"
	: "=&r" (l), "=&r" (x1), "=&r" (y1), "=r" (a)
	: "3" (a), "r" (b), "r" (t), "r" (v)
	: "cc" );
  *x = x1 << 1;
  MB();
  *y = y1 << 1;
}

static inline void XNPROD31(ogg_int32_t  a, ogg_int32_t  b,
			    ogg_int32_t  t, ogg_int32_t  v,
			    ogg_int32_t *x, ogg_int32_t *y)
{
  int x1, y1, l;
  asm(	"rsb	%2, %4, #0\n\t"
	"smull	%0, %1, %3, %5\n\t"
	"smlal	%0, %1, %2, %6\n\t"
	"smull	%0, %2, %4, %5\n\t"
	"smlal	%0, %2, %3, %6"
	: "=&r" (l), "=&r" (x1), "=&r" (y1)
	: "r" (a), "r" (b), "r" (t), "r" (v)
	: "cc" );
  *x = x1 << 1;
  MB();
  *y = y1 << 1;
}

#endif

#ifndef _V_CLIP_MATH
#define _V_CLIP_MATH

static inline ogg_int32_t CLIP_TO_15(ogg_int32_t x) {
  int tmp;
  asm volatile("subs	%1, %0, #32768\n\t"
	       "movpl	%0, #0x7f00\n\t"
	       "orrpl	%0, %0, #0xff\n"
	       "adds	%1, %0, #32768\n\t"
	       "movmi	%0, #0x8000"
	       : "+r"(x),"=r"(tmp)
	       :
	       : "cc");
  return(x);
}

#endif

#ifndef _V_LSP_MATH_ASM
#define _V_LSP_MATH_ASM

static inline void lsp_loop_asm(ogg_uint32_t *qip,ogg_uint32_t *pip,
				ogg_int32_t *qexpp,
				ogg_int32_t *ilsp,ogg_int32_t wi,
				ogg_int32_t m){
  
  ogg_uint32_t qi=*qip,pi=*pip;
  ogg_int32_t qexp=*qexpp;

  register ogg_uint32_t r0 asm("r0");
  register ogg_uint32_t r1 asm("r1");
  register ogg_uint32_t r2 asm("r2");
  register ogg_uint32_t r3 asm("r3");

  asm("mov     %[r0],%[ilsp];"
      "mov     %[r1],%[m],asr#1;"
      "add     %[r0],%[r0],%[r1],lsl#3;"
      "beq 2f;"
      "1:"
      
      "ldmdb   %[r0]!,{%[r1],%[r3]};"
      "subs    %[r1],%[r1],%[wi];"          //ilsp[j]-wi
      "rsbmi   %[r1],%[r1],#0;"             //labs(ilsp[j]-wi)
      "umull   %[qi],%[r2],%[r1],%[qi];"    //qi*=labs(ilsp[j]-wi)
      
      "subs    %[r1],%[r3],%[wi];"          //ilsp[j+1]-wi
      "rsbmi   %[r1],%[r1],#0;"             //labs(ilsp[j+1]-wi)
      "umull   %[pi],%[r3],%[r1],%[pi];"    //pi*=labs(ilsp[j+1]-wi)
      
      "cmn     %[r2],%[r3];"                // shift down 16?
      "beq     0f;"
      "add     %[qexp],%[qexp],#16;"
      "mov     %[qi],%[qi],lsr #16;"
      "orr     %[qi],%[qi],%[r2],lsl #16;"
      "mov     %[pi],%[pi],lsr #16;"
      "orr     %[pi],%[pi],%[r3],lsl #16;"
      "0:"
      "cmp     %[r0],%[ilsp];"
      "bhi     1b;"
      
      // odd filter assymetry
      "ands    %[r0],%[m],#1;"
      "beq     2f;"
      "add     %[r0],%[ilsp],%[m],lsl#2;"
      
      "ldr     %[r1],[%[r0],#-4];"
      "mov     %[r0],#0x4000;"
      
      "subs    %[r1],%[r1],%[wi];"          //ilsp[j]-wi
      "rsbmi   %[r1],%[r1],#0;"             //labs(ilsp[j]-wi)
      "umull   %[qi],%[r2],%[r1],%[qi];"    //qi*=labs(ilsp[j]-wi)
      "umull   %[pi],%[r3],%[r0],%[pi];"    //pi*=labs(ilsp[j+1]-wi)
      
      "cmn     %[r2],%[r3];"                // shift down 16?
      "beq     2f;"
      "add     %[qexp],%[qexp],#16;"
      "mov     %[qi],%[qi],lsr #16;"
      "orr     %[qi],%[qi],%[r2],lsl #16;"
      "mov     %[pi],%[pi],lsr #16;"
      "orr     %[pi],%[pi],%[r3],lsl #16;"
      
      //qi=(pi>>shift)*labs(ilsp[j]-wi);
      //pi=(qi>>shift)*labs(ilsp[j+1]-wi);
      //qexp+=shift;
      
      //}
     
      /* normalize to max 16 sig figs */
      "2:"
      "mov     %[r2],#0;"
      "orr     %[r1],%[qi],%[pi];"
      "tst     %[r1],#0xff000000;"
      "addne   %[r2],%[r2],#8;"
      "movne   %[r1],%[r1],lsr #8;"
      "tst     %[r1],#0x00f00000;"
      "addne   %[r2],%[r2],#4;"
      "movne   %[r1],%[r1],lsr #4;"
      "tst     %[r1],#0x000c0000;"
      "addne   %[r2],%[r2],#2;"
      "movne   %[r1],%[r1],lsr #2;"
      "tst     %[r1],#0x00020000;"
      "addne   %[r2],%[r2],#1;"
      "movne   %[r1],%[r1],lsr #1;"
      "tst     %[r1],#0x00010000;"
      "addne   %[r2],%[r2],#1;"
      "mov     %[qi],%[qi],lsr %[r2];"
      "mov     %[pi],%[pi],lsr %[r2];"
      "add     %[qexp],%[qexp],%[r2];"
      
      : [qi] "+r"(qi), [pi] "+r"(pi), [qexp] "+r"(qexp), [r0] "=r"(r0), [r1] "=r"(r1), [r2] "=r"(r2), [r3] "=r"(r3)
      : [ilsp] "r"(ilsp), [wi] "r"(wi), [m] "r"(m)
      : "cc");
  
  *qip=qi;
  *pip=pi;
  *qexpp=qexp;
}

static inline void lsp_norm_asm(ogg_uint32_t *qip,ogg_int32_t *qexpp){

  ogg_uint32_t qi=*qip;
  ogg_int32_t qexp=*qexpp;

  asm("tst     %0,#0x0000ff00;"
      "moveq   %0,%0,lsl #8;"
      "subeq   %1,%1,#8;"
      "tst     %0,#0x0000f000;"
      "moveq   %0,%0,lsl #4;"
      "subeq   %1,%1,#4;"
      "tst     %0,#0x0000c000;"
      "moveq   %0,%0,lsl #2;"
      "subeq   %1,%1,#2;"
      "tst     %0,#0x00008000;"
      "moveq   %0,%0,lsl #1;"
      "subeq   %1,%1,#1;"
      : "+r"(qi),"+r"(qexp)
      :
      : "cc");
  *qip=qi;
  *qexpp=qexp;
}

#endif
#endif

