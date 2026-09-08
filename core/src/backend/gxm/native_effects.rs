//! Direct GXM built-in effects bridge. Opt-in so the legacy host ABI is intact.
use crate::render_pipeline::{draw::*, shader::*};

#[repr(C)]
#[derive(Clone, Copy)]
struct Effects {
    flags: [f32; 4],
    transition: [f32; 4],
    corners: [f32; 16],
    uv_rect: [f32; 4],
    model_clip: [f32; 4],
    wipe: [f32; 4],
    model_x: [f32; 4],
    model_y: [f32; 4],
}
#[repr(C)]
struct EffectDraw {
    texture: u64,
    mask: u64,
    transform: [f32; 6],
    quad: [f32; 2],
    uv: [f32; 4],
    tint: [f32; 4],
    clip: [f32; 4],
    effects: Effects,
    blend: u32,
    has_clip: u32,
    mesh: *const [f32; 4],
    mesh_count: usize,
}
unsafe extern "C" {
    fn art3m1s_gxm_draw_effect(draw: *const EffectDraw);
    fn art3m1s_gxm_group_begin() -> i32;
    fn art3m1s_gxm_group_mask_begin() -> i32;
    fn art3m1s_gxm_group_end(draw: *const EffectDraw);
}
fn blend_code(blend: BlendMode) -> u32 {
    match blend {
        BlendMode::Alpha => 0,
        BlendMode::Add => 1,
        BlendMode::Multiply => 2,
        BlendMode::Screen => 3,
        BlendMode::NativeReverseSubtract => 4,
        BlendMode::PremultipliedAlpha => 5,
        BlendMode::PremultipliedAdd => 6,
        BlendMode::NativeAdd => 7,
        BlendMode::NativeMultiply => 8,
        BlendMode::NativeScreen => 9,
    }
}
fn scalar(effect: &ShaderEffect, name: &str, default: f32) -> f32 {
    effect
        .uniforms
        .get(name)
        .and_then(|v| v.first())
        .copied()
        .filter(|v| v.is_finite())
        .unwrap_or(default)
}
fn kind(effect: Option<&ShaderEffect>) -> u32 {
    match effect.map(|e| e.name.as_str()) {
        Some(RULE_TRANS_SHADER) => 1,
        Some(ALPHA_MASK_SHADER) => 2,
        Some(GROUP_COMPOSITE_SHADER) => 3,
        _ => 0,
    }
}
fn encode(cmd: &DrawCommand, width: u32, height: u32) -> Option<EffectDraw> {
    let clip = super::stage_clip(cmd.clip_bounds, width, height).ok()?;
    let m = cmd.transform.matrix2;
    let t = cmd.transform.translation;
    let mut draw = EffectDraw {
        texture: cmd.texture.0,
        mask: cmd
            .shader
            .as_ref()
            .and_then(|e| e.mask_texture)
            .map_or(0, |t| t.0),
        transform: [m.x_axis.x, m.x_axis.y, m.y_axis.x, m.y_axis.y, t.x, t.y],
        quad: cmd.clip.quad_size,
        uv: [
            cmd.clip.uv_offset[0],
            cmd.clip.uv_offset[1],
            cmd.clip.uv_scale[0],
            cmd.clip.uv_scale[1],
        ],
        tint: [
            cmd.color.multiply[0],
            cmd.color.multiply[1],
            cmd.color.multiply[2],
            cmd.opacity,
        ],
        clip: clip.unwrap_or_default(),
        has_clip: u32::from(clip.is_some()),
        blend: blend_code(cmd.blend),
        mesh: std::ptr::null(),
        mesh_count: 0,
        effects: Effects {
            flags: [
                kind(cmd.shader.as_ref()) as f32,
                u32::from(cmd.color.grayscale) as f32,
                u32::from(cmd.color.negative) as f32,
                0.0,
            ],
            transition: [0.0, 1.0 / 255.0, 0.0, 0.0],
            corners: [0.0; 16],
            uv_rect: [0.0, 0.0, 1.0, 1.0],
            model_clip: [0.0, 0.0, 1.0, 1.0],
            wipe: [0.0; 4],
            model_x: [0.0; 4],
            model_y: [0.0; 4],
        },
    };
    if let Some(mesh) = &cmd.mesh {
        draw.mesh = mesh.vertices.as_ptr();
        draw.mesh_count = mesh.vertices.len();
    }
    if let Some(e) = &cmd.shader {
        if kind(Some(e)) != 0 {
            draw.tint[3] = scalar(e, "alpha", draw.tint[3]);
            if let Some(v) = e.uniforms.get("colorMultiply").filter(|v| v.len() >= 3) {
                draw.tint[..3].copy_from_slice(&v[..3]);
            }
            draw.effects.transition = [
                scalar(e, "progress", 0.0),
                scalar(e, "vague", 1.0 / 255.0),
                scalar(e, "opaque", 0.0),
                0.0,
            ];
            // These names exist on group-composite, not rule/alpha-mask.
            if kind(Some(e)) == 3 {
                draw.effects.flags[1] = scalar(e, "grayscale", 0.0);
                draw.effects.flags[2] = scalar(e, "negative", 0.0);
            }
        }
    }
    if let Some(e) = &cmd.native_emote {
        draw.effects.flags[3] = 1.0;
        for (i, c) in e.corner_colors.iter().enumerate() {
            draw.effects.corners[i * 4..i * 4 + 4].copy_from_slice(c);
        }
        draw.effects.uv_rect = e.uv_rect;
        draw.effects.model_clip = e.clip_rect;
        draw.effects.wipe = [e.wipe[0], e.wipe[1], e.wipe[2], (e.blend_mode & 15) as f32];
        draw.effects.model_x[3] = u32::from((e.blend_mode & 0xf0) == 0x10) as f32;
    }
    Some(draw)
}
fn next_group(
    frame: &DrawList,
    start: usize,
    end: usize,
    limit: usize,
) -> Option<(usize, &ShaderGroup)> {
    frame
        .shader_groups
        .iter()
        .enumerate()
        .take(limit)
        .filter(|(_, g)| g.start == start && g.end > start && g.end <= end)
        .max_by_key(|(i, g)| (g.end, *i))
}
fn group_command(group: &ShaderGroup, width: u32, height: u32) -> DrawCommand {
    let blend = if group.effect.name == GROUP_COMPOSITE_SHADER {
        match scalar(&group.effect, "blendMode", 0.0) as i32 {
            1 => BlendMode::PremultipliedAdd,
            2 => BlendMode::Screen,
            3 => BlendMode::Multiply,
            _ => BlendMode::PremultipliedAlpha,
        }
    } else {
        BlendMode::PremultipliedAlpha
    };
    let size = TextureInfo { width, height };
    DrawCommand {
        texture: TextureId(0),
        size,
        transform: glam::Affine2::IDENTITY,
        opacity: 1.0,
        blend,
        color: ColorFilter::default(),
        clip: ClipRect::full(size),
        clip_bounds: group.clip_bounds,
        shader: Some(group.effect.clone()),
        mesh: None,
        stencil: None,
        native_emote: None,
    }
}
fn render_range(frame: &DrawList, start: usize, end: usize, limit: usize, width: u32, height: u32) {
    let mut index = start;
    while index < end {
        if let Some((group_index, group)) = next_group(frame, index, end, limit) {
            let Some(draw) = encode(&group_command(group, width, height), width, height) else {
                index = group.end;
                continue;
            };
            if unsafe { art3m1s_gxm_group_begin() } != 0 {
                render_range(frame, group.start, group.end, group_index, width, height);
                if let Some([start, end]) = group.mask_range {
                    if unsafe { art3m1s_gxm_group_mask_begin() } != 0 {
                        for cmd in frame.mask_commands.get(start..end).unwrap_or_default() {
                            if let Some(draw) = encode(cmd, width, height) {
                                unsafe { art3m1s_gxm_draw_effect(&draw) };
                            }
                        }
                    } else {
                        crate::core_warn!("GXM mask target allocation failed");
                    }
                }
                unsafe { art3m1s_gxm_group_end(&draw) };
            } else {
                crate::core_warn!("GXM group target allocation failed: {}", group.effect.name);
                render_range(frame, group.start, group.end, group_index, width, height);
            }
            index = group.end;
        } else {
            if let Some(draw) = encode(&frame.commands[index], width, height) {
                unsafe { art3m1s_gxm_draw_effect(&draw) };
            }
            index += 1;
        }
    }
}
pub(super) fn render(frame: &DrawList, width: u32, height: u32) {
    unsafe { super::art3m1s_gxm_frame_begin(width, height) };
    render_range(
        frame,
        0,
        frame.commands.len(),
        frame.shader_groups.len(),
        width,
        height,
    );
    unsafe { super::art3m1s_gxm_frame_end() };
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    thread_local! { static EVENTS: RefCell<Vec<String>> = const { RefCell::new(Vec::new()) }; }
    fn event(s: String) {
        EVENTS.with(|v| v.borrow_mut().push(s));
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_frame_begin(_: u32, _: u32) {
        event("frame".into());
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_frame_end() {
        event("end-frame".into());
    }
    #[unsafe(no_mangle)]
    unsafe extern "C" fn art3m1s_gxm_draw_effect(draw: *const EffectDraw) {
        event(format!("draw:{}", unsafe { (*draw).texture }));
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_group_begin() -> i32 {
        event("begin-group".into());
        1
    }
    #[unsafe(no_mangle)]
    extern "C" fn art3m1s_gxm_group_mask_begin() -> i32 {
        event("begin-mask".into());
        1
    }
    #[unsafe(no_mangle)]
    unsafe extern "C" fn art3m1s_gxm_group_end(draw: *const EffectDraw) {
        event(format!("end-group:{}", unsafe { (*draw).effects.flags[0] }));
    }
    #[test]
    fn blend_modes_remain_distinct_and_effect_layout_matches_c() {
        let modes = [
            BlendMode::Alpha,
            BlendMode::Add,
            BlendMode::Multiply,
            BlendMode::Screen,
            BlendMode::NativeReverseSubtract,
            BlendMode::PremultipliedAlpha,
            BlendMode::PremultipliedAdd,
            BlendMode::NativeAdd,
            BlendMode::NativeMultiply,
            BlendMode::NativeScreen,
        ];
        for (i, m) in modes.iter().enumerate() {
            assert_eq!(blend_code(*m), i as u32);
        }
        assert_eq!(std::mem::size_of::<Effects>(), 176);
        assert_eq!(std::mem::offset_of!(EffectDraw, effects), 96);
        assert_eq!(std::mem::offset_of!(EffectDraw, blend), 272);
    }
    #[test]
    fn group_uniforms_override_color_and_keep_premultiplied_blend() {
        let effect = ShaderEffect {
            name: GROUP_COMPOSITE_SHADER.into(),
            uniforms: [
                ("alpha".into(), vec![0.4]),
                ("grayscale".into(), vec![1.0]),
                ("negative".into(), vec![1.0]),
                ("opaque".into(), vec![1.0]),
                ("blendMode".into(), vec![1.0]),
            ]
            .into(),
            mask_texture: Some(TextureId(42)),
            user_texture: None,
        };
        let group = ShaderGroup {
            key: None,
            start: 0,
            end: 1,
            effect,
            clip_bounds: None,
            mask_range: None,
        };
        let draw = encode(&group_command(&group, 960, 544), 960, 544).unwrap();
        assert_eq!(draw.blend, 6);
        assert_eq!(draw.mask, 42);
        assert_eq!(draw.tint[3], 0.4);
        assert_eq!(draw.effects.flags, [3.0, 1.0, 1.0, 0.0]);
        assert_eq!(draw.effects.transition[2], 1.0);
    }
    fn test_group(kind: &str, start: usize, end: usize) -> ShaderGroup {
        ShaderGroup {
            key: None,
            start,
            end,
            clip_bounds: None,
            mask_range: None,
            effect: ShaderEffect {
                name: kind.into(),
                uniforms: Default::default(),
                mask_texture: None,
                user_texture: None,
            },
        }
    }
    #[test]
    fn nested_groups_masks_and_following_sprites_keep_draw_order() {
        let inner = test_group(GROUP_COMPOSITE_SHADER, 0, 1);
        let mut outer = test_group(ALPHA_MASK_SHADER, 0, 2);
        outer.mask_range = Some([0, 1]);
        let mut frame = DrawList::new();
        for id in [10, 20, 30] {
            let mut c = group_command(&inner, 960, 544);
            c.texture = TextureId(id);
            c.shader = None;
            frame.push(c);
        }
        let mut mask = frame.commands[0].clone();
        mask.texture = TextureId(99);
        frame.mask_commands.push(mask);
        frame.shader_groups = vec![inner, outer];
        EVENTS.with(|v| v.borrow_mut().clear());
        render(&frame, 960, 544);
        EVENTS.with(|v| {
            assert_eq!(
                *v.borrow(),
                [
                    "frame",
                    "begin-group",
                    "begin-group",
                    "draw:10",
                    "end-group:3",
                    "draw:20",
                    "begin-mask",
                    "draw:99",
                    "end-group:2",
                    "draw:30",
                    "end-frame"
                ]
            )
        });
    }
    #[test]
    fn mesh_and_native_emote_parameters_cross_the_ffi_intact() {
        let mut cmd = group_command(&test_group(SPRITE_SHADER, 0, 1), 960, 544);
        cmd.mesh = Some(DrawMesh {
            vertices: std::sync::Arc::from([[0., 0., 0., 0.], [1., 0., 1., 0.], [0., 1., 0., 1.]]),
        });
        cmd.native_emote = Some(NativeEmoteMaterial {
            corner_colors: [[0.2, 0.3, 0.4, 0.5]; 4],
            uv_rect: [0.1, 0.2, 0.7, 0.8],
            blend_mode: 0x13,
            clip_rect: [0.1, 0.1, 0.9, 0.9],
            wipe: [2., -0.2, 1.],
        });
        let draw = encode(&cmd, 960, 544).unwrap();
        assert_eq!(draw.mesh_count, 3);
        assert_eq!(draw.mesh, cmd.mesh.as_ref().unwrap().vertices.as_ptr());
        assert_eq!(draw.effects.flags[3], 1.);
        assert_eq!(draw.effects.wipe, [2., -0.2, 1., 3.]);
        assert_eq!(draw.effects.model_x[3], 1.);
        assert_eq!(&draw.effects.corners[..4], &[0.2, 0.3, 0.4, 0.5]);
    }

    #[test]
    fn grayscale_toggle_preserves_texture_alpha_and_tint_at_ffi_boundary() {
        let mut cmd = group_command(&test_group(SPRITE_SHADER, 0, 1), 960, 544);
        cmd.shader = None;
        cmd.texture = TextureId(125);
        cmd.opacity = 0.35;
        cmd.color.multiply = [0.6, 0.8, 0.4];
        for (gray, negative) in [(true, false), (false, false), (true, true), (false, false)] {
            cmd.color.grayscale = gray;
            cmd.color.negative = negative;
            let draw = encode(&cmd, 960, 544).unwrap();
            assert_eq!(draw.texture, 125);
            assert_eq!(draw.tint, [0.6, 0.8, 0.4, 0.35]);
            assert_eq!(draw.effects.flags, [0.0, u32::from(gray) as f32, u32::from(negative) as f32, 0.0]);
        }
    }
}
