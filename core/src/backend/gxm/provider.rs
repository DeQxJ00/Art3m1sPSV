use crate::render_pipeline::draw::{
    TextureId, TextureInfo, TextureProvider, masked_texture_name, solid_texture_name,
};
use image::ImageReader;
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

#[derive(Clone)]
struct Entry {
    id: TextureId,
    info: TextureInfo,
    rgba: Vec<u8>,
    opaque: bool,
    revision: u64,
    last_used: u64,
    cacheable: bool,
}

// Budget only inactive source textures; current scene/transitions stay pinned.
// Charge both CPU RGBA storage and the host's estimated aligned GPU storage.
const IDLE_TEXTURE_BUDGET: usize = 16 * 1024 * 1024;

impl Entry {
    fn cache_bytes(&self) -> usize {
        self.rgba.capacity().saturating_add(
            ((self.info.width as usize + 7) & !7).saturating_mul(self.info.height as usize).saturating_mul(4)
        )
    }
}

unsafe extern "C" {
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
    fn art3m1s_gxm_upload_video_texture(texture: u64, width: u32, height: u32, rgba: *const u8, length: usize) -> i32;
}

pub struct GxmTextureProvider {
    source: Option<Source>,
    entries: HashMap<String, Entry>,
    ids: HashMap<TextureId, String>,
    next_id: u64,
    revision: u64,
    reported_failures: HashSet<String>,
    cache_clock: u64,
    cache_hits: u64,
    cache_misses: u64,
    cache_evictions: u64,
    retain_count: u64,
    idle_budget: usize,
    timing: TextureTiming,
    timing_started: Instant,
}

