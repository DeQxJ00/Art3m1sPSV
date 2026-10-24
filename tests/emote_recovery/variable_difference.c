/* Host regression for independent named differences and model ownership. */
#define _Static_assert(...)
#include "../../vendor/emote-recovery/native/timeline.c"
#include <assert.h>
#include <stdio.h>
int main(void){
 EmTimelineContext *a=timeline_context_create(),*b=timeline_context_create();
 timeline_context_use(a);
 assert(timeline_set_variable("face",2,0,0,0));
 assert(timeline_set_difference("cheek","face",3,0,0));
 assert(timeline_value("face")==5 && timeline_script_value("face")==2);
 assert(timeline_set_difference("cheek","face",4,0,0));
 assert(timeline_value("face")==6);
 /* Native difference identity is (key, variable), not key alone. */
 assert(timeline_set_difference("cheek","other",7,0,0));
 assert(timeline_value("other")==7 && timeline_value("face")==6);
 assert(timeline_set_difference("second","face",1,0,0));
 assert(timeline_value("face")==7);
 assert(timeline_set_variable("face",5,0,0,0));
 assert(timeline_value("face")==10 && timeline_script_value("face")==5);
 assert(timeline_set_difference("cheek","face",0,4,0));
 variable_step(2);assert(timeline_value("face")==8);
 timeline_finish_variables();assert(timeline_value("face")==6);
 timeline_context_use(b);assert(timeline_value("face")==0);
 timeline_context_use(a);assert(timeline_value("face")==6);
 assert(!timeline_set_difference("bad","face",NAN,0,0));
 timeline_bind_script((V){0});assert(timeline_value("face")==0);
 assert(timeline_variable_count()==0 && !*timeline_variable_label(0));
 timeline_context_use(NULL);timeline_context_destroy(a);timeline_context_destroy(b);
 puts("named differences: PASS");return 0;
}
