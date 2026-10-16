#ifndef EM_PARAMETER_H
#define EM_PARAMETER_H
#include <stdint.h>
typedef struct {uint8_t id_storage[28];uint8_t discretize,padding[3];float begin,end,division,frame;uint32_t mode;} EmParameter;
_Static_assert(sizeof(EmParameter)==52,"original parameter layout");
void em_parameter_set(void *player,EmParameter *parameter,uint32_t mode,float value); /* 81192C3E */
#endif
