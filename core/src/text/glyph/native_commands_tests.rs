use super::*;

fn glyphs(text: &str) -> Vec<GlyphInfo> {
    text.chars().map(|c| GlyphInfo {
        logical_size: 10.0, font_generation: 0, character: c.to_string(), texture_id: TextureId(0),
        atlas_x: 0.0, atlas_y: 0.0, atlas_w: 10.0, atlas_h: 20.0,
        offset_x: 0.0, offset_y: 0.0, width: if c == '\n' { 0.0 } else { 10.0 },
        height: 20.0, advance_x: if c == '\n' { 0.0 } else { 10.0 },
    }).collect()
}
fn config() -> TextLayoutConfig {
    TextLayoutConfig { indent_pair: "（）「」".into(), indent_nest: true,
        prohibit_head: String::new(), prohibit_foot: String::new(), wordparts: String::new(),
        ..Default::default() }
}

#[test]
#[ignore = "requires ART3M1S_TEST_FONT pointing to a local font fixture"]
fn appended_text_and_line_break_do_not_leave_auto_waiting_forever() {
    let mut r = GlyphTextRenderer::new();
    r.set_font_bytes(std::fs::read(std::env::var("ART3M1S_TEST_FONT").unwrap()).unwrap()).unwrap();
    r.push_text("first", false);
    r.advance_reveal(100);
    assert!(r.is_reveal_complete());
    r.push_text(" second", false);
    r.advance_reveal(100);
    assert!(r.is_reveal_complete(), "appending to a completed page must reach completion again");
    let clock = r.state.active_layer_mut().reveal_clock_ms;
    r.push_line_break();
    assert!(r.is_reveal_complete(), "a structural newline must not restart completed text");
    assert_eq!(r.state.active_layer_mut().reveal_clock_ms, clock);
}

#[test]
fn changing_indent_midpage_preserves_old_layout_and_applies_new_pair_forward() {
    let mut r = GlyphTextRenderer::new();
    r.switch_message_layer(Some("body"), false);
    r.state.configure_indent("「」", None, true, false);
    r.state.active_layer_mut().text_buffer = glyphs("「ab\nc");
    let layout = |r: &GlyphTextRenderer| {
        let l = &r.state.layers["body"];
        r.layout_cache.borrow_mut().layout_indented(&l.text_buffer, 200.0, &r.state.layout,
            &[], TextAlignment::Left, l.indent_options.as_ref(), &l.indent_initial, &l.indent_actions)
    };
    let before = layout(&r);
    r.state.configure_indent("『』", None, true, false);
    r.state.active_layer_mut().text_buffer.extend(glyphs("\n『d\ne"));
    let after = layout(&r);
    assert_eq!(&after.positions[..5], &before.positions[..]);
    assert_eq!(after.positions[6].x, 10.0);
    assert_eq!(after.positions[9].x, 20.0);
    r.push_page_break(Some(0));
    assert_eq!(r.state.layers["body"].indent_options.as_ref().unwrap().pair, "『』");
    // A later font/advance change must not scale inherited indent pixels.
    let mut next = glyphs("e\ne");
    for glyph in &mut next { glyph.advance_x *= 2.0; glyph.width *= 2.0; }
    r.state.active_layer_mut().text_buffer = next;
    let page = layout(&r);
    assert_eq!(page.positions[0].x, 20.0);
    assert_eq!(page.positions[2].x, 20.0);
}

#[test]
fn zero_range_is_unlimited_and_logical_range_survives_auto_wrap() {
    let text = glyphs("aaaa（bbbbb");
    let mut cfg = config(); cfg.indent_range = Some(0);
    let unrestricted = layout_glyphs(&text, 40.0, &cfg, &[]);
    cfg.indent_range = None;
    assert_eq!(unrestricted, layout_glyphs(&text, 40.0, &cfg, &[]));
    cfg.indent_range = Some(3);
    assert_eq!(layout_glyphs(&text, 40.0, &cfg, &[])[8].x, 10.0);
    cfg.indent_logical_range = true;
    assert_eq!(layout_glyphs(&text, 40.0, &cfg, &[])[8].x, 0.0);
    let explicit = glyphs("aaaa\n（bbbbb");
    assert_eq!(layout_glyphs(&explicit, 40.0, &cfg, &[])[9].x, 10.0);
}

#[test]
fn pop_and_clear_apply_at_the_command_boundary_without_moving_old_text() {
    let text = glyphs("（a「b\nc\nd");
    let base = layout_glyphs(&text, 200.0, &config(), &[]);
    for (count, expected) in [(0,30.0),(1,10.0),(2,0.0),(99,0.0),(-1,0.0),(-2,0.0)] {
        let (got, _) = layout_glyphs_indented(&text, 200.0, &config(), &[], &IndentState::default(),
            &[IndentAction { at: 5, unindent: count, configure: None }]);
        assert_eq!(&got[..5], &base[..5]);
        assert_eq!(got[5].x, expected, "count={count}");
        assert_eq!(got[7].x, expected, "next line count={count}");
    }
}

#[test]
fn midline_pop_changes_next_line_but_not_current_cursor() {
    let text = glyphs("（ab\nc");
    let (got, end) = layout_glyphs_indented(&text, 100.0, &config(), &[], &IndentState::default(),
        &[IndentAction { at: 2, unindent: -1, configure: None }]);
    assert_eq!(got[2].x, 20.0);
    assert_eq!(got[4].x, 0.0);
    assert!(end.stack.is_empty());
}

