#pragma once

// Lua timeline methods have no success result. An absent optional action is
// a no-op, not an interpreter exception. Keep invalid inputs and allocation
// failures distinct: only a proven metadata/active-name miss returns 2.
inline int emote_script_timeline(unsigned op,const char *label,const float *args,unsigned flags){
 if(!label)return 0;
 switch(op){
 case 4:
  if(flags&~3u)return 0;
  if(!timeline_has_label(label))return 2;
  return timeline_play(label,flags);
 case 5:
  if(!timeline_has_label(label))return 2;
  return timeline_fade_in(label,args[0],args[1]);
 case 6:
  if(!timeline_is_playing(label))return 2;
  return timeline_fade(label,0,args[0],args[1],1);
 case 7:
  timeline_stop(label);return 1;
 default:return 0;
 }
}
