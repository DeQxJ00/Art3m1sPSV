//! Host presentation settings never enter script font tables or backlog tags.
use super::*;

#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) struct MessageFontSizes { enabled: bool, name: u32, dialogue: u32 }
impl Default for MessageFontSizes {
    fn default() -> Self { Self { enabled: false, name: 100, dialogue: 100 } }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roles_and_disabled_values_are_independent() {
        let s = MessageFontSizes { enabled: true, name: 125, dialogue: 150 };
        assert_eq!(s.scale("100.mw.name", None), 1.25);
        assert_eq!(s.scale("100.mw.adv", None), 1.5);
        assert_eq!(s.scale("100.mw.sub", None), 1.5);
        assert_eq!(s.scale("1.80.mw.adv_name", None), 1.25);
        assert_eq!(s.scale("1.80.mw.adv_adv", None), 1.5);
        assert_eq!(s.scale("1.80.mw.adv_sub", None), 1.5);
        for id in ["save.name", "backlog", "config.name", "100.mw.name.icon", "backlog.adv_name", "1.80.mw.adv_name.icon", "1.80.mw.adv_other"] { assert_eq!(s.scale(id, None), 1.0); }
        let off = MessageFontSizes { enabled: false, ..s };
        assert_eq!(off.scale("100.mw.name", None), 1.0);
        assert_eq!(off.scale("100.mw.adv", None), 1.0);
        assert_eq!(off.scale("1.80.mw.adv_name", None), 1.0);
        assert_eq!(off.scale("1.80.mw.adv_adv", None), 1.0);
        assert_eq!(off.name, 125);
    }
    #[test]
    fn exact_roles_override_spelling_and_never_match_children() {
        let s=MessageFontSizes{enabled:true,name:125,dialogue:150};
        let roles=asb_interpreter::MessageLayerIds{name:Some("panel.speaker".into()),
            dialogue:Some("panel.paragraph".into()),subtitle:Some("translation".into())};
        assert_eq!(s.scale("panel.speaker",Some(&roles)),1.25);
        assert_eq!(s.scale("panel.paragraph",Some(&roles)),1.5);
        assert_eq!(s.scale("translation",Some(&roles)),1.5);
        for id in ["panel.speaker.icon","panel.paragraph.child","100.mw.name","100.mw.adv","save.name","backlog"] {
            assert_eq!(s.scale(id,Some(&roles)),1.0);
        }
        let empty=asb_interpreter::MessageLayerIds::default();
        assert_eq!(s.scale("100.mw.adv",Some(&empty)),1.0);
        assert_eq!(s.scale(DEFAULT_MESSAGE_LAYER,Some(&empty)),1.5);
        assert_eq!(MessageFontSizes{enabled:false,..s}.scale("panel.speaker",Some(&roles)),1.0);
    }
    #[test]
    #[ignore = "requires ART3M1S_TEST_FONT pointing to a local font fixture"]
    fn mapped_roles_reflow_existing_pages_and_restore_without_touching_script_state() {
        let mut r=GlyphTextRenderer::new();
        r.set_named_font_bytes("fixture",std::fs::read(std::env::var("ART3M1S_TEST_FONT").unwrap()).unwrap()).unwrap();
        let it=asb_interpreter::Interpreter::default();
        it.lua().load("ids={name='speaker',adv='paragraph',sub='translation'}; function mw_getmsgid(r) return ids[r] end").exec().unwrap();
        assert!(r.set_message_font_roles(it.query_message_layer_ids().unwrap()));
        for id in ["speaker","paragraph","translation","100.mw.adv_name"] {
            r.state.active_layer=Some(id.into());
            r.state.active_layer_mut().font.size=Some(24.0);
            r.state.active_layer_mut().width=100.0;
            r.push_text("ABCDEFGHIJKLMN",false);
            r.ruby_start("ab");r.push_text("CD",false);r.ruby_end();
        }
        let original=r.state.layers.clone();
        assert!(r.update_message_sizes(true,125,150));
        for id in ["speaker","paragraph","translation"] {
            assert!(r.state.layers[id].text_buffer[0].advance_x>original[id].text_buffer[0].advance_x);
        }
        assert_eq!(r.state.layers["100.mw.adv_name"].text_buffer,original["100.mw.adv_name"].text_buffer);
        // Same existing IDs exchange roles; the old subtitle becomes ordinary UI.
        it.lua().load("ids={name='paragraph',adv='speaker'}").exec().unwrap();
        assert!(r.set_message_font_roles(it.query_message_layer_ids().unwrap()));
        assert_eq!(r.message_scale("paragraph"),1.25);
        assert_eq!(r.message_scale("speaker"),1.5);
        assert_eq!(r.state.layers["translation"].text_buffer,original["translation"].text_buffer);
        for (id,old) in &original {
            assert_eq!(r.state.layers[id].page_tags,old.page_tags);
            assert_eq!(r.state.layers[id].font,old.font);
            assert_eq!(r.state.layers[id].reveal_index,old.reveal_index);
            assert_eq!(r.state.layers[id].reveal_clock_ms,old.reveal_clock_ms);
        }
        assert!(r.update_message_sizes(false,125,150));
        for (id,old) in &original {
            assert_eq!(r.state.layers[id].text_buffer,old.text_buffer);
            assert_eq!(r.state.layers[id].rubies,old.rubies);
        }
        // Failed resizing preserves the prior role map and entire page.
        assert!(r.update_message_sizes(true,125,150));
        r.state.layers.get_mut("speaker").unwrap().text_buffer[0].logical_size=0.0;
        let previous=r.message_roles.clone();let saved=r.state.layers["speaker"].clone();
        assert!(!r.set_message_font_roles(Some(Default::default())));
        assert_eq!(r.message_roles,previous);
        assert_eq!(r.state.layers["speaker"].text_buffer,saved.text_buffer);
    }

