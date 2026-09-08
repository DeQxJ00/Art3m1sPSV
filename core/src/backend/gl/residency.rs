//! Bounded reuse of decoded file textures between appearances. Mutable runtime
//! textures (video, text atlases, edits and transition captures) are excluded.

pub(super) const IDLE_BYTES: u64 = 8 * 1024 * 1024;
pub(super) const TOTAL_BYTES: u64 = 32 * 1024 * 1024;
const IDLE_COUNT: usize = 64;

pub(super) struct Resident<'a> {
    pub name: &'a str,
    pub bytes: u64,
    pub active: bool,
    // None means this is not a reloadable file texture.
    pub last_used: Option<u64>,
}

pub(super) fn evictions<'a>(textures: impl Iterator<Item = Resident<'a>>) -> Vec<String> {
    let mut active_bytes = 0u64;
    let mut idle_bytes = 0u64;
    let mut idle = Vec::new();
    let mut stale = Vec::new();
    for texture in textures {
        if texture.active {
            active_bytes = active_bytes.saturating_add(texture.bytes);
        } else if texture.last_used.is_some() {
            idle_bytes = idle_bytes.saturating_add(texture.bytes);
            idle.push(texture);
        } else {
            stale.push(texture.name.to_owned());
        }
    }
    // Active scene resources are never evicted to satisfy a cache budget.
    let mut budget = IDLE_BYTES.min(TOTAL_BYTES.saturating_sub(active_bytes));
    if idle_bytes <= budget && idle.len() <= IDLE_COUNT {
        return stale;
    }
    idle.sort_unstable_by(|a, b| b.last_used.cmp(&a.last_used).then(a.name.cmp(b.name)));
    let mut slots = IDLE_COUNT;
    for texture in idle {
        if slots > 0 && texture.bytes <= budget {
            budget -= texture.bytes;
            slots -= 1;
        } else {
            stale.push(texture.name.to_owned());
        }
    }
    stale
}

#[cfg(test)]
mod tests {
    use super::*;
    const MIB: u64 = 1024 * 1024;

    fn image(name: &str, bytes: u64, active: bool, last_used: Option<u64>) -> Resident<'_> {
        Resident { name, bytes, active, last_used }
    }

    #[test]
    fn reusable_faces_survive_but_unused_runtime_textures_do_not() {
        let dropped = evictions([
            image("eyes-open", MIB, true, Some(3)),
            image("eyes-closed", MIB, false, Some(2)),
            image("mouth-open", MIB, false, Some(1)),
            image("text-atlas", MIB, false, None),
            image("video", MIB, false, None),
            image("transition", MIB, true, None),
        ].into_iter());
        assert_eq!(dropped, ["text-atlas", "video"]);
    }

    #[test]
    fn budget_keeps_recent_images_and_counts_active_cpu_and_gpu_bytes() {
        let dropped = evictions([
            image("active", 28 * MIB, true, None),
            image("old-bg", 3 * MIB, false, Some(1)),
            image("recent-face", 3 * MIB, false, Some(9)),
            image("small-face", MIB, false, Some(2)),
        ].into_iter());
        assert_eq!(dropped, ["old-bg"]);
        let dropped = evictions([
            image("active", TOTAL_BYTES + MIB, true, Some(1)),
            image("idle", 1, false, Some(10)),
        ].into_iter());
        assert_eq!(dropped, ["idle"]);
    }

    #[test]
    fn oversized_idle_asset_does_not_evict_small_reusable_assets() {
        let dropped = evictions([
            image("oversized", IDLE_BYTES + 1, false, Some(10)),
            image("small", MIB, false, Some(1)),
        ].into_iter());
        assert_eq!(dropped, ["oversized"]);
    }

    #[test]
    fn tiny_files_cannot_grow_cache_metadata_without_bound() {
        let names: Vec<_> = (0..100).map(|n| format!("file-{n}")).collect();
        let dropped = evictions(names.iter().enumerate().map(|(n, name)| {
            image(name, 1, false, Some(n as u64))
        }));
        assert_eq!(dropped.len(), 36);
        assert!(dropped.contains(&"file-0".to_owned()));
        assert!(!dropped.contains(&"file-99".to_owned()));
    }
}
