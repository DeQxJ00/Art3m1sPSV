//! A single host-owned vitaGL context. Calls stay on the host thread.
use super::*;
use std::ffi::{CString, c_char, c_void};

unsafe extern "C" {
    fn vglGetProcAddress(name: *const c_char) -> *const c_void;
    fn vglSwapBuffers(has_commondialog: u8);
}

struct VitaContext;
impl GLPlatformContext for VitaContext {
    fn make_current(&self) -> bool { true }
    fn get_proc_address(&self, name: &str) -> *const c_void {
        CString::new(name).map(|s| unsafe { vglGetProcAddress(s.as_ptr()) })
            .unwrap_or(std::ptr::null())
    }
    fn bind_save(&self) -> SavedGlContext { SavedGlContext::NONE }
    fn restore(&self, _: SavedGlContext) {}
    fn set_external_surface(&self, kind: i32, _: *mut c_void, w: i32, h: i32) -> Result<(), String> {
        if kind != 4 || w != 960 || h != 544 {
            return Err("Vita display requires surface kind 4 at 960x544".into());
        }
        Ok(())
    }
    fn bind_external_surface(&self) -> Result<(), String> { Ok(()) }
    fn present_external_surface(&self) -> Result<(), String> {
        unsafe { vglSwapBuffers(0); }
        Ok(())
    }
}

pub(super) fn create() -> Result<(Rc<glow::Context>, Box<dyn GLPlatformContext>, GfxBackend), String> {
    let ctx = VitaContext;
    crate::core_warn!("Vita: loading GL entry points");
    // The host initializes vitaGL before calling runtime_create.
    let gl = unsafe { glow::Context::from_loader_function(|name| ctx.get_proc_address(name)) };
    crate::core_warn!("Vita: GL entry points loaded");
    Ok((Rc::new(gl), Box::new(ctx), GfxBackend::Angle(AngleBackend::OpenGL)))
}
