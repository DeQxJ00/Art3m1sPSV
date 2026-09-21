use crate::render_pipeline::draw::{
    TextureId, TextureInfo, TextureProvider, masked_texture_name, solid_texture_name,
};
use image::{ImageReader,ImageDecoder};
use crate::resource_ledger::{Owner,Tracked};
use crate::image_proof::TileProof;
use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::io::Cursor;
use std::time::Instant;

#[derive(Default)]
struct TextureTiming {
    reads: u64,
    missing: u64,
    decoded: u64,
    decode_errors: u64,
    uploads: u64,
    upload_errors: u64,
    upload_bytes: u64,
    read_us: u64,
    decode_us: u64,
    upload_us: u64,
    upload_max_us: u64,
}

fn elapsed_us(start: Instant) -> u64 { start.elapsed().as_micros().min(u64::MAX as u128) as u64 }

type Source = Box<dyn Fn(&str) -> Option<Vec<u8>>>;
pub(crate) enum PreparedPixels {
    Rgba(Tracked<image::RgbaImage>),
    Gray(u32,u32,Tracked<Vec<u8>>),
}
impl From<Tracked<image::RgbaImage>> for PreparedPixels{fn from(p:Tracked<image::RgbaImage>)->Self{Self::Rgba(p)}}
impl From<image::RgbaImage> for PreparedPixels{fn from(p:image::RgbaImage)->Self{Self::Rgba(p.into())}}
impl PreparedPixels{
    fn dimensions(&self)->(u32,u32){match self{Self::Rgba(p)=>p.dimensions(),Self::Gray(w,h,_) =>(*w,*h)}}
    fn is_gray(&self)->bool{matches!(self,Self::Gray(..))}
    fn into_raw(self)->Tracked<Vec<u8>>{match self{Self::Rgba(p)=>p.into_raw(),Self::Gray(_,_,p)=>p}}
}
type PreparedImage = (Result<PreparedPixels,Tracked<Vec<u8>>>,Option<TileProof>,Option<Tracked<Vec<u8>>>);
type PrefetchSource = Box<dyn Fn(&str) -> Option<PreparedImage>>;
enum PixelStorage<'a>{Borrowed(&'a [u8]),Owned(Tracked<Vec<u8>>)}
impl PixelStorage<'_>{fn as_ref(&self)->&[u8]{match self{Self::Borrowed(v)=>v,Self::Owned(v)=>v.as_slice()}}}


struct Entry {
    id: TextureId,
    info: TextureInfo,
    rgba: Tracked<Vec<u8>>,
    opaque: bool,
    revision: u64,
    last_used: u64,
    cacheable: bool,
    reclaimable: bool, // Set by retain; any new resolve/upload pins it again.
    shared: bool,
    // rgba stores packed L8 for gray entries; CPU RGBA consumers expand on demand.
    gray: bool,
    alpha_only: bool,
}
struct DecodedEntry { gray:bool, info: TextureInfo, rgba: Tracked<Vec<u8>>, last_used: u64 }
struct EncodedEntry { bytes:Tracked<Vec<u8>>,proof:Option<TileProof>,last_used:u64 }
impl EncodedEntry {fn capacity(&self)->usize{self.bytes.capacity()+self.proof.as_ref().map_or(0,TileProof::bytes)}}

// Current GPU scene/transitions stay pinned. Optional CPU source backups are
// charged even when their GPU texture is active; all other retention is idle.
// Charge both CPU RGBA storage and the host's estimated aligned GPU storage.
const IDLE_TEXTURE_BUDGET: usize = 96 * 1024 * 1024;
// CPU spill shares the session retention budget, not the scarce CDRAM pool.
const IDLE_GPU_BUDGET: usize = 32 * 1024 * 1024;

impl Entry {
    fn cache_bytes(&self) -> usize {
        self.rgba.capacity().saturating_add(
            ((self.info.width as usize + 7) & !7).saturating_mul(self.info.height as usize).saturating_mul(if self.gray||self.alpha_only{1}else{4})
        )
    }
}

unsafe extern "C" {
    fn art3m1s_gxm_upload_alpha_region(id:u64,w:u32,h:u32,p:*const u8,len:usize,x:u32,y:u32,rw:u32,rh:u32)->i32;
    fn art3m1s_gxm_upload_luma(texture:u64,w:u32,h:u32,pixels:*const u8,length:usize)->i32;
    fn art3m1s_gxm_surface_prepare(w:u32,h:u32,pixels:*mut *mut u8,capacity:*mut usize)->usize;
    fn art3m1s_gxm_surface_abort(handle:usize);
    fn art3m1s_gxm_surface_publish(handle:usize,id:u64,proof:*const u8,count:usize)->i32;
    fn art3m1s_gxm_surface_view(id:u64,stride:*mut usize)->*const u8;
    fn art3m1s_gxm_update_texture_region(texture: u64, width: u32, height: u32,
        rgba: *const u8, length: usize, x: u32, y: u32, w: u32, h: u32) -> i32;
    fn art3m1s_gxm_upload_texture(
        texture: u64,
        width: u32,
        height: u32,
        rgba: *const u8,
        length: usize,
    ) -> i32;
    fn art3m1s_gxm_delete_texture(texture: u64);
    fn art3m1s_gxm_upload_texture_proof(texture:u64,width:u32,height:u32,rgba:*const u8,length:usize,cells:*const u8,count:usize)->i32;
    fn art3m1s_gxm_upload_video_texture(texture: u64, width: u32, height: u32, rgba: *const u8, length: usize) -> i32;
}

struct PrivateSurface { handle:usize,pixels:*mut u8,capacity:usize }
impl PrivateSurface {
    fn new(w:u32,h:u32)->Option<Self>{
        let mut pixels=std::ptr::null_mut();let mut capacity=0;
        let handle=unsafe{art3m1s_gxm_surface_prepare(w,h,&mut pixels,&mut capacity)};
        if handle==0{return None;}
        let surface=Self{handle,pixels,capacity};
        if pixels.is_null()||capacity<(w as usize).checked_mul(h as usize)?.checked_mul(4)?{return None;}
        Some(surface)
    }
    fn bytes(&mut self)->&mut[u8]{unsafe{std::slice::from_raw_parts_mut(self.pixels,self.capacity)}}
    fn publish(mut self,id:u64,proof:Option<&[u8]>)->bool{
        let cells=proof.unwrap_or(&[]);
        if unsafe{art3m1s_gxm_surface_publish(self.handle,id,cells.as_ptr(),cells.len())}<=0{return false;}
        self.handle=0;true
    }
}
impl Drop for PrivateSurface{fn drop(&mut self){if self.handle!=0{unsafe{art3m1s_gxm_surface_abort(self.handle)}}}}

pub struct GxmTextureProvider {
    source: Option<Source>,
    prefetch: Option<PrefetchSource>,
    entries: HashMap<String, Entry>,
    decoded: HashMap<String, DecodedEntry>,
    encoded: HashMap<String,EncodedEntry>,
    encoded_hits: u64,
    decoded_hits: u64,
    gpu_demotions: u64,
    ids: HashMap<TextureId, String>,
    next_id: u64,
    revision: u64,
    reported_failures: HashSet<String>,
    cache_clock: u64,
    cache_hits: u64,
    cache_misses: u64,
    cache_evictions: u64,
    reclaim_samples: u32,
    retain_count: u64,
    idle_budget: usize,
    shared_surfaces: bool,
    cache_budget:Option<crate::image_cache_budget::SharedCacheBudget>,
    timing: TextureTiming,
    timing_started: Instant,
    upload_retry_pending: bool,
    upload_reclaim_bytes: usize,
}

impl GxmTextureProvider {
    // Sample at the existing bounded log cadence, never scan pixels or take
    // loader state. Optional prepared CPU backups are charged even while active.
    fn idle_parts(&self)->crate::image_cache_budget::CacheParts{
        use crate::image_cache_budget::CacheParts;
        let mut parts=CacheParts::default();
        for e in self.entries.values(){
            if e.shared||e.reclaimable{parts.decoded+=e.rgba.capacity();}
            if e.reclaimable{parts.gpu+=e.cache_bytes()-e.rgba.capacity();}
        }
        parts.decoded+=self.decoded.values().map(|e|e.rgba.capacity()).sum::<usize>();
        for e in self.encoded.values(){parts.encoded+=e.bytes.capacity();parts.proof+=e.proof.as_ref().map_or(0,TileProof::bytes);}
        parts
    }
    /// Main/render thread only, between completed frames. Hardware video may
    /// reclaim cold GPU residency once after decoder allocation fails. Never
    /// resolve/decode, copy shared pixels back, or call the loader from here.
    pub fn reclaim_video_gpu_cache(&mut self,requested:usize)->usize {
        self.reclaim_idle_gpu_cache(requested,"video-cache-reclaim")
    }
    fn reclaim_idle_gpu_cache(&mut self,requested:usize,reason:&str)->usize {
        let requested=requested.min(16*1024*1024);
        if requested==0{return 0;}
        let cache=self.cache_budget.clone();let mut account=cache.as_ref().map(|b|b.lock().unwrap());
        let mut idle:Vec<_>=self.entries.iter().filter(|(_,e)|e.cacheable&&e.reclaimable)
            .map(|(n,e)|(e.last_used,n.clone())).collect();idle.sort_unstable();
        let mut released=0usize;let mut count=0;
        for (_,name) in idle {
            if released>=requested{break;}
            let e=self.entries.remove(&name).unwrap();self.ids.remove(&e.id);
            unsafe{art3m1s_gxm_delete_texture(e.id.0)};
            let gpu=((e.info.width as usize+7)&!7).saturating_mul(e.info.height as usize).saturating_mul(if e.gray||e.alpha_only{1}else{4});
            released=released.saturating_add((gpu+0x3ffff)&!0x3ffff);count+=1;
            if !e.rgba.is_empty(){
                self.decoded.insert(name,DecodedEntry { gray:e.gray, info:e.info,rgba:e.rgba,last_used:e.last_used});self.gpu_demotions+=1;
            }else{self.cache_evictions+=1;}
        }
        let idle_bytes=self.idle_parts().total();
        if let Some(b)=account.as_mut(){b.set_idle(idle_bytes);}
        crate::core_info!("[{}] textures={} gpu_est_bytes={} requested={} idle_after={}; active pinned, encoded retained",reason,count,released,requested,idle_bytes);
        released
    }
    pub fn needs_upload_retry(&self)->bool { self.upload_retry_pending }
    pub fn begin_scene_build(&mut self) {
        self.upload_retry_pending=false;
        self.upload_reclaim_bytes=0;
    }
    fn defer_upload(&mut self,bytes:usize) {
        self.upload_retry_pending=true;
        self.upload_reclaim_bytes=self.upload_reclaim_bytes.max(bytes);
    }
    /// Import a completed display snapshot without routing pixels through CPU
    /// memory. The host keeps physical display size; logical size follows stage
    /// coordinates, just as full-screen video textures do.
    #[cfg(feature = "gxm-builtin-effects")]
    pub fn capture_completed_frame(&mut self, name: &str, width: u32, height: u32) -> Option<(TextureId, TextureInfo)> {
        unsafe extern "C" {
            fn art3m1s_gxm_capture_previous_texture(id: u64, width: u32, height: u32) -> i32;
        }
        if width == 0 || height == 0 { return None; }
        let id = self.entries.get(name).map_or(TextureId(self.next_id), |entry| entry.id);
        if unsafe { art3m1s_gxm_capture_previous_texture(id.0, width, height) } <= 0 { return None; }
        if id.0 == self.next_id { self.next_id += 1; }
        self.revision = self.revision.wrapping_add(1).max(1);
        let info = TextureInfo { width, height };
        self.decoded.remove(name);
        self.encoded.remove(name);
        self.entries.insert(name.to_owned(), Entry { alpha_only:false, gray:false, id, info, rgba: Vec::new().into(), opaque: false,
            revision: self.revision, last_used: self.cache_clock, cacheable: false,reclaimable:false, shared:false });
        self.ids.insert(id, name.to_owned());
        Some((id, info))
    }
    pub fn new() -> Self {
        Self {
            source: None,
            prefetch: None,
            entries: HashMap::new(),
            decoded: HashMap::new(), decoded_hits: 0, gpu_demotions: 0,
            encoded: HashMap::new(),encoded_hits:0,
            ids: HashMap::new(),
            next_id: 1,
            revision: 0,
            reported_failures: HashSet::new(),
            cache_clock: 0,
            cache_hits: 0,
            cache_misses: 0,
            cache_evictions: 0,
            reclaim_samples: 0,
            retain_count: 0,
            idle_budget: IDLE_TEXTURE_BUDGET,
            shared_surfaces: cfg!(target_os="vita"),
            cache_budget:None,
            timing: TextureTiming::default(),
            timing_started: Instant::now(),
            upload_retry_pending:false,
            upload_reclaim_bytes:0,
        }
    }

    pub fn with_source<F>(mut self, source: F) -> Self
    where
        F: Fn(&str) -> Option<Vec<u8>> + 'static,
    {
        self.source = Some(Box::new(source));
        self
    }

    pub(crate) fn with_cache_budget(mut self,budget:crate::image_cache_budget::SharedCacheBudget)->Self{
        // CPU retention may borrow the shared pool's unused space. GPU and
        // compressed backups retain their own caps; ready reservations win.
        self.idle_budget=budget.lock().unwrap().limit;
        self.cache_budget=Some(budget);self
    }

    /// CPU-only background result. Resolution and GPU upload stay synchronous.
    pub fn with_prefetch(mut self, source: impl Fn(&str) -> Option<Result<image::RgbaImage, Vec<u8>>> + 'static) -> Self {
        self.prefetch = Some(Box::new(move |name|source(name).map(|r|(r.map(Into::into).map_err(Into::into),None,None)))); self
    }
    pub(crate) fn with_tracked_prefetch(mut self,source:impl Fn(&str)->Option<PreparedImage>+'static)->Self{
        self.prefetch=Some(Box::new(source));self
    }

    pub fn cached_info(&self, name: &str) -> Option<TextureInfo> {
        self.entries.get(name).map(|entry| entry.info)
    }

    pub fn content_revision(&self) -> u64 { self.revision }

    pub fn changed_texture_ids_since(&self, revision: u64) -> HashSet<TextureId> {
        self.entries.values().filter(|e| e.revision > revision).map(|e| e.id).collect()
    }

