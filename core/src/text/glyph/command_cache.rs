//! Reuse completed message-layer commands without trusting mutation generations.
use super::*;

const MAX_ENTRIES: usize = 8;
const MAX_COMMANDS: usize = 4096;
const MAX_KEY_BYTES: usize = 256 * 1024;

#[derive(Clone, PartialEq)]
struct LayerKey {
    rect: [f32; 4],
    font: FontDesc,
    glyphs: Vec<GlyphInfo>,
    reveal_index: usize,
    clock: u64,
    hidden: bool,
    animations: Vec<ScetweenConfig>,
    links: Vec<LinkRange>,
    rubies: Vec<RubyRange>,
    open_ruby: Option<(usize, String)>,
    indent_initial: IndentState,
    indent_actions: Vec<IndentAction>,
    indent_options: Option<IndentOptions>,
}

impl LayerKey {
    fn new(layer: &MessageLayer) -> Self {
        Self {
            rect: [layer.left, layer.top, layer.width, layer.height],
            font: layer.font.clone(),
            glyphs: layer.text_buffer.clone(),
            reveal_index: layer.reveal_index,
            clock: layer.reveal_clock_ms,
            hidden: layer.text_hidden,
            animations: layer.scetween.clone(),
            links: layer.links.clone(),
            rubies: layer.rubies.clone(),
            open_ruby: layer.open_ruby.clone(),
            indent_initial: layer.indent_initial.clone(),
            indent_actions: layer.indent_actions.clone(),
            indent_options: layer.indent_options.clone(),
        }
    }

    fn matches(&self, layer: &MessageLayer) -> bool {
        self.rect == [layer.left, layer.top, layer.width, layer.height]
            && self.font == layer.font
            && self.glyphs == layer.text_buffer
            && self.reveal_index == layer.reveal_index
            && self.clock == layer.reveal_clock_ms
            && self.hidden == layer.text_hidden
            && self.animations == layer.scetween
            && self.links == layer.links
            && self.rubies == layer.rubies
            && self.open_ruby == layer.open_ruby
            && self.indent_initial == layer.indent_initial
            && self.indent_actions == layer.indent_actions
            && self.indent_options == layer.indent_options
    }
}

#[derive(Clone, PartialEq)]
pub(super) struct Environment {
    pub font_generation: u64,
    pub layout: TextLayoutConfig,
    pub textures: Vec<Option<(TextureId, TextureInfo)>>,
    pub links_enabled: bool,
    pub white_patch: Option<(usize, u32, u32)>,
}

struct Entry {
    id: String,
    key: LayerKey,
    commands: Vec<DrawCommand>,
    bytes: usize,
}

pub(super) struct CommandCache {
    pub enabled: bool,
    environment: Option<Environment>,
    entries: Vec<Entry>,
    bytes: usize,
    commands: usize,
    #[cfg(test)]
    pub hits: usize,
}

impl Default for CommandCache {
    fn default() -> Self {
        Self {
            enabled: true,
            environment: None,
            entries: Vec::new(),
            bytes: 0,
            commands: 0,
            #[cfg(test)]
            hits: 0,
        }
    }
}

impl CommandCache {
    pub fn prepare(&mut self, environment: Environment, layers: &HashMap<String, MessageLayer>) {
        if self.environment.as_ref() != Some(&environment) {
            self.entries.clear();
            self.bytes = 0;
            self.commands = 0;
            self.environment = Some(environment);
        }
        let mut index = 0;
        while index < self.entries.len() {
            if layers
                .get(&self.entries[index].id)
                .is_none_or(|l| l.text_buffer.is_empty())
            {
                self.remove(index);
            } else {
                index += 1;
            }
        }
    }

    fn remove(&mut self, index: usize) {
        let old = self.entries.remove(index);
        self.bytes -= old.bytes;
        self.commands -= old.commands.len();
    }

    pub fn get(&mut self, id: &str, layer: &MessageLayer) -> Option<Vec<DrawCommand>> {
        if !self.enabled || layer.reveal_pending {
            return None;
        }
        let index = self
            .entries
            .iter()
            .position(|e| e.id == id && e.key.matches(layer))?;
        let entry = self.entries.remove(index);
        let result = entry.commands.clone();
        self.entries.push(entry);
        #[cfg(test)]
        {
            self.hits += 1;
        }
        Some(result)
    }

    pub fn insert(&mut self, id: &str, layer: &MessageLayer, commands: &[DrawCommand]) {
        if !self.enabled || layer.reveal_pending {
            return;
        }
        if let Some(index) = self.entries.iter().position(|e| e.id == id) {
            self.remove(index);
        }
        if commands.len() > MAX_COMMANDS {
            return;
        }
        // Bound owned input memory too; page tags/history/font stacks are not copied.
        let bytes = key_bytes(id, layer);
        if bytes > MAX_KEY_BYTES {
            return;
        }
        while !self.entries.is_empty()
            && (self.entries.len() >= MAX_ENTRIES
                || self.bytes + bytes > MAX_KEY_BYTES
                || self.commands + commands.len() > MAX_COMMANDS)
        {
            self.remove(0);
        }
        self.entries.push(Entry {
            id: id.into(),
            key: LayerKey::new(layer),
            commands: commands.to_vec(),
            bytes,
        });
        self.bytes += bytes;
        self.commands += commands.len();
    }
}

