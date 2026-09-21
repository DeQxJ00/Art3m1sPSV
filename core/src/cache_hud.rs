//! Optional, bounded diagnostics. No filesystem I/O or loader lock in the reader.
use std::sync::{Mutex,atomic::{AtomicBool,AtomicU64,Ordering}};
static ENABLED:AtomicBool=AtomicBool::new(false);
static NEXT:AtomicU64=AtomicU64::new(0);
static SNAPSHOT:Mutex<[u64;24]>=Mutex::new([0;24]);
#[unsafe(no_mangle)]
pub extern "C" fn art3m1s_cache_hud_enable(enabled:i32){
    let enabled=enabled!=0;
    if ENABLED.swap(enabled,Ordering::Relaxed)!=enabled{
        NEXT.store(0,Ordering::Relaxed);
        *SNAPSHOT.lock().unwrap()=[0;24];
    }
}
pub(crate) fn should_sample()->bool{
    if !ENABLED.load(Ordering::Relaxed){return false;}
    static START:std::sync::OnceLock<crate::profile_clock::Instant>=std::sync::OnceLock::new();
    due(START.get_or_init(crate::profile_clock::Instant::now).elapsed().as_micros() as u64)
}
fn due(now_us:u64)->bool{
    if !ENABLED.load(Ordering::Relaxed){return false;}
    let next=NEXT.load(Ordering::Relaxed);
    if now_us<next{return false;}
    NEXT.store(now_us.saturating_add(500_000),Ordering::Relaxed);true
}
pub(crate) fn publish(values:[u64;24]){if ENABLED.load(Ordering::Relaxed){*SNAPSHOT.lock().unwrap()=values;}}
/// Schema 1 prefix: 20 u64 fields; optional extension at 20..24 is
/// Lua planned/completed/ready-pixels/ready-encoded path counts. Returns 0 until a sample exists or on contention.
/// Caller owns a writable buffer of at least count u64 elements.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn art3m1s_cache_hud_snapshot(out:*mut u64,count:usize)->i32{
    if out.is_null()||count<20||!ENABLED.load(Ordering::Relaxed){return 0;}
    let Ok(s)=SNAPSHOT.try_lock()else{return 0;};if s[0]!=1{return 0;}
    unsafe{std::ptr::copy_nonoverlapping(s.as_ptr(),out,count.min(24));}1
}
#[cfg(test)] mod tests{
    use super::*;
    #[test] fn optional_bounded_snapshot_has_no_stale_data_after_disable(){
        art3m1s_cache_hud_enable(0);assert!(!due(0));
        art3m1s_cache_hud_enable(1);assert!(due(0));assert!(!due(499999));assert!(due(500000));
        let mut v=[0;24];v[0]=1;v[1]=192*1024*1024;v[20]=100;v[21]=35;v[22]=20;v[23]=10;publish(v);
        let mut out=[0;24];assert_eq!(unsafe{art3m1s_cache_hud_snapshot(out.as_mut_ptr(),19)},0);
        assert_eq!(unsafe{art3m1s_cache_hud_snapshot(out.as_mut_ptr(),24)},1);assert_eq!(out,v);
        let mut legacy=[0;21];legacy[20]=999;assert_eq!(unsafe{art3m1s_cache_hud_snapshot(legacy.as_mut_ptr(),20)},1);assert_eq!(&legacy[..20],&v[..20]);assert_eq!(legacy[20],999);
        art3m1s_cache_hud_enable(0);assert_eq!(unsafe{art3m1s_cache_hud_snapshot(out.as_mut_ptr(),24)},0);
        art3m1s_cache_hud_enable(1);assert_eq!(unsafe{art3m1s_cache_hud_snapshot(out.as_mut_ptr(),20)},0);
        art3m1s_cache_hud_enable(0);
    }
}
