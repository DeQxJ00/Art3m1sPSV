//! Bounded GXM diagnostics. These counters observe invalidation; they never
//! decide whether to render, skip an event, or advance an animation.

#[derive(Default)]
pub(super) struct RebuildTrace {
    start_ms: Option<u64>,
    pending: u8,
    window: Window,
}

pub(super) const EVENTS: u8 = 1;
pub(super) const COMPOSITOR: u8 = 2;
pub(super) const EMOTE: u8 = 4;
pub(super) const REVEAL: u8 = 8;
pub(super) const TRANSLATION: u8 = 16;
pub(super) const WAIT_ICON: u8 = 32;

#[derive(Debug, Default, PartialEq)]
pub(super) struct Window {
    pub decisions: u64,
    pub rebuilt: u64,
    pub dirty: u64,
    pub first: u64,
    pub texture: u64,
    pub transition: u64,
    pub sources: [u64; 6],
    pub dirty_unattributed: u64,
    pub event_count: u64,
    /// Counts of raw events, not render decisions or effects on the screen.
    pub event_kinds: [u64; 5],
}

impl RebuildTrace {
    pub fn mark(&mut self, enabled: bool, source: u8) {
        if enabled {
            self.pending |= source;
        } else {
            *self = Self::default();
        }
    }

    pub fn event(&mut self, enabled: bool, event: &asb_interpreter::Event) {
        self.mark(enabled, EVENTS);
        if !enabled {
            return;
        }
        use asb_interpreter::Event;
        let kind = match event {
            Event::Layer(_) => 0,
            Event::Exec { .. } => 1,
            Event::Text { .. } | Event::ScenarioText { .. } | Event::FontSettings(_) => 2,
            Event::BgmPlay { .. } | Event::SePlay { .. } | Event::VoicePlay { .. } => 3,
            _ => 4,
        };
        self.window.event_count += 1;
        self.window.event_kinds[kind] += 1;
    }

    /// A decision is counted only after transition capture is ready. Several
    /// logic ticks can precede one decision; each source counts at most once
    /// per decision. Sources overlap, so their counts must not be added as
    /// separate rebuilt frames. Inputs/explicit resets are left unattributed.
    pub fn decision(
        &mut self,
        enabled: bool,
        now_ms: u64,
        dirty: bool,
        first: bool,
        texture: bool,
        transition: bool,
    ) -> Option<Window> {
        if !enabled {
            *self = Self::default();
            return None;
        }
        let start = self.start_ms.get_or_insert(now_ms);
        // Loading a save may reset the logical clock. Start a new window,
        // keeping only pending reasons for the decision about to be counted.
        if now_ms < *start {
            *start = now_ms;
            self.window = Window::default();
        }
        self.window.decisions += 1;
        self.window.rebuilt += u64::from(dirty || first || texture || transition);
        self.window.dirty += u64::from(dirty);
        self.window.first += u64::from(first);
        self.window.texture += u64::from(texture);
        self.window.transition += u64::from(transition);
        self.window.dirty_unattributed += u64::from(dirty && self.pending == 0);
        for (index, count) in self.window.sources.iter_mut().enumerate() {
            *count += u64::from(self.pending & (1 << index) != 0);
        }
        self.pending = 0;
        if now_ms.saturating_sub(*start) < 5000 {
            return None;
        }
        self.start_ms = Some(now_ms);
        Some(std::mem::take(&mut self.window))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reasons_overlap_and_ticks_collapse_without_leaking_across_windows() {
        let mut trace = RebuildTrace::default();
        trace.event(
            true,
            &asb_interpreter::Event::Text {
                content: "line".into(),
            },
        );
        trace.event(
            true,
            &asb_interpreter::Event::Text {
                content: "line2".into(),
            },
        );
        trace.mark(true, REVEAL);
        assert!(trace.decision(true, 100, true, true, true, false).is_none());
        let result = trace
            .decision(true, 5100, false, false, false, false)
            .unwrap();
        assert_eq!(result.decisions, 2);
        assert_eq!(result.rebuilt, 1);
        assert_eq!(result.sources, [1, 0, 0, 1, 0, 0]);
        assert_eq!(result.event_count, 2);
        assert_eq!(result.event_kinds, [0, 0, 2, 0, 0]);
        let result = trace
            .decision(true, 10100, true, false, false, false)
            .unwrap();
        assert_eq!(result.sources, [0; 6]);
        assert_eq!(result.event_count, 0);
        assert_eq!(result.dirty_unattributed, 1);
    }

    #[test]
    fn disabled_diagnostics_and_clock_rewind_drop_old_windows() {
        let mut trace = RebuildTrace::default();
        trace.mark(true, COMPOSITOR);
        trace.decision(true, 9000, true, false, false, false);
        trace.mark(false, EVENTS);
        assert!(
            trace
                .decision(false, 14000, true, true, true, true)
                .is_none()
        );
        trace.decision(true, 20000, false, false, false, false);
        trace.mark(true, WAIT_ICON);
        trace.decision(true, 0, true, false, false, false);
        let result = trace
            .decision(true, 5000, false, false, false, false)
            .unwrap();
        assert_eq!(result.decisions, 2);
        assert_eq!(result.rebuilt, 1);
        assert_eq!(result.sources, [0, 0, 0, 0, 0, 1]);
    }
}
