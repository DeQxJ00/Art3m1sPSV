#include <cassert>
#include <cstring>
static int plays,stops,fades,play_result=1;
static bool active=true;
int timeline_has_label(const char *s){return s&&!std::strcmp(s,"known");}
int timeline_play(const char *,unsigned){++plays;return play_result;}
int timeline_fade_in(const char *,float,float){++fades;return play_result;}
int timeline_is_playing(const char *s){return active&&timeline_has_label(s);}
int timeline_fade(const char *,float,float,float,int){++fades;return play_result;}
int timeline_stop(const char *){++stops;active=false;return 0;}
#include "../../host-direct/src/emote_script_timeline.hpp"
int main(){
 float args[4]={20,0,0,0};
 // Missing optional actions leave existing playback and variables alone.
 assert(emote_script_timeline(4,"missing",args,0)==2);
 assert(emote_script_timeline(4,"",args,1)==2);
 assert(emote_script_timeline(5,"missing",args,0)==2);
 assert(emote_script_timeline(6,"missing",args,0)==2);
 assert(plays==0&&stops==0&&fades==0&&active);
 assert(emote_script_timeline(4,"known",args,1)==1&&plays==1);
 assert(emote_script_timeline(5,"known",args,0)==1&&fades==1);
 assert(emote_script_timeline(6,"known",args,0)==1&&fades==2);
 // Do not turn invalid flags or failed allocations into successful playback.
 assert(emote_script_timeline(4,"known",args,4)==0&&plays==1);
 assert(emote_script_timeline(4,"missing",args,4)==0);
 assert(emote_script_timeline(4,nullptr,args,1)==0);
 play_result=0;
 assert(emote_script_timeline(4,"known",args,1)==0);
 assert(emote_script_timeline(5,"known",args,0)==0);
 assert(emote_script_timeline(6,"known",args,0)==0);
 assert(emote_script_timeline(7,"missing",args,0)==1);
 assert(emote_script_timeline(7,"",args,0)==1&&stops==2);
}