#[test]
fn rp_keeps_indent_until_explicit_unindent_and_layers_are_independent() {
    let mut r = GlyphTextRenderer::new();
    r.switch_message_layer(Some("body"), false);
    r.state.configure_indent("（）", None, true, true);
    r.state.active_layer_mut().text_buffer = glyphs("（abc");
    r.push_page_break(Some(0));
    assert_eq!(r.state.layers["body"].indent_initial.x, 10.0);
    assert_eq!(r.state.layers["body"].indent_initial.stack.len(), 1);
    r.switch_message_layer(Some("name"), true);
    r.state.configure_indent("「」", Some(2), false, false);
    r.modify_indent(-1);
    assert_eq!(r.state.layers["body"].indent_initial.x, 10.0);
    r.pop_message_layer();
    r.modify_indent(-1);
    r.state.active_layer_mut().text_buffer = glyphs("abc");
    let body = &r.state.layers["body"];
    let got = r.layout_cache.borrow_mut().layout_indented(&body.text_buffer, 100.0, &r.state.layout,
        &[], TextAlignment::Left, body.indent_options.as_ref(), &body.indent_initial, &body.indent_actions);
    assert_eq!(got.positions[0].x, 0.0);
    assert!(got.final_indent.stack.is_empty());
    assert_eq!(body.indent_options.as_ref().unwrap().pair, "（）");
    let tags = r.state.get_message_tags("body", false).unwrap();
    assert!(tags.iter().any(|t| t.contains("__art3_indent_state")));
    assert!(tags.iter().any(|t| t == "[indentmodify unindent=\"-1\"]"));
    r.clear_scene_text();
    assert!(r.state.layers["body"].indent_initial.stack.is_empty());
}

#[test]
fn changed_indent_invalidates_layout_and_command_cache() {
    let mut cache = LayoutCache::default();
    let text = glyphs("（abc\ndef"); let cfg = config(); let initial = IndentState::default();
    let a = cache.layout_indented(&text, 60.0, &cfg, &[], TextAlignment::Left, None, &initial, &[]);
    let b = cache.layout_indented(&text, 60.0, &cfg, &[], TextAlignment::Left, None, &initial, &[]);
    assert!(std::sync::Arc::ptr_eq(&a.positions, &b.positions));
    let edits = [IndentAction { at: 5, unindent: -1, configure: None }];
    let c = cache.layout_indented(&text, 60.0, &cfg, &[], TextAlignment::Left, None, &initial, &edits);
    assert!(!std::sync::Arc::ptr_eq(&a.positions, &c.positions));
    assert_ne!(a.positions[5].x, c.positions[5].x);
    cache.enabled = false;
    let d = cache.layout_indented(&text, 60.0, &cfg, &[], TextAlignment::Left, None, &initial, &edits);
    assert_eq!(c.positions, d.positions);
    let mut commands = CommandCache::default(); let mut layer = MessageLayer::new("body".into());
    layer.text_buffer = text;
    commands.insert("body", &layer, &[]);
    assert!(commands.get("body", &layer).is_some());
    layer.indent_actions = edits.to_vec();
    assert!(commands.get("body", &layer).is_none());
}

#[test]
fn reproduction_seed_round_trips_and_rejects_late_or_invalid_payload() {
    let seed = IndentState { stack: vec![('）',0.0),('」',10.0)], x:30.0 };
    let json = serde_json::to_string(&seed).unwrap();
    let mut r = GlyphTextRenderer::new(); r.restore_indent_state(&json);
    assert_eq!(r.state.active_layer_mut().indent_initial, seed);
    r.modify_indent(1);
    r.state.active_layer_mut().text_buffer = glyphs("a\nb");
    r.restore_indent_state("{\"stack\":[],\"x\":0}");
    assert_eq!(r.state.active_layer_mut().indent_initial, seed);
    r.push_page_break(Some(0));
    assert_eq!(r.state.active_layer_mut().indent_initial.x, 10.0);
    r.restore_indent_state("invalid");
    assert_eq!(r.state.active_layer_mut().indent_initial.x, 10.0);
}

#[test]
fn serialized_indent_tags_parse_and_replay_through_the_interpreter() {
    use asb_interpreter::{Interpreter, InterpreterConfig, CallbackResult, ExecutionResult, Event};
    let seed = IndentState { stack: vec![('）', 0.0), ('」', 10.0)], x: 30.0 };
    let tags = [BacklogTag::IndentState(serde_json::to_string(&seed).unwrap()),
        BacklogTag::Indent { pair:"（）「」".into(), range:None, nest:true, logical_range:true },
        BacklogTag::IndentModify(1)];
    let captured = std::sync::Arc::new(std::sync::Mutex::new(Vec::new()));
    let target = captured.clone();
    let mut interpreter = Interpreter::new(InterpreterConfig::default());
    interpreter.set_callback(move |event| { target.lock().unwrap().push(event.clone()); CallbackResult::Continue });
    interpreter.load_script("roundtrip", &format!("*main\n{}\n", tags.iter().map(BacklogTag::to_tag_string).collect::<Vec<_>>().join("\n"))).unwrap();
    interpreter.start("roundtrip", "main").unwrap();
    assert!(matches!(interpreter.run().unwrap(), ExecutionResult::Completed));
    let mut renderer = GlyphTextRenderer::new();
    for event in captured.lock().unwrap().iter() {
        match event {
            Event::RestoreIndentState { data } => renderer.restore_indent_state(data),
            Event::IndentConfig { pair, range, nest, logical_range } => renderer.state.configure_indent(pair,*range,*nest,*logical_range),
            Event::IndentModify { unindent } => renderer.modify_indent(*unindent),
            _ => {}
        }
    }
    assert_eq!(renderer.state.active_layer_mut().indent_initial, seed, "events: {:?}", captured.lock().unwrap());
    renderer.state.active_layer_mut().text_buffer = glyphs("a\nb");
    renderer.push_page_break(Some(0));
    assert_eq!(renderer.state.active_layer_mut().indent_initial.x,10.0);
}