impl GxmTextureProvider {
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
        self.entries.insert(name.to_owned(), Entry { id, info, rgba: Vec::new(), opaque: false,
            revision: self.revision, last_used: self.cache_clock, cacheable: false });
        self.ids.insert(id, name.to_owned());
        Some((id, info))
    }
    pub fn new() -> Self {
        Self {
            source: None,
            entries: HashMap::new(),
            ids: HashMap::new(),
            next_id: 1,
            revision: 0,
            reported_failures: HashSet::new(),
            cache_clock: 0,
            cache_hits: 0,
            cache_misses: 0,
            cache_evictions: 0,
            retain_count: 0,
            idle_budget: IDLE_TEXTURE_BUDGET,
            timing: TextureTiming::default(),
            timing_started: Instant::now(),
        }
    }

    pub fn with_source<F>(mut self, source: F) -> Self
    where
        F: Fn(&str) -> Option<Vec<u8>> + 'static,
    {
        self.source = Some(Box::new(source));
        self
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

    pub fn evict_prefix(&mut self, prefix: &str) -> usize {
        let names = self.entries.keys().filter(|name| name.starts_with(prefix)).cloned().collect::<Vec<_>>();
        for name in &names { self.remove(name); }
        names.len()
    }

    pub fn set_profile_enabled(&self, _enabled: bool) {}

    pub fn take_profile_uploads(&self) -> crate::backend::gl::provider::TextureUploadProfile {
        crate::backend::gl::provider::TextureUploadProfile::default()
    }

    pub fn profile_memory(&self) -> (usize, u64, u64) {
        let cpu_bytes = self.entries.values().map(|entry| entry.rgba.len() as u64).sum();
        let gpu_bytes = self.entries.values().map(|entry| {
            ((u64::from(entry.info.width) + 7) & !7) * u64::from(entry.info.height) * 4
        }).sum();
        (self.entries.len(), cpu_bytes, gpu_bytes)
    }

    fn upload(&mut self, name: &str, width: u32, height: u32, rgba: &[u8]) -> Option<(TextureId, TextureInfo)> {
        self.upload_impl(name, width, height, rgba, false, true)
    }

    fn upload_impl(&mut self, name: &str, width: u32, height: u32, rgba: &[u8], video: bool, retain_pixels: bool) -> Option<(TextureId, TextureInfo)> {
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
            else { art3m1s_gxm_upload_texture(id.0, width, height, rgba.as_ptr(), rgba.len()) }
        };
        let host_us = elapsed_us(started);
        self.timing.uploads += 1;
        self.timing.upload_bytes += rgba.len() as u64;
        if uploaded <= 0 {
            self.timing.upload_errors += 1;
            self.timing.upload_us += host_us;
            self.timing.upload_max_us = self.timing.upload_max_us.max(host_us);
            return None;
        }
        self.revision = self.revision.wrapping_add(1).max(1);
        let info = TextureInfo { width, height };
        // Render-only callers own the source pixels (e.g. the glyph atlas).
        // Conservatively keep blending enabled without scanning/copying that buffer.
        let opaque = retain_pixels && rgba.chunks_exact(4).all(|pixel| pixel[3] == 255);
        self.cache_clock = self.cache_clock.saturating_add(1);
        if let Some(entry) = self.entries.get_mut(name) {
            entry.info=info;entry.opaque=opaque;entry.revision=self.revision;
            entry.last_used=self.cache_clock;entry.cacheable=false;
            if retain_pixels {
                entry.rgba.clear();entry.rgba.extend_from_slice(rgba);
            } else {
                // Drop capacity too if this texture previously kept readable pixels.
                entry.rgba = Vec::new();
            }
        } else {
            self.entries.insert(name.to_owned(), Entry { id, info, rgba: if retain_pixels { rgba.to_vec() } else { Vec::new() }, opaque, revision: self.revision, last_used: self.cache_clock, cacheable: false });
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
}

impl Default for GxmTextureProvider {
    fn default() -> Self { Self::new() }
}

impl Drop for GxmTextureProvider {
    fn drop(&mut self) {
        for entry in self.entries.values() {
            unsafe { art3m1s_gxm_delete_texture(entry.id.0) };
        }
    }
}

impl TextureProvider for GxmTextureProvider {
    fn resolve(&mut self, name: &str) -> Option<(TextureId, TextureInfo)> {
        self.cache_clock = self.cache_clock.saturating_add(1);
        if let Some(entry) = self.entries.get_mut(name) {
            entry.last_used = self.cache_clock;
            self.cache_hits += 1;
            return Some((entry.id, entry.info));
        }
        if crate::video::is_video_layer_texture_name(name) { return None; }
        self.cache_misses += 1;
        let source = self.source.as_ref()?;
        let started = Instant::now();
        let bytes = source.as_ref()(name);
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
        let started = Instant::now();
        let reader = match ImageReader::new(Cursor::new(bytes)).with_guessed_format() {
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
        let image = match reader.decode() {
            Ok(image) => image.to_rgba8(),
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
        let result = self.upload(name, image.width(), image.height(), image.as_raw());
        if result.is_some() {
            self.entries.get_mut(name).unwrap().cacheable = true;
        }
        if result.is_none() && self.reported_failures.insert(name.to_owned()) {
            crate::core_warn!("GXM texture upload failure: {name}: {}x{}", image.width(), image.height());
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
        if entry.info != (TextureInfo { width, height }) || !entry.rgba.is_empty() {
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
        self.timing.upload_bytes += w as u64 * h as u64 * 4;
        self.revision = self.revision.wrapping_add(1).max(1);
        let entry = self.entries.get_mut(name).unwrap();
        entry.revision = self.revision;
        entry.opaque = false;
        Some((id, entry.info))
    }

    fn pixel_alpha(&self, texture: TextureId, x: u32, y: u32) -> Option<u8> {
        let entry = self.entries.get(self.ids.get(&texture)?)?;
        if x >= entry.info.width || y >= entry.info.height { return None; }
        entry.rgba.get(((y * entry.info.width + x) * 4 + 3) as usize).copied()
    }

    fn texture_is_opaque(&self, texture: TextureId) -> bool {
        self.ids.get(&texture).and_then(|name| self.entries.get(name)).is_some_and(|entry| entry.opaque)
    }

    fn retain(&mut self, names: &HashSet<String>) {
        let wall_us = elapsed_us(self.timing_started);
        if wall_us >= 5000000 {
            let t = std::mem::take(&mut self.timing);
            self.timing_started = Instant::now();
            crate::core_info!("GXM texture-perf wall_us={} reads={} missing={} decoded={} decode_errors={} uploads={} upload_errors={} upload_bytes={} read_us={} decode_us={} upload_us={} upload_max_us={}",
                wall_us, t.reads, t.missing, t.decoded, t.decode_errors, t.uploads, t.upload_errors, t.upload_bytes, t.read_us, t.decode_us, t.upload_us, t.upload_max_us);
        }
        let stale = self.entries.keys()
            .filter(|name| !names.contains(*name) && !name.starts_with("__solid_") && name.as_str() != ":bg/black" && name.as_str() != ":bg/white")
            .cloned().collect::<Vec<_>>();
        let mut idle = Vec::new();
        let mut idle_bytes = 0usize;
        for name in stale {
            let entry = &self.entries[&name];
            if entry.cacheable {
                let bytes = entry.cache_bytes();
                idle_bytes = idle_bytes.saturating_add(bytes);
                idle.push((entry.last_used, name, bytes));
            } else {
                // Video, text and capture targets retain their explicit lifecycle.
                self.remove(&name);
            }
        }
        idle.sort_unstable();
        for (_, name, bytes) in idle {
            if idle_bytes <= self.idle_budget { break; }
            self.remove(&name);
            idle_bytes = idle_bytes.saturating_sub(bytes);
            self.cache_evictions += 1;
        }
        self.retain_count += 1;
        if self.retain_count % 120 == 0 {
            crate::core_info!("GXM texture-cache hits={} misses={} evictions={} inactive_est_bytes={} budget={}",
                self.cache_hits, self.cache_misses, self.cache_evictions, idle_bytes, self.idle_budget);
        }
    }

    fn solid_texture(&mut self, rgba: [u8; 4]) -> Option<(TextureId, TextureInfo)> {
        let name = solid_texture_name(rgba);
        if let Some(entry) = self.entries.get(&name) { return Some((entry.id, entry.info)); }
        self.upload(&name, 1, 1, &rgba)
    }

    fn resolve_with_mask(&mut self, file: &str, mask: &str) -> Option<(TextureId, TextureInfo)> {
        let name = masked_texture_name(file, mask);
        if let Some(entry) = self.entries.get(&name) { return Some((entry.id, entry.info)); }
        let (file_id, file_info) = self.resolve(file)?;
        let (mask_id, mask_info) = self.resolve(mask)?;
        if file_info != mask_info { return Some((file_id, file_info)); }
        let source = self.entries.get(self.ids.get(&file_id)?)?.rgba.clone();
        let mask_pixels = self.entries.get(self.ids.get(&mask_id)?)?.rgba.clone();
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
        Some((entry.info.width, entry.info.height, entry.rgba.clone()))
    }
}

#[cfg(all(test, not(target_os = "vita")))]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;
    use std::sync::{Mutex, atomic::{AtomicUsize, Ordering}};

    static LOCK: Mutex<()> = Mutex::new(());
    static UPLOADS: AtomicUsize = AtomicUsize::new(0);
    #[cfg(feature = "gxm-builtin-effects")]
    static CAPTURE_READY: AtomicUsize = AtomicUsize::new(0);
    #[cfg(feature = "gxm-builtin-effects")]
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_capture_previous_texture(_: u64, _: u32, _: u32) -> i32 {
        CAPTURE_READY.load(Ordering::Relaxed) as i32
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_texture(_: u64, _: u32, _: u32, _: *const u8, _: usize) -> i32 {
        UPLOADS.fetch_add(1, Ordering::Relaxed); 1
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_upload_video_texture(_: u64, _: u32, _: u32, _: *const u8, _: usize) -> i32 { 1 }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_delete_texture(_: u64) {}
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_update_texture_region(_: u64, _: u32, _: u32, _: *const u8, _: usize, _: u32, _: u32, _: u32, _: u32) -> i32 { 1 }

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
        p.idle_budget = 0;
        p.retain(&HashSet::from(["active".to_string()]));
        assert_eq!(p.entries.len(),1);
        p.retain(&HashSet::new());
        assert!(p.entries.is_empty());
    }
}
