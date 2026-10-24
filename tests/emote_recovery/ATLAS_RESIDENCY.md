# Four-model atlas residency stress

This test targets missing characters after GPU memory fragmentation. It does
not establish that every allocation failure is fragmentation. Run at CPU 444 / GPU 222 with the recovered
backend and a fixed 4×4 subdivision setting.

## Allocator and pixel probe

Enable Debug and create `ux0:data/art3m1s-gxm/texture-fragment-probe.once` before
launch. The flag is consumed once. Normal launches do not run the probe.

The probe packs mapped CDRAM with small blocks, releases interleaved blocks,
and measures the largest successful allocation up to 8 MiB. It then allocates
four 4096×2048 BC3 atlases through the production recovery/reservation path.
Four different colors check aliasing and corruption while subsequent atlases
are allocated. After each round, all test blocks must be released and free
capacity must recover. At least one round must actually reproduce an 8 MiB
allocation failure despite at least 8 MiB total free memory.

There are 12 deterministic mixed-size rounds and one adverse release-order
round. In the latter, separated cold blocks are retired first, so an increase
in free bytes does not immediately create a sufficiently large block.

Three additional rounds retain 256 KiB, 1 MiB and 512 KiB published image
textures between free holes. None of these live images may be evicted. The
largest allocatable block must be less than 8 MiB while free capacity is at
least 32 MiB. Bounded relocation must then admit four 8 MiB atlases. Both all
original images and all atlases must pass sampled pixel checks, and CDRAM
capacity and the relocation registry must return to their original counts.
Readback coordinates avoid the device's performance overlay.

## Full-host four-model fixture

Prepare the fixture outside the repository, supplying four local PSBs with
distinct 8 MiB atlases and pose/idle timeline names that all four support:

```text
python scripts/prepare-atlas-fragment-fixture.py --output <external-directory>/game --models <actor1.psb> <actor2.psb> <actor3.psb> <actor4.psb> --pose <pose-label> --idle <idle-label> --rounds 12 --seed 104729
```

Use `--resident-images 16 --seed 15485863 --unique-images --staged-models`
for the stronger variant: new image files each round prevent a warmed GPU
cache from eliminating allocation churn; two models stay live during the
second half of loading, then sixteen images remain visible with four models.
This represents live barriers left by normal scene progression, which a
cold-cache-only test cannot cover.

Copy the generated directory into an isolated test game on the device. The
script repeatedly removes the models, changes residency of 24 differently
sized RGBA images, then recreates all four models. Three images remain active
to prevent the test from trivially clearing everything. Each round stores a
`four-NN.png` capture in the test game's save directory. Inspect all four
characters in every capture; script markers alone do not prove successful
uploads. Preserve logs, fixture hashes and captures outside the repository.

Observe `texture-block-recovery`, reservation consumption, allocation errors,
resource-ledger faults, model capture counts, and memory after each phase.
Both `ready=1` and complete four-person pixels are required. Check stable
resident frames separately from initial PSB reads, decode and scene creation.

## 2026-10-24 observations

The initial 12-round allocator run reproduced fragmentation in 7 rounds. One
round had 41 MiB total free but only a 5 MiB allocatable block. All 48 atlas
uploads passed pixel checks; each round returned to the same free capacity.
The final 13-round build also passed all 52 atlas pixel checks and all memory
return checks. Its adverse round had 31 MiB free but only a 2.25 MiB largest
block; the first atlas required five reclaim batches. Eight of the 13 rounds
confirmed fragmentation before recovery.

The first full-host run completed 12 rounds with four complete characters in
every saved capture. Nine rounds needed demand recovery: three needed 4
reclaim batches, five needed 5, and one needed 6. The final recovery released
27 MiB of idle textures. This exceeds the former four-batch cutoff. Ledger
fault counts stayed zero. These results do not constitute a no-stutter claim:
model recreation still includes substantial PSB loading/CPU work.

The previously installed executable was then run with the same fixture,
settings and seed. Its first six rounds matched the successful sequence, but
rounds 7–10 stopped at `ready=0 retries=4` with 27–29 MiB still free. All four
captures for those rounds showed only three characters. It recovered in
rounds 11–12 after the residency pattern changed. The fixed run displayed
four characters in all 12 captures. This is a device A/B reproduction, not
only an inference from a mocked allocator.

The initial changes let pending demand stop speculative uploads, release unpublished
GPU staging before cold textures, and continue bounded cold-cache reclamation
until the actual atlas fits or no eligible entry remains. Subsequent gameplay
still failed with 34 MiB free and a largest block below 8 MiB after every
eligible cache entry was gone. Thus the earlier results cover reclaimable
fragmentation only, not every missing-character case.

The additional recovery stage relocates only published CPU-uploaded textures,
outside a scene and after all GPU readers finish. Render targets, imported
video and unpublished staging with outstanding writer pointers are excluded.
The destination block and updated descriptor are prepared while the old block
is still valid; only then is storage swapped and the old block released.
Object handles, content revisions and pixels are preserved. Moves only go to
lower addresses and are bounded by 32 MiB copied, 64 moves and 128 attempts.
Failure keeps existing images valid. There is no continuous background
compaction. A later scene releasing a published texture of at least 1 MiB
rearms an outstanding demand once; probes, glyph churn and releases within
the recovery cannot trigger an immediate repeat.

The added device probe passed all three live-barrier rounds. It retained 27,
20 and 24 images; free CDRAM was respectively 55, 41 and 49 MiB while the
largest successful allocation was only 6, 2 and 2 MiB. All twelve new atlas
uploads and every retained image passed pixel checks. Each round restored
capacity and the registry count. The complete 16-round probe passed, with
61 MiB available both before and after. Individual successful compactions
copied 0.25–3 MiB and took approximately 14–85 ms on the device.

A complete-host 12-round run retaining sixteen images also displayed all four
models in every capture. It did not trigger the new compaction path, because
the fixed image set became fully cached. It is a rendering/lifetime regression
check, not evidence of recovery from live fragmentation; use unique streamed
images for that check.

The streamed-image variant completed all twelve rounds at CPU 444 / GPU 222.
All twelve resident captures contain four complete characters and the visible
image barriers. Nine atlas requests remained blocked after the cold cache was
exhausted; all nine were admitted after relocation. They copied 1–13.75 MiB
and took 29.7–294.2 ms each. No exhausted recovery or resource-ledger fault was
reported. These are one-off allocation recovery costs, not per-frame work or
a claim that all scene-loading stalls are eliminated.

During round nine the device stopped presenting and logging. Waking the
display and returning the app to the foreground resumed the same process and
script, which then completed; no new crash dump was found. Keep the device
awake for unattended runs. Real gameplay from a fresh start is a separate
manual acceptance step, since loading a save does not reproduce allocation
history.

The subsequent manual gameplay acceptance passed on the installed fix. The
user confirmed that all characters were visible in the previously affected
four-person scene and supplied a local recording. This complements the
allocator and full-host fixtures with the original gameplay allocation
history; it does not guarantee success for every possible memory layout.
