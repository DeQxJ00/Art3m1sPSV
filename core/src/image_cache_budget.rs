//! Shared retention budget, not a cap on process memory or active GPU surfaces.
//! Loader ready pixels/sources can borrow unused idle texture space. Publication
//! and provider retain serialize their counters; decoder scratch and GPU-retired
//! storage remain covered separately by the resource ledger/lifetime guards.
use std::sync::{Arc,Mutex,OnceLock};
pub(crate) const SESSION_RETENTION_BYTES:usize=192*1024*1024;
pub(crate) type SharedCacheBudget=Arc<Mutex<CacheBudget>>;
// Retained allocation capacities, not file lengths or whole-process memory.
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub(crate) struct CacheParts { pub decoded:usize,pub gpu:usize,pub encoded:usize,pub proof:usize }
impl CacheParts {
    pub fn total(&self)->usize{self.decoded+self.gpu+self.encoded+self.proof}
    pub fn add(&mut self,p:Self){self.decoded+=p.decoded;self.gpu+=p.gpu;self.encoded+=p.encoded;self.proof+=p.proof;}
}
// Distinct paths with a live script binding. Completed is a load-progress
// counter, not residency: successful payload handoff/reclamation preserves it.
#[derive(Clone,Copy,Default,Debug,PartialEq,Eq)]
pub(crate) struct ScriptPreloadCounts { pub planned:usize,pub completed:usize,pub pixels:usize,pub encoded:usize }
pub(crate) struct CacheBudget { pub limit:usize,pub ready:usize,pub idle:usize,pub ready_goal:usize,pub ready_parts:CacheParts,pub mask_parts:CacheParts,pub animation_parts:CacheParts,pub script_preload:ScriptPreloadCounts }
impl CacheBudget {
    pub fn new(limit:usize)->SharedCacheBudget{Arc::new(Mutex::new(Self{limit,ready:0,idle:0,ready_goal:0,ready_parts:CacheParts::default(),mask_parts:CacheParts::default(),animation_parts:CacheParts::default(),script_preload:ScriptPreloadCounts::default()}))}
    pub fn ready_limit(&self)->usize{self.limit.saturating_sub(self.idle)}
    pub fn idle_limit(&self,maximum:usize)->usize{maximum.min(self.limit.saturating_sub(self.ready.max(self.ready_goal)))}
    // Request one bounded growth window beyond actual ready allocations, not
    // the entire chapter allowance as soon as one async job is queued. The
    // former 5/6 reservation evicted warm backgrounds with >100 MiB still free.
    // Publication refreshes this window; reclaim and admission remain serialized.
    // This is not permission to spend occupied bytes. Keep the 1/6 idle floor.
    pub fn request_ready(&mut self,pending_async:bool){
        self.ready_goal=if pending_async{
            self.ready.saturating_add(16*1024*1024).min(self.limit-self.limit/6)
        }else{0};
    }
    pub fn set_ready_parts(&mut self,parts:CacheParts){let bytes=parts.total();debug_assert!(bytes<=self.ready_limit());self.ready=bytes;self.ready_parts=parts;}
    #[cfg(test)]
    pub fn set_ready(&mut self,bytes:usize){self.set_ready_parts(CacheParts{decoded:bytes,..Default::default()});}
    pub fn set_idle(&mut self,bytes:usize){debug_assert!(bytes<=self.limit.saturating_sub(self.ready));self.idle=bytes;}
}
// Like the existing loader, one game session owns these accounts. Shutdown and
// provider Drop release their respective counters, including a project reload.
pub(crate) fn session_budget()->SharedCacheBudget{
    static SHARED:OnceLock<SharedCacheBudget>=OnceLock::new();
    SHARED.get_or_init(||CacheBudget::new(SESSION_RETENTION_BYTES)).clone()
}

#[cfg(test)] mod tests{
    use super::*;
    #[test] fn pending_prefetch_grows_reservation_without_purging_warm_images(){
        const M:usize=1024*1024;
        let shared=CacheBudget::new(192*M);let mut b=shared.lock().unwrap();
        b.set_ready(48*M);b.set_idle(60*M);b.request_ready(true);
        assert_eq!(b.ready_goal,64*M);assert_eq!(b.idle_limit(64*M),64*M);
        assert_eq!(b.idle,60*M); // A request never changes ownership.
        b.set_ready(128*M);b.request_ready(true);
        assert_eq!(b.ready_goal,144*M);assert_eq!(b.idle_limit(64*M),48*M);
        assert_eq!(b.ready_limit(),132*M); // Cannot publish into promised space yet.
        b.set_idle(48*M);b.set_ready(144*M);b.request_ready(true);
        assert_eq!(b.ready_goal,160*M);assert_eq!(b.idle_limit(64*M),32*M);
        b.set_idle(32*M);b.set_ready(160*M);b.request_ready(true);
        assert_eq!(b.ready_goal,160*M);
        b.request_ready(false);assert_eq!(b.ready_goal,0);
        b.set_ready(48*M);assert_eq!(b.idle_limit(64*M),64*M);
    }
    #[test] fn concurrent_retention_never_spends_the_same_headroom_twice(){
        let budget=CacheBudget::new(1024);
        let handles:Vec<_>=(0..2).map(|owner|{let b=budget.clone();std::thread::spawn(move||{
            for i in 0..2000{let mut s=b.lock().unwrap();
                if owner==0{let n=(i*79)%1500;let cap=s.ready_limit();s.set_ready(n.min(cap));}
                else{let cap=s.idle_limit(768);s.set_idle(((i*31)%1000).min(cap));}
                assert!(s.ready+s.idle<=s.limit);
            }
        })}).collect();
        for h in handles{h.join().unwrap();}
    }
}