    #[test]
    fn invalid_range_preserves_settings() {
        let mut r = GlyphTextRenderer::new();
        assert!(!r.update_message_sizes(true, 74, 100));
        assert!(!r.update_message_sizes(true, 100, 151));
        assert_eq!(r.message_sizes, MessageFontSizes::default());
    }
    #[test]
    #[ignore = "requires ART3M1S_TEST_FONT pointing to a local font fixture"]
    fn font_override_reraster_reflow_restore_and_logical_state() {
        let bytes = std::fs::read(std::env::var("ART3M1S_TEST_FONT").unwrap()).unwrap();
        let mut r = GlyphTextRenderer::new();
        r.set_named_font_bytes("first", bytes.clone()).unwrap();
        for id in ["100.mw.adv", "100.mw.name", "1.80.mw.adv_adv", "1.80.mw.adv_name", "1.80.mw.adv_sub", "save.name"] {
            r.state.active_layer = Some(id.into());
            r.state.active_layer_mut().font.size = Some(30.0);
            r.state.active_layer_mut().width = 130.0;
            r.push_text("ABCDABCD", false);
            r.ruby_start("abc"); r.push_text("ABC", false); r.ruby_end();
        }
        // Mixed inline sizes and font generations must survive on/off cycles.
        r.state.active_layer = Some("100.mw.adv".into());
        r.state.active_layer_mut().font.size = Some(20.0);
        r.set_named_font_bytes("second", bytes).unwrap();
        let span = r.push_text_tracked("Z", false).unwrap();
        r.state.active_layer_mut().reveal_index = 3;
        r.state.active_layer_mut().reveal_clock_ms = 271;
        let before = r.state.layers.clone();
        let height = r.active_layer_text_metrics().unwrap().1;
        assert!(r.update_message_sizes(false, 125, 150));
        for (id, old) in &before { assert_eq!(r.state.layers[id].text_buffer, old.text_buffer); }
        assert!(r.update_message_sizes(true, 125, 150));
        for id in ["100.mw.name", "100.mw.adv", "1.80.mw.adv_adv", "1.80.mw.adv_name", "1.80.mw.adv_sub"] {
            let layer = &r.state.layers[id]; let old = &before[id];
            assert!(layer.text_buffer[0].advance_x > old.text_buffer[0].advance_x);
            assert!(layer.rubies[0].glyphs[0].advance_x > old.rubies[0].glyphs[0].advance_x);
            assert_eq!(layer.page_tags, old.page_tags);
            assert_eq!(layer.page_font, old.page_font);
            assert_eq!(layer.font, old.font);
            assert_eq!((layer.reveal_index, layer.reveal_clock_ms), (old.reveal_index, old.reveal_clock_ms));
        }
        assert_eq!(r.state.layers["save.name"].text_buffer, before["save.name"].text_buffer);
        assert!(r.active_layer_text_metrics().unwrap().1 > height);
        assert_eq!(r.replace_text_span(&span, "Z"), Some(0));
        for _ in 0..3 {
            assert!(r.update_message_sizes(false, 125, 150));
            for (id, old) in &before {
                assert_eq!(r.state.layers[id].text_buffer, old.text_buffer);
                assert_eq!(r.state.layers[id].rubies, old.rubies);
            }
            assert!(r.update_message_sizes(true, 125, 150));
        }
        assert_eq!(r.font_generation, r.fonts["second"].1);
    }
}
impl MessageFontSizes {
    pub(super) fn scale(self, id: &str, roles: Option<&asb_interpreter::MessageLayerIds>) -> f32 {
        if !self.enabled { return 1.0; }
        if let Some(roles) = roles {
            let percent = if roles.name.as_deref() == Some(id) { self.name }
                else if roles.dialogue.as_deref() == Some(id) || roles.subtitle.as_deref() == Some(id)
                    || id == DEFAULT_MESSAGE_LAYER { self.dialogue }
                else { 100 };
            return percent as f32 / 100.0;
        }
        // Older scripts without mw_getmsgid hard-code these layer conventions.
        // Compatibility only: once a resolver exists its exact IDs are authoritative.
        // Never broaden this to arbitrary suffix/substring matching.
        let role = id.rsplit_once(".mw.").map(|(_, role)| role);
        let percent = match role {
            Some("name" | "adv_name") => self.name,
            Some("adv" | "sub" | "adv_adv" | "adv_sub") => self.dialogue,
            _ if id == DEFAULT_MESSAGE_LAYER => self.dialogue,
            _ => 100,
        };
        percent as f32 / 100.0
    }
}
impl GlyphTextRenderer {
    pub(super) fn message_scale(&self, id: &str) -> f32 {
        self.message_sizes.scale(id, self.message_roles.as_ref())
    }
    pub(super) fn active_message_scale(&self) -> f32 {
        self.message_scale(self.state.active_layer.as_deref().unwrap_or(DEFAULT_MESSAGE_LAYER))
    }
    pub(super) fn rasterize_message_glyph(&mut self, c: char, logical: f32, ratio: f32) -> Option<GlyphInfo> {
        let mut glyph = self.rasterize_glyph(c, logical * ratio)?;
        glyph.logical_size = logical;
        Some(glyph)
    }
    pub(super) fn message_metrics(&self, layer: &crate::text::render::MessageLayer, body: f32) -> TextLineMetrics {
        let mut m = text_line_metrics(&layer.font, body);
        let ratio = self.message_scale(&layer.id);
        m.line_height *= ratio; m.body_top *= ratio; m.ruby_top *= ratio;
        m
    }
    pub(super) fn update_message_sizes(&mut self, enabled: bool, name: u32, dialogue: u32) -> bool {
        if !(75..=150).contains(&name) || !(75..=150).contains(&dialogue) { return false; }
        let next = MessageFontSizes { enabled, name, dialogue };
        self.update_message_presentation(next, self.message_roles.clone())
    }
    pub(super) fn update_message_presentation(&mut self, next: MessageFontSizes,
        roles: Option<asb_interpreter::MessageLayerIds>) -> bool {
        if next == self.message_sizes && roles == self.message_roles { return true; }
        let mut layers: Vec<_> = self.state.layers.values()
            .filter(|l| next.scale(&l.id, roles.as_ref()) != self.message_scale(&l.id)).cloned().collect();
        // Work on copies: a missing original font cannot leave a half-resized page.
        let old_font = self.font.clone(); let old_generation = self.font_generation;
        let ok = (|| {
            for layer in &mut layers {
                let ratio = next.scale(&layer.id, roles.as_ref());
                for glyph in layer.text_buffer.iter_mut().chain(layer.rubies.iter_mut().flat_map(|r| r.glyphs.iter_mut())) {
                    if glyph.character == "\n" { continue; }
                    if glyph.logical_size <= 0.0 { return false; }
                    if glyph.font_generation != self.font_generation {
                        if glyph.font_generation == old_generation { self.font = old_font.clone(); self.font_generation = old_generation; }
                        else if let Some((font, generation)) = self.fonts.values().find(|(_, g)| *g == glyph.font_generation) {
                            self.font = Some(font.clone()); self.font_generation = *generation;
                        } else { return false; }
                    }
                    let Some(c) = glyph.character.chars().next() else { return false; };
                    let Some(replacement) = self.rasterize_message_glyph(c, glyph.logical_size, ratio) else { return false; };
                    *glyph = replacement;
                }
            }
            true
        })();
        self.font = old_font; self.font_generation = old_generation;
        if !ok { return false; }
        for layer in layers { self.state.layers.insert(layer.id.clone(), layer); }
        self.message_sizes = next;
        self.message_roles = roles;
        let enabled = self.layout_cache.borrow().enabled;
        *self.layout_cache.borrow_mut() = LayoutCache::default();
        self.layout_cache.borrow_mut().enabled = enabled;
        let enabled = self.command_cache.enabled;
        self.command_cache = CommandCache::default(); self.command_cache.enabled = enabled;
        self.mark_snapshot_changed();
        true
    }
}
