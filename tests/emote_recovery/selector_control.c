/* Caller-owned plain PSB fixture; test actual metadata, no bundled assets. */
#define _Static_assert(...)
#include "psb.h"
#include "timeline.h"
#include "model_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static EmPsbValue get(EmPsbValue v,const char*k){EmPsbValue r={0};if(v.data&&*v.data==33)em_psb_find(&v,(const uint8_t*)k,&r);return r;}
static unsigned count(EmPsbValue v){return v.data&&*v.data==32?em_psb_array(v.data+1).count:0;}
static EmPsbValue at(EmPsbValue v,unsigned i){EmPsbValue r={0};em_psb_at(&r,&v,i);return r;}
static const char*str(EmPsbValue v){return em_psb_string(&v);}
int main(int argc,char**argv){
 assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char*bytes=malloc(size);assert(fread(bytes,1,size,f)==(size_t)size);fclose(f);
 EmPsb psb={.raw=bytes,.length=size};em_psb_init_header(&psb);EmPsbValue root;em_psb_root(&root,&psb);
 EmTimelineContext*t=timeline_context_create();EmControlsContext*c=model_controls_context_create();
 timeline_context_use(t);model_controls_context_use(c);timeline_bind_script(root);model_controls_bind(root);
 EmPsbValue selectors=get(get(root,"metadata"),"selectorControl");unsigned checked=0;
 for(unsigned s=0;s<count(selectors);++s){EmPsbValue selector=at(selectors,s);if(!em_psb_int(&(EmPsbValue){.psb=&psb,.data=get(selector,"enabled").data}))continue;
 const char*label=str(get(selector,"label"));EmPsbValue options=get(selector,"optionList");unsigned n=count(options);if(!n)continue;
 for(int sample=-2;sample<=(int)n*2;++sample){float value=sample*.5f;assert(timeline_set_variable(label,value,0,0,0));model_controls_step(0);
 int selected=(int)value;
 for(unsigned i=0;i<n;++i){EmPsbValue option=at(options,i),on=get(option,"onValue"),off=get(option,"offValue");
 float a=em_psb_float(&on),b=em_psb_float(&off),expected=selected==(int)i?a:b;
 assert(fabsf(model_controls_value(str(get(option,"label")))-expected)<.00001f);++checked;}
 assert(timeline_script_value(label)==(float)selected);
 }
 if(n>=3){
  /* Nonadjacent options crossfade directly; intermediate poses stay off. */
  assert(timeline_set_variable(label,0,0,0,0));
  assert(timeline_set_variable(label,n-1,10,0,0));timeline_step(2.5f);model_controls_step(0);
  assert(timeline_script_value(label)==n-1);
  for(unsigned i=0;i<n;++i){EmPsbValue option=at(options,i),on=get(option,"onValue"),off=get(option,"offValue");
   float a=em_psb_float(&on),b=em_psb_float(&off),weight=i==0?.75f:i==n-1?.25f:0;
   assert(fabsf(model_controls_value(str(get(option,"label")))-(b+(a-b)*weight))<.00001f);++checked;
  }
  /* Reverse a quarter-finished fade: remaining distance shortens duration. */
  assert(timeline_set_variable(label,0,10,0,0));timeline_step(2.5f);model_controls_step(0);
  for(unsigned i=0;i<n;++i){EmPsbValue option=at(options,i),v=get(option,i==0?"onValue":"offValue");
   assert(fabsf(model_controls_value(str(get(option,"label")))-em_psb_float(&v))<.00001f);++checked;}
  assert(timeline_set_variable(label,n-1,10,0,0));timeline_finish_variables();model_controls_step(0);
  assert(timeline_script_value(label)==n-1);
  for(unsigned i=0;i<n;++i){EmPsbValue option=at(options,i),v=get(option,i==n-1?"onValue":"offValue");
   assert(fabsf(model_controls_value(str(get(option,"label")))-em_psb_float(&v))<.00001f);++checked;}
 }
 }
 /* Actual authored timeline commands must also dispatch selector endpoints. */
 EmPsbValue all=get(get(root,"metadata"),"timelineControl");
 unsigned authored=0;
 for(unsigned tindex=0;tindex<count(all);++tindex){EmPsbValue tl=at(all,tindex),vars=get(tl,"variableList");
  for(unsigned vi=0;vi<count(vars);++vi){EmPsbValue v=at(vars,vi);const char *name=str(get(v,"label"));int selector=0;
   for(unsigned si=0;si<count(selectors);++si)if(!strcmp(name,str(get(at(selectors,si),"label"))))selector=1;
   if(!selector)continue;EmPsbValue keys=get(v,"frameList");
   for(unsigned ki=0;ki<count(keys);++ki){EmPsbValue key=at(keys,ki),type=get(key,"type"),time=get(key,"time"),value=get(get(key,"content"),"value");
    if(!type.data||!time.data||!value.data||!em_psb_int(&type))continue;
    timeline_bind_script(root);assert(timeline_play(str(get(tl,"label")),0));
    assert(timeline_seek_named(str(get(tl,"label")),em_psb_float(&time)));timeline_step(0);
    assert(timeline_script_value(name)==(float)(int)em_psb_float(&value));++authored;
   }
  }
 }
 assert(authored);printf("authored selector keys: %u PASS\n",authored);
 assert(checked);model_controls_context_use(NULL);timeline_context_use(NULL);model_controls_context_destroy(c);timeline_context_destroy(t);free(bytes);
 printf("selector discrete, direct crossfade, interrupt and finish checks: %u PASS\n",checked);return 0;
}
