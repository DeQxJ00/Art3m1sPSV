//! A renderer-owned mutation stamp; identity is never inferred from an address.
use std::sync::atomic::{AtomicU64, Ordering};
static NEXT_ID: AtomicU64 = AtomicU64::new(1);

pub(super) struct SnapshotRevision {
    identity: Option<u64>,
    revision: Option<u64>,
}
impl Default for SnapshotRevision {
    fn default() -> Self {
        Self {
            identity: NEXT_ID.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |v| v.checked_add(1)).ok(),
            revision: Some(0),
        }
    }
}
impl SnapshotRevision {
    pub(super) fn bump(&mut self) {
        self.revision = self.revision.and_then(|v| v.checked_add(1));
    }
    pub(super) fn token(&self) -> Option<(u64, u64)> { self.identity.zip(self.revision) }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn distinct_renderers_and_counter_exhaustion_cannot_alias() {
        let mut a=SnapshotRevision::default();let b=SnapshotRevision::default();
        assert_ne!(a.token(), b.token());
        a.revision=Some(u64::MAX);a.bump();assert_eq!(a.token(),None);
        a.bump();assert_eq!(a.token(),None);
    }
}
