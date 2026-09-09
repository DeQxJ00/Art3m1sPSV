# PSV PNG decoder comparison

Standalone benchmark, not a game renderer replacement. Temporarily run with ART3DIR01 and restore the exact original eboot in a `finally` block. Input images and result logs live in a separate `ux0:/data/art3m1s-png-bench` directory. Game assets and saves are not modified.

Sources: [official pnggroup/libpng](https://github.com/pnggroup/libpng/tree/v1.6.58) **v1.6.58**, commit `3061454d980de7d53608f594194cfac722721d2a`; [official madler/zlib](https://github.com/madler/zlib/tree/v1.3.2) **v1.3.2**, commit `da607da739fa6047df13e66a2af6b8bec7c2a498`. Local dependencies are in build/png-bench-libpng and build/png-bench-zlib. CMake builds both with O3/Cortex-A9/NEON, explicitly PNG_ARM_NEON_OPT=2. These are separately compiled sources, not a library bundled with this project's SDK.

Rust includes the actual core image_decode.rs helper, with image 0.25.10, png 0.18.1, fdeflate 0.3.7, miniz_oxide 0.8.9. Cargo.lock pins flate2 1.1.9 to match the application; the initial exploratory run used 1.1.10 and should not replace the aligned run. Release optimized build, same Vita Rust toolchain as the game.

## Method

- Fixed 333MHz CPU, 222MHz bus, 111MHz GPU; standalone process without concurrent game rendering/audio.
- The final probe shows a blue progress bar and a green completion bar, with temporary automatic suspend/OLED locks and per-sample power ticks outside timing. Keep the application in the foreground: these locks do not authorize or guarantee uninterrupted execution after manually switching apps. The CPU-written display uses a separate 2.25MiB CDRAM allocation, shared equally by both decoder tests. Do not merge absolute timings with earlier headless runs.
- Load compressed file once, then compare all RGBA bytes before timing. PAL/tRNS, RGBA32, 16-bit channels, real background and real portrait. No gamma correction or premultiplication in either path; libpng scale_16 and Rust rounding compared byte-for-byte.
- Alternate Rust/libpng and libpng/Rust, 10 samples each. Allocation, decode, transformations, decoder destruction included; IO, byte comparison, output destruction and logging excluded from timing.
- A separate untimed allocation tracking pass measures requested live heap bytes. Rust uses a GlobalAlloc wrapper; libpng uses allocator callbacks (including its zlib allocations) plus output. Excludes encoded input, allocator headers/rounding, stack and some bookkeeping. This is **not process heap peak or maximum contiguous free space**. Tiny per-allocation branch/header overhead remains in timing mode.
- libc output allocation remains fallible for the large buffer. libpng longjmp is contained in C; cleanup state lives on the heap, not in locals modified across setjmp. This probe is not production error-recovery implementation.
- Results measure decoder-to-CPU-RGBA, **not direct CDRAM writes, texture upload, cache reuse, or game FPS**. A good decode result does not prove scene switching becomes smooth.

## Reproduce

1. Fetch the two exact official tags into the build directories above.
2. Use the Vita nightly Rust toolchain and `CARGO_TARGET_DIR=build/png-bench-rust`. Build `tests/png_decode_bench/rust/Cargo.toml` with `--lib --release --target armv7-sony-vita-newlibeabihf -Z build-std=std,panic_abort`.
3. On desktop, run the same manifest's `extract` binary with the installed SHUF00002 directory and `build/png-bench-assets` as arguments. It extracts the named background/portrait entries and one suitable real RGBA32 image, identified by reading PNG headers. Commercial assets remain under ignored build/.
4. Run `python scripts/prepare-png-bench-assets.py` to generate synthetic cases and a SHA256/dimensions manifest.
5. Configure this CMake project with the actual Vita toolchain and VITASDK environment; output build/png-bench-vita. Build png_bench.self.
6. Run `python scripts/run-png-decode-bench.py`. It verifies inputs and eboot upload, records backup and restoration hashes, and fetches DONE output.
7. If the user reports suspension/backgrounding, set `performance_valid: false` and an `interruption` reason in that run's manifest, even if it later completed. Run `python scripts/summarize-png-decode-bench.py build/png-bench-runs/<run>`; it rejects those runs as well as incomplete, un-restored, pixel-mismatching or failed runs.

The full Cargo lock and probe sources are versioned. The output summary and immutable eboot backup are kept with each local run. Do not deploy this probe as the game executable permanently.
