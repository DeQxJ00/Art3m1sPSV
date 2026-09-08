# Native GXM NV12 probe

Build from the workspace with `wsl -d Ubuntu-24.04 -- bash tests/video_gxm/build.sh` after the media SDK has been built. Install `build/gxm-host/gxm_nv12_probe.vpk` (separate title `ART3GN012`).

The probe uses Borealis' sole GXM context, production `host/video_direct.c` with `ART3M1S_HOST_GXM`, and production `host-gxm/src/video_gxm.cpp`. It generates limited-range BT.601 NV12 color bars; it does **not** invoke H.264 decoding or access game data.

It submits 600 frames over five pool lifetimes, releasing the producer AVFrame immediately after import. The first four cycles alternate bar order; the fifth remains stationary. Expect `PASS 600 frames`, two 786432-byte CDRAM slots per lifetime, and no exhausted-pool/live-frame/unmap errors. Capture the final frame, then run:

```sh
python3 tests/video_direct/check_screen.py /path/to/screenshot.png
```

This shared pixel checker verifies all eight bars at the top, middle and bottom two rows, including padded-row bleed. Press Cross after frame 600; require `cleanup complete` and a stopped, non-crashed session.

2026-09-07: session `bc5c47a8-e4fa-4413-8c99-3c7d0ac501c2` passed all five cycles, the pixel checker and Cross cleanup. Screenshot: `build/test-artifacts/gxm-nv12-bars.png`. VPK SHA256: `7C90227FE04AE587DD0146C1B0627613E3DA72D0387C004E4FC689EAAC0BE6A5`.

This is rendering and frame-reference evidence on Vita3K. It does not prove real-device H.264 decoding, arbitrary color spaces, video/audio synchronization, long-duration registry memory stability or physical-device performance.
