//! Allocation-event ledger. Observation only: no admission, eviction, IO or waits.
//! Retired is a subset of live, never an additional allocation. Codec/driver internals
//! and allocations outside the explicitly instrumented paths remain untracked.
use std::sync::Mutex;
use std::ops::{Deref,DerefMut};
#[repr(usize)]
#[derive(Clone,Copy,Debug)]
pub(crate) enum Owner { Source, Decode, Ready, Provider, Texture, Fixed, Offscreen, Temporary, Media, Font, Audio }
const OWNERS:usize=11;
const REGIONS:usize=3; // CPU heap, CDRAM, uncached RAM.
#[derive(Clone,Copy,Default,Debug,PartialEq)]
struct Cell { live:u64, reserved:u64, retired:u64, peak:u64, committed_peak:u64 }
#[derive(Clone,Copy)]
struct Ledger { cells:[[Cell;OWNERS];REGIONS], totals:[Cell;REGIONS], events:u64, faults:u64 }
// Keep peak comparisons scalar: LLVM otherwise combines adjacent u64 maxima
// into VQSUB.U64, which the Vita3K ARM fallback cannot execute. Do not disable
// vectorization in decoding/rendering just for this observation-only counter.
#[inline(never)]
fn peak_max(a:u64,b:u64)->u64 {a.max(b)}
impl Ledger {
    const fn new()->Self { const C:Cell=Cell{live:0,reserved:0,retired:0,peak:0,committed_peak:0};Self{cells:[[C;OWNERS];REGIONS],totals:[C;REGIONS],events:0,faults:0} }
    fn changed(c:Cell,l:i64,r:i64,t:i64)->Option<Cell>{
        let live=c.live.checked_add_signed(l)?;let reserved=c.reserved.checked_add_signed(r)?;let retired=c.retired.checked_add_signed(t)?;
        if retired>live{return None;}
        Some(Cell{live,reserved,retired,peak:peak_max(c.peak,live),committed_peak:peak_max(c.committed_peak,live.checked_add(reserved)?)})
    }
    fn event(&mut self,region:usize,owner:usize,l:i64,r:i64,t:i64){
        let next=(||{let c=*self.cells.get(region)?.get(owner)?;Some((Self::changed(c,l,r,t)?,Self::changed(self.totals[region],l,r,t)?))})();
        if let Some((c,total))=next {self.cells[region][owner]=c;self.totals[region]=total;self.events+=1;}else{self.faults+=1;}
    }
    fn transfer(&mut self,region:usize,from:usize,to:usize,live:u64,reserved:u64){
        if from==to{return;}
        let a=Self::changed(self.cells[region][from],-(live as i64),-(reserved as i64),0);
        let b=Self::changed(self.cells[region][to],live as i64,reserved as i64,0);
        if let (Some(a),Some(b))=(a,b){self.cells[region][from]=a;self.cells[region][to]=b;self.events+=1;}else{self.faults+=1;}
    }
}
static LEDGER:Mutex<Ledger>=Mutex::new(Ledger::new());
/// Move-only charge. Declare after the owned storage so destruction releases storage first.
pub(crate) struct Charge { ledger:&'static Mutex<Ledger>,owner:Owner,live:usize,reserved:usize }
impl Charge {
    fn on(ledger:&'static Mutex<Ledger>,owner:Owner,bytes:usize)->Self {
        if bytes>0{ledger.lock().unwrap_or_else(|e|e.into_inner()).event(0,owner as usize,0,bytes as i64,0);}
        Self{ledger,owner,live:0,reserved:bytes}
    }
    pub fn reserve(owner:Owner,bytes:usize)->Self {Self::on(&LEDGER,owner,bytes)}
    pub fn observed(owner:Owner,bytes:usize)->Self {let mut t=Self::reserve(owner,0);t.commit(bytes);t}
    pub fn commit(&mut self,actual:usize){
        if self.live==actual&&self.reserved==0{return;}
        self.ledger.lock().unwrap_or_else(|e|e.into_inner()).event(0,self.owner as usize,actual as i64-self.live as i64,-(self.reserved as i64),0);self.live=actual;self.reserved=0;
    }
    pub fn transfer(&mut self,owner:Owner){
        if self.owner as usize==owner as usize{return;}
        if self.live>0||self.reserved>0{self.ledger.lock().unwrap_or_else(|e|e.into_inner()).transfer(0,self.owner as usize,owner as usize,self.live as u64,self.reserved as u64);}
        self.owner=owner;
    }
}
impl Drop for Charge {fn drop(&mut self){if self.live>0||self.reserved>0{self.ledger.lock().unwrap_or_else(|e|e.into_inner()).event(0,self.owner as usize,-(self.live as i64),-(self.reserved as i64),0);}}}
pub(crate) struct Tracked<T>{pub data:T,pub charge:Charge}
impl<T> Tracked<T>{
    pub fn map<U>(self,f:impl FnOnce(T)->U)->Tracked<U>{Tracked{data:f(self.data),charge:self.charge}}
    pub fn transfer(&mut self,owner:Owner){self.charge.transfer(owner);}
}
impl<T> Deref for Tracked<T>{type Target=T;fn deref(&self)->&T{&self.data}}
impl<T> DerefMut for Tracked<T>{fn deref_mut(&mut self)->&mut T{&mut self.data}}
impl AsRef<[u8]> for Tracked<Vec<u8>>{fn as_ref(&self)->&[u8]{self.data.as_slice()}}
impl Tracked<Vec<u8>> {
    pub fn bytes(data:Vec<u8>,owner:Owner)->Self{let charge=Charge::observed(owner,data.capacity());Self{data,charge}}
    pub fn sync_capacity(&mut self){self.charge.commit(self.data.capacity());}
}
impl Tracked<image::RgbaImage>{pub fn into_raw(self)->Tracked<Vec<u8>>{self.map(image::RgbaImage::into_raw)}}
impl From<Vec<u8>> for Tracked<Vec<u8>>{fn from(v:Vec<u8>)->Self{Self::bytes(v,Owner::Temporary)}}
impl From<image::RgbaImage> for Tracked<image::RgbaImage>{fn from(data:image::RgbaImage)->Self{let charge=Charge::observed(Owner::Temporary,data.as_raw().capacity());Self{data,charge}}}
pub(crate) fn decode_rgba(decoder:impl image::ImageDecoder,limit:usize)->image::ImageResult<Tracked<image::RgbaImage>> {
    let mut charge=Charge::observed(Owner::Decode,0);
    let data=crate::image_decode::rgba_with_reserve(decoder,limit,|v,n|{
        charge=Charge::reserve(Owner::Decode,n);
        let ok=v.try_reserve_exact(n).is_ok();charge.commit(v.capacity());ok
    })?;
    Ok(Tracked{data,charge})
}

/// Lossless single-channel storage. The decoder, not a filename, proves that
/// there is no alpha/color to discard (PNG tRNS expands to La8/Rgba8).
pub(crate) fn decode_luma(decoder:impl image::ImageDecoder,limit:usize)->Option<Tracked<Vec<u8>>> {
    if decoder.color_type()!=image::ColorType::L8{return None;}
    let (w,h)=decoder.dimensions();
    let n=(w as usize).checked_mul(h as usize)?;
    if n==0||n>limit||decoder.total_bytes()!=n as u64{return None;}
    let mut charge=Charge::reserve(Owner::Decode,n);
    let mut data=Vec::new();data.try_reserve_exact(n).ok()?;data.resize(n,0);
    charge.commit(data.capacity());decoder.read_image(&mut data).ok()?;
    Some(Tracked{data,charge})
}

/// C host reports only successfully owned physical blocks, with the same lifecycle rules.
#[unsafe(no_mangle)]
pub extern "C" fn art3m1s_resource_ledger_version()->u32 {3}
#[unsafe(no_mangle)]
pub extern "C" fn art3m1s_resource_ledger_audio_version()->u32 {3}
#[unsafe(no_mangle)]
pub extern "C" fn art3m1s_resource_event(region:u32,owner:u32,live:i64,reserved:i64,retired:i64){
    LEDGER.lock().unwrap_or_else(|e|e.into_inner()).event(region as usize,owner as usize,live,reserved,retired);
}
#[unsafe(no_mangle)]
pub extern "C" fn art3m1s_resource_report(){
    let s=*LEDGER.lock().unwrap_or_else(|e|e.into_inner()); // Never log under the ledger lock.
    for (region,name) in ["heap","cdram","uncached"].iter().enumerate(){let c=s.totals[region];let o=&s.cells[region];
        crate::core_info!("[resource-ledger] region={} live={} reserved={} retired={} peak_live={} peak_committed={} source={} decode={} ready={} provider={} texture={} fixed={} offscreen={} temporary={} media={} font={} audio={} events={} faults={} observe_only=1 untracked=codec,audio_static,font_parser,driver,containers,scratch",name,c.live,c.reserved,c.retired,c.peak,c.committed_peak,o[0].live,o[1].live,o[2].live,o[3].live,o[4].live,o[5].live,o[6].live,o[7].live,o[8].live,o[9].live,o[10].live,s.events,s.faults);
    }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn gray_decoder_preserves_all_values_and_honors_limit(){
        use image::ImageEncoder;
        let raw:Vec<u8>=(0..256).map(|v|v as u8).collect();let mut png=Vec::new();
        image::codecs::png::PngEncoder::new(&mut png).write_image(&raw,16,16,image::ExtendedColorType::L8).unwrap();
        let decoder=||image::ImageReader::new(std::io::Cursor::new(&png)).with_guessed_format().unwrap().into_decoder().unwrap();
        assert!(decode_luma(decoder(),255).is_none());
        assert_eq!(decode_luma(decoder(),256).unwrap().data,raw);
    }
    #[test] fn peak_comparison_preserves_all_u64_bits(){
        for a in [0,1,u32::MAX as u64,1u64<<32,1u64<<63,u64::MAX] {
            for b in [0,1,u32::MAX as u64,1u64<<32,1u64<<63,u64::MAX] {
                assert_eq!(peak_max(a,b),if a>b {a}else{b});
            }
        }
    }
    #[test] fn owned_buffer_transfer_does_not_reallocate_and_drop_releases_charge(){
        let ledger=Box::leak(Box::new(Mutex::new(Ledger::new())));
        let mut charge=Charge::on(ledger,Owner::Decode,128);
        let data=vec![7;64];charge.commit(data.capacity());let ptr=data.as_ptr();
        let mut tracked=Tracked{data,charge};tracked.transfer(Owner::Ready);tracked.transfer(Owner::Provider);
        assert_eq!(tracked.as_ptr(),ptr);assert_eq!(ledger.lock().unwrap().totals[0].live,64);
        let mapped=tracked.map(|v|image::RgbaImage::from_raw(4,4,v).unwrap()).into_raw();assert_eq!(mapped.as_ptr(),ptr);
        drop(mapped);let l=ledger.lock().unwrap();assert_eq!(l.totals[0].live+l.totals[0].reserved,0);assert_eq!(l.faults,0);
    }
    #[test] fn cancelled_or_failed_reservation_returns_without_allocation(){
        let ledger=Box::leak(Box::new(Mutex::new(Ledger::new())));
        {let _ticket=Charge::on(ledger,Owner::Decode,8192);}
        let l=ledger.lock().unwrap();assert_eq!(l.totals[0].live+l.totals[0].reserved,0);assert_eq!(l.faults,0);
    }
    #[test] fn reserve_commit_transfer_and_retirement_count_physical_bytes_once(){
        let mut l=Ledger::new();l.event(0,0,0,1024,0);l.event(0,0,1088,-1024,0);l.transfer(0,0,2,1088,0);l.transfer(0,2,3,1088,0);
        assert_eq!(l.totals[0].live,1088);assert_eq!(l.totals[0].peak,1088);assert_eq!(l.cells[0][0].live,0);
        l.event(1,4,262144,0,0);l.event(1,4,0,0,262144);assert_eq!(l.totals[1].live,262144);assert_eq!(l.totals[1].retired,262144);
        l.event(1,4,-262144,0,-262144);l.event(0,3,-1088,0,0);assert_eq!(l.totals[0].live+l.totals[1].live,0);assert_eq!(l.faults,0);
    }
    #[test] fn failed_cdram_reservation_rolls_back_before_uncached_fallback(){
        let mut l=Ledger::new();l.event(1,4,0,262144,0);l.event(1,4,0,-262144,0);l.event(2,4,0,262144,0);l.event(2,4,262144,-262144,0);
        assert_eq!(l.totals[1].live+l.totals[1].reserved,0);assert_eq!(l.totals[2].live,262144);assert_eq!(l.faults,0);
    }
    #[test] fn invalid_release_is_reported_without_corrupting_other_regions(){
        let mut l=Ledger::new();l.event(0,0,64,0,0);l.event(1,0,-64,0,0);l.event(0,0,0,0,65);l.event(99,0,1,0,0);
        assert_eq!(l.faults,3);assert_eq!(l.totals[0].live,64);assert_eq!(l.totals[1].live,0);
    }
}
