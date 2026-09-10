#pragma once
#include <cstdint>
#include <cstddef>
#ifdef DIRECT_RESOURCE_LEDGER
extern "C" void art3m1s_resource_event(uint32_t region,uint32_t owner,int64_t live,int64_t reserved,int64_t retired);
extern "C" void art3m1s_resource_report();
extern "C" uint32_t art3m1s_resource_ledger_version();
#endif
namespace direct {
// ABI indices match core/resource_ledger.rs. Observation only, no allocator policy.
struct AllocationCharge { size_t bytes=0;uint32_t region=0,owner=4;bool retired=false; };
inline void resource_event(uint32_t region,uint32_t owner,int64_t live,int64_t reserved=0,int64_t retired=0){
#ifdef DIRECT_RESOURCE_LEDGER
    art3m1s_resource_event(region,owner,live,reserved,retired);
#endif
}
inline void resource_retire(AllocationCharge& c){if(c.bytes&&!c.retired){resource_event(c.region,c.owner,0,0,c.bytes);c.retired=true;}}
inline void resource_free(const AllocationCharge& c){if(c.bytes)resource_event(c.region,c.owner,-int64_t(c.bytes),0,c.retired?-int64_t(c.bytes):0);}
}
