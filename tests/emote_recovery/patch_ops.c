#include "patch_ops.h"
void vector_patch(unsigned mode,float *out,const float *a,const float *b,const float *p){
 if(mode==0)em_patch_accumulate(out,a,b,p[0],(int)p[1],(int)p[2]);
 if(mode==1)em_patch_combine(out,b);
 if(mode==2)em_patch_transform(out,a,p[0],p[1],p[2],p[3],p[4],p[5]);
}
void scalar_patch(unsigned mode,float *out,const float *a,const float *b,const float *p){
 if(mode==0)for(unsigned k=0;k<32;++k){float v=(int)p[1]?a[k]:a[k]*(1-p[0])+b[k]*p[0];if((int)p[2])out[k]+=v;else out[k]=v;}
 if(mode==1)for(unsigned k=0;k<16;++k){out[2*k]=(out[2*k]+b[2*k])-(k%4)/3.0f;out[2*k+1]=(out[2*k+1]+b[2*k+1])-(k/4)/3.0f;}
 if(mode==2)for(unsigned k=0;k<16;++k){out[2*k]=(p[1]*a[2*k+1]+p[0]*a[2*k])+p[4];out[2*k+1]=(p[3]*a[2*k+1]+p[2]*a[2*k])+p[5];}
}
