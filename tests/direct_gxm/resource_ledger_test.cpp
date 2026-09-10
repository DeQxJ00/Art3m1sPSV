#define DIRECT_RESOURCE_LEDGER 1
#include "../../host-direct/src/resource_ledger.hpp"
#include <cassert>
#include <cstdio>
static int64_t live[3][8]{},reserved[3][8]{},retired[3][8]{};
extern "C" void art3m1s_resource_event(uint32_t r,uint32_t o,int64_t l,int64_t s,int64_t t){
    assert(r<3&&o<8);live[r][o]+=l;reserved[r][o]+=s;retired[r][o]+=t;
    assert(live[r][o]>=0&&reserved[r][o]>=0&&retired[r][o]>=0&&retired[r][o]<=live[r][o]);
}
int main(){
    using namespace direct;
    resource_event(1,4,0,262144);resource_event(1,4,0,-262144); // Failed CDRAM attempt.
    resource_event(2,4,0,262144);resource_event(2,4,262144,-262144);
    AllocationCharge c{262144,2,4,false};resource_retire(c);resource_retire(c);
    assert(live[2][4]==262144&&retired[2][4]==262144&&live[1][4]==0);
    resource_free(c);assert(live[2][4]==0&&retired[2][4]==0);
    AllocationCharge imported{};resource_retire(imported);resource_free(imported);
    resource_event(1,6,262144);resource_free({262144,1,6,false});assert(live[1][6]==0);
    puts("resource ledger host lifecycle OK");
}