    pub fn upload_video_rgba(&mut self, name: &str, width: u32, height: u32, rgba: &[u8]) -> bool {
        self.upload_impl(name, width, height, rgba, true, true).is_some()
    }

    /// Completed GPU-owned RGBA. Preserve a shared readback view for the rare
    /// CPU reader, but do not scan or copy CDRAM into heap on every video frame.
    pub fn upload_video_shared_rgba(&mut self, name: &str, width: u32, height: u32, rgba: &[u8]) -> bool {
        if self.upload_impl(name, width, height, rgba, true, false).is_none() { return false; }
        if let Some(entry)=self.entries.get_mut(name) { entry.shared=true; }
        true
    }

    pub fn evict_prefix(&mut self, prefix: &str) -> usize {
        let names=self.entries.keys().chain(self.decoded.keys()).chain(self.encoded.keys())
            .filter(|name|name.starts_with(prefix)).cloned().collect::<HashSet<_>>();
        for name in &names {self.remove(name);self.decoded.remove(name);self.encoded.remove(name);}
        names.len()
    }

    pub fn set_profile_enabled(&self, _enabled: bool) {}

    pub fn take_profile_uploads(&self) -> crate::backend::gl::provider::TextureUploadProfile {
        crate::backend::gl::provider::TextureUploadProfile::default()
    }

    pub fn profile_memory(&self) -> (usize, u64, u64) {
        let cpu_bytes = self.entries.values().map(|entry| entry.rgba.len() as u64)
            .chain(self.decoded.values().map(|entry|entry.rgba.len() as u64))
            .chain(self.encoded.values().map(|entry|entry.capacity() as u64)).sum();
        let gpu_bytes = self.entries.values().map(|entry| {
            ((u64::from(entry.info.width) + 7) & !7) * u64::from(entry.info.height) * if entry.gray||entry.alpha_only{1}else{4}
        }).sum();
        (self.entries.len(), cpu_bytes, gpu_bytes)
    }

    fn upload(&mut self, name: &str, width: u32, height: u32, rgba: &[u8]) -> Option<(TextureId, TextureInfo)> {
        self.upload_impl(name, width, height, rgba, false, true)
    }

    fn upload_impl(&mut self, name: &str, width: u32, height: u32, rgba: &[u8], video: bool, retain_pixels: bool) -> Option<(TextureId, TextureInfo)> {
        self.upload_storage(name, width, height, Cow::Borrowed(rgba), video, retain_pixels)
    }

    fn upload_storage(&mut self, name: &str, width: u32, height: u32, pixels: Cow<'_, [u8]>, video: bool, retain_pixels: bool) -> Option<(TextureId, TextureInfo)> {
        let pixels=match pixels{Cow::Borrowed(p)=>PixelStorage::Borrowed(p),Cow::Owned(p)=>PixelStorage::Owned(Tracked::bytes(p,Owner::Temporary))};
        self.upload_tracked(name,width,height,pixels,video,retain_pixels)
    }
    fn upload_tracked(&mut self,name:&str,width:u32,height:u32,pixels:PixelStorage<'_>,video:bool,retain_pixels:bool)->Option<(TextureId,TextureInfo)>{
        self.upload_prepared(name,width,height,pixels,video,retain_pixels,None)
    }
    fn upload_prepared(&mut self,name:&str,width:u32,height:u32,mut pixels:PixelStorage<'_>,video:bool,retain_pixels:bool,proof:Option<&TileProof>)->Option<(TextureId,TextureInfo)>{
        if let PixelStorage::Owned(p)=&mut pixels {p.transfer(Owner::Temporary);}
        let rgba = pixels.as_ref();
        let expected = width as usize * height as usize * 4;
        if width == 0 || height == 0 || rgba.len() != expected { return None; }
        let id = self.entries.get(name).map(|entry| entry.id).unwrap_or_else(|| {
            let id = TextureId(self.next_id);
            self.next_id += 1;
            id
        });
        let started = Instant::now();
        let uploaded = unsafe {
            if video { art3m1s_gxm_upload_video_texture(id.0, width, height, rgba.as_ptr(), rgba.len()) }
            else if let Some(cells)=proof.and_then(|p|p.certificate_for_size(width,height)){art3m1s_gxm_upload_texture_proof(id.0,width,height,rgba.as_ptr(),rgba.len(),cells.as_ptr(),cells.len())}
            else { art3m1s_gxm_upload_texture(id.0, width, height, rgba.as_ptr(), rgba.len()) }
        };
        let host_us = elapsed_us(started);
        self.timing.uploads += 1;
        self.timing.upload_bytes += rgba.len() as u64;
        if uploaded <= 0 {
            if !video && retain_pixels {self.defer_upload(expected);}
            self.timing.upload_errors += 1;
            self.timing.upload_us += host_us;
            self.timing.upload_max_us = self.timing.upload_max_us.max(host_us);
            // Allocation pressure is not a source/decode failure. Keep an owned
            // static image for a later frame, after normal retirement has freed
            // GPU memory; never reread the archive in this resolve call.
            if !video && retain_pixels && !self.entries.contains_key(name)
                && expected <= self.idle_budget.min(16 * 1024 * 1024)
                && let PixelStorage::Owned(mut data) = pixels
            {
                data.transfer(Owner::Provider);
                self.cache_clock = self.cache_clock.saturating_add(1);
                self.decoded.insert(name.to_owned(), DecodedEntry { gray:false, info: TextureInfo { width, height }, rgba: data, last_used: self.cache_clock,
                });
                crate::core_info!("GXM upload-deferred name={} decoded_bytes={}; retry pixels after frame retirement", name, expected);
            }
            return None;
        }
        self.revision = self.revision.wrapping_add(1).max(1);
        let info = TextureInfo { width, height };
        self.decoded.remove(name); // An explicit upload supersedes any demoted source.
        self.encoded.remove(name);
        // Render-only callers own the source pixels (e.g. the glyph atlas).
        // Conservatively keep blending enabled without scanning/copying that buffer.
        let opaque = retain_pixels && proof.and_then(|p|p.opaque_for_size(width,height))
            .unwrap_or_else(||rgba.chunks_exact(4).all(|pixel| pixel[3] == 255));
        self.cache_clock = self.cache_clock.saturating_add(1);
        if let Some(entry) = self.entries.get_mut(name) {
            entry.info=info;entry.opaque=opaque;entry.revision=self.revision;
            entry.last_used=self.cache_clock;entry.cacheable=false;entry.reclaimable=false;
            entry.shared=false;entry.gray=false;entry.alpha_only=false;
            if retain_pixels {
                match pixels {
                    PixelStorage::Owned(mut data) => {data.transfer(Owner::Provider);entry.rgba = data;},
                    PixelStorage::Borrowed(data) => { entry.rgba.clear();entry.rgba.extend_from_slice(data);entry.rgba.sync_capacity(); }
                }
            } else {
                // Drop capacity too if this texture previously kept readable pixels.
                entry.rgba = Tracked::bytes(Vec::new(),Owner::Provider);
            }
        } else {
            self.entries.insert(name.to_owned(), Entry { alpha_only:false, gray:false, id, info, rgba: if retain_pixels { match pixels {PixelStorage::Owned(mut p)=>{p.transfer(Owner::Provider);p},PixelStorage::Borrowed(p)=>Tracked::bytes(p.to_vec(),Owner::Provider)} } else { Tracked::bytes(Vec::new(),Owner::Provider) }, opaque, revision: self.revision, last_used: self.cache_clock, cacheable: false,reclaimable:false,shared:false });
        }
        self.ids.insert(id, name.to_owned());
        let total_us = elapsed_us(started);
        self.timing.upload_us += total_us;
        self.timing.upload_max_us = self.timing.upload_max_us.max(total_us);
        if total_us >= 50000 {
            crate::core_info!("GXM texture-slow-upload name={} size={}x{} video={} host_us={} total_us={}", name, width, height, video, host_us, total_us);
        }
        Some((id, info))
    }

    fn remove(&mut self, name: &str) {
        if let Some(entry) = self.entries.remove(name) {
            self.ids.remove(&entry.id);
            unsafe { art3m1s_gxm_delete_texture(entry.id.0) };
        }
    }

    fn keep_encoded(&mut self,name:&str,mut bytes:Tracked<Vec<u8>>,mut proof:Option<TileProof>){
        // Reuse already-owned compressed data only for large static images.
        // Sources share the existing idle budget, with an additional 1/4 cap;
        // no separate cache allowance, reads, copies, workers or waits here.
        let Some(info)=self.entries.get(name).map(|e|e.info) else{return;};
        if u64::from(info.width)*u64::from(info.height)*4<1024*1024{return;}
        let cap=(self.idle_budget/4).min(4*1024*1024);
        let cost=bytes.capacity()+proof.as_ref().map_or(0,TileProof::bytes);
        if bytes.is_empty()||cost>cap{return;}
        self.encoded.remove(name);
        let mut used=self.encoded.values().map(EncodedEntry::capacity).sum::<usize>();
        while used+cost>cap{
            let oldest=self.encoded.iter().min_by_key(|(_,e)|e.last_used).map(|(name,_)|name.clone()).unwrap();
            used-=self.encoded.remove(&oldest).unwrap().capacity();
        }
        bytes.transfer(Owner::Provider);if let Some(p)=proof.as_mut(){p.transfer(Owner::Provider);}
        self.encoded.insert(name.into(),EncodedEntry{bytes,proof,last_used:self.cache_clock});
    }
    fn share_static_pixels(&mut self,name:&str){
        if !self.shared_surfaces{return;}
        let mut account=self.cache_budget.as_ref().map(|b|b.lock().unwrap());
        let Some(e)=self.entries.get_mut(name) else{return;};
        if e.gray||u64::from(e.info.width)*u64::from(e.info.height)<256*256{return;}
        let mut stride=0;
        if unsafe{art3m1s_gxm_surface_view(e.id.0,&mut stride)}.is_null()||stride<e.info.width as usize{return;}
        let bytes=e.rgba.capacity();
        // Reuse CPU pixels already delivered by prefetch. Never read CDRAM
        // back: a 960x540 copy took 100 ms on hardware. Charge this optional
        // backup even while the matching GPU texture is active.
        // Admission honors promised prefetch headroom as well as live ready
        // bytes. Only keep already-owned pixels; no allocation or GPU readback.
        let keep=bytes>0&&account.as_ref()
            .is_some_and(|b|bytes<=b.idle_limit(self.idle_budget).saturating_sub(b.idle));
        if keep{let b=account.as_mut().unwrap();let idle=b.idle+bytes;b.set_idle(idle);}
        else{e.rgba=Tracked::bytes(Vec::new(),Owner::Provider);}
        e.shared=true;
        crate::core_info!("GXM shared-surface-cache name={} cpu_released={} cpu_retained={} size={}x{}",name,if keep{0}else{bytes},if keep{bytes}else{0},e.info.width,e.info.height);
    }
    fn upload_gray(&mut self,name:&str,width:u32,height:u32,mut pixels:Tracked<Vec<u8>>)->Option<(TextureId,TextureInfo)>{
        if width==0||height==0||pixels.len()!=(width as usize).checked_mul(height as usize)?{return None;}
        let id=self.entries.get(name).map_or(TextureId(self.next_id),|e|e.id);
        let info=TextureInfo{width,height};let started=Instant::now();
        let ok=unsafe{art3m1s_gxm_upload_luma(id.0,width,height,pixels.as_ptr(),pixels.len())};
        if ok<0{
            // Unsupported/self-test failure: preserve grayscale RGB and opaque alpha.
            let mut rgba=Vec::new();rgba.try_reserve_exact(pixels.len().checked_mul(4)?).ok()?;
            for &v in pixels.iter(){rgba.extend_from_slice(&[v,v,v,255]);}
            let result=self.upload_storage(name,width,height,Cow::Owned(rgba),false,true);
            if result.is_some(){self.entries.get_mut(name).unwrap().cacheable=true;}
            return result;
        }
        let us=elapsed_us(started);self.timing.uploads+=1;self.timing.upload_us+=us;
        self.timing.upload_max_us=self.timing.upload_max_us.max(us);
        pixels.transfer(Owner::Provider);
        if ok<=0{
            self.defer_upload(((width as usize+7)&!7).saturating_mul(height as usize));
            self.timing.upload_errors+=1;
            self.decoded.insert(name.into(),DecodedEntry{gray:true,info,rgba:pixels,last_used:self.cache_clock});
            return None;
        }
        self.timing.upload_bytes+=pixels.len() as u64;
        if id.0==self.next_id{self.next_id+=1;}
        self.revision=self.revision.wrapping_add(1).max(1);
        self.decoded.remove(name);self.encoded.remove(name);
        self.entries.insert(name.into(),Entry{alpha_only:false,gray:true,id,info,rgba:pixels,opaque:true,
            revision:self.revision,last_used:self.cache_clock,cacheable:true,reclaimable:false,shared:false});
        self.ids.insert(id,name.into());
        crate::core_info!("GXM gray8-upload name={} size={}x{} pixel_bytes={} upload_us={}",name,width,height,width as usize*height as usize,us);
        Some((id,info))
    }
    fn entry_pixels(&self,e:&Entry)->Option<Vec<u8>>{
        if e.gray{
            let mut out=Vec::new();out.try_reserve_exact(e.rgba.len().checked_mul(4)?).ok()?;
            for &v in e.rgba.iter(){out.extend_from_slice(&[v,v,v,255]);}return Some(out);
        }
        if !e.shared||!e.rgba.is_empty(){return Some(e.rgba.data.clone());}
        let mut stride=0;let p=unsafe{art3m1s_gxm_surface_view(e.id.0,&mut stride)};
        if p.is_null()||stride<e.info.width as usize{return None;}
        let row=e.info.width as usize*4;let mut out=Vec::new();
        out.try_reserve_exact(row*e.info.height as usize).ok()?;
        for y in 0..e.info.height as usize{out.extend_from_slice(unsafe{std::slice::from_raw_parts(p.add(y*stride*4),row)});}
        Some(out)
    }
}

impl Default for GxmTextureProvider {
    fn default() -> Self { Self::new() }
}

impl Drop for GxmTextureProvider {
    fn drop(&mut self) {
        for entry in self.entries.values() {
            unsafe { art3m1s_gxm_delete_texture(entry.id.0) };
        }
        if let Some(cache)=&self.cache_budget{cache.lock().unwrap().set_idle(0);}
    }
}

