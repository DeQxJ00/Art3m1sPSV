//! Cached coverage dilation. Unlike four offset copies this produces a connected
//! stroke and does not accumulate alpha at overlapping antialiased edges.
use super::*;

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub(super) struct OutlineKey {
    page: usize, x: u32, y: u32, w: u32, h: u32, radius: u32,
}
impl OutlineKey {
    pub(super) fn new(g: &GlyphInfo, radius: f32) -> Option<Self> {
        if !radius.is_finite() || radius <= 0.0 || radius > 16.0 || g.atlas_w <= 0.0 || g.atlas_h <= 0.0 { return None; }
        // Bound pathological script values and cache keys. Normal Artemis fonts
        // use 1-2 px; preserve fractional widths to 1/16 px.
        let radius = (radius * 16.0).round().max(1.0) as u32;
        let pad = radius.div_ceil(16) + 1;
        let key = Self { page: g.texture_id.0 as usize, x: g.atlas_x as u32, y: g.atlas_y as u32,
            w: g.atlas_w as u32, h: g.atlas_h as u32, radius };
        (key.w + 2 * pad + 2 <= ATLAS_SZ && key.h + 2 * pad + 2 <= ATLAS_SZ).then_some(key)
    }
}

#[derive(Clone, Copy)]
pub(super) struct OutlineRegion { pub page: usize, x: u32, y: u32, w: u32, h: u32, pub pad: u32 }
impl OutlineRegion {
    pub fn clip(&self) -> ClipRect {
        ClipRect { uv_offset: [self.x as f32 / ATLAS_SZ as f32, self.y as f32 / ATLAS_SZ as f32],
            uv_scale: [self.w as f32 / ATLAS_SZ as f32, self.h as f32 / ATLAS_SZ as f32],
            quad_size: [self.w as f32, self.h as f32] }
    }
}

fn dilate(source: &[u8], w: u32, h: u32, radius: f32) -> (u32, u32, u32, Vec<u8>) {
    let pad = radius.ceil() as u32 + 1;
    let (ow, oh) = (w + pad * 2, h + pad * 2);
    let mut out = [255, 255, 255, 0].repeat((ow * oh) as usize);
    let mut kernel = Vec::new();
    for dy in -(pad as i32)..=pad as i32 {
        for dx in -(pad as i32)..=pad as i32 {
            let distance = ((dx * dx + dy * dy) as f32).sqrt();
            let coverage = ((radius + 1.0 - distance).clamp(0.0, 1.0) * 255.0).round() as u8;
            if coverage != 0 { kernel.push((dx, dy, coverage)); }
        }
    }
    for y in 0..h {
        for x in 0..w {
            let alpha = source[(y * w + x) as usize];
            if alpha == 0 { continue; }
            for &(dx, dy, weight) in &kernel {
                let ox = (x + pad) as i32 + dx;
                let oy = (y + pad) as i32 + dy;
                let index = (oy as usize * ow as usize + ox as usize) * 4 + 3;
                out[index] = out[index].max(((alpha as u16 * weight as u16 + 127) / 255) as u8);
            }
        }
    }
    (ow, oh, pad, out)
}

