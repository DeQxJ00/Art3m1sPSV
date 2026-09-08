use super::CoreRuntime;
use crate::backend::gl::RenderRegion;
use crate::render_pipeline::RenderPipeline;
use crate::render_pipeline::draw::{DrawList, Renderer, TextureProvider};
use asb_interpreter::event::WaitReason;

unsafe extern "C" {
    fn art3m1s_gxm_capture_previous(width: u32, height: u32, output: *mut u8, length: usize) -> i32;
    fn art3m1s_gxm_cancel_capture();
    fn art3m1s_gxm_read_completed_frame(width: u32, height: u32, output: *mut u8, length: usize) -> i32;
}

impl CoreRuntime {
    pub fn prepare_gxm_textures(&mut self) {
        if let Some(renderer) = self.text_renderer.as_mut() {
            renderer.prepare_textures(&mut self.texture_provider);
        }
    }
    /// Draw only; the Vita host advances scripts before opening its GXM scene.
    pub fn present_gxm(&mut self) -> bool {
        let mut profile = self.begin_profile_frame();
        let repaint = self.render_current_frame(&mut profile).is_some();
        self.frame_visual_dirty = false;
        self.finish_profile_frame(&mut profile);
        repaint
    }

    pub(super) fn resize_stage(&mut self, new_width: u32, new_height: u32) -> Result<(), String> {
        self.stage_w = new_width;
        self.stage_h = new_height;
        self.renderer.set_viewport_size(new_width, new_height);
        self.renderer.set_stage_size(new_width, new_height);
        self.last_rendered_scene = None;
        self.last_submitted_frame = None;
        Ok(())
    }

    /// Submit into the host GXM frame that is already active.
    /// The display surface is cleared each frame, so a cached draw list is
    /// submitted even when logic and texture state did not change.
    pub(super) fn render_current_frame(
        &mut self,
        profile: &mut crate::profiler::FrameProfile,
    ) -> Option<RenderRegion> {
        if RenderPipeline::new(&self.compositor).needs_trans_capture() {
            #[cfg(feature = "gxm-builtin-effects")]
            {
                let captured = self.texture_provider.capture_completed_frame(
                    crate::render_pipeline::transition::CAPTURE_TEXTURE_NAME, self.stage_w, self.stage_h);
                if let Some((texture, info)) = captured {
                    RenderPipeline::new(&self.compositor).capture_trans_external_texture(texture, info, false);
                } else {
                    self.renderer.render(self.last_submitted_frame.as_ref().unwrap_or(&DrawList::default()));
                    return Some(RenderRegion::Full);
                }
            }
            #[cfg(not(feature = "gxm-builtin-effects"))]
            {
            let mut pixels = vec![0u8; self.stage_w as usize * self.stage_h as usize * 4];
            let ready = unsafe {
                art3m1s_gxm_capture_previous(self.stage_w, self.stage_h, pixels.as_mut_ptr(), pixels.len())
            };
            if ready == 0 {
                // Hold the last submitted scene until its display buffer is
                // complete. The host copies only after ending the GXM scene.
                self.renderer.render(self.last_submitted_frame.as_ref().unwrap_or(&DrawList::default()));
                return Some(RenderRegion::Full);
            }
            RenderPipeline::new(&self.compositor).capture_trans_texture(
                &pixels, self.stage_w, self.stage_h, &mut self.texture_provider,
            );
            }
        } else {
            unsafe { art3m1s_gxm_cancel_capture() };
        }
        let build_started = profile.mark();
        self.frame_visual_dirty |= self.drive_click_wait_icon();
        let texture_revision = self.texture_provider.content_revision();
        let rebuild = self.frame_visual_dirty
            || self.last_submitted_frame.is_none()
            || self.last_submitted_texture_revision != texture_revision
            || RenderPipeline::new(&self.compositor).is_transition_in_progress();

        if rebuild {
            let backlog_started = profile.mark();
            self.sync_backlog_snapshot();
            profile.frame_backlog_ns = crate::profiler::FrameProfile::elapsed(backlog_started);
            let (frame, _, _) = self.build_bound_scene(true, None, Some(profile));
            profile.draw_list_commands = (frame.commands.len() + frame.mask_commands.len()) as u64;
            self.last_submitted_frame = Some(frame);
            self.last_submitted_texture_revision = self.texture_provider.content_revision();
            // GXM transitions preserve last_submitted_frame and capture its
            // completed display. Only the GL backend consumes a scene clone.
            #[cfg(not(feature = "gxm-native-renderer"))]
            { self.last_rendered_scene = Some(self.compositor.scene().render_snapshot()); }
            self.last_rendered_clock_ms = self.compositor.clock_ms();
        }
        profile.frame_build_ns = crate::profiler::FrameProfile::elapsed(build_started);

        let frame = self.last_submitted_frame.as_ref()?;
        let gpu_started = profile.mark();
        self.renderer.render(frame);
        profile.gpu_submit_ns = crate::profiler::FrameProfile::elapsed(gpu_started);
        profile.rendered = true;
        profile.stage_pixels = self.stage_w as u64 * self.stage_h as u64;
        profile.damage_pixels = profile.stage_pixels;
        Some(RenderRegion::Full)
    }

