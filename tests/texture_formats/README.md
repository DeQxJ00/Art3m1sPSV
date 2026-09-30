# External texture format comparison

The native comparison is opt-in through `texture-study.scene`. It does not
replace the normal image loader or package any game resources in the VPK.
Use legally available local PNG assets; keep the extracted inputs and outputs
under ignored `temp/`. Do not commit samples from games.

`tools/texture-study` is a local Windows conversion utility. It uses
PVRTexLib.NET (including its separately licensed PowerVR native library) and
ImageSharp through NuGet. These dependencies are not included in the VPK.

```powershell
dotnet run --project tools/texture-study/converter.csproj --artifacts-path build/texture-study -- temp/texture-format-study/A
```

Inputs are named `layer0.png`, `layer1.png`, etc. Existing generated previews
are excluded from subsequent conversion runs. Outputs include raw RGBA, BC3,
PSV-swizzled BC3, PVRTC1/2 payloads and software-decoded reference PNGs. PVR
containers are also retained locally for inspection; the native demo reads
payloads, not the container headers. Encoding preserves straight alpha and
does not resize the logical image. PVRTC uses edge-padded power-of-two storage.

The PVRTC2 `.pvr2` output is row-major. The converter also writes `.pvr2tw`,
reordering its 8-byte 4-by-4 blocks with the same Morton mapping as
`direct::bc3_block_index` using padded width/4 and height/4 (Y bit first).
The linear PVRTC2 mode is retained
as a negative test because `sceGxmTextureInitLinear` rejects it on hardware.

Each external demo directory requires `system.ini`, `first.iet`, `title.txt`
for launcher discovery, and a `texture-study.scene` manifest:

```text
3
layer0 960 540 0 2 960 540 0 1
layer1 320 480 320 40 320 480 1 0
layer2 120 96 400 80 120 96 2 0
```

The first line is the layer count (1–8). Each row contains a basename, logical
width/height, destination x/y/width/height, role, and certified opacity flag.
Roles: 0 background; 1 stable character layer; 2 outgoing expression;
3 incoming expression. Preserve original full-canvas alignment or cropped
image offsets when choosing positions. Opacity may be 1 only if every source
alpha value is 255. The reference and candidate always use identical placement.

Select a demo through the launcher. Left/right changes the candidate format;
Circle changes the expression/fade; Cross returns. PNG is on the left and the
candidate is on the right. The demo temporarily requests CPU 333/GPU 111 MHz
and restores previous frequencies on normal exit.

For automatic runs, install three external directories `TEST_TEXTURE_A`,
`TEST_TEXTURE_B`, `TEST_TEXTURE_C` under the data games directory, then create
`ux0:data/art3m1s-gxm/texture-study.once` and restart the application. This
produces per-layer, three-round `timings.csv`, rendered RGBA captures and
software-decoded reference captures in each directory. Reserve about 250 MiB
of free storage for captured output. No user saves are touched.

Metrics distinguish file I/O, decoder, opacity scan and upload. GPU bytes are
actual allocations including padding and allocator granularity. CPU buffer
peak describes source/decoded/proof buffers only, not total process memory or
codec workspace. Runs do not flush filesystem caches. Keep FTP idle during
timing runs. Capture writes are outside the render timer; averages must not be
interpreted as an uncapped FPS benchmark. Exclude changing plugin overlays
from pixel comparisons, and compare compressed rendering against both the
original PNG and its software-decoded reference.
