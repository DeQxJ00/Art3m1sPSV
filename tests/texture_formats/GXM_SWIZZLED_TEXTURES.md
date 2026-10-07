# PSV pre-swizzled BC textures

Art3m1sPSV accepts both linear and pre-swizzled BC1 (DXT1) / BC3 (DXT5).
BC1 is suitable for opaque images (4 bits per pixel); BC3 preserves smooth alpha
(8 bits per pixel). Swizzling changes block order, not image quality or compression.

## E-mote PSB

| Texture `type` | Payload |
| --- | --- |
| `DXT1`, `DXT1_LINEAR` | Row-major BC1 blocks |
| `DXT1_SWIZZLED` | PSV GXM swizzled BC1 blocks |
| `DXT5`, `DXT5_LINEAR` | Row-major BC3 blocks |
| `DXT5_SWIZZLED` | PSV GXM swizzled BC3 blocks |

Type names are case-insensitive. Preserve the logical texture dimensions, icon
rectangles, origins and model coordinates. BC1 one-bit alpha is supported; it is
not a replacement for BC3 when smooth transparency is required.

## Ordinary DDS images

Keep the standard DDS FOURCC `DXT1` or `DXT5`. Write the five ASCII bytes `GXMSW`
at absolute file offsets **0x20–0x24** (32–36), at the beginning of
`DDS_HEADER.dwReserved1`. There is no version byte. Leave the other reserved bytes
zero. Do not replace the FOURCC with this marker.

Without this marker, the engine uses the existing linear-block upload path.
File names and resource paths do not change; no sidecar file is needed.
The marker is a custom Art3m1sPSV extension, not a standard DDS flag.

## Payload layout

1. Keep logical width and height in the container. Supported PSV dimensions are
   1–4096 in each direction.
2. Pad each dimension independently to the next power of two, at least 4 pixels.
3. Treat each 4×4 block as an indivisible unit: BC1 uses 8 bytes, BC3 uses 16.
4. Order blocks using rectangular Morton order: interleave the **Y bit first,
   then X bit**, from least significant bits. When the shorter dimension has no
   more bits, append the remaining bits of the longer dimension.
5. Keep bytes inside each block unchanged. Zero-fill unused padded blocks.
6. Supply exactly one mip level and exactly the padded payload length:
   `(padded_width / 4) * (padded_height / 4) * block_bytes`.

For example, 960×544 occupies 1024×1024 storage: 512 KiB BC1 or 1 MiB BC3.
Power-of-two padding can make the file larger than linear BC data.

The GXM path copies these blocks directly into GPU-accessible memory and skips
runtime swizzling. It does not expand them to RGBA. GPU allocation and copying
are still required. CPU fallback for PSB textures reverses the block order before
decoding. Invalid dimensions, truncated/extra payload, unsupported marked formats
and multiple mip levels are rejected.