    pub(super) fn read_current_frame_into(&mut self, out_pixels: &mut [u8]) -> usize {
        let length=self.pixel_buffer_size();
        if out_pixels.len()<length { return 0; }
        let ready=unsafe { art3m1s_gxm_read_completed_frame(self.stage_w,self.stage_h,out_pixels.as_mut_ptr(),length) };
        if ready!=0 { length } else { 0 }
    }

    pub(super) fn refresh_transition_source_frame(&mut self) {
        // Preserve the old submission for the deferred host capture. A future
        // offscreen pass must also rebuild hidden text, as the GL path does.
    }

    fn build_bound_scene(
        &mut self,
        include_transition: bool,
        scene_snapshot: Option<(&crate::compositor::Scene, u64)>,
        mut profile: Option<&mut crate::profiler::FrameProfile>,
    ) -> (DrawList, usize, usize) {
        let text_started = profile.as_ref().and_then(|p| p.mark());
        let text_map = self.build_text_commands();
        if let Some(p) = profile.as_deref_mut() {
            p.frame_text_ns = crate::profiler::FrameProfile::elapsed(text_started);
        }
        let text_layer_count = text_map.len();
        let text_command_count = text_map.values().map(Vec::len).sum();
        let emote_started = profile.as_ref().and_then(|p| p.mark());
        let (mut emote_map, emote_files) = self.build_emote_commands();
        if let Some(p) = profile.as_deref_mut() {
            p.frame_emote_ns = crate::profiler::FrameProfile::elapsed(emote_started);
        }
        let scene_started = profile.as_ref().and_then(|p| p.mark());
        let has_emote_commands = !emote_map.is_empty();
        let has_text_commands = !text_map.is_empty();
        let mut content_source = |layer_id: &str| emote_map.remove(layer_id).unwrap_or_default();
        let mut text_map = text_map;
        let mut text_source = |layer_id: &str| text_map.remove(layer_id).unwrap_or_default();
        let content_for: Option<&mut crate::render_pipeline::LayerDrawSource<'_>> =
            has_emote_commands.then_some(&mut content_source);
        let text_for: Option<&mut crate::render_pipeline::LayerDrawSource<'_>> =
            has_text_commands.then_some(&mut text_source);
        let pipeline = RenderPipeline::new(&self.compositor);
        let mut frame = if let Some((scene, clock_ms)) = scene_snapshot {
            pipeline.build_scene_with_content(
                scene,
                clock_ms,
                &mut self.texture_provider,
                content_for,
                text_for,
            )
        } else if include_transition {
            pipeline.build_composited_with_content(
                &mut self.texture_provider,
                content_for,
                text_for,
            )
        } else {
            pipeline.build_with_content(&mut self.texture_provider, content_for, text_for)
        };
        frame.materialize_stencil_groups(crate::render_pipeline::shader::ALPHA_MASK_SHADER);
        if let Some(p) = profile.as_deref_mut() {
            p.frame_scene_ns = crate::profiler::FrameProfile::elapsed(scene_started);
        }
        let retain_started = profile.as_ref().and_then(|p| p.mark());
        let mut used_files = scene_snapshot
            .map(|(scene, _)| scene.collect_files())
            .unwrap_or_else(|| self.compositor.scene().collect_files());
        if let Some(renderer) = self.text_renderer.as_ref() {
            used_files.extend(renderer.retained_texture_names());
        }
        used_files.extend(emote_files);
        for file in RenderPipeline::new(&self.compositor).retained_files() {
            used_files.insert(file);
        }
        self.texture_provider.retain(&used_files);
        if let Some(p) = profile.as_deref_mut() {
            p.frame_retain_ns = crate::profiler::FrameProfile::elapsed(retain_started);
        }
        (frame, text_layer_count, text_command_count)
    }

    fn drive_click_wait_icon(&mut self) -> bool {
        let show = matches!(self.wait_reason.as_ref(), Some(WaitReason::Generic | WaitReason::Generic0))
            && self.is_text_reveal_complete();
        if show { self.enter_click_wait_icon(false) } else { self.exit_click_wait_icon() }
    }
}
