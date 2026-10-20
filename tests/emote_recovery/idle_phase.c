/* Include the production implementation to inspect clocks without test APIs. */
#include "timeline.c"
#include <assert.h>
#include <stdio.h>
static float start(V root,uint64_t seed,const char *label,int fade){
 timeline_bind_script(root);timeline_seed_idle(seed);
 assert(fade?timeline_fade_in(label,0,0):timeline_play(label,3));
 return timeline_time();
}
int main(int argc,char **argv){assert(argc==3);
 EmTimelineContext *a=timeline_context_create(),*b=timeline_context_create();assert(a&&b);
 for(int spec=1;spec<argc;++spec){
  FILE*f=fopen(argv[spec],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
  unsigned char*bytes=malloc(n);assert(bytes&&fread(bytes,1,n,f)==n);fclose(f);
  EmPsb psb={.raw=bytes,.length=n};em_psb_init_header(&psb);V root;em_psb_root(&root,&psb);
  timeline_context_use(a);float phase=start(root,100,"idle",1);
  assert(phase>=0&&phase<120);if(spec==1)assert(phase>0);else assert(phase==0);
  timeline_context_use(b);float other=start(root,101,"idle",1);if(spec==1)assert(fabsf(phase-other)>1);
  timeline_context_use(a);assert(timeline_time()==phase);
  timeline_step(5);assert(fabsf(timeline_time()-fmodf(phase+5,120))<.001f);
  float before=timeline_time();assert(timeline_fade_in("idle",20,0));assert(timeline_time()==before);
  if(spec==1){assert(timeline_finish_playback(0));assert(timeline_time()==before);}
  assert(timeline_finish_playback(1));assert(timeline_time()==before);
  assert(timeline_seek_named("idle",17));assert(timeline_time()==17);
  assert(start(root,100,"idle",1)==phase);
  assert(start(root,0,"idle",1)==0);
  assert(start(root,100,"idle",0)==0); // Explicit playback keeps authored start.
  assert(start(root,100,"intro",1)==0);
  assert(start(root,100,"gesture",1)==0);
  assert(start(root,100,"face",1)==0);
  if(spec==1){
   start(root,0,"idle",0);timeline_step(119);
   Lane *loop=focused_lane();assert(fabsf(loop->tracks[0].value-1)<.0001f);
   timeline_step(1);assert(timeline_time()==0);
   /* A wrap queues the first target from the previous cycle's live value. */
   assert(fabsf(loop->tracks[0].value-1)<.0001f);
   timeline_step(1);assert(fabsf(loop->tracks[0].value-(1-1/59.f))<.0001f);
   for(int cycle=0;cycle<3;++cycle){
    while(timeline_time()<119)timeline_step(1);
    float tail=loop->tracks[0].value;timeline_step(1);
    assert(timeline_time()==0&&fabsf(loop->tracks[0].value-tail)<.0001f);
    timeline_step(1);
   }
   start(root,0,"idle",0);timeline_step(119.5f);timeline_step(.75f);
   assert(timeline_time()==.25f&&fabsf(focused_lane()->tracks[0].value-1)<.0001f);
   start(root,0,"intro",0);timeline_step(119);timeline_step(1);
   assert(timeline_time()==10&&fabsf(focused_lane()->tracks[0].value-1)<.0001f);
  }
  for(unsigned seed=1;seed<=64;++seed){float t=start(root,seed,"idle",1);assert(t>=0&&t<120);}
  timeline_bind_neutral((V){0});timeline_context_use(b);timeline_bind_neutral((V){0});free(bytes);
 }
 timeline_context_destroy(a);timeline_context_destroy(b);
 puts("PASS per-instance deterministic idle phase, equal speed, repeated fade, explicit seek/play, Step/Skip, continuous loop boundaries, intro/one-shot/instant exclusions, Vita unchanged");
}