impl TextureProvider for GxmTextureProvider {
    fn resolve(&mut self, name: &str) -> Option<(TextureId, TextureInfo)> {
        self.cache_clock = self.cache_clock.saturating_add(1);
        if let Some(entry) = self.entries.get_mut(name) {
            entry.last_used = self.cache_clock;entry.reclaimable=false;
            if let Some(source)=self.encoded.get_mut(name){source.last_used=self.cache_clock;}
            self.cache_hits += 1;
            return Some((entry.id, entry.info));
        }
        if crate::video::is_video_layer_texture_name(name) { return None; }
        if let Some(entry)=self.decoded.remove(name){
            let source=self.encoded.remove(name);
            let started=Instant::now();let info=entry.info;
            let result=if entry.gray{self.upload_gray(name,info.width,info.height,entry.rgba)}else{
                self.upload_prepared(name,info.width,info.height,PixelStorage::Owned(entry.rgba),false,true,source.as_ref().and_then(|s|s.proof.as_ref()))};
            if result.is_some(){
                self.entries.get_mut(name).unwrap().cacheable=true;self.decoded_hits+=1;
                self.share_static_pixels(name);
                if let Some(source)=source{self.keep_encoded(name,source.bytes,source.proof);}
                crate::core_info!("GXM decoded-cache-hit name={} size={}x{} upload_us={}",name,info.width,info.height,elapsed_us(started));
                return result;
            }
            // upload_prepared puts owned pixels back on failure. Do not turn a
            // GPU allocation failure into another archive read and PNG decode.
            return None;
        }
        self.cache_misses += 1;
        // Keep the loader's existing delivery/wait protocol and prefer prepared
        // pixels over decoding a retained source again.
        let ready = self.prefetch.as_ref().and_then(|f|f(name));
        let ready = ready.or_else(|| {
            let source=self.encoded.remove(name)?;
            self.encoded_hits+=1;
            crate::core_info!("GXM encoded-cache-hit name={} bytes={} hits={}",name,source.bytes.len(),self.encoded_hits);
            Some((Err(source.bytes),source.proof,None))
        });
        let (ready,mut proof,source)=match ready{Some((r,p,s))=>(Some(r),p,s),None=>(None,None,None)};
        let bytes = match ready {
            Some(Ok(image)) => {
                let (w,h) = image.dimensions();
                let gray=image.is_gray();
                let result = if gray{self.upload_gray(name,w,h,image.into_raw())}else{
                    self.upload_prepared(name,w,h,PixelStorage::Owned(image.into_raw()),false,true,proof.as_ref())};
                if result.is_some() {
                    self.entries.get_mut(name).unwrap().cacheable = true;
                    self.share_static_pixels(name);
                    if let Some(source)=source{self.keep_encoded(name,source,proof);}
                    crate::core_info!("GXM prefetch-hit name={} size={}x{}",name,w,h);
                    return result;
                }
                return None;
            }
            Some(Err(encoded)) => {
                crate::core_info!("GXM prefetch-encoded-hit name={} bytes={}",name,encoded.len());
                Some(encoded)
            },
            None => None,
        };
        let started = Instant::now();
        let bytes = bytes.or_else(|| self.source.as_ref().and_then(|source| source(name)).map(|v|Tracked::bytes(v,Owner::Source)));
        let read_us = elapsed_us(started);
        self.timing.reads += 1;
        self.timing.read_us += read_us;
        if read_us >= 50000 {
            crate::core_info!("GXM texture-slow-read name={} found={} read_us={}", name, bytes.is_some(), read_us);
        }
        let bytes = match bytes {
            Some(bytes) => bytes,
            None => {
                self.timing.missing += 1;
                if self.reported_failures.insert(name.to_owned()) {
                    crate::core_warn!("GXM texture source missing: {name}");
                }
                return None;
            }
        };
        let mut bytes=bytes;bytes.transfer(Owner::Source);
        let started = Instant::now();
        let reader = match ImageReader::new(Cursor::new(bytes.as_slice())).with_guessed_format() {
            Ok(reader) => reader,
            Err(error) => {
                self.timing.decode_errors += 1;
                self.timing.decode_us += elapsed_us(started);
                if self.reported_failures.insert(name.to_owned()) {
                    crate::core_warn!("GXM texture format failure: {name}: {error}");
                }
                return None;
            }
        };
        let decoder=match reader.into_decoder(){Ok(d)=>d,Err(error)=>{
            self.timing.decode_errors+=1;crate::core_warn!("GXM texture decoder failure: {name}: {error}");return None;
        }};
        let (width,height)=decoder.dimensions();
        if decoder.color_type()==image::ColorType::L8{
            let pixels=crate::resource_ledger::decode_luma(decoder,512*1024*1024)?;
            self.timing.decoded+=1;self.timing.decode_us+=elapsed_us(started);
            let result=self.upload_gray(name,width,height,pixels);
            if result.is_some(){self.keep_encoded(name,bytes,None);}
            return result;
        }
        let logical=u64::from(width)*u64::from(height)*4;
        if self.shared_surfaces&&logical>=256*256*4&&logical<=16*1024*1024&&decoder.total_bytes()<=logical{
            if let Some(mut surface)=PrivateSurface::new(width,height){
                if let Err(error)=crate::image_decode::rgba_into(decoder,surface.bytes(),16*1024*1024){
                    self.timing.decode_errors+=1;crate::core_warn!("GXM shared-surface decode failure: {name}: {error}");return None;
                }
                let decode_us=elapsed_us(started);self.timing.decoded+=1;self.timing.decode_us+=decode_us;
                let opaque=proof.as_ref().and_then(|p|p.opaque_for_size(width,height))
                    .unwrap_or_else(||surface.bytes()[..logical as usize].chunks_exact(4).all(|p|p[3]==255));
                let id=self.entries.get(name).map_or(TextureId(self.next_id),|e|e.id);
                let upload_started=Instant::now();
                if !surface.publish(id.0,proof.as_ref().and_then(|p|p.certificate_for_size(width,height))){
                    self.defer_upload(logical as usize);self.timing.upload_errors+=1;return None;
                }
                if id.0==self.next_id{self.next_id+=1;}
                self.revision=self.revision.wrapping_add(1).max(1);
                let info=TextureInfo{width,height};self.decoded.remove(name);
                self.entries.insert(name.into(),Entry { alpha_only:false, gray:false, id,info,rgba:Tracked::bytes(Vec::new(),Owner::Provider),opaque,
                    revision:self.revision,last_used:self.cache_clock,cacheable:true,reclaimable:false,shared:true});
                self.ids.insert(id,name.into());self.keep_encoded(name,bytes,proof);
                let publish_us=elapsed_us(upload_started);self.timing.uploads+=1;self.timing.upload_us+=publish_us;
                self.timing.upload_max_us=self.timing.upload_max_us.max(publish_us);
                crate::core_info!("GXM shared-surface-decode name={} size={}x{} decode_us={} publish_us={} upload_copy_bytes=0",name,width,height,decode_us,publish_us);
                return Some((id,info));
            }
        }
        let image = match crate::resource_ledger::decode_rgba(decoder,512*1024*1024) {
            Ok(image) => image,
            Err(error) => {
                self.timing.decode_errors += 1;
                self.timing.decode_us += elapsed_us(started);
                if self.reported_failures.insert(name.to_owned()) {
                    crate::core_warn!("GXM texture decode failure: {name}: {error}");
                }
                return None;
            }
        };
        let decode_us = elapsed_us(started);
        self.timing.decoded += 1;
        self.timing.decode_us += decode_us;
        if decode_us >= 50000 {
            crate::core_info!("GXM texture-slow-decode name={} size={}x{} decode_us={}", name, image.width(), image.height(), decode_us);
        }
        let (width, height) = image.dimensions();
        let result = self.upload_prepared(name, width, height, PixelStorage::Owned(image.into_raw()), false, true,proof.as_ref());
        if result.is_some() {
            self.entries.get_mut(name).unwrap().cacheable = true;
            self.share_static_pixels(name);
            self.keep_encoded(name,bytes,proof);
        }
        if result.is_none() && self.reported_failures.insert(name.to_owned()) {
            crate::core_warn!("GXM texture upload failure: {name}: {}x{}", width, height);
        }
        result
    }

    fn upload_rgba(&mut self, name: &str, width: u32, height: u32, data: &[u8]) -> Option<(TextureId, TextureInfo)> {
        self.upload(name, width, height, data)
    }

    fn upload_rgba_render_only(&mut self, name: &str, width: u32, height: u32, data: &[u8]) -> Option<(TextureId, TextureInfo)> {
        self.upload_impl(name, width, height, data, false, false)
    }

    fn upload_rgba_render_only_region(&mut self, name: &str, width: u32, height: u32, data: &[u8], region: [u32; 4]) -> Option<(TextureId, TextureInfo)> {
        let [x, y, w, h] = region;
        if width == 0 || height == 0 || data.len() != width as usize * height as usize * 4 ||
            w == 0 || h == 0 || x >= width || y >= height || w > width-x || h > height-y { return None; }
        let Some(entry) = self.entries.get(name) else {
            return self.upload_impl(name, width, height, data, false, false);
        };
        if entry.gray || entry.alpha_only || entry.info != (TextureInfo { width, height }) || !entry.rgba.is_empty() {
            return self.upload_impl(name, width, height, data, false, false);
        }
        let id = entry.id;
        let started = Instant::now();
        let ok = unsafe { art3m1s_gxm_update_texture_region(id.0, width, height, data.as_ptr(), data.len(), x, y, w, h) };
        let us = elapsed_us(started);
        self.timing.uploads += 1;
        self.timing.upload_us += us;
        self.timing.upload_max_us = self.timing.upload_max_us.max(us);
        if ok <= 0 { self.timing.upload_errors += 1; return None; }
        self.encoded.remove(name);
        self.timing.upload_bytes += w as u64 * h as u64 * 4;
        self.revision = self.revision.wrapping_add(1).max(1);
        let entry = self.entries.get_mut(name).unwrap();
        entry.reclaimable=false;
        entry.revision = self.revision;
        entry.opaque = false;
        Some((id, entry.info))
    }

    fn upload_alpha_render_only_region(&mut self,name:&str,width:u32,height:u32,data:&[u8],region:[u32;4])->Option<(TextureId,TextureInfo)>{
        let [x,y,w,h]=region;
        if width==0||height==0||data.len()!=(width as usize).checked_mul(height as usize)?||w==0||h==0||x>=width||y>=height||w>width-x||h>height-y{return None;}
        let info=TextureInfo{width,height};
        let id=self.entries.get(name).map_or(TextureId(self.next_id),|e|e.id);
        let partial=self.entries.get(name).is_some_and(|e|e.alpha_only&&e.info==info);
        let started=Instant::now();
        let ok=unsafe{art3m1s_gxm_upload_alpha_region(id.0,width,height,data.as_ptr(),data.len(),x,y,w,h)};
        if ok<0{
            let mut rgba=Vec::new();rgba.try_reserve_exact(data.len().checked_mul(4)?).ok()?;
            for &a in data{rgba.extend_from_slice(&[255,255,255,a]);}
            return self.upload_rgba_render_only_region(name,width,height,&rgba,region);
        }
        let us=elapsed_us(started);self.timing.uploads+=1;self.timing.upload_us+=us;
        self.timing.upload_max_us=self.timing.upload_max_us.max(us);
        if ok==0{self.timing.upload_errors+=1;return None;}
        self.timing.upload_bytes+=if partial{w as u64*h as u64}else{data.len() as u64};
        if id.0==self.next_id{self.next_id+=1;}
        self.revision=self.revision.wrapping_add(1).max(1);
        if partial{
            let entry=self.entries.get_mut(name).unwrap();entry.revision=self.revision;entry.reclaimable=false;
            return Some((id,info));
        }
        self.decoded.remove(name);self.encoded.remove(name);
        self.entries.insert(name.into(),Entry{alpha_only:true,gray:false,id,info,rgba:Tracked::bytes(Vec::new(),Owner::Provider),opaque:false,
            revision:self.revision,last_used:self.cache_clock,cacheable:false,reclaimable:false,shared:false});
        self.ids.insert(id,name.into());
        if !partial{crate::core_info!("GXM alpha-atlas name={} size={}x{} gpu_pixel_bytes={} cpu_mirror=0",name,width,height,data.len());}
        Some((id,info))
    }

    fn pixel_alpha(&self, texture: TextureId, x: u32, y: u32) -> Option<u8> {
        let entry = self.entries.get(self.ids.get(&texture)?)?;
        if x >= entry.info.width || y >= entry.info.height { return None; }
        if entry.gray{return Some(255);}
        if entry.shared&&entry.rgba.is_empty(){
            let mut stride=0;let p=unsafe{art3m1s_gxm_surface_view(texture.0,&mut stride)};
            if p.is_null()||stride<entry.info.width as usize{return None;}
            return Some(unsafe{*p.add((y as usize*stride+x as usize)*4+3)});
        }
        entry.rgba.get(((y * entry.info.width + x) * 4 + 3) as usize).copied()
    }

    fn texture_is_opaque(&self, texture: TextureId) -> bool {
        self.ids.get(&texture).and_then(|name| self.entries.get(name)).is_some_and(|entry| entry.opaque)
    }

