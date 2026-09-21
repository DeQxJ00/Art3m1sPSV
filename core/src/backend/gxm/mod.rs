//! Host-rendered GXM backend.
//!
//! The Direct Vita host owns the GXM context. Rust keeps texture identity and
//! CPU metadata, then submits uploads and draw commands through the host ABI.
//! The host controls scene boundaries and GPU resource synchronization.

mod provider;
#[cfg(feature = "gxm-builtin-effects")]
mod native_effects;
#[cfg(feature = "gxm-builtin-effects")]
mod external_effects;
#[cfg(feature = "gxm-builtin-effects")]
mod bundled_effects;

pub use provider::GxmTextureProvider;
#[cfg(target_os = "vita")]
pub(crate) use provider::PreparedPixels;

use crate::render_pipeline::draw::{BlendMode, DrawList, Renderer};

unsafe extern "C" {
    fn art3m1s_gxm_frame_begin(stage_width: u32, stage_height: u32);
    fn art3m1s_gxm_draw_texture(
        texture: u64,
        texture_width: u32,
        texture_height: u32,
        a: f32,
        b: f32,
        c: f32,
        d: f32,
        tx: f32,
        ty: f32,
        quad_width: f32,
        quad_height: f32,
        uv_x: f32,
        uv_y: f32,
        uv_width: f32,
        uv_height: f32,
        opacity: f32,
        blend: u32,
        color_r: f32,
        color_g: f32,
        color_b: f32,
        grayscale: i32,
        negative: i32,
        rule_texture: u64,
        rule_progress: f32,
        rule_vague: f32,
        has_clip: i32,
        clip_x: f32,
        clip_y: f32,
        clip_width: f32,
        clip_height: f32,
    );
    fn art3m1s_gxm_frame_end();
}

pub struct GxmRenderer {
    #[cfg(feature = "gxm-builtin-effects")]
    retained_group: native_effects::RetainedGroups,
    stage_width: u32,
    stage_height: u32,
}

impl GxmRenderer {
    pub fn new(stage_width: u32, stage_height: u32) -> Result<Self, String> {
        Ok(Self { stage_width, stage_height,
            #[cfg(feature = "gxm-builtin-effects")]
            retained_group: native_effects::RetainedGroups::default(),
        })
    }

    pub fn set_viewport_size(&mut self, _width: u32, _height: u32) {}

    pub fn set_stage_size(&mut self, width: u32, height: u32) {
        self.stage_width = width;
        self.stage_height = height;
    }

    pub fn register_hlsl_shader(&mut self, id: &str, source: &[u8]) -> Result<(), String> {
        self.register_hlsl_shader_at(id,id,source)
    }
    pub fn register_hlsl_shader_at(&mut self,id:&str,file:&str,source:&[u8])->Result<(),String>{
        #[cfg(feature="gxm-builtin-effects")]{
            external_effects::register_source_at(id,file,source)?;self.retained_group=Default::default();Ok(())
        }
        #[cfg(not(feature="gxm-builtin-effects"))]{let _=source;Err(format!("GXM external shader backend disabled: {id}"))}
    }

    #[cfg(feature = "gxm-builtin-effects")]
    pub fn register_external_shader(&mut self,id:&str,source:&[u8],package:&[u8])->Result<(),String>{
        external_effects::register(id,source,package)?;
        self.retained_group=Default::default();
        Ok(())
    }
    pub fn set_profile_enabled(&self, _enabled: bool) {}

    pub fn take_profile_stats(&self) -> crate::backend::gl::RenderProfile {
        crate::backend::gl::RenderProfile::default()
    }
}

fn blend_code(mode: BlendMode) -> u32 {
    match mode {
        BlendMode::Alpha | BlendMode::PremultipliedAlpha => 0,
        BlendMode::Add | BlendMode::PremultipliedAdd | BlendMode::NativeAdd => 1,
        BlendMode::Multiply | BlendMode::NativeMultiply => 2,
        BlendMode::Screen | BlendMode::NativeScreen => 3,
        BlendMode::NativeReverseSubtract => 4,
    }
}

fn rule_parameters(effect: Option<&crate::render_pipeline::draw::ShaderEffect>) -> (u64, f32, f32) {
    let Some(effect) = effect.filter(|e| e.name == crate::render_pipeline::shader::RULE_TRANS_SHADER) else {
        return (0, 0.0, 1.0 / 255.0);
    };
    let Some(mask) = effect.mask_texture else { return (0, 0.0, 1.0 / 255.0); };
    let scalar = |name: &str, default: f32| {
        effect.uniforms.get(name).and_then(|values| values.first()).copied()
            .filter(|v| v.is_finite()).unwrap_or(default)
    };
    (mask.0, scalar("progress", 0.0).clamp(0.0, 1.0), scalar("vague", 1.0 / 255.0).max(1.0 / 255.0))
}

