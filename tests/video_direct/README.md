# NV12 direct presentation probe

Build with `wsl -d Ubuntu-24.04 -- bash tests/video_direct/build.sh` from the workspace.
Install `build/nv12-probe/probe.vpk` (separate title `ART3MNV12`).

The probe uses the production `host/video_direct.c` with synthetic limited-range
BT.601 NV12 color bars. It does not use or verify H.264 hardware decoding.
It submits 600 frames across five pool lifetimes, releasing the caller's AVFrame
immediately after each submission. Each lifetime should allocate only two 768 KiB
CDRAM slots. The first four cycles alternate bar order; the last is stationary.
The log must reach `PASS 600 frames` with no pool-exhaustion, live-frame or unmap
errors. Press X to verify final cleanup and exit.

After the last cycle capture a 960x544 screenshot and run:

```sh
python3 tests/video_direct/check_screen.py /path/to/screenshot.png
```

The image contains 960x540 visible pixels in a 960x544 aligned surface. The source
padding is deliberately magenta; the production presenter must extend the edge
to prevent linear chroma sampling from drawing a colored line at the bottom.
The check covers all eight color bars at the top, center and last two rows.

2026-09-07: Vita3K passed all five cycles, pixel checks and X exit. The first
revision exposed bottom-row chroma bleed; the production edge-padding fix passed
the same test. This provides rendering/lifetime evidence, not real-device speed,
hardware-decoder output compatibility, or arbitrary color-space coverage.

Real-device follow-up: use the main VPK to test logo playback to EOF, skip,
another video, loop and return to the game. Confirm `output=NV12-direct` and
`first frame bound; no RGBA conversion/upload` in media.log, then evaluate the
`video-perf` frame counts and timings. RGBA/software fallback is not a direct-path
pass. Layer videos and alpha-mask videos still use the existing RGBA path.