    fn retain(&mut self, names: &HashSet<String>) {
        // Keep admission serialized with loader publication. No loader calls
        // (or waits for loader tickets) occur while this budget lock is held.
        let cache=self.cache_budget.clone();
        let mut account=cache.as_ref().map(|b|b.lock().unwrap());
        let idle_limit=account.as_ref().map_or(self.idle_budget,|b|b.idle_limit(self.idle_budget));
        let wall_us = elapsed_us(self.timing_started);
        if wall_us >= 5000000 {
            let t = std::mem::take(&mut self.timing);
            self.timing_started = Instant::now();
            crate::core_info!("GXM texture-perf wall_us={} reads={} missing={} decoded={} decode_errors={} uploads={} upload_errors={} upload_bytes={} read_us={} decode_us={} upload_us={} upload_max_us={}",
                wall_us, t.reads, t.missing, t.decoded, t.decode_errors, t.uploads, t.upload_errors, t.upload_bytes, t.read_us, t.decode_us, t.upload_us, t.upload_max_us);
        }
        let stale = self.entries.iter_mut().filter_map(|(name,entry)|{
            let stale=!names.contains(name)&&!name.starts_with("__solid_");
            entry.reclaimable=entry.cacheable&&stale;
            stale.then(||name.clone())
        }).collect::<Vec<_>>();
        let mut idle = Vec::new();
        let mut idle_bytes = self.decoded.values().map(|e|e.rgba.capacity()).sum::<usize>()
            +self.encoded.values().map(EncodedEntry::capacity).sum::<usize>()
            +self.entries.values().filter(|e|e.shared).map(|e|e.rgba.capacity()).sum::<usize>();
        for name in stale {
            let entry = &self.entries[&name];
            if entry.cacheable {
                let bytes = entry.cache_bytes()-if entry.shared{entry.rgba.capacity()}else{0};
                idle_bytes = idle_bytes.saturating_add(bytes);
                idle.push((entry.last_used, name, bytes));
            } else {
                // Video, text and capture targets retain their explicit lifecycle.
                self.remove(&name);
            }
        }
        // One recency order across both tiers. Old decoded scenes must not
        // evict GPU copies of a hot animation merely because they are CPU-only.
        let mut reclaim:Vec<_>=idle.into_iter().map(|(used,name,bytes)|(used,name,0,bytes)).collect();
        reclaim.extend(self.decoded.iter().map(|(name,e)|(e.last_used,name.clone(),1,0)));
        reclaim.extend(self.encoded.iter().map(|(name,e)|(e.last_used,name.clone(),2,0)));
        reclaim.extend(self.entries.iter().filter(|(_,e)|e.shared&&!e.rgba.is_empty()).map(|(name,e)|(e.last_used,name.clone(),3,0)));
        // Preserve the small last-resort sources when dropping costly surfaces.
        // GPU and decoded copies retain their existing shared recency order.
        // Sources cannot occupy more than 1/4 of this same total budget.
        reclaim.sort_unstable_by(|a,b|(a.2==2,a.0,&a.1,a.2).cmp(&(b.2==2,b.0,&b.1,b.2)));
        let mut idle_gpu=self.entries.values().filter(|e|e.reclaimable)
            .map(|e|e.cache_bytes()-e.rgba.capacity()).sum::<usize>();
        let gpu_limit=IDLE_GPU_BUDGET.min(idle_limit);
        for (_,name,tier,gpu_bytes) in reclaim {
            if idle_bytes<=idle_limit&&idle_gpu<=gpu_limit {break;}
            if tier!=0&&idle_bytes<=idle_limit {continue;}
            let before=idle_bytes;
            if tier==3{
                if let Some(e)=self.entries.get_mut(&name){
                    idle_bytes=idle_bytes.saturating_sub(e.rgba.capacity());
                    e.rgba=Tracked::bytes(Vec::new(),Owner::Provider);
                }
                continue;
            }
            if tier==2{
                if let Some(source)=self.encoded.remove(&name){idle_bytes=idle_bytes.saturating_sub(source.capacity());}
                continue;
            }
            if tier==0 {
                let entry=self.entries.remove(&name).unwrap();self.ids.remove(&entry.id);
                idle_gpu=idle_gpu.saturating_sub(entry.cache_bytes()-entry.rgba.capacity());
                unsafe {art3m1s_gxm_delete_texture(entry.id.0)};
                if entry.shared&&entry.rgba.is_empty(){
                    idle_bytes=idle_bytes.saturating_sub(gpu_bytes);self.cache_evictions+=1;
                    if self.reclaim_samples<32{self.reclaim_samples+=1;crate::core_info!("GXM shared-surface-reclaim name={} freed={} idle_after={} budget={}",name,gpu_bytes,idle_bytes,idle_limit);}
                    continue; // No duplicate CPU pixels to demote; encoded fallback is retained.
                }
                idle_bytes=idle_bytes.saturating_sub(if entry.shared{gpu_bytes}else{gpu_bytes-entry.rgba.capacity()});
                self.decoded.insert(name.clone(),DecodedEntry { gray:entry.gray, info:entry.info,rgba:entry.rgba,last_used:entry.last_used});
                self.gpu_demotions+=1;
            }
            // Demotion may already meet the budget. Otherwise reclaim the
            // remaining bytes of this same old asset before touching newer ones.
            if idle_bytes>idle_limit {
                let e=self.decoded.remove(&name).unwrap();idle_bytes=idle_bytes.saturating_sub(e.rgba.capacity());
                self.cache_evictions+=1;
            }
            // Bound diagnostics to large reclamations. No per-frame scan or
            // policy change: identify which cold resolves were caused by LRU.
            if self.reclaim_samples<32 && before.saturating_sub(idle_bytes)>=1024*1024 {
                self.reclaim_samples+=1;
                crate::core_info!("GXM texture-reclaim name={} tier={} freed_bytes={} idle_before={} idle_after={} budget={} decoded_kept={} active_names={} sample={}/32",
                    name,if tier==0{"gpu"}else{"decoded"},before-idle_bytes,before,idle_bytes,
                    idle_limit,self.decoded.contains_key(&name),names.len(),self.reclaim_samples);
            }
        }
        self.retain_count += 1;
        if let Some(b)=account.as_mut(){b.set_idle(idle_bytes);}
        if crate::cache_hud::should_sample(){
            if let Some(b)=account.as_ref(){
                let p=self.idle_parts();let r=b.ready_parts;
                crate::cache_hud::publish([1,b.limit as u64,b.ready as u64,r.decoded as u64,r.encoded as u64,
                    b.idle as u64,idle_limit as u64,p.decoded as u64,p.gpu as u64,p.encoded as u64,
                    idle_limit.saturating_sub(p.gpu+p.encoded+p.proof) as u64,gpu_limit as u64,self.cache_hits,self.decoded_hits,
                    self.encoded_hits,self.cache_misses,self.cache_evictions,b.ready_goal as u64,
                    b.mask_parts.total() as u64,b.animation_parts.total() as u64,
                    b.script_preload.planned as u64,b.script_preload.completed as u64,
                    b.script_preload.pixels as u64,b.script_preload.encoded as u64]);
            }
        }
        if self.retain_count % 120 == 0 {
            if let Some(b)=account.as_ref(){
                let p=self.idle_parts();let r=b.ready_parts;
                debug_assert_eq!(p.total(),b.idle);
                crate::core_info!("[image-cache-budget] ready={} idle={} total={} limit={} idle_limit={} ready_goal={} active_excluded=1 parts_version=1 ready_decoded={} ready_encoded={} ready_proof={} idle_decoded={} idle_gpu_est={} idle_encoded={} idle_proof={}",
                    b.ready,b.idle,b.ready+b.idle,b.limit,idle_limit,b.ready_goal,r.decoded,r.encoded,r.proof,p.decoded,p.gpu,p.encoded,p.proof);
                crate::core_info!("[image-prefetch-lanes] mask_decoded={} mask_encoded={} mask_proof={} mask_limit={} animation_decoded={} animation_encoded={} animation_proof={} animation_limit={} ready_subset=1",
                    b.mask_parts.decoded,b.mask_parts.encoded,b.mask_parts.proof,16*1024*1024,
                    b.animation_parts.decoded,b.animation_parts.encoded,b.animation_parts.proof,32*1024*1024);
            }
            crate::core_info!("GXM texture-cache hits={} misses={} evictions={} inactive_est_bytes={} budget={} decoded_hits={} gpu_demotions={} decoded_entries={} encoded_hits={} encoded_entries={} encoded_bytes={}",
                self.cache_hits, self.cache_misses, self.cache_evictions, idle_bytes, idle_limit,
                self.decoded_hits,self.gpu_demotions,self.decoded.len(),self.encoded_hits,self.encoded.len(),self.encoded.values().map(EncodedEntry::capacity).sum::<usize>());
        }
        // Allocation failure can occur below the logical cache budget (physical
        // CDRAM/uncached exhaustion or fragmentation). Only reclaim after every
        // current scene resource has been pinned; never evict while traversing
        // a scene whose later draws may still reference the previous frame's idle set.
        let requested=std::mem::take(&mut self.upload_reclaim_bytes);
        drop(account);
        if requested>0 {self.reclaim_idle_gpu_cache(requested,"texture-pressure-reclaim");}
    }

    fn solid_texture(&mut self, rgba: [u8; 4]) -> Option<(TextureId, TextureInfo)> {
        let name = solid_texture_name(rgba);
        if let Some(entry) = self.entries.get(&name) { return Some((entry.id, entry.info)); }
        self.upload(&name, 1, 1, &rgba)
    }

    fn resolve_with_mask(&mut self, file: &str, mask: &str) -> Option<(TextureId, TextureInfo)> {
        let name = masked_texture_name(file, mask);
        if let Some(entry) = self.entries.get_mut(&name) { entry.reclaimable=false;return Some((entry.id, entry.info)); }
        let (file_id, file_info) = self.resolve(file)?;
        let (mask_id, mask_info) = self.resolve(mask)?;
        if file_info != mask_info { return Some((file_id, file_info)); }
        let source = self.entry_pixels(self.entries.get(self.ids.get(&file_id)?)?)?;
        let mask_pixels = self.entry_pixels(self.entries.get(self.ids.get(&mask_id)?)?)?;
        let mut output = source;
        for (pixel, alpha) in output.chunks_exact_mut(4).zip(mask_pixels.chunks_exact(4)) {
            let gray = (u16::from(alpha[0]) + u16::from(alpha[1]) + u16::from(alpha[2])) / 3;
            pixel[3] = ((u16::from(pixel[3]) * gray + 127) / 255) as u8;
        }
        self.upload(&name, file_info.width, file_info.height, &output)
    }

    fn pixels_of(&mut self, name: &str) -> Option<(u32, u32, Vec<u8>)> {
        self.resolve(name)?;
        let entry = self.entries.get(name)?;
        Some((entry.info.width, entry.info.height, self.entry_pixels(entry)?))
    }
}

