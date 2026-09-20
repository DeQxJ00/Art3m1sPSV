# Scene order cache (optQ)

This is a CPU traversal change on the optP/optO core and optK GPU baseline. It
does not skip animation, redraw, shader groups, or event dispatch.

`Scene` retains the original insertion-order strings. Sorting caches contain
stable-sorted **positions** into those strings, preserving numeric aliases,
negative/overflow IDs, lexical components, and message overlay ordering. The
frame builder consumes a borrowed iterator, avoiding per-subtree temporary
vectors when the cache is warm. The existing public vector-returning APIs
still return vectors.

The caches live in Scene, not the public Layer struct. `get_mut` invalidates
that node's child order before exposing the public children vector. Typed
property-only setters leave ordering intact. Creation registers new nodes and
invalidates the parent/root list; deletion also removes cache entries for the
subtree; rename clears the order cache before rekeying. Clone preserves valid
positions, render snapshots start with empty caches, and serialization excludes
both cache and diagnostic preference. No raw pointers or unsafe code are added.

Warm reads use `OnceLock::get_or_init`'s initialized acquire/read path. They do
not take a Mutex on every subtree; first initialization can synchronize. The
actual nightly-2026-08-28 standard-library source was inspected: `get_or_try_init`
returns immediately when `self.get()` succeeds, before `initialize(f)`.

The candidate flag is `DIRECT_SCENE_ORDER_CANDIDATE`. It implies optP diagnostics
and optO CPU gates, and retains the rejection of combining these candidates
with optL deferred GPU waits. `scene-order-cache.off` restores the former
allocate/sort traversal in the same runtime. The host applies it at boot and
polls once a second, logging `[scene-order-state]` with read-only clocks. The
runtime marks one frame dirty on a gate change and reapplies its preference to
the current Scene before GXM construction, including after scene replacement.

Regression evidence:

- 160 full-frame cached/uncached comparisons with moving/hidden groups, overlay
  text, intermediate grayscale/mask groups, stable equal-ID reordering, subtree
  creation/deletion/renaming, and serialization. A second cached read verifies
  the warm path. Existing stencil/key, traversal, and visibility tests also pass.
- Cache-locality test proves property changes retain order, public mutable
  access invalidates it, unrelated subtrees retain their cached allocation,
  removed subtrees release their entries, and cached/uncached serialization is
  identical. Missing ancestors, clone, replace, and gate transitions are covered.
- `build/01.02-optQ/frame-benchmark.log`: 528 nodes, 1000 complete frame builds,
  updating one parent's rotation each frame. Off/on/off/on totals are
  174181 / 64428 / 121359 / 57768 microseconds. This synthetic desktop CPU
  benchmark is not real PSV frame time.
- Full test-all: 962 passed, 23 ignored, one pre-existing pf8 Windows separator
  failure. pfs-upk separately: 6 passed. All-features lib, Vita core, host build,
  and the new order module's rustfmt pass. Full all-features still fails on the
  missing emote_parity_probe target; repository-wide fmt still reports style
  differences. No unrelated bulk formatting was performed.

Actual game evidence is recorded in the baseline optimization notes and
`build/01.02-optQ/`, including same-session A/B logs and text crop comparisons.
Real PSV performance remains unverified; its installed optL package was not
replaced for this emulator-only experiment.