fn key_bytes(id: &str, l: &MessageLayer) -> usize {
    fn glyphs(g: &[GlyphInfo]) -> usize {
        std::mem::size_of_val(g) + g.iter().map(|g| g.character.len()).sum::<usize>()
    }
    fn opt(s: &Option<String>) -> usize {
        s.as_ref().map_or(0, String::len)
    }
    let f = &l.font;
    let font_strings = [
        &f.face,
        &f.ruby_face,
        &f.color,
        &f.outline_color,
        &f.shadow_color,
        &f.style,
        &f.align,
        &f.overflow,
        &f.layer_mode,
    ]
    .into_iter()
    .map(opt)
    .sum::<usize>();
    std::mem::size_of::<LayerKey>()
        + std::mem::size_of_val(l.indent_initial.stack.as_slice())
        + std::mem::size_of_val(l.indent_actions.as_slice())
        + l.indent_actions.iter().map(|a| a.configure.as_ref().map_or(0, |o| o.pair.len())).sum::<usize>()
        + l.indent_options.as_ref().map_or(0, |o| o.pair.len())
        + id.len()
        + glyphs(&l.text_buffer)
        + font_strings
        + f.custom
            .iter()
            .map(|(k, v)| k.len() + v.len() + 2 * std::mem::size_of::<String>())
            .sum::<usize>()
        + l.scetween
            .iter()
            .map(|a| {
                std::mem::size_of::<ScetweenConfig>()
                    + opt(&a.param)
                    + a.random_order
                        .as_ref()
                        .map_or(0, |r| std::mem::size_of_val(r.as_slice()))
            })
            .sum::<usize>()
        + l.links
            .iter()
            .map(|k| {
                std::mem::size_of::<LinkRange>()
                    + opt(&k.file)
                    + opt(&k.label)
                    + opt(&k.color)
                    + opt(&k.shadow_color)
                    + opt(&k.outline_color)
            })
            .sum::<usize>()
        + l.rubies
            .iter()
            .map(|r| std::mem::size_of::<RubyRange>() + r.text.len() + glyphs(&r.glyphs))
            .sum::<usize>()
        + l.open_ruby.as_ref().map_or(0, |(_, s)| s.len())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn limits_eviction_and_direct_edits_do_not_return_stale_commands() {
        let mut cache = CommandCache::default();
        let mut layer = MessageLayer::new("a".into());
        cache.insert("a", &layer, &[]);
        assert!(cache.get("a", &layer).is_some());
        layer.font.color = Some("ff0000".into());
        assert!(cache.get("a", &layer).is_none());
        cache.insert("a", &layer, &[]);
        layer.reveal_pending = true;
        assert!(cache.get("a", &layer).is_none());
        layer.reveal_pending = false;
        cache.enabled = false;
        assert!(cache.get("a", &layer).is_none());
        cache.enabled = true;
        assert!(cache.get("a", &layer).is_some());
        for i in 0..20 {
            cache.insert(&i.to_string(), &layer, &[]);
        }
        assert_eq!(cache.entries.len(), MAX_ENTRIES);
        assert!(cache.get("a", &layer).is_none());
        layer
            .font
            .custom
            .insert("large".into(), "x".repeat(MAX_KEY_BYTES));
        cache.insert("large", &layer, &[]);
        assert!(cache.get("large", &layer).is_none());
        assert!(cache.bytes <= MAX_KEY_BYTES);
    }

    struct Provider {
        id: u64,
        available: bool,
    }
    impl TextureProvider for Provider {
        fn resolve(&mut self, _: &str) -> Option<(TextureId, TextureInfo)> {
            self.available.then_some((
                TextureId(self.id),
                TextureInfo {
                    width: ATLAS_SZ,
                    height: ATLAS_SZ,
                },
            ))
        }
        fn upload_rgba(
            &mut self,
            name: &str,
            _: u32,
            _: u32,
            _: &[u8],
        ) -> Option<(TextureId, TextureInfo)> {
            self.resolve(name)
        }
    }

    fn fixture() -> (GlyphTextRenderer, Provider) {
        let mut r = GlyphTextRenderer::new();
        r.set_font_owned(
            std::fs::read(std::env::var("ART3M1S_TEST_FONT").expect("set font fixture")).unwrap(),
        )
        .unwrap();
        let glyphs = "很长的句子，用于测试换行与描边。ABC123「第二行」"
            .chars()
            .filter_map(|c| r.rasterize_glyph(c, 32.0))
            .collect();
        let layer = r.state.active_layer_mut();
        layer.text_buffer = glyphs;
        layer.width = 190.0;
        layer.height = 160.0;
        layer.font.size = Some(32.0);
        layer.font.outline_size = Some(1.0);
        layer.font.shadow_size = Some(2.0);
        layer.reveal_pending = false;
        layer.reveal_index = layer.text_buffer.len();
        (
            r,
            Provider {
                id: 10,
                available: true,
            },
        )
    }

    fn compare(r: &mut GlyphTextRenderer, p: &mut Provider) {
        r.command_cache.enabled = true;
        let actual = r.build_text_commands(p);
        r.command_cache.enabled = false;
        let expected = r.build_text_commands(p);
        assert_eq!(actual, expected);
        r.command_cache.enabled = true;
    }

    #[test]
    #[ignore = "requires ART3M1S_TEST_FONT pointing to a local font fixture"]
    fn completed_commands_match_original_across_mutations_and_animation() {
        let (mut r, mut p) = fixture();
        compare(&mut r, &mut p);
        compare(&mut r, &mut p);
        assert!(r.command_cache.hits > 0);
        for i in 0..80 {
            let l = r.state.active_layer_mut();
            match i % 16 {
                0 => l.left += 3.0,
                1 => l.top += 2.0,
                2 => l.width += 19.0,
                3 => l.height += 11.0,
                4 => l.font.color = Some(format!("{:06x}", i * 701)),
                5 => l.font.entire_alpha = Some(180 + i as u8),
                6 => l.font.entire_rotate = Some(i as f32),
                7 => l.font.entire_xscale = Some(80.0 + i as f32),
                8 => l.font.outline_size = Some((i % 4) as f32),
                9 => l.font.shadow_size = Some((i % 3) as f32),
                10 => l.font.align = Some("right".into()),
                11 => l.text_buffer[0].atlas_x += 1.0,
                12 => l.text_buffer[0].advance_x += 1.0,
                13 => l.text_buffer[0].character = format!("{i}"),
                14 => l.font.entire_anchorx = Some(i as f32),
                _ => l.text_buffer[0].offset_y += 1.0,
            }
            compare(&mut r, &mut p);
            compare(&mut r, &mut p);
        }
        r.state.layout.wordparts.clear();
        compare(&mut r, &mut p);
        r.font_generation += 1;
        compare(&mut r, &mut p);
        p.id = 99;
        compare(&mut r, &mut p);
        p.available = false;
        compare(&mut r, &mut p);
        p.available = true;
        compare(&mut r, &mut p);
        let ruby = r.rasterize_glyph('a', 16.0).unwrap();
        r.state.active_layer_mut().rubies.push(RubyRange {
            start: 0,
            end: 2,
            text: "a".into(),
            size: 16.0,
            glyphs: vec![ruby],
        });
        compare(&mut r, &mut p);
        r.state.active_layer_mut().open_ruby = Some((3, "open".into()));
        compare(&mut r, &mut p);
        r.state.links_enabled = true;
        r.state.active_layer_mut().links.push(LinkRange {
            start: 0,
            end: Some(3),
            file: None,
            label: None,
            link_type: 1,
            color: Some("ff0000".into()),
            shadow_color: None,
            outline_color: None,
            hovered: true,
        });
        compare(&mut r, &mut p);
        compare(&mut r, &mut p);
        r.state.active_layer_mut().links[0].link_type = 0;
        compare(&mut r, &mut p);
        compare(&mut r, &mut p);
        r.state.links_enabled = false;
        compare(&mut r, &mut p);
        r.state.active_layer_mut().scetween.push(ScetweenConfig {
            time_per_char: 100,
            delay_per_char: 5,
            param: Some("alpha".into()),
            diff: Some(255.0),
            ..Default::default()
        });
        r.show_text();
        for _ in 0..50 {
            r.advance_reveal(20);
            compare(&mut r, &mut p);
        }
        r.hide_text();
        for _ in 0..50 {
            r.advance_reveal(20);
            compare(&mut r, &mut p);
        }
        r.reveal_all();
        compare(&mut r, &mut p);
        r.state.active_layer_mut().clear_page();
        compare(&mut r, &mut p);
    }

    #[test]
    #[ignore = "font fixture and opt-in microbenchmark; not Vita FPS"]
    fn command_cache_benchmark() {
        let (mut r, mut p) = fixture();
        let glyphs = r.state.active_layer_mut().text_buffer.clone();
        for count in [40, 120, 360] {
            let l = r.state.active_layer_mut();
            l.text_buffer = glyphs.iter().cloned().cycle().take(count).collect();
            l.reveal_index = count;
            for enabled in [false, true] {
                r.command_cache.enabled = enabled;
                r.build_text_commands(&mut p);
                let started = std::time::Instant::now();
                for _ in 0..2000 {
                    std::hint::black_box(r.build_text_commands(&mut p));
                }
                eprintln!(
                    "glyphs={count} cache={enabled} ns={}",
                    started.elapsed().as_nanos() / 2000
                );
            }
        }
    }
}