#[cfg(all(test, not(target_os = "vita")))]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::sync::{Mutex, atomic::{AtomicUsize, Ordering}};

    static LOCK: Mutex<()> = Mutex::new(());
    #[test] fn alpha_atlas_updates_preserve_identity_and_retry_without_cpu_mirror(){
        let _guard=LOCK.lock().unwrap();let mut p=GxmTextureProvider::new();let mut data=vec![0;512*512];
        data[7*512+5]=139;
        let (id,info)=p.upload_alpha_render_only_region(":text/atlas",512,512,&data,[5,7,1,1]).unwrap();
        assert_eq!(p.profile_memory(),(1,0,256*1024));assert!(!p.texture_is_opaque(id));
        assert_eq!(p.timing.upload_bytes,256*1024);let revision=p.content_revision();
        FAIL_UPLOAD.store(1,Ordering::Relaxed);
        let failed=p.upload_alpha_render_only_region(":text/atlas",512,512,&data,[9,12,4,3]);
        FAIL_UPLOAD.store(0,Ordering::Relaxed);assert!(failed.is_none());assert_eq!(p.content_revision(),revision);
        assert_eq!(p.upload_alpha_render_only_region(":text/atlas",512,512,&data,[9,12,4,3]),Some((id,info)));
        assert_eq!(p.timing.upload_bytes,256*1024+12);assert!(p.content_revision()>revision);
        p.retain(&HashSet::from([":text/atlas".into()]));assert!(p.entries.contains_key(":text/atlas"));
        let rgba=vec![255;512*512*4];p.upload_rgba_render_only_region(":text/atlas",512,512,&rgba,[0,0,1,1]).unwrap();
        assert!(!p.entries[":text/atlas"].alpha_only);assert_eq!(p.profile_memory(),(1,0,1024*1024));
    }
    #[test] fn unsupported_alpha_atlas_uses_rgba_without_marking_it_as_single_channel(){
        let _guard=LOCK.lock().unwrap();ALPHA_UNSUPPORTED.store(1,Ordering::Relaxed);
        let mut p=GxmTextureProvider::new();let result=p.upload_alpha_render_only_region("atlas",2,1,&[0,139],[0,0,2,1]);
        ALPHA_UNSUPPORTED.store(0,Ordering::Relaxed);assert!(result.is_some());
        assert!(!p.entries["atlas"].alpha_only);assert_eq!(p.profile_memory(),(1,0,32));
    }
    #[test] fn gray8_decode_keeps_compact_pixels_through_gpu_reclaim_and_rgba_replacement(){
        let _guard=LOCK.lock().unwrap();
        let img=image::GrayImage::from_fn(17,9,|x,y|image::Luma([(x*17+y*31) as u8]));
        let expected=image::DynamicImage::ImageLuma8(img.clone()).to_rgba8().into_raw();
        let mut png=Cursor::new(Vec::new());img.write_to(&mut png,image::ImageFormat::Png).unwrap();
        let bytes=png.into_inner();let calls=Rc::new(Cell::new(0));let count=calls.clone();
        let mut p=GxmTextureProvider::new().with_source(move |_|{count.set(count.get()+1);Some(bytes.clone())});
        let (id,_)=p.resolve("rule").unwrap();assert!(p.entries["rule"].gray);
        assert_eq!(p.profile_memory(),(1,153,216));assert_eq!(p.pixel_alpha(id,16,8),Some(255));
        assert_eq!(p.pixels_of("rule").unwrap().2,expected);
        p.retain(&HashSet::new());assert_eq!(p.reclaim_video_gpu_cache(1),256*1024);
        assert!(p.decoded["rule"].gray);assert_eq!(p.decoded["rule"].rgba.len(),153);
        p.resolve("rule").unwrap();assert_eq!(calls.get(),1);assert_eq!(p.pixels_of("rule").unwrap().2,expected);
        let replacement=vec![128;153*4];p.upload_rgba("rule",17,9,&replacement).unwrap();
        assert!(!p.entries["rule"].gray);assert_eq!(p.pixels_of("rule").unwrap().2,replacement);
    }
    #[test] fn gray8_prefetch_failed_upload_retries_compact_pixels_without_io(){
        let _guard=LOCK.lock().unwrap();let ready=std::cell::RefCell::new(Some((
            Ok(PreparedPixels::Gray(17,9,vec![123;153].into())),None,None)));
        let mut p=GxmTextureProvider::new().with_source(|_|panic!("must not reread"))
            .with_tracked_prefetch(move |_|ready.borrow_mut().take());
        FAIL_UPLOAD.store(1,Ordering::Relaxed);let failed=p.resolve("mask");FAIL_UPLOAD.store(0,Ordering::Relaxed);
        assert!(failed.is_none());assert!(p.decoded["mask"].gray);
        let (id,_)=p.resolve("mask").unwrap();assert_eq!(p.pixel_alpha(id,0,0),Some(255));
        assert_eq!(p.pixels_of("mask").unwrap().2,[123,123,123,255].repeat(153));
    }
    #[test] fn grayscale_alpha_png_is_not_treated_as_opaque_luma(){
        let _guard=LOCK.lock().unwrap();let mut png=Cursor::new(Vec::new());
        image::GrayAlphaImage::from_pixel(7,3,image::LumaA([123,139])).write_to(&mut png,image::ImageFormat::Png).unwrap();
        let bytes=png.into_inner();let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.clone()));
        let (id,_)=p.resolve("maskface").unwrap();assert!(!p.entries["maskface"].gray);
        assert_eq!(p.pixel_alpha(id,6,2),Some(139));assert!(!p.texture_is_opaque(id));
        assert_eq!(p.pixels_of("maskface").unwrap().2,[123,123,123,139].repeat(21));
    }
    #[test] fn palette_transparency_keeps_rgba_pixels_even_when_used_as_mask(){
        let _guard=LOCK.lock().unwrap();
        let bytes=include_bytes!("../../image_decode_testdata/pal8-trns.png");
        let expected=image::load_from_memory(bytes).unwrap().to_rgba8().into_raw();
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.to_vec()));
        p.resolve("rule/wipe_17").unwrap();assert!(!p.entries["rule/wipe_17"].gray);
        assert_eq!(p.pixels_of("rule/wipe_17").unwrap().2,expected);
    }
    #[test] fn gray8_png_transparent_key_prevents_opaque_luma_upload(){
        let _guard=LOCK.lock().unwrap();let bytes=include_bytes!("../../image_decode_testdata/gray8-trns.png");
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.to_vec()));
        let (id,_)=p.resolve("maskface").unwrap();assert!(!p.entries["maskface"].gray);
        assert_eq!(p.pixel_alpha(id,0,0),Some(255));assert_eq!(p.pixel_alpha(id,1,0),Some(0));
    }
    #[test] fn unsupported_luma_upload_falls_back_to_identical_rgba(){
        let _guard=LOCK.lock().unwrap();LUMA_UNSUPPORTED.store(1,Ordering::Relaxed);
        let mut p=GxmTextureProvider::new();let result=p.upload_gray("mask",2,1,vec![0,139].into());
        LUMA_UNSUPPORTED.store(0,Ordering::Relaxed);let (id,_)=result.unwrap();
        assert!(!p.entries["mask"].gray);assert!(p.entries["mask"].cacheable);assert_eq!(p.pixel_alpha(id,1,0),Some(255));
        assert_eq!(p.pixels_of("mask").unwrap().2,[0,0,0,255,139,139,139,255]);
    }
    #[test] fn compact_gray_cpu_mask_conversion_preserves_coverage(){
        let _guard=LOCK.lock().unwrap();let mut p=GxmTextureProvider::new();
        p.upload_rgba("image",2,1,&[30,40,50,128,30,40,50,128]).unwrap();
        p.upload_gray("mask",2,1,vec![0,139].into()).unwrap();
        let (id,_)=p.resolve_with_mask("image","mask").unwrap();
        assert_eq!(p.pixel_alpha(id,0,0),Some(0));assert_eq!(p.pixel_alpha(id,1,0),Some(70));
        assert!(p.entries["mask"].gray);assert_eq!(p.entries["mask"].rgba.len(),2);
    }
    #[test] fn game_black_white_aliases_use_source_and_normal_idle_budget() {
        let _lock=LOCK.lock().unwrap();
        let mut png=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(32,16,image::Rgba([30,40,50,139]))
            .write_to(&mut png,image::ImageFormat::Png).unwrap();
        let bytes=png.into_inner();
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.clone()));
        for name in [":bg/black",":bg/white"] {
            let (id,info)=p.resolve(name).unwrap();
            assert_eq!((info.width,info.height),(32,16));
            assert_eq!(p.pixel_alpha(id,0,0),Some(139));
        }
        p.retain(&HashSet::from([":bg/black".into()]));
        assert!(!p.entries[":bg/black"].reclaimable);
        assert!(p.entries[":bg/white"].reclaimable);
        p.idle_budget=0;
        p.retain(&HashSet::new());
        assert!(p.entries.is_empty()&&p.decoded.is_empty()&&p.encoded.is_empty());
    }
    #[test] fn video_reclaim_keeps_active_and_newly_resolved_images_and_accounts_cpu_demotions(){
        let _lock=LOCK.lock().unwrap();
        let budget=crate::image_cache_budget::CacheBudget::new(80*1024*1024);
        let mut p=GxmTextureProvider::new().with_cache_budget(budget.clone());
        for name in ["old","active","reused"]{
            p.upload(name,64,64,&vec![255;64*64*4]).unwrap();p.entries.get_mut(name).unwrap().cacheable=true;
        }
        let active=p.entries["active"].id;let reused=p.entries["reused"].id;
        p.retain(&HashSet::from(["active".into()]));
        p.resolve("reused").unwrap(); // A reference after last retain must pin again.
        let ready=budget.lock().unwrap().ready;
        assert_eq!(p.reclaim_video_gpu_cache(1),256*1024);
        assert_eq!(p.entries["active"].id,active);assert_eq!(p.entries["reused"].id,reused);
        assert!(!p.entries.contains_key("old"));assert_eq!(p.decoded["old"].rgba.len(),64*64*4);
        assert_eq!(budget.lock().unwrap().ready,ready);
        assert_eq!(budget.lock().unwrap().idle,64*64*4);
        assert_eq!(p.reclaim_video_gpu_cache(16*1024*1024),0);
        assert!(p.resolve("old").is_some()); // Reuses retained CPU pixels without source I/O.
    }
    #[test] fn upload_pressure_reclaims_only_after_current_scene_pin_and_retries_decoded_pixels(){
        let _lock=LOCK.lock().unwrap();
        let budget=crate::image_cache_budget::CacheBudget::new(192*1024*1024);
        let (source,reads)=provider();
        let mut p=source.with_cache_budget(budget.clone());
        for name in ["cold","used-later-in-frame"]{
            p.upload(name,64,64,&vec![255;64*64*4]).unwrap();
            p.entries.get_mut(name).unwrap().cacheable=true;
        }
        p.retain(&HashSet::new());
        p.begin_scene_build();
        FAIL_UPLOAD.store(1,Ordering::Relaxed);
        let failed=p.resolve("body");FAIL_UPLOAD.store(0,Ordering::Relaxed);
        assert!(failed.is_none());assert!(p.needs_upload_retry());
        assert!(p.entries.contains_key("cold")&&p.entries.contains_key("used-later-in-frame"));
        p.resolve("used-later-in-frame").unwrap();
        let live=HashSet::from(["body".into(),"used-later-in-frame".into()]);
        p.retain(&live);
        assert!(!p.entries.contains_key("cold"));assert!(p.decoded.contains_key("cold"));
        assert!(p.entries.contains_key("used-later-in-frame"));
        assert!(p.needs_upload_retry());assert_eq!(reads.get(),1);
        assert_eq!(budget.lock().unwrap().idle,p.idle_parts().total());
        p.begin_scene_build();let (id,_)=p.resolve("body").unwrap();
        assert_eq!(reads.get(),1);assert!(!p.needs_upload_retry());
        assert_eq!(p.pixel_alpha(id,0,0),Some(128));
    }
    #[test] fn upload_pressure_does_not_evict_active_textures_when_no_idle_space_exists(){
        let _lock=LOCK.lock().unwrap();let (mut p,_)=provider();
        let first=p.resolve("active").unwrap();p.begin_scene_build();
        FAIL_UPLOAD.store(1,Ordering::Relaxed);
        let failed=p.resolve("body");FAIL_UPLOAD.store(0,Ordering::Relaxed);assert!(failed.is_none());
        p.retain(&HashSet::from(["active".into(),"body".into()]));
        assert_eq!(p.resolve("active"),Some(first));assert!(p.needs_upload_retry());
        assert!(p.decoded.contains_key("body"));
        p.begin_scene_build();assert!(p.resolve("body").is_some());assert!(!p.needs_upload_retry());
    }
    static UPLOADS: AtomicUsize = AtomicUsize::new(0);
    #[test] fn prepared_backups_borrow_shared_headroom_and_yield_to_prefetch_without_dropping_active_gpu(){
        let _lock=LOCK.lock().unwrap();
        const M:usize=1024*1024;
        let budget=crate::image_cache_budget::CacheBudget::new(192*M);
        {let mut b=budget.lock().unwrap();b.set_ready(48*M);b.request_ready(true);}
        let mut p=GxmTextureProvider::new().with_cache_budget(budget.clone())
            .with_prefetch(|_|Some(Ok(image::RgbaImage::from_pixel(512,512,image::Rgba([30,40,50,139])))));
        p.shared_surfaces=true;
        let mut active=HashSet::new();
        for n in 0..131{
            let name=format!("p{n}");let (id,_)=p.resolve(&name).unwrap();
            // The ordinary upload mock does not expose mapped surfaces. Supply
            // that host capability explicitly so backup admission is exercised.
            NATIVE.lock().unwrap().get_or_insert_with(HashMap::new).insert(id.0,
                MockSurface{w:512,h:512,stride:512,data:vec![0;M]});
            p.share_static_pixels(&name);active.insert(name);
            p.retain(&active);
            assert_eq!(p.idle_parts().decoded,((n+1).min(128))*M);
            assert_eq!(p.idle_parts().total(),budget.lock().unwrap().idle);
        }
        assert_eq!(p.entries.len(),131);assert_eq!(p.idle_parts().gpu,0);
        assert_eq!(p.idle_parts().decoded,128*M); // Exceeds both former fixed caps.
        let ids=p.ids.clone();
        // Publication cannot spend promised bytes until retain has freed them.
        for ready in [64,80,96,112,120]{
            {let mut b=budget.lock().unwrap();assert!(ready*M<=b.ready_limit());b.set_ready(ready*M);b.request_ready(true);}
            p.retain(&active);
            let b=budget.lock().unwrap();assert!(b.ready+b.idle<=b.limit);
        }
        assert_eq!(p.ids,ids);assert_eq!(p.idle_parts().decoded,56*M);
        assert!(p.entries["p0"].rgba.is_empty());assert!(!p.entries["p127"].rgba.is_empty());
        // Return released prefetch space to CPU retention without increasing GPU cap.
        {let mut b=budget.lock().unwrap();b.set_ready(0);b.request_ready(false);}
        for n in 131..151{
            let name=format!("p{n}");let (id,_)=p.resolve(&name).unwrap();
            NATIVE.lock().unwrap().get_or_insert_with(HashMap::new).insert(id.0,
                MockSurface{w:512,h:512,stride:512,data:vec![0;M]});
            p.share_static_pixels(&name);active.insert(name);p.retain(&active);
        }
        assert_eq!(p.idle_parts().decoded,76*M);assert_eq!(p.idle_parts().gpu,0);
        {let b=budget.lock().unwrap();assert_eq!(p.idle_parts().total(),b.idle);assert!(b.ready+b.idle<=b.limit);}
        p.evict_prefix("p");p.retain(&HashSet::new());assert_eq!(budget.lock().unwrap().idle,0);
    }
    #[test] fn borrowed_cold_pixels_survive_gpu_demotion_and_resolve_without_loading(){
        let _lock=LOCK.lock().unwrap();const M:usize=1024*1024;
        let budget=crate::image_cache_budget::CacheBudget::new(192*M);
        let mut p=GxmTextureProvider::new().with_cache_budget(budget.clone())
            .with_source(|_|panic!("borrowed pixels must not need source IO"))
            .with_prefetch(|_|Some(Ok(image::RgbaImage::from_pixel(512,512,image::Rgba([30,40,50,139])))));
        p.shared_surfaces=true;
        for n in 0..110{
            let name=format!("cold{n}");let (id,_)=p.resolve(&name).unwrap();
            NATIVE.lock().unwrap().get_or_insert_with(HashMap::new).insert(id.0,
                MockSurface{w:512,h:512,stride:512,data:vec![0;M]});
            p.share_static_pixels(&name);p.retain(&HashSet::from([name]));
        }
        let parts=p.idle_parts();assert_eq!(parts.decoded,110*M);
        assert_eq!(parts.gpu,32*M);assert_eq!(parts.total(),142*M);
        assert!(p.decoded.contains_key("cold0"));assert!(!p.entries.contains_key("cold0"));
        p.prefetch=Some(Box::new(|_|panic!("decoded hit must not call prefetch")));
        let id=p.resolve("cold0").unwrap().0;
        assert_eq!(p.decoded_hits,1);assert_eq!((p.timing.reads,p.timing.decoded),(0,0));
        assert_eq!(p.pixel_alpha(id,0,0),Some(139));
        p.retain(&HashSet::from(["cold0".into()]));
        assert!(p.idle_parts().gpu<=32*M);
        assert_eq!(budget.lock().unwrap().idle,p.idle_parts().total());
    }
    #[test] fn prepared_shared_surface_keeps_owned_pixels_without_reread_or_gpu_readback(){
        let _lock=LOCK.lock().unwrap();
        let mut png=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(1023,541,image::Rgba([30,40,50,139]))
            .write_to(&mut png,image::ImageFormat::Png).unwrap();
        let bytes=png.into_inner();let reads=Rc::new(Cell::new(0));let count=reads.clone();
        let image=image::RgbaImage::from_pixel(1023,541,image::Rgba([30,40,50,139]));
        let proof=TileProof::from_pixels(&image).unwrap();
        let ready=std::cell::RefCell::new(Some((Ok(image.into()),Some(proof),Some(Tracked::bytes(bytes.clone(),Owner::Ready)))));
        let budget=crate::image_cache_budget::CacheBudget::new(192*1024*1024);
        let mut p=GxmTextureProvider::new().with_cache_budget(budget.clone())
            .with_source(move |_|{count.set(count.get()+1);Some(bytes.clone())})
            .with_tracked_prefetch(move|name|if name=="bg0"{ready.borrow_mut().take()}else{None});p.shared_surfaces=true;
        for n in 0..18{p.resolve(&format!("bg{n}")).unwrap();}
        let active=p.entries["bg17"].id;
        p.retain(&HashSet::from(["bg17".into()]));
        assert_eq!(p.entries["bg17"].id,active);
        assert!(p.decoded.contains_key("bg0"));assert!(!p.entries.contains_key("bg0"));
        let parts=p.idle_parts();assert!(parts.gpu<=IDLE_GPU_BUDGET);
        assert!(parts.total()<=IDLE_TEXTURE_BUDGET);assert_eq!(parts.total(),budget.lock().unwrap().idle);
        let proofs=PROOF_UPLOADS.load(Ordering::Relaxed);
        let (id,_)=p.resolve("bg0").unwrap();assert_eq!(reads.get(),17);assert_eq!(p.decoded_hits,1);
        assert_eq!(PROOF_UPLOADS.load(Ordering::Relaxed),proofs+1);
        assert_eq!(p.pixel_alpha(id,1022,540),Some(139));
        // New chapter reservation still wins over CPU spill and never touches active.
        {let mut b=budget.lock().unwrap();b.request_ready(true);}
        p.retain(&HashSet::from(["bg17".into(),"bg0".into()]));
        let b=budget.lock().unwrap();assert!(b.idle<=b.idle_limit(IDLE_TEXTURE_BUDGET));
        assert_eq!(b.idle,p.idle_parts().total());assert!(p.entries.contains_key("bg17"));
    }
    #[test] fn video_reclaim_drops_shared_gpu_only_keeps_backup_and_is_bounded(){
        let _lock=LOCK.lock().unwrap();
        let mut png=Cursor::new(Vec::new());image::RgbaImage::new(512,512).write_to(&mut png,image::ImageFormat::Png).unwrap();let bytes=png.into_inner();
        let reads=Rc::new(Cell::new(0));let count=reads.clone();
        let mut p=GxmTextureProvider::new().with_source(move |_|{count.set(count.get()+1);Some(bytes.clone())});p.shared_surfaces=true;
        for n in ["first","second","active"]{p.resolve(n).unwrap();}
        p.retain(&HashSet::from(["active".into()]));
        assert_eq!(p.reclaim_video_gpu_cache(0),0);assert_eq!(p.entries.len(),3);
        let active=p.entries["active"].id;
        assert_eq!(p.reclaim_video_gpu_cache(1),1024*1024);
        assert!(!p.entries.contains_key("first"));assert!(p.entries.contains_key("second"));
        assert_eq!(p.entries["active"].id,active);assert!(p.decoded.is_empty());
        assert!(p.encoded.contains_key("first"));assert_eq!(reads.get(),3);
        let proof_bytes=p.encoded["first"].bytes.len();assert!(proof_bytes>0);
        p.resolve("first").unwrap();assert_eq!(reads.get(),3);assert!(p.entries["first"].shared);
        assert_eq!(p.reclaim_video_gpu_cache(usize::MAX),1024*1024); // Only second is still reclaimable.
        assert!(p.entries.contains_key("first")&&p.entries.contains_key("active"));
    }
    static PROOF_UPLOADS: AtomicUsize = AtomicUsize::new(0);
    static FAIL_PROOF: AtomicUsize = AtomicUsize::new(0);
    static FAIL_UPLOAD: AtomicUsize = AtomicUsize::new(0);
    struct MockSurface { w:usize,h:usize,stride:usize,data:Vec<u8> }
    static NATIVE:Mutex<Option<HashMap<u64,MockSurface>>>=Mutex::new(None);
    static FAIL_SURFACE:AtomicUsize=AtomicUsize::new(0);
    static ABORTED:AtomicUsize=AtomicUsize::new(0);
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_surface_prepare(w:u32,h:u32,p:*mut *mut u8,n:*mut usize)->usize{
        if FAIL_SURFACE.load(Ordering::Relaxed)==1{return 0;}
        let stride=(w as usize+7)&!7;let mut s=Box::new(MockSurface{w:w as usize,h:h as usize,stride,data:vec![0;stride*h as usize*4]});
        unsafe{*p=s.data.as_mut_ptr();*n=s.data.len();}Box::into_raw(s) as usize
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_surface_abort(p:usize){ABORTED.fetch_add(1,Ordering::Relaxed);drop(unsafe{Box::from_raw(p as *mut MockSurface)});}
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_surface_publish(p:usize,id:u64,_:*const u8,_:usize)->i32{
        if FAIL_SURFACE.load(Ordering::Relaxed)==2{return 0;}
        let mut s=unsafe{Box::from_raw(p as *mut MockSurface)};
        for y in (0..s.h).rev(){let w=s.w;let stride=s.stride;s.data.copy_within(y*w*4..(y+1)*w*4,y*stride*4);}
        NATIVE.lock().unwrap().get_or_insert_with(HashMap::new).insert(id,*s);1
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_surface_view(id:u64,stride:*mut usize)->*const u8{
        let lock=NATIVE.lock().unwrap();let Some(s)=lock.as_ref().and_then(|m|m.get(&id))else{return std::ptr::null();};
        unsafe{*stride=s.stride;}s.data.as_ptr()
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_texture_proof(_:u64,w:u32,h:u32,rgba:*const u8,length:usize,cells:*const u8,count:usize)->i32{
        PROOF_UPLOADS.fetch_add(1,Ordering::Relaxed);
        if FAIL_PROOF.swap(0,Ordering::Relaxed)!=0{return 0;}
        let pixels=unsafe{std::slice::from_raw_parts(rgba,length)};
        let certificate=unsafe{std::slice::from_raw_parts(cells,count)};
        let columns=(w as usize+63)/64;
        if length!=w as usize*h as usize*4||count!=32+columns*((h as usize+63)/64){return 0;}
        let words:Vec<_>=certificate[..32].chunks_exact(4).map(|b|u32::from_le_bytes(b.try_into().unwrap())).collect();
        let (mut left,mut top,mut right,mut bottom)=(w,h,0,0);
        for y in 0..h{for x in 0..w{if pixels[(y as usize*w as usize+x as usize)*4+3]!=0{
            left=left.min(x);top=top.min(y);right=right.max(x+1);bottom=bottom.max(y+1);
        }}}
        let opaque=pixels.chunks_exact(4).all(|p|p[3]==255) as u32;
        if words!=[0x31504641,w,h,left,top,right,bottom,opaque]{return 0;}
        let flags=&certificate[32..];
        for (i,&flag) in flags.iter().enumerate(){
            let x0=(i%columns)*64;let y0=(i/columns)*64;
            let mut expected=true;
            for y in y0..(y0+64).min(h as usize){for x in x0..(x0+64).min(w as usize){expected&=pixels[(y*w as usize+x)*4+3]==255;}}
            if flag!=expected as u8{return 0;}
        }
        UPLOADS.fetch_add(1,Ordering::Relaxed);1
    }
    #[cfg(feature = "gxm-builtin-effects")]
    static CAPTURE_READY: AtomicUsize = AtomicUsize::new(0);
    #[cfg(feature = "gxm-builtin-effects")]
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_capture_previous_texture(_: u64, _: u32, _: u32) -> i32 {
        CAPTURE_READY.load(Ordering::Relaxed) as i32
    }
    static ALPHA_UNSUPPORTED:AtomicUsize=AtomicUsize::new(0);
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_alpha_region(_:u64,w:u32,h:u32,_:*const u8,n:usize,_:u32,_:u32,_:u32,_:u32)->i32{
        if ALPHA_UNSUPPORTED.load(Ordering::Relaxed)!=0{return -1;}
        assert_eq!(n,w as usize*h as usize);if FAIL_UPLOAD.load(Ordering::Relaxed)!=0{0}else{1}
    }
    static LUMA_UNSUPPORTED:AtomicUsize=AtomicUsize::new(0);
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_luma(_:u64,w:u32,h:u32,_:*const u8,n:usize)->i32{
        if LUMA_UNSUPPORTED.load(Ordering::Relaxed)!=0{return -1;}
        if FAIL_UPLOAD.load(Ordering::Relaxed)!=0{return 0;}
        assert_eq!(n,w as usize*h as usize);UPLOADS.fetch_add(1,Ordering::Relaxed);1
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_texture(_: u64, _: u32, _: u32, _: *const u8, _: usize) -> i32 {
        if FAIL_UPLOAD.load(Ordering::Relaxed)!=0{return 0;}
        UPLOADS.fetch_add(1, Ordering::Relaxed); 1
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_video_texture(_: u64, _: u32, _: u32, _: *const u8, _: usize) -> i32 { 1 }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_delete_texture(id:u64) {if let Some(m)=NATIVE.lock().unwrap().as_mut(){m.remove(&id);}}
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_update_texture_region(_: u64, _: u32, _: u32, _: *const u8, _: usize, _: u32, _: u32, _: u32, _: u32) -> i32 { 1 }

    #[test]
    fn shared_decode_uses_one_surface_and_preserves_odd_width_reads_and_eviction(){
        let _guard=LOCK.lock().unwrap();
        let mut image=image::RgbaImage::new(513,512);
        for (x,y,p) in image.enumerate_pixels_mut(){*p=image::Rgba([x as u8,y as u8,17,if x%7==0{0}else{128}]);}
        let mut png=Cursor::new(Vec::new());image.write_to(&mut png,image::ImageFormat::Png).unwrap();let bytes=png.into_inner();
        let reads=Rc::new(Cell::new(0));let count=reads.clone();
        let mut p=GxmTextureProvider::new().with_source(move |_|{count.set(count.get()+1);Some(bytes.clone())});p.shared_surfaces=true;
        let (id,info)=p.resolve("shared").unwrap();assert!(p.entries["shared"].shared);assert_eq!(p.entries["shared"].rgba.capacity(),0);
        assert_eq!(p.pixel_alpha(id,512,511),Some(image.get_pixel(512,511)[3]));
        assert_eq!(p.pixels_of("shared").unwrap().2,*image.as_raw());assert_eq!(reads.get(),1);
        assert_eq!(info.width,513);p.retain(&HashSet::new());assert_eq!(p.resolve("shared").unwrap().0,id);
        p.idle_budget=64*1024;p.retain(&HashSet::new());assert!(p.entries.is_empty()&&p.decoded.is_empty());
        p.resolve("shared").unwrap();assert_eq!(reads.get(),1);assert_eq!(p.encoded_hits,1);
        p.evict_prefix("shared");assert!(NATIVE.lock().unwrap().as_ref().unwrap().is_empty());
    }
    #[test]
    fn shared_allocation_falls_back_and_failed_publish_releases_private_storage(){
        let _guard=LOCK.lock().unwrap();
        let mut out=Cursor::new(Vec::new());image::RgbaImage::new(512,512).write_to(&mut out,image::ImageFormat::Png).unwrap();let bytes=out.into_inner();
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.clone()));p.shared_surfaces=true;
        FAIL_SURFACE.store(1,Ordering::Relaxed);assert!(p.resolve("fallback").is_some());assert!(!p.entries["fallback"].shared);
        let before=ABORTED.load(Ordering::Relaxed);FAIL_SURFACE.store(2,Ordering::Relaxed);
        assert!(p.resolve("failed").is_none());FAIL_SURFACE.store(0,Ordering::Relaxed);
        assert_eq!(ABORTED.load(Ordering::Relaxed),before+1);assert!(!p.entries.contains_key("failed"));
        assert!(p.resolve("failed").is_some());
    }
    #[test]
    fn incomplete_decode_drops_private_surface_without_publishing(){
        let _guard=LOCK.lock().unwrap();
        let mut png=Cursor::new(Vec::new());image::RgbaImage::new(512,512).write_to(&mut png,image::ImageFormat::Png).unwrap();
        let mut bytes=png.into_inner();bytes.truncate(bytes.len()/2);
        let before=ABORTED.load(Ordering::Relaxed);
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.clone()));p.shared_surfaces=true;
        assert!(p.resolve("broken").is_none());assert!(p.entries.is_empty());
        assert_eq!(ABORTED.load(Ordering::Relaxed),before+1);
    }

    #[test]
    fn owned_upload_preserves_allocation_pixels_and_identity_when_replaced() {
        let _guard = LOCK.lock().unwrap();
        let mut p = GxmTextureProvider::new();
        let pixels = vec![255u8; 64];
        let pointer = pixels.as_ptr();
        let first = p.upload_storage("owned", 4, 4, Cow::Owned(pixels), false, true).unwrap();
        assert_eq!(p.entries["owned"].rgba.as_ptr(), pointer);
        assert!(p.texture_is_opaque(first.0));
        let mut replacement = vec![129u8; 64];
        replacement[3] = 0;
        let pointer = replacement.as_ptr();
        assert_eq!(p.upload_storage("owned", 4, 4, Cow::Owned(replacement), false, true), Some(first));
        assert_eq!(p.entries["owned"].rgba.as_ptr(), pointer);
        assert_eq!(p.pixel_alpha(first.0, 0, 0), Some(0));
        assert_eq!(p.pixel_alpha(first.0, 1, 0), Some(129));
        assert!(!p.texture_is_opaque(first.0));
        assert!(p.upload_storage("owned", 4, 4, Cow::Owned(vec![0; 3]), false, true).is_none());
        assert_eq!(p.entries["owned"].rgba.as_ptr(), pointer);
    }

    #[test]
    fn prepared_proof_follows_pixels_and_encoded_source_but_not_replacements(){
        let _guard=LOCK.lock().unwrap();
        for encoded in [false,true]{
            let mut image=image::RgbaImage::from_pixel(961,541,image::Rgba([23,53,82,255]));
            image.put_pixel(960,540,image::Rgba([0,255,0,128]));
            let proof=TileProof::from_pixels(&image).unwrap();
            let data=if encoded{
                let mut bytes=Cursor::new(Vec::new());image.write_to(&mut bytes,image::ImageFormat::Png).unwrap();
                Err(Tracked::bytes(bytes.into_inner(),Owner::Ready))
            }else{Ok(image.into())};
            let ready=std::cell::RefCell::new(Some((data,Some(proof),None)));
            let before=PROOF_UPLOADS.load(Ordering::Relaxed);
            let mut p=GxmTextureProvider::new().with_tracked_prefetch(move |_|ready.borrow_mut().take()).with_source(|_|panic!("prepared source must not be reread"));
            let (id,_)=p.resolve("bg").unwrap();assert_eq!(p.pixel_alpha(id,960,540),Some(128));
            assert_eq!(PROOF_UPLOADS.load(Ordering::Relaxed),before+1);
            p.upload_rgba("bg",961,541,&vec![0;961*541*4]).unwrap();
            assert_eq!(p.pixel_alpha(id,0,0),Some(0));assert_eq!(PROOF_UPLOADS.load(Ordering::Relaxed),before+1);
        }
    }
    #[test]
    fn failed_prepared_upload_keeps_pixels_without_rereading_source(){
        let _guard=LOCK.lock().unwrap();
        let image=image::RgbaImage::from_pixel(960,540,image::Rgba([1,2,3,255]));
        let proof=TileProof::from_pixels(&image).unwrap();
        let ready=std::cell::RefCell::new(Some((Ok(image.into()),Some(proof),None)));
        let mut p=GxmTextureProvider::new().with_tracked_prefetch(move |_|ready.borrow_mut().take())
            .with_source(|_|panic!("GPU pressure must not cause a source read"));
        let before=PROOF_UPLOADS.load(Ordering::Relaxed);FAIL_PROOF.store(1,Ordering::Relaxed);
        assert!(p.resolve("changed").is_none());
        assert_eq!(p.decoded["changed"].rgba.len(),960*540*4);
        let (id,_)=p.resolve("changed").unwrap();
        assert_eq!(p.pixel_alpha(id,0,0),Some(255));assert_eq!(PROOF_UPLOADS.load(Ordering::Relaxed),before+1);
    }

    #[test]
    fn failed_cold_upload_retries_decode_once_and_stays_within_idle_budget(){
        let _guard=LOCK.lock().unwrap();
        let mut bytes=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(512,512,image::Rgba([4,5,6,123]))
            .write_to(&mut bytes,image::ImageFormat::Png).unwrap();
        let bytes=bytes.into_inner();let reads=Rc::new(Cell::new(0));let count=reads.clone();
        let mut p=GxmTextureProvider::new().with_source(move |_|{count.set(count.get()+1);Some(bytes.clone())});
        p.idle_budget=1024*1024;
        FAIL_UPLOAD.store(1,Ordering::Relaxed);
        assert!(p.resolve("cold").is_none());
        p.retain(&HashSet::from(["cold".into()]));
        assert!(p.resolve("cold").is_none());
        FAIL_UPLOAD.store(0,Ordering::Relaxed);
        assert_eq!(reads.get(),1);assert_eq!(p.timing.decoded,1);
        assert_eq!(p.decoded["cold"].rgba.len(),1024*1024);
        let (id,_)=p.resolve("cold").unwrap();assert_eq!(p.pixel_alpha(id,0,0),Some(123));
        assert!(p.decoded.is_empty());assert_eq!(reads.get(),1);
        FAIL_UPLOAD.store(1,Ordering::Relaxed);
        for n in ["second","third"]{assert!(p.resolve(n).is_none());p.retain(&HashSet::new());}
        FAIL_UPLOAD.store(0,Ordering::Relaxed);
        assert!(p.idle_parts().total()<=p.idle_budget);
    }

    #[test]
    fn region_updates_reuse_identity_charge_changed_bytes_and_reject_invalid_bounds() {
        let mut p = GxmTextureProvider::new();
        let data = [27; 16 * 16 * 4];
        let first = p.upload_rgba_render_only_region("atlas", 16, 16, &data, [2, 3, 4, 5]).unwrap();
        assert_eq!(p.timing.upload_bytes, 1024);
        let rev = p.revision;
        assert_eq!(p.upload_rgba_render_only_region("atlas", 16, 16, &data, [2, 3, 4, 5]), Some(first));
        assert_eq!(p.timing.upload_bytes, 1024 + 80);
        assert_ne!(p.revision, rev);
        assert!(p.entries["atlas"].rgba.is_empty());
        assert!(p.upload_rgba_render_only_region("atlas", 16, 16, &data, [15, 0, 2, 1]).is_none());
        assert_eq!(p.timing.upload_bytes, 1104);
    }

    fn provider() -> (GxmTextureProvider, Rc<Cell<usize>>) {
        let mut png = Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(4,4,image::Rgba([255,255,255,128]))
            .write_to(&mut png,image::ImageFormat::Png).unwrap();
        let reads = Rc::new(Cell::new(0));
        let counter = reads.clone();
        (GxmTextureProvider::new().with_source(move |_| {
            counter.set(counter.get()+1); Some(png.get_ref().clone())
        }), reads)
    }

    #[test] fn split_idle_account_excludes_active_and_counts_backups_separately(){
        let _guard=LOCK.lock().unwrap();
        let budget=crate::image_cache_budget::CacheBudget::new(1024*1024);
        let (p,_)=provider();let mut p=p.with_cache_budget(budget.clone());
        for name in ["cold","active"]{p.resolve(name).unwrap();}
        p.decoded.insert("cpu-only".into(),DecodedEntry { gray:false, info:TextureInfo{width:2,height:2},rgba:vec![0;16].into(),last_used:0});
        let proof=TileProof::from_pixels(&image::RgbaImage::new(960,540)).unwrap();let proof_bytes=proof.bytes();
        let mut source=Vec::with_capacity(1024);source.push(1);let source_bytes=source.capacity();
        p.encoded.insert("backup".into(),EncodedEntry{bytes:source.into(),proof:Some(proof),last_used:0});
        p.retain(&HashSet::from(["active".into()]));
        let parts=p.idle_parts();
        assert_eq!(parts.decoded,p.entries["cold"].rgba.capacity()+16);
        assert_eq!(parts.gpu,8*4*4);
        assert_eq!((parts.encoded,parts.proof),(source_bytes,proof_bytes));
        assert_eq!(parts.total(),budget.lock().unwrap().idle);
        p.entries.get_mut("cold").unwrap().rgba=Vec::new().into();
        p.entries.get_mut("cold").unwrap().shared=true;
        p.retain(&HashSet::from(["active".into()]));
        assert_eq!(p.idle_parts().decoded,16);assert_eq!(p.idle_parts().gpu,128);
        assert_eq!(p.idle_parts().total(),budget.lock().unwrap().idle);
    }
    #[test] fn idle_textures_yield_to_ready_pixels_but_keep_the_active_scene(){
        let _guard=LOCK.lock().unwrap();
        let budget=crate::image_cache_budget::CacheBudget::new(512);
        budget.lock().unwrap().set_ready(384);
        let (p,_)=provider();let mut p=p.with_cache_budget(budget.clone());
        for name in ["cold-a","cold-b","active"]{p.resolve(name).unwrap();}
        p.retain(&HashSet::from(["active".into()]));
        assert!(p.entries.contains_key("active"));
        {let b=budget.lock().unwrap();assert!(b.idle<=128);assert!(b.ready+b.idle<=512);}
        budget.lock().unwrap().set_ready(0);
        p.resolve("recent").unwrap();p.retain(&HashSet::from(["active".into()]));
        assert!(p.entries.contains_key("recent"));assert!(budget.lock().unwrap().idle>128);
        drop(p);assert_eq!(budget.lock().unwrap().idle,0);
    }
    #[test] fn async_headroom_request_reclaims_cold_textures_before_ready_publication(){
        let _guard=LOCK.lock().unwrap();let budget=crate::image_cache_budget::CacheBudget::new(480);
        let (p,_)=provider();let mut p=p.with_cache_budget(budget.clone());
        for name in ["cold-a","cold-b","active"]{p.resolve(name).unwrap();}
        let active=HashSet::from(["active".into()]);p.retain(&active);
        assert_eq!(budget.lock().unwrap().idle,384);
        budget.lock().unwrap().request_ready(true);p.retain(&active);
        {let b=budget.lock().unwrap();assert_eq!(b.ready,0);assert!(b.idle<=80);assert!(b.ready_limit()>=400);}
        assert!(p.entries.contains_key("active"));
        budget.lock().unwrap().request_ready(false);
        p.resolve("recent").unwrap();p.retain(&active);
        assert!(p.entries.contains_key("recent"));
    }

    #[cfg(feature = "gxm-builtin-effects")]
    #[test]
    fn completed_capture_waits_without_allocating_and_reuses_then_releases_identity() {
        let _guard = LOCK.lock().unwrap();
        let mut p = GxmTextureProvider::new();
        let next = p.next_id;
        let revision = p.revision;
        CAPTURE_READY.store(0, Ordering::Relaxed);
        assert!(p.capture_completed_frame("snapshot", 960, 540).is_none());
        assert_eq!(p.next_id, next);
        assert_eq!(p.revision, revision);
        assert!(p.entries.is_empty());
        CAPTURE_READY.store(1, Ordering::Relaxed);
        assert!(p.capture_completed_frame("snapshot", 0, 540).is_none());
        let first = p.capture_completed_frame("snapshot", 960, 540).unwrap();
        assert_eq!(first.0, TextureId(next));
        assert_eq!(p.entries["snapshot"].rgba.capacity(), 0);
        assert_eq!(p.timing.upload_bytes, 0);
        assert!(!p.texture_is_opaque(first.0));
        let revision = p.revision;
        assert_eq!(p.capture_completed_frame("snapshot", 960, 540), Some(first));
        assert_eq!(p.next_id, next + 1);
        assert!(p.revision > revision);
        p.retain(&HashSet::from(["snapshot".to_owned()]));
        assert!(p.entries.contains_key("snapshot"));
        p.retain(&HashSet::new());
        assert!(p.entries.is_empty());
        assert!(p.ids.is_empty());
        CAPTURE_READY.store(0, Ordering::Relaxed);
    }

    #[test]
    fn render_only_upload_releases_cpu_copy_but_preserves_gpu_identity_and_readable_uploads() {
        let _guard = LOCK.lock().unwrap();
        let mut p = GxmTextureProvider::new();
        let pixels = [255u8; 64];
        let (id, info) = p.upload_rgba("generated", 4, 4, &pixels).unwrap();
        assert_eq!(p.pixel_alpha(id, 0, 0), Some(255));
        assert!(p.texture_is_opaque(id));
        let revision = p.content_revision();
        assert_eq!(p.upload_rgba_render_only("generated", 4, 4, &pixels), Some((id, info)));
        assert_eq!(p.entries["generated"].rgba.capacity(), 0);
        assert_eq!(p.pixel_alpha(id, 0, 0), None);
        assert!(!p.texture_is_opaque(id));
        assert!(p.content_revision() > revision);
        assert_eq!(p.profile_memory(), (1, 0, 128)); // GPU rows aligned to eight pixels.
        p.retain(&HashSet::from(["generated".to_owned()]));
        assert_eq!(p.resolve("generated"), Some((id, info)));
        assert_eq!(p.upload_rgba("generated", 4, 4, &pixels), Some((id, info)));
        assert_eq!(p.pixel_alpha(id, 0, 0), Some(255));
        assert_eq!(p.profile_memory(), (1, 64, 128));
    }

    #[test]
    fn timing_distinguishes_missing_decode_failure_and_success_without_suppressing_retry() {
        let _guard = LOCK.lock().unwrap();
        let (mut p, reads) = provider();
        p.resolve("valid").unwrap();
        p.resolve("valid").unwrap();
        assert_eq!(reads.get(), 1);
        assert_eq!((p.timing.reads, p.timing.decoded, p.timing.uploads), (1, 1, 1));
        assert_eq!(p.timing.upload_bytes, 64);
        p.source = Some(Box::new(|_| None));
        assert!(p.resolve("later").is_none());
        assert!(p.resolve("later").is_none());
        assert_eq!(p.timing.missing, 2);
        p.source = Some(Box::new(|_| Some(vec![0, 1, 2])));
        assert!(p.resolve("later").is_none());
        assert_eq!(p.timing.decode_errors, 1);
        assert_eq!(p.timing.reads, 4);
        assert_eq!(p.timing.decoded, 1);
    }

    fn large_png()->Vec<u8>{
        let mut out=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(512,512,image::Rgba([7,8,9,128])).write_to(&mut out,image::ImageFormat::Png).unwrap();
        out.into_inner()
    }
    #[test]
    fn encoded_tier_survives_pixel_eviction_without_copy_or_second_source_read(){
        let _guard=LOCK.lock().unwrap();
        let source=large_png();let pointer=source.as_ptr();
        let input=std::cell::RefCell::new(Some(source));
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(input.borrow_mut().take().expect("second source read")));
        let first=p.resolve("large").unwrap();
        assert_eq!(p.encoded["large"].bytes.as_ptr(),pointer);
        p.idle_budget=64*1024;p.retain(&HashSet::new());
        assert!(p.entries.is_empty()&&p.decoded.is_empty());
        assert!(p.profile_memory().1<=p.idle_budget as u64);
        let second=p.resolve("large").unwrap();assert_ne!(first.0,second.0);
        assert_eq!(p.encoded_hits,1);assert_eq!(p.pixel_alpha(second.0,0,0),Some(128));
        assert_eq!(p.encoded["large"].bytes.as_ptr(),pointer);
        p.idle_budget=0;p.retain(&HashSet::new());
        assert!(p.entries.is_empty()&&p.decoded.is_empty()&&p.encoded.is_empty());
        assert_eq!(p.profile_memory(),(0,0,0));
    }
    #[test]
    fn prepared_pixels_deliver_source_for_reuse_and_explicit_replace_invalidates_it(){
        let _guard=LOCK.lock().unwrap();
        let bytes=large_png();let pointer=bytes.as_ptr();
        let input=std::cell::RefCell::new(Some((Ok(image::RgbaImage::from_pixel(512,512,image::Rgba([7,8,9,128])).into()),None,Some(bytes.into()))));
        let mut p=GxmTextureProvider::new().with_tracked_prefetch(move |_|input.borrow_mut().take()).with_source(|_|panic!("unexpected source IO"));
        p.resolve("image/large").unwrap();assert_eq!(p.timing.decoded,0);
        assert_eq!(p.encoded["image/large"].bytes.as_ptr(),pointer);
        p.idle_budget=64*1024;p.retain(&HashSet::new());p.resolve("image/large").unwrap();
        assert_eq!(p.encoded_hits,1);
        p.upload_rgba("image/large",1,1,&[1,2,3,255]).unwrap();assert!(!p.encoded.contains_key("image/large"));
        p.evict_prefix("image/");assert!(p.entries.is_empty()&&p.decoded.is_empty()&&p.encoded.is_empty());
    }
    #[test]
    fn fresh_prefetched_pixels_take_priority_over_retained_encoded_source(){
        let _guard=LOCK.lock().unwrap();
        let input=std::cell::RefCell::new(Some(large_png()));
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(input.borrow_mut().take().expect("second source read")));
        p.resolve("large").unwrap();
        p.idle_budget=64*1024;p.retain(&HashSet::new());
        assert!(p.entries.is_empty()&&p.decoded.is_empty()&&p.encoded.contains_key("large"));
        let fresh=image::RgbaImage::from_pixel(512,512,image::Rgba([21,22,23,64]));
        let mut out=Cursor::new(Vec::new());fresh.write_to(&mut out,image::ImageFormat::Png).unwrap();
        let bytes=out.into_inner();let pointer=bytes.as_ptr();
        let delivery=std::cell::RefCell::new(Some((Ok(fresh.into()),None,Some(bytes.into()))));
        p.prefetch=Some(Box::new(move |_|delivery.borrow_mut().take()));
        let id=p.resolve("large").unwrap().0;
        assert_eq!(p.pixel_alpha(id,0,0),Some(64));
        assert_eq!((p.timing.decoded,p.timing.reads,p.encoded_hits),(1,1,0));
        assert_eq!(p.encoded["large"].bytes.as_ptr(),pointer);
        p.retain(&HashSet::new());let id=p.resolve("large").unwrap().0;
        assert_eq!(p.pixel_alpha(id,0,0),Some(64));assert_eq!(p.encoded_hits,1);
    }
    #[test]
    fn encoded_entries_share_idle_budget_even_while_their_images_are_active(){
        let _guard=LOCK.lock().unwrap();let bytes=large_png();
        let mut p=GxmTextureProvider::new().with_source(move |_|Some(bytes.clone()));
        p.idle_budget=64*1024;
        for i in 0..20{
            let name=format!("large/{i}");p.resolve(&name).unwrap();
            let active=HashSet::from([name]);p.retain(&active);
            let encoded=p.encoded.values().map(EncodedEntry::capacity).sum::<usize>();
            let idle=p.entries.iter().filter(|(n,_)|!active.contains(*n)).map(|(_,e)|e.cache_bytes()).sum::<usize>()
                +p.decoded.values().map(|e|e.rgba.capacity()).sum::<usize>()+encoded;
            assert!(encoded<=p.idle_budget/4);assert!(idle<=p.idle_budget);
        }
        p.evict_prefix("large/");assert!(p.encoded.is_empty());
    }
    #[test]
    fn large_background_round_trip_avoids_repeated_io_under_the_original_budget(){
        let _guard=LOCK.lock().unwrap();
        let mut out=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(1920,1080,image::Rgba([7,8,9,128])).write_to(&mut out,image::ImageFormat::Png).unwrap();
        let mut bytes=out.into_inner();bytes.resize(803091,0); // source footprint from the hardware sample
        let reads=Rc::new(Cell::new(0));let counter=reads.clone();
        let mut p=GxmTextureProvider::new().with_source(move |_|{counter.set(counter.get()+1);Some(bytes.clone())});
        p.idle_budget=16*1024*1024;
        for i in 0..9{let name=format!("bg/{}",i%3);p.resolve(&name).unwrap();p.retain(&HashSet::from([name]));}
        assert_eq!(reads.get(),3);assert!(p.encoded_hits>0);
        assert!(p.encoded.values().map(EncodedEntry::capacity).sum::<usize>()<=4*1024*1024);
    }

    #[test]
    fn pending_prefetch_keeps_decoded_backgrounds_when_shared_budget_has_headroom(){
        let _guard=LOCK.lock().unwrap();
        const M:usize=1024*1024;
        let budget=crate::image_cache_budget::CacheBudget::new(192*M);
        let mut p=GxmTextureProvider::new().with_cache_budget(budget.clone());
        p.idle_budget=64*M;
        for (i,name) in ["old-background","recent-background"].iter().enumerate(){
            p.decoded.insert((*name).into(),DecodedEntry { gray:false, info:TextureInfo{width:2560,height:2048},
                rgba:Tracked::bytes(vec![255;20*M],Owner::Provider),last_used:i as u64});
        }
        {let mut b=budget.lock().unwrap();b.set_ready(48*M);b.set_idle(40*M);b.request_ready(true);}
        p.retain(&HashSet::new());
        assert!(p.decoded.contains_key("old-background"));assert_eq!(p.decoded.len(),2);
        assert_eq!(budget.lock().unwrap().idle,40*M);
        // Real prefetch pressure still evicts in LRU order and never overcommits.
        {let mut b=budget.lock().unwrap();b.set_ready(144*M);b.request_ready(true);}
        p.retain(&HashSet::new());
        assert!(!p.decoded.contains_key("old-background"));assert!(p.decoded.contains_key("recent-background"));
        let b=budget.lock().unwrap();assert!(b.ready+b.idle<=b.limit);
    }
    #[test]
    fn larger_idle_budget_preserves_decoded_background_under_surface_pressure(){
        let _guard=LOCK.lock().unwrap();
        let mut out=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(1920,1080,image::Rgba([7,8,9,128])).write_to(&mut out,image::ImageFormat::Png).unwrap();
        let source=out.into_inner();
        for (width,height,needed_mib) in [(1280,1024,24),(1600,1600,32)]{
          for budget_mib in [16,24,32]{
            let source=source.clone();
            let mut p=GxmTextureProvider::new().with_source(move |_|Some(source.clone()));
            p.idle_budget=budget_mib*1024*1024;
            p.resolve("background").unwrap();
            p.upload_rgba("newer_surface",width,height,&vec![255;(width*height*4) as usize]).unwrap();
            p.entries.get_mut("newer_surface").unwrap().cacheable=true;
            p.retain(&HashSet::new());
            let kept=p.entries.contains_key("background")||p.decoded.contains_key("background");
            assert_eq!(kept,budget_mib>=needed_mib);
            let id=p.resolve("background").unwrap().0;
            assert_eq!(p.pixel_alpha(id,0,0),Some(128));
            assert_eq!(p.timing.decoded,if kept{1}else{2});
            assert_eq!(p.encoded_hits,if kept{0}else{1});
          }
        }
    }

    #[test]
    fn prefetched_pixels_upload_on_resolve_without_source_read_or_second_decode() {
        let _guard = LOCK.lock().unwrap();
        let calls = Rc::new(Cell::new(0)); let c = calls.clone();
        let mut p = GxmTextureProvider::new().with_source(|_| panic!("unexpected IO"))
            .with_prefetch(move |_| { c.set(c.get()+1); Some(Ok(image::RgbaImage::from_pixel(4,4,image::Rgba([7,8,9,128])))) });
        let first = p.resolve("prefetched").unwrap();
        assert_eq!(p.pixel_alpha(first.0,0,0),Some(128));
        assert_eq!(p.resolve("prefetched"),Some(first));
        assert_eq!(calls.get(),1); assert_eq!(p.timing.decoded,0); assert_eq!(p.timing.reads,0);
        p.idle_budget=64; p.retain(&HashSet::new()); p.resolve("prefetched").unwrap();
        assert_eq!(calls.get(),1); assert_eq!(p.decoded_hits,1);
    }

    #[test]
    fn encoded_prefetch_uses_normal_decode_and_missing_prefetch_uses_source() {
        let _guard = LOCK.lock().unwrap();
        let (mut p, reads) = provider();
        p.prefetch=Some(Box::new(|_| None)); p.resolve("fallback").unwrap();
        assert_eq!(reads.get(),1);
        let mut png=Cursor::new(Vec::new());
        image::RgbaImage::from_pixel(4,4,image::Rgba([1,2,3,254])).write_to(&mut png,image::ImageFormat::Png).unwrap();
        let bytes=png.into_inner();p.prefetch=Some(Box::new(move |_| Some((Err(bytes.clone().into()),None,None))));
        let id=p.resolve("encoded").unwrap().0;
        assert_eq!(reads.get(),1);assert_eq!(p.pixel_alpha(id,0,0),Some(254));
    }

    #[test]
    fn three_frame_animation_uploads_once_per_frame_asset() {
        let _guard = LOCK.lock().unwrap();
        let (mut baseline, baseline_reads) = provider();
        baseline.idle_budget = 0; // Previous retain behavior: evict every inactive asset.
        for i in 0..300 {
            let name = format!("animation/frame{}", i % 3);
            baseline.resolve(&name).unwrap();
            baseline.retain(&HashSet::from([name]));
        }
        assert_eq!(baseline_reads.get(), 300);
        let uploads = UPLOADS.load(Ordering::Relaxed);
        let (mut p, reads) = provider();
        let mut ids = HashMap::new();
        for i in 0..300 {
            let name = format!("animation/frame{}",i%3);
            let (id,_) = p.resolve(&name).unwrap();
            if let Some(old) = ids.insert(name.clone(),id) { assert_eq!(id,old); }
            p.retain(&HashSet::from([name]));
        }
        assert_eq!(reads.get(),3);
        assert_eq!(UPLOADS.load(Ordering::Relaxed)-uploads,3);
        assert_eq!(p.entries.len(),3);
        assert_eq!(p.cache_evictions,0);
    }

    #[test]
    fn old_decoded_scene_does_not_demote_hot_animation_frames(){
        let _guard=LOCK.lock().unwrap();let (mut p,reads)=provider();
        p.idle_budget=448;
        // A previously demoted scene consumes most of the idle budget. Each
        // animation frame costs 192 bytes (64 CPU + 128 aligned GPU); two idle frames fit.
        let (_,info)=p.resolve("seed").unwrap();p.remove("seed");
        p.decoded.insert("old-scene".into(),DecodedEntry { gray:false, info,rgba:vec![0;256].into(),last_used:0});
        let uploads=UPLOADS.load(Ordering::Relaxed);let mut ids=HashMap::new();
        for i in 0..90 {
            let name=format!("animation/frame{}",i%3);
            let (id,_)=p.resolve(&name).unwrap();
            if let Some(old)=ids.insert(name.clone(),id){assert_eq!(old,id);}
            p.retain(&HashSet::from([name]));
            let idle=p.entries.values().filter(|e|e.last_used!=p.cache_clock).map(Entry::cache_bytes).sum::<usize>()
                +p.decoded.values().map(|e|e.rgba.capacity()).sum::<usize>();
            assert!(idle<=p.idle_budget);
        }
        assert_eq!(UPLOADS.load(Ordering::Relaxed)-uploads,3);
        assert_eq!(reads.get(),4);assert!(!p.decoded.contains_key("old-scene"));
        assert_eq!(p.gpu_demotions,0);
    }

    #[test]
    fn demoted_pixels_reupload_without_reading_or_decoding_and_obey_shared_budget(){
        let _guard=LOCK.lock().unwrap();let (mut p,reads)=provider();
        let first=p.resolve("scene").unwrap();let pixels=p.entries["scene"].rgba.as_ptr();
        p.idle_budget=64;p.retain(&HashSet::new());
        assert!(p.entries.is_empty());assert_eq!(p.decoded["scene"].rgba.as_ptr(),pixels);
        assert_eq!(p.profile_memory(),(0,64,0));
        let restored=p.resolve("scene").unwrap();assert_ne!(first.0,restored.0);
        assert_eq!(p.entries["scene"].rgba.as_ptr(),pixels);
        assert_eq!(reads.get(),1);assert_eq!(p.timing.decoded,1);assert_eq!(p.decoded_hits,1);
        assert_eq!(p.pixel_alpha(restored.0,0,0),Some(128));
        p.retain(&HashSet::new());p.idle_budget=0;p.retain(&HashSet::new());
        assert!(p.decoded.is_empty());assert_eq!(p.profile_memory(),(0,0,0));
    }

    #[test]
    fn explicit_upload_and_prefix_eviction_invalidate_demoted_sources(){
        let _guard=LOCK.lock().unwrap();let (mut p,reads)=provider();
        p.resolve("scene/a").unwrap();p.idle_budget=64;p.retain(&HashSet::new());
        let replacement=p.upload_rgba("scene/a",4,4,&[255;64]).unwrap();
        assert!(p.decoded.is_empty());assert_eq!(p.resolve("scene/a"),Some(replacement));
        assert_eq!(p.pixel_alpha(replacement.0,0,0),Some(255));
        p.resolve("scene/b").unwrap();p.retain(&HashSet::from(["scene/a".into()]));
        assert!(p.decoded.contains_key("scene/b"));assert_eq!(p.evict_prefix("scene/"),2);
        assert!(p.decoded.is_empty());assert!(p.entries.is_empty());assert_eq!(reads.get(),2);
    }

    #[test]
    fn shared_video_has_no_cpu_mirror_and_regular_upload_restores_readable_pixels() {
        let _guard=LOCK.lock().unwrap();let (mut p,_)=provider();
        assert!(p.upload_video_shared_rgba("video",4,4,&[127;64]));
        let e=&p.entries["video"];let id=e.id;let revision=e.revision;
        assert!(e.shared&&!e.opaque&&!e.cacheable&&!e.reclaimable);
        assert!(e.rgba.is_empty());assert_eq!(e.rgba.capacity(),0);
        assert!(p.upload_video_shared_rgba("video",4,4,&[255;64]));
        assert_eq!(p.entries["video"].id,id);assert!(p.entries["video"].revision>revision);
        assert!(p.upload_video_rgba("video",4,4,&[255;64]));
        assert!(!p.entries["video"].shared);assert_eq!(p.entries["video"].rgba.len(),64);
        assert!(!p.upload_video_shared_rgba("video",4,4,&[0;63]));
        assert!(!p.entries["video"].shared);
    }

    #[test]
    fn idle_budget_evicts_oldest_but_keeps_active_and_releases_dynamic_targets() {
        let _guard = LOCK.lock().unwrap();
        let (mut p,_) = provider();
        for name in ["old", "recent", "new", "active"] { p.resolve(name).unwrap(); }
        let charge = p.entries["old"].cache_bytes();
        p.idle_budget = charge * 2;
        p.resolve("old").unwrap(); // Touching a source updates LRU order.
        p.upload_rgba("dynamic",4,4,&[0;64]).unwrap();
        p.retain(&HashSet::from(["active".to_string()]));
        assert!(!p.entries.contains_key("recent"));
        assert!(!p.entries.contains_key("dynamic"));
        for name in ["old", "new", "active"] { assert!(p.entries.contains_key(name)); }
        assert!(!p.decoded.contains_key("recent")); // Reclaim oldest completely before demoting newer GPU data.
        p.idle_budget = 0;
        p.retain(&HashSet::from(["active".to_string()]));
        assert_eq!(p.entries.len(),1);
        assert!(p.decoded.is_empty());
        p.retain(&HashSet::new());
        assert!(p.entries.is_empty());
    }
}