// clip_bounds is already in stage coordinates, not layer-local coordinates.
// Match the GL path: intersect with the stage and omit empty/invalid draws.
fn stage_clip(bounds: Option<[f32; 4]>, width: u32, height: u32) -> Result<Option<[f32; 4]>, ()> {
    let Some(rect) = bounds else { return Ok(None); };
    if !rect.iter().all(|v| v.is_finite()) { return Err(()); }
    let x0 = rect[0].max(0.0);
    let y0 = rect[1].max(0.0);
    let x1 = (rect[0] + rect[2]).min(width as f32);
    let y1 = (rect[1] + rect[3]).min(height as f32);
    if x1 <= x0 || y1 <= y0 { return Err(()); }
    Ok(Some([x0, y0, x1 - x0, y1 - y0]))
}

impl Renderer for GxmRenderer {
    fn render(&mut self, frame: &DrawList) {
        #[cfg(feature = "gxm-builtin-effects")]
        { native_effects::render_cached(frame, self.stage_width, self.stage_height, &mut self.retained_group); }
        #[cfg(not(feature = "gxm-builtin-effects"))]
        {
        unsafe { art3m1s_gxm_frame_begin(self.stage_width, self.stage_height) };
        for command in &frame.commands {
            let Ok(clip) = stage_clip(command.clip_bounds, self.stage_width, self.stage_height) else { continue; };
            let bounds = clip.unwrap_or([0.0; 4]);
            let matrix = command.transform.matrix2;
            let translation = command.transform.translation;
            let (rule_texture, rule_progress, rule_vague) = rule_parameters(command.shader.as_ref());
            unsafe {
                art3m1s_gxm_draw_texture(
                    command.texture.0,
                    command.size.width,
                    command.size.height,
                    matrix.x_axis.x,
                    matrix.x_axis.y,
                    matrix.y_axis.x,
                    matrix.y_axis.y,
                    translation.x,
                    translation.y,
                    command.clip.quad_size[0],
                    command.clip.quad_size[1],
                    command.clip.uv_offset[0],
                    command.clip.uv_offset[1],
                    command.clip.uv_scale[0],
                    command.clip.uv_scale[1],
                    command.opacity,
                    blend_code(command.blend),
                    command.color.multiply[0],
                    command.color.multiply[1],
                    command.color.multiply[2],
                    i32::from(command.color.grayscale),
                    i32::from(command.color.negative),
                    rule_texture,
                    rule_progress,
                    rule_vague,
                    i32::from(clip.is_some()),
                    bounds[0], bounds[1], bounds[2], bounds[3],
                );
            }
        }
        unsafe { art3m1s_gxm_frame_end() };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::render_pipeline::draw::{ShaderEffect, TextureId};

    #[test]
    fn stage_clip_preserves_stage_space_and_rejects_empty_or_invalid_regions() {
        assert_eq!(stage_clip(None, 960, 544), Ok(None));
        assert_eq!(stage_clip(Some([120.5, 90.0, 300.0, 200.0]), 960, 544),
            Ok(Some([120.5, 90.0, 300.0, 200.0])));
        assert_eq!(stage_clip(Some([-20.0, -10.0, 100.0, 40.0]), 960, 544),
            Ok(Some([0.0, 0.0, 80.0, 30.0])));
        assert_eq!(stage_clip(Some([900.0, 500.0, 200.0, 100.0]), 960, 544),
            Ok(Some([900.0, 500.0, 60.0, 44.0])));
        for rect in [[960.0, 0.0, 20.0, 10.0], [0.0, 0.0, -1.0, 10.0],
            [0.0, 0.0, 20.0, 0.0], [f32::NAN, 0.0, 10.0, 10.0], [0.0, 0.0, f32::INFINITY, 10.0]] {
            assert!(stage_clip(Some(rect), 960, 544).is_err());
        }
    }

    #[test]
    fn rule_arguments_preserve_mask_and_normalized_progress_without_affecting_other_shaders() {
        let mut effect = ShaderEffect {
            name: crate::render_pipeline::shader::RULE_TRANS_SHADER.to_owned(),
            uniforms: [("progress".to_owned(), vec![0.375]), ("vague".to_owned(), vec![32.0 / 255.0])].into(),
            mask_texture: Some(TextureId(42)),
            user_texture: None,
        };
        assert_eq!(rule_parameters(Some(&effect)), (42, 0.375, 32.0 / 255.0));
        effect.uniforms.insert("progress".to_owned(), vec![2.0]);
        effect.uniforms.insert("vague".to_owned(), vec![0.0]);
        assert_eq!(rule_parameters(Some(&effect)), (42, 1.0, 1.0 / 255.0));
        effect.uniforms.insert("progress".to_owned(), vec![f32::NAN]);
        assert_eq!(rule_parameters(Some(&effect)).1, 0.0);
        effect.name = "different-shader".to_owned();
        assert_eq!(rule_parameters(Some(&effect)).0, 0);
        effect.name = crate::render_pipeline::shader::RULE_TRANS_SHADER.to_owned();
        effect.mask_texture = None;
        assert_eq!(rule_parameters(Some(&effect)).0, 0);
        assert_eq!(rule_parameters(None).0, 0);
    }
}

#[cfg(feature = "gxm-builtin-effects")]
impl Drop for GxmRenderer {fn drop(&mut self){external_effects::clear();}}
