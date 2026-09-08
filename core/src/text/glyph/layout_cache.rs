//! Bounded memoization of pure layout, independent of texture/reveal state.
use super::{GlyphInfo, LaidGlyph, TextAlignment, TextLayoutConfig, layout_message_layer};
use std::sync::Arc;

const MAX_ENTRIES: usize = 8;
const MAX_GLYPHS: usize = 4096;
const MAX_KEY_TEXT_BYTES: usize = 8192;
const MAX_KEEP_RANGES: usize = 1024;

struct Entry {
    glyphs: Vec<(String, u32, u32)>,
    width: u32,
    config: TextLayoutConfig,
    keep_ranges: Vec<(usize, usize)>,
    alignment: TextAlignment,
    positions: Arc<[LaidGlyph]>,
}

pub(super) struct LayoutCache {
    // Oldest first; hits move to the end without copying glyphs/positions.
    entries: Vec<Entry>,
    glyph_count: usize,
    pub(super) enabled: bool,
}

impl Default for LayoutCache {
    fn default() -> Self {
        Self {
            entries: Vec::new(),
            glyph_count: 0,
            enabled: true,
        }
    }
}

impl LayoutCache {
    pub(super) fn layout(
        &mut self,
        glyphs: &[GlyphInfo],
        line_width: f32,
        config: &TextLayoutConfig,
        keep_ranges: &[(usize, usize)],
        alignment: TextAlignment,
    ) -> Arc<[LaidGlyph]> {
        if !self.enabled {
            return layout_message_layer(glyphs, line_width, config, keep_ranges, alignment).into();
        }
        let hit = self.entries.iter().position(|entry| {
            entry.width == line_width.to_bits()
                && entry.alignment == alignment
                && entry.config == *config
                && entry.keep_ranges == keep_ranges
                && entry.glyphs.len() == glyphs.len()
                && entry
                    .glyphs
                    .iter()
                    .zip(glyphs)
                    .all(|((text, width, advance), glyph)| {
                        *text == glyph.character
                            && *width == glyph.width.to_bits()
                            && *advance == glyph.advance_x.to_bits()
                    })
        });
        if let Some(index) = hit {
            let entry = self.entries.remove(index);
            let positions = Arc::clone(&entry.positions);
            self.entries.push(entry);
            return positions;
        }
        let positions: Arc<[LaidGlyph]> =
            layout_message_layer(glyphs, line_width, config, keep_ranges, alignment).into();
        // Large pages still render completely, without retaining a large key.
        let text_bytes = config
            .prohibit_head
            .len()
            .saturating_add(config.prohibit_foot.len())
            .saturating_add(config.wordparts.len())
            .saturating_add(config.indent_pair.len());
        let text_bytes = glyphs.iter().fold(text_bytes, |sum, glyph| {
            sum.saturating_add(glyph.character.len())
        });
        if glyphs.len() > MAX_GLYPHS
            || text_bytes > MAX_KEY_TEXT_BYTES
            || keep_ranges.len() > MAX_KEEP_RANGES
        {
            return positions;
        }
        while !self.entries.is_empty()
            && (self.entries.len() >= MAX_ENTRIES || self.glyph_count + glyphs.len() > MAX_GLYPHS)
        {
            self.glyph_count -= self.entries.remove(0).glyphs.len();
        }
        self.glyph_count += glyphs.len();
        self.entries.push(Entry {
            glyphs: glyphs
                .iter()
                .map(|g| {
                    (
                        g.character.clone(),
                        g.width.to_bits(),
                        g.advance_x.to_bits(),
                    )
                })
                .collect(),
            width: line_width.to_bits(),
            config: config.clone(),
            keep_ranges: keep_ranges.to_vec(),
            alignment,
            positions: Arc::clone(&positions),
        });
        positions
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render_pipeline::draw::TextureId;

    fn glyphs(text: &str) -> Vec<GlyphInfo> {
        text.chars()
            .map(|c| GlyphInfo {
                character: c.to_string(),
                texture_id: TextureId(0),
                atlas_x: 0.0,
                atlas_y: 0.0,
                atlas_w: 10.0,
                atlas_h: 20.0,
                offset_x: 0.0,
                offset_y: 0.0,
                width: if c == '\n' { 0.0 } else { 10.0 },
                height: 20.0,
                advance_x: if c == '\n' { 0.0 } else { 10.0 },
            })
            .collect()
    }

    #[test]
    fn repeated_queries_share_positions_but_mutated_inputs_do_not() {
        let mut cache = LayoutCache::default();
        let original = glyphs("「中文，English words。」\n第二行");
        let config = TextLayoutConfig::default();
        let a = cache.layout(&original, 90.0, &config, &[], TextAlignment::Left);
        let b = cache.layout(&original, 90.0, &config, &[], TextAlignment::Left);
        assert!(Arc::ptr_eq(&a, &b));
        for kind in 0..12 {
            let mut text = original.clone();
            let mut cfg = config.clone();
            let mut width = 90.0;
            let mut alignment = TextAlignment::Left;
            let mut keep = Vec::new();
            match kind {
                0 => text[0].character = "（".into(),
                1 => text[0].width = 20.0,
                2 => text[0].advance_x = 22.0,
                3 => width = 40.0,
                4 => alignment = TextAlignment::Right,
                5 => keep.push((2, 8)),
                6 => cfg.prohibit_head.clear(),
                7 => cfg.prohibit_foot.clear(),
                8 => cfg.wordparts.clear(),
                9 => cfg.indent_pair = "「」".into(),
                10 => cfg.indent_range = Some(2),
                _ => cfg.indent_nest = true,
            }
            let got = cache.layout(&text, width, &cfg, &keep, alignment);
            assert!(!Arc::ptr_eq(&a, &got), "mutation {kind}");
            assert_eq!(
                &*got,
                layout_message_layer(&text, width, &cfg, &keep, alignment)
            );
        }
        // Texture/color/reveal-related data do not change line breaking.
        let mut metadata = original.clone();
        metadata[0].atlas_x = 100.0;
        metadata[0].offset_y = 9.0;
        let a = cache.layout(&original, 90.0, &config, &[], TextAlignment::Left);
        assert!(Arc::ptr_eq(
            &a,
            &cache.layout(&metadata, 90.0, &config, &[], TextAlignment::Left)
        ));
    }

    #[test]
    fn live_disable_and_reenable_keep_layout_exact() {
        let mut cache = LayoutCache::default();
        let text = glyphs("文本 Word，第二行。");
        let cfg = TextLayoutConfig::default();
        let cached = cache.layout(&text, 50.0, &cfg, &[], TextAlignment::Left);
        cache.enabled = false;
        let plain = cache.layout(&text, 50.0, &cfg, &[], TextAlignment::Left);
        assert_eq!(&*cached, &*plain);
        assert!(!Arc::ptr_eq(&cached, &plain));
        // Disabled changes cannot resurrect stale positions when re-enabled.
        let mut changed = text.clone();
        changed[0].advance_x = 25.0;
        let expected = cache.layout(&changed, 50.0, &cfg, &[], TextAlignment::Left);
        cache.enabled = true;
        let actual = cache.layout(&changed, 50.0, &cfg, &[], TextAlignment::Left);
        assert_eq!(&*expected, &*actual);
        assert!(!Arc::ptr_eq(&cached, &actual));
        assert!(Arc::ptr_eq(
            &cached,
            &cache.layout(&text, 50.0, &cfg, &[], TextAlignment::Left)
        ));
    }

    #[test]
    fn cached_layout_matches_uncached_for_changing_multilingual_pages() {
        let mut cache = LayoutCache::default();
        let alphabet: Vec<char> = "「日本語，中文。」ABC abc!?（）\n".chars().collect();
        let mut seed = 17u32;
        for case in 0..500 {
            let mut next = || {
                seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
                seed
            };
            let len = (next() % 150 + 1) as usize;
            let text: String = (0..len)
                .map(|_| alphabet[next() as usize % alphabet.len()])
                .collect();
            let gs = glyphs(&text);
            let cfg = TextLayoutConfig {
                indent_pair: "「」（）".into(),
                indent_nest: case % 2 == 0,
                ..Default::default()
            };
            let width = (next() % 300 + 1) as f32;
            let alignment = [
                TextAlignment::Left,
                TextAlignment::Center,
                TextAlignment::Right,
                TextAlignment::Equalize,
            ][case % 4];
            let keep = if len > 10 { vec![(2, 8)] } else { vec![] };
            let expected = layout_message_layer(&gs, width, &cfg, &keep, alignment);
            let miss = cache.layout(&gs, width, &cfg, &keep, alignment);
            let hit = cache.layout(&gs, width, &cfg, &keep, alignment);
            assert_eq!(&*hit, expected);
            assert!(Arc::ptr_eq(&hit, &miss));
        }
    }

    #[test]
    fn limits_evict_and_bypass_without_truncating_text_or_invalidating_live_results() {
        let mut cache = LayoutCache::default();
        let cfg = TextLayoutConfig::default();
        let small = glyphs("首行。");
        let held = cache.layout(&small, 30.0, &cfg, &[], TextAlignment::Left);
        let page = glyphs(&"文".repeat(1024));
        for width in 100..120 {
            cache.layout(&page, width as f32, &cfg, &[], TextAlignment::Left);
            assert!(cache.entries.len() <= MAX_ENTRIES);
            assert!(cache.glyph_count <= MAX_GLYPHS);
        }
        assert_eq!(
            &*held,
            layout_message_layer(&small, 30.0, &cfg, &[], TextAlignment::Left)
        );
        let large = glyphs(&"文".repeat(MAX_GLYPHS + 1));
        let a = cache.layout(&large, 500.0, &cfg, &[], TextAlignment::Left);
        let b = cache.layout(&large, 500.0, &cfg, &[], TextAlignment::Left);
        assert_eq!(a.len(), large.len());
        assert!(!Arc::ptr_eq(&a, &b));
        let huge_cfg = TextLayoutConfig {
            prohibit_head: "。".repeat(MAX_KEY_TEXT_BYTES),
            ..cfg
        };
        let a = cache.layout(&small, 50.0, &huge_cfg, &[], TextAlignment::Left);
        let b = cache.layout(&small, 50.0, &huge_cfg, &[], TextAlignment::Left);
        assert_eq!(&*a, &*b);
        assert!(!Arc::ptr_eq(&a, &b));
    }

    #[test]
    #[ignore = "manual release microbenchmark; not a hardware FPS test"]
    fn benchmark_layout_reuse() {
        use std::hint::black_box;
        use std::time::Instant;
        let cfg = TextLayoutConfig {
            indent_pair: "「」".into(),
            ..Default::default()
        };
        for length in [40, 120, 360] {
            let text: String = "「文本文字，English words。」"
                .chars()
                .cycle()
                .take(length)
                .collect();
            let glyphs = glyphs(&text);
            let mut cache = LayoutCache::default();
            let a = cache.layout(&glyphs, 300.0, &cfg, &[], TextAlignment::Left);
            assert_eq!(
                &*a,
                layout_message_layer(&glyphs, 300.0, &cfg, &[], TextAlignment::Left)
            );
            let loops = 5000;
            let start = Instant::now();
            for _ in 0..loops {
                black_box(layout_message_layer(
                    black_box(&glyphs),
                    300.0,
                    &cfg,
                    &[],
                    TextAlignment::Left,
                ));
            }
            let uncached = start.elapsed();
            let start = Instant::now();
            for _ in 0..loops {
                black_box(cache.layout(black_box(&glyphs), 300.0, &cfg, &[], TextAlignment::Left));
            }
            println!(
                "layout chars={length} loops={loops} uncached_ns={} cached_ns={}",
                uncached.as_nanos() / loops,
                start.elapsed().as_nanos() / loops
            );
        }
    }
}
