# Restored input wait regression

Standalone synthetic fixture with no game resources. Deploy this folder separately under `games/TEST_UPSTREAM_LOAD` and copy `host/assets/menu.ttf` to `font.ttf` in the deployed folder. It is not bundled in VPKs.

1. Blue button (200, 445) saves through a nested script. Orange (700, 445) loads. Stay on page one while saving and loading; text and dark blue background must survive. The log must contain `SYNC-LOAD helper returned`, never `ERROR stale load tail`.
2. One Circle press after loading advances to the green page two, not directly to page three.
3. Save page two, advance to page three, load: return to green page two with its text and indentation. One Circle press returns to page three (no extra click).
4. Repeat loads to check that restored waits do not accumulate. Existing saves remain unchanged; only the fixture's own `sync.dat` is used.

Interpreter tests additionally cover same-depth helper jumps, normal return cleanup, old queue discard, and inline-versus-queued PC advancement. PFS mixed-encoding fixtures run through the same C ABI as the PSV file host.

The white button (890, 445) captures the completed framebuffer as `save/capture.png` for physical-device comparison. It does not advance the story.

Controller equivalents: Left saves, Right loads, Triangle captures. Circle advances. These fixture-only bindings also work when synthetic touch is unavailable.
