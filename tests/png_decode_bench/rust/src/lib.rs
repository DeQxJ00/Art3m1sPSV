use std::alloc::{GlobalAlloc,Layout,System};
use std::sync::atomic::{AtomicBool,AtomicUsize,Ordering::Relaxed};
#[path="../../../../core/src/image_decode.rs"]
mod image_decode;
struct Tracked;
static TRACK:AtomicBool=AtomicBool::new(false);
static LIVE:AtomicUsize=AtomicUsize::new(0);
static PEAK:AtomicUsize=AtomicUsize::new(0);
fn add(n:usize){if TRACK.load(Relaxed){let live=LIVE.fetch_add(n,Relaxed)+n;PEAK.fetch_max(live,Relaxed);}}
fn sub(n:usize){if TRACK.load(Relaxed){LIVE.fetch_sub(n,Relaxed);}}
unsafe impl GlobalAlloc for Tracked {
    unsafe fn alloc(&self,l:Layout)->*mut u8 {let p=System.alloc(l);if !p.is_null(){add(l.size());}p}
    unsafe fn alloc_zeroed(&self,l:Layout)->*mut u8 {let p=System.alloc_zeroed(l);if !p.is_null(){add(l.size());}p}
    unsafe fn dealloc(&self,p:*mut u8,l:Layout){sub(l.size());System.dealloc(p,l)}
    unsafe fn realloc(&self,p:*mut u8,l:Layout,n:usize)->*mut u8 {let q=System.realloc(p,l,n);if !q.is_null(){sub(l.size());add(n);}q}
}
#[global_allocator] static ALLOC:Tracked=Tracked;
#[no_mangle]
pub unsafe extern "C" fn bench_rust_decode(data:*const u8,len:usize,out:*mut *mut u8,capacity:*mut usize,w:*mut u32,h:*mut u32,peak:*mut usize,track:i32)->i32 {
    LIVE.store(0,Relaxed);PEAK.store(0,Relaxed);TRACK.store(track!=0,Relaxed);
    let result=image::ImageReader::new(std::io::Cursor::new(std::slice::from_raw_parts(data,len))).with_guessed_format()
        .and_then(|reader|reader.into_decoder().map_err(std::io::Error::other))
        .and_then(|decoder|image_decode::rgba(decoder,32*1024*1024).map_err(std::io::Error::other));
    let code=match result {
        Ok(image)=>{*w=image.width();*h=image.height();let mut pixels=image.into_raw();*capacity=pixels.capacity();*out=pixels.as_mut_ptr();std::mem::forget(pixels);0},
        Err(_)=>-1,
    };
    TRACK.store(false,Relaxed);*peak=PEAK.load(Relaxed);code
}
#[no_mangle]
pub unsafe extern "C" fn bench_rust_free(data:*mut u8,len:usize,capacity:usize){drop(Vec::from_raw_parts(data,len,capacity));}
