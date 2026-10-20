#include "timeline.c"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv){assert(argc==3);
 for(int spec=1;spec<argc;++spec){
  FILE*f=fopen(argv[spec],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
  unsigned char*bytes=malloc(n);assert(fread(bytes,1,n,f)==n);fclose(f);
  EmPsb psb={.raw=bytes,.length=n};em_psb_init_header(&psb);V root;em_psb_root(&root,&psb);
  timeline_bind_script(root);assert(timeline_play("face",1));
  if(spec==1){assert(timeline_set_variable("face_talk",4,0,0,0));timeline_step(0);assert(timeline_value("face_talk")==4);}
  timeline_step(2);
  for(int frame=0;frame<40;++frame){float mouth=frame%2?0:4;
   assert(timeline_set_variable("face_talk",mouth,0,0,0));timeline_step(1);
   if(timeline_value("face_talk")!=mouth)fprintf(stderr,"spec=%d frame=%d got=%f want=%f\n",spec,frame,timeline_value("face_talk"),mouth);assert(timeline_value("face_talk")==mouth);
  }
  timeline_step(19);assert(timeline_value("face_talk")==2);
  assert(timeline_set_variable("face_talk",0,0,0,0));timeline_step(1);
  assert(timeline_value("face_talk")==0);
  assert(timeline_seek_named("face",0));assert(timeline_value("face_talk")==0);
  assert(timeline_set_variable("face_talk",4,0,0,0));assert(timeline_play("face",1));
  assert(timeline_value("face_talk")==0);
  timeline_bind_script((V){0});free(bytes);
 }
 puts("PASS external mouth open/close survives old normal keys; new authored key resumes control; Win and Vita");
}
