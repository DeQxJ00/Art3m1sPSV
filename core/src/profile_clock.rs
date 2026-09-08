//! Profiling uses Vita's monotonic microsecond counter, matching host timings.
//! Keep this separate from the game's clocks and input-repeat semantics.
#[cfg(not(target_os = "vita"))]
pub(crate) use std::time::Instant;

#[cfg(target_os = "vita")]
#[derive(Clone, Copy, Debug)]
pub(crate) struct Instant(u64);

#[cfg(target_os = "vita")]
impl Instant {
    pub(crate) fn now() -> Self {
        unsafe extern "C" { fn sceKernelGetProcessTimeWide() -> u64; }
        Self(unsafe { sceKernelGetProcessTimeWide() })
    }
    pub(crate) fn elapsed(self) -> std::time::Duration {
        Self::now().saturating_duration_since(self)
    }
    pub(crate) fn saturating_duration_since(self, earlier: Self) -> std::time::Duration {
        std::time::Duration::from_micros(self.0.saturating_sub(earlier.0))
    }
}
