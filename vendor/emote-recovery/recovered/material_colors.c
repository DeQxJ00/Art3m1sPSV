#include "material_colors.h"
static uint32_t mix(uint32_t a,uint32_t b,unsigned t){
 uint32_t low=((a&0xff00ffu)*(256-t)+(b&0xff00ffu)*t)>>8;
 uint32_t high=(((a>>8)&0xff00ffu)*(256-t)+((b>>8)&0xff00ffu)*t)>>8;
 return (low&0xff00ffu)|((high&0xff00ffu)<<8);
}
int em_material_color_grid(const uint32_t corners[4],unsigned columns,
                           unsigned rows,uint32_t *out,unsigned capacity){
 if(!corners||!out||!columns||!rows||columns>65535||rows>65535||
    (uint64_t)(columns+1)*(rows+1)>capacity)return 0;
 uint32_t tl=corners[0],tr=corners[1],bl=corners[2],br=corners[3];
 if(tl==tr&&tl==bl&&tl==br){
  unsigned count=(columns+1)*(rows+1);for(unsigned i=0;i<count;++i)out[i]=tl;
  return 1;
 }
 for(unsigned y=0;y<=rows;++y){
  unsigned ty=256*y/rows;uint32_t left=mix(tl,bl,ty),right=mix(tr,br,ty);
  for(unsigned x=0;x<=columns;++x)out[y*(columns+1)+x]=mix(left,right,256*x/columns);
 }
 return 1;
}

uint32_t em_material_prepare_color(uint32_t rgba,uint8_t opacity,uint32_t tint){
 unsigned alpha=(rgba&255u)*opacity/255u;
 uint32_t out=alpha*(tint&255u)/255u;
 for(unsigned shift=8;shift<=24;shift+=8){
  unsigned value=(((rgba>>shift)&255u)*((tint>>shift)&255u))>>7;
  out|=(value>255u?255u:value)<<shift;
 }
 return out;
}