impl GlyphTextRenderer {
    pub(super) fn prepare_outlines(&mut self) {
        #[cfg(feature = "gxm-text-epoch")]
        {
            let token = self.snapshot_revision.token();
            if token.is_some() && token == self.outline_revision { return; }
            self.outline_revision = token;
        }
        let mut missing = std::collections::HashSet::new();
        for layer in self.state.layers.values() {
            if !(layer.font.style.as_deref().unwrap_or("").contains("outline")
                || layer.font.outline_size.is_some_and(|r| r > 0.0)) { continue; }
            let radius = layer.font.outline_size.unwrap_or(1.0);
            for glyph in &layer.text_buffer {
                if let Some(key) = OutlineKey::new(glyph, radius) {
                    if !self.outlines.contains_key(&key) { missing.insert(key); }
                }
            }
        }
        for key in missing {
            let Some(atlas) = self.atlases.get(key.page) else { continue; };
            if key.x + key.w > ATLAS_SZ || key.y + key.h > ATLAS_SZ { continue; }
            let mut alpha = Vec::with_capacity((key.w * key.h) as usize);
            for y in key.y..key.y + key.h {
                for x in key.x..key.x + key.w {
                    alpha.push(atlas.alpha_at(x,y));
                }
            }
            let (w, h, pad, rgba) = dilate(&alpha, key.w, key.h, key.radius as f32 / 16.0);
            let (page, x, y) = self.alloc_atlas_region(w + 2, h + 2);
            self.atlases[page].write(x + 1, y + 1, w, h, &rgba);
            self.outlines.insert(key, OutlineRegion { page, x: x + 1, y: y + 1, w, h, pad });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn continuous_round_stroke_preserves_coverage_and_clear_gutter() {
        let (w, h, p, pixels) = dilate(&[128], 1, 1, 2.0);
        let a = |x: u32, y: u32| pixels[((y * w + x) * 4 + 3) as usize];
        assert_eq!(a(p, p), 128); // no repeated blend amplification
        for (x,y) in [(p-2,p),(p+2,p),(p,p-2),(p,p+2)] { assert_eq!(a(x,y),128); }
        assert!(a(p+2,p+1) > 0 && a(p+2,p+1) < 128);
        for y in 0..h { for x in 0..w {
            assert_eq!(a(x,y), a(w-1-x,y)); assert_eq!(a(x,y),a(x,h-1-y));
            assert_eq!(&pixels[((y*w+x)*4) as usize..((y*w+x)*4+3) as usize], &[255;3]);
            if x==0 || y==0 || x==w-1 || y==h-1 { assert_eq!(a(x,y),0); }
        }}
    }
    #[test]
    fn fractional_stroke_grows_without_discontinuity() {
        let (w, _, p, pixels) = dilate(&[255], 1, 1, 0.5);
        assert_eq!(pixels[((p*w+p+1)*4+3) as usize],128);
        assert_eq!(pixels[((p*w+p)*4+3) as usize],255);
    }
    #[test]
    #[ignore = "requires ART3M1S_TEST_FONT"]
    fn outline_reuses_atlas_and_keeps_layout_and_reveal_state() {
        let mut r = GlyphTextRenderer::new();
        r.set_font(&std::fs::read(std::env::var("ART3M1S_TEST_FONT").unwrap()).unwrap()).unwrap();
        r.state.active_layer_mut().font.size = Some(24.0);
        r.state.active_layer_mut().font.outline_size = Some(2.0);
        r.push_text("能够在才华横溢、光彩照人的主人手下做事。", false);
        let before = r.state.layers.clone();
        r.prepare_outlines();
        assert!(!r.outlines.is_empty());
        let count = r.outlines.len();
        let pages: Vec<_> = r.atlases.iter().map(|a| a.px.clone()).collect();
        for _ in 0..20 { r.prepare_outlines(); }
        assert_eq!(r.outlines.len(), count);
        assert_eq!(r.atlases.iter().map(|a| a.px.clone()).collect::<Vec<_>>(), pages);
        for (id, layer) in before {
            assert_eq!(r.state.layers[&id].text_buffer, layer.text_buffer);
            assert_eq!(r.state.layers[&id].reveal_index, layer.reveal_index);
            assert_eq!(r.state.layers[&id].page_tags, layer.page_tags);
        }
        struct Provider;
        impl TextureProvider for Provider {
            fn resolve(&mut self, _: &str) -> Option<(TextureId, TextureInfo)> {
                Some((TextureId(10), TextureInfo { width: ATLAS_SZ, height: ATLAS_SZ }))
            }
            fn upload_rgba(&mut self, _: &str, _: u32, _: u32, _: &[u8]) -> Option<(TextureId, TextureInfo)> {
                self.resolve("")
            }
        }
        let mut provider = Provider;
        let glyph_count = r.state.active_layer_mut().text_buffer.len();
        r.prepare_textures(&mut provider);
        assert!(r.atlases.iter().all(|a| !a.dirty));
        let commands = r.build_text_commands(&mut provider);
        let commands = commands.values().next().unwrap();
        assert_eq!(commands.len(), glyph_count * 2);
        for pair in commands.chunks_exact(2) {
            assert_eq!(pair[0].opacity, pair[1].opacity);
            assert!(pair[0].clip.quad_size[0] > pair[1].clip.quad_size[0]);
        }
        // A style change must invalidate the prepared epoch, even without a
        // change to the text. Disabling the outline emits only body glyphs.
        r.font_state_mut().active_layer_mut().font.outline_size = Some(1.0);
        r.prepare_textures(&mut provider);
        assert_eq!(r.outlines.len(), count * 2);
        r.font_state_mut().active_layer_mut().font.outline_size = Some(0.0);
        let plain = r.build_text_commands(&mut provider);
        assert_eq!(plain.values().next().unwrap().len(), glyph_count);
    }
}
