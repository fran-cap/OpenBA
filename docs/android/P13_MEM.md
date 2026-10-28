# P13 memory: BC1 / BC3 transcoded to ETC2 at load, and Half textures on low-memory phones

> Branch `android/p13-mem`, based on `android/p12` `2a5c2c481`. Owner rulings of 2026-10-28: (1) a device
> without `textureCompressionBC` may get the retail BC1 / BC3 textures re-encoded at load to ETC2 or ASTC,
> lossy, at about BC's size; (2) `ext.texture_quality` Half is the default on low-memory devices, defined
> from total RAM through a platform hook, overridable by settings.cfg. The facts this plan starts from are
> `P13_PROFILE.md` §6 (memory) and §9.2 / §9.3.

## 1. The problem in numbers (P13_PROFILE §6)

The Galaxy S10 (Adreno 640) has no `textureCompressionBC`, so every BC1 / BC3 level is decoded to RGBA8
at load (`src/render/common/bc_decode/`, `seeded_create.cpp`). One stress run: 125 textures (77 BC1)
decode to **397 MB** of RGBA8 from ~66 MB of BC; the app's real GPU memory (kgsl `page_alloc`, running
minus stopped) is ~590 MB at the menu, ~620 MB in the stress town, ~657 MB in normal play. The decode is
~97 ms of CPU for the town's 157 MB. Transcoding to a format of BC's size saves ~330 MB.

## 2. Format: ETC2, per texture

| Engine format | Retail | Transcoded to | Bytes / block |
|---|---|---|---|
| 0 | BC1, no texel transparent | `VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK` | 8 (= BC1) |
| 0 | BC1 with a transparent texel (three-colour block, index 3) | `VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK` | 8 (= BC1) |
| 1, 2 | BC3 | `VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK` (EAC alpha + ETC2 colour) | 16 (= BC3) |

Why ETC2 and not ASTC 4x4:
- **Size.** ASTC's smallest block footprint with a 4x4 grid is 16 bytes, 8 bpp: twice BC1. 77 of the 125
  textures are BC1, so ASTC 4x4 would hold roughly 1.6x what ETC2 holds; ETC2 RGB8 / RGB8A1 is exactly
  BC1's 4 bpp and ETC2 RGBA8 exactly BC3's 8 bpp. The staging blob and the region layout of a level are
  then the same as BC's (same block grid, same bytes per block): only the bytes and the Vulkan format
  change.
- **Encode cost at load.** A useful ASTC encoder (partition search, mode search) is an order of magnitude
  more work than an ETC2 one; a fast ETC2 encode is a per-block search over 8 tables and two layouts.
- **Support.** ETC2 (`textureCompressionETC2`) is required of every Android Vulkan device by the Android
  baseline profile; the S10 is checked at run time (the device log line gains `etc2=` / `astc=`), and the
  formats' `SAMPLED_IMAGE` feature is asked before the path is taken.
- **Quality.** ASTC 4x4 with 2-bit weights would reproduce BC1's 1/3 - 2/3 interpolation almost exactly,
  ETC2 does not: ETC2 is a re-encode from the decoded pixels and is lossy. The ruling accepts that; the
  encoder's error is bounded by unit tests and the town is compared by screenshot (§6).

Not taken: ETC2's T and H modes (two-colour blocks with 4-bit endpoints). The encoder does the
individual, differential and planar modes; planar is the one that matters for smooth gradients (sky,
terrain). T / H would help hard two-colour edges; measured by PSNR on the town's textures and added only
if the screenshots show the need.

## 3. The encoder: in the tree, pure, unit-tested

`src/render/common/etc2/etc2.{h,cpp}`: block encoders (ETC2 RGB, RGB with punch-through alpha, EAC
alpha) and the matching decoders (the tests' reference), no graphics API, no allocation;
`bc_to_etc2.{h,cpp}`: a BC1 / BC3 level transcoded block by block (each BC block decoded with
`BcDecode::DecodeColourBlock` / `DecodeBC3Block`, encoded at once: no RGBA8 level is ever held).

Why not etcpak (BSD-3): it is fast (SIMD) but brings a new dependency (a `THIRD_PARTY_NOTICES.md` row,
a pinned version, licence text in every package), its quality modes are the same individual /
differential / planar search, and its fast path is x86 SIMD; on the phone it would run its NEON path or
scalar code. The tree already holds its own BC4 encoder (`src/render/common/bc4/`) in the same shape. If
the measured encode cost (§5) is too high, etcpak is the fallback.

The search, per 4x4 block:
- **Individual / differential** (ETC1): for each flip (two 2x4 or two 4x2 halves), each half's base is its
  mean colour quantised to 4 bits (individual) or 5 bits with a 3-bit difference (differential); for each
  of the 8 modifier tables each pixel takes the modifier nearest its luminance offset from the base
  (the modifiers are grey, so the best one per pixel is the one nearest the mean of its three channel
  offsets) and the error is measured exactly, with the clamp to 0..255. The best table per half; then
  one refinement: the base re-fitted as the mean of (pixel - chosen modifier), re-quantised, re-measured.
- **Planar** (ETC2): a least-squares plane per channel through the 16 pixels, O / H / V quantised to
  6:7:6 bits, measured exactly.
- The block takes the mode with the least squared RGB error. The overflow bits that select planar mode
  are set as the format requires (differential R and G in range, B out of range).
- **Punch-through** (RGB8A1): a block with no transparent texel is encoded opaque (differential or planar:
  the opaque bit takes the place of the individual / differential bit); a block with one is differential
  with the opaque bit clear, its transparent texels on index 2, the others on {0, +b, -b}.
- **EAC alpha**: a constant block is exact (base, multiplier 1, the table with a 0 modifier); otherwise
  for each of the 16 tables the multiplier that spans the block's range (and its neighbours), the base
  centred, each pixel the nearest of 8 values.
- Texels of an edge block outside the level (levels smaller than 4) take the nearest texel inside, so
  they do not pull the fit.

Unit tests (`tests/unit/etc2_test.cpp`, synthetic data only, AGENTS.md §1): round trips of flat colours
(exact for 4-bit-representable colours), gradients (planar, bounded error), random blocks (bounded mean
error), punch-through (alpha exact: transparent stays transparent, opaque stays opaque), EAC (constant
exact; error bounded), the BC1 / BC3 level transcode against `BcDecode` (PSNR bound), odd level sizes.
The decoders were cross-checked once against an independent decoder (the `texture2ddecoder` Python
package, scratch only, not in the tree).

## 4. Where it plugs in, and when it is taken

- `Caps` gains `textureCompressionETC2` (enabled at device creation when present) and the device line
  logs it; the path is taken when the device has no `textureCompressionBC`, has the ETC2 feature, and the
  three ETC2 formats are sampleable. A BC-capable device (desktop, the S25: `docs/vulkan/PLAN.md` §2.1 read
  `textureCompressionBC = 1` on its Adreno 830) never reaches it: it samples BC as before.
- `seeded_create.cpp MakeTexture`: where today a BC level set is decoded to RGBA8, a level set whose
  every level holds its blocks is transcoded and the image made in the ETC2 format instead; a short level
  keeps today's RGBA8 decode (the fallback is unchanged). `ImageFormat` keeps answering RGBA8 for a
  BC format on such a device: an image made without levels stays what it is today.
- No platform `#if`, no env flag, no getenv: the capability decides.

## 5. The derived-data cache, and the load-time budget

The transcode is keyed in the existing derived-data cache (`src/ext/derived_cache/`, ruled allowed
2026-10-22; the Vulkan shader cache already uses the shared store through `OpenShared`): producer
`'ETC2'`, an encoder version, shape {width, height, levels, source format}, the source levels' size and
chained Hash64. A hit is the stored transcode; a miss is encoded and stored. Under the faithful profile
there is no cache (every boot encodes), as for the shader cache. A load-end summary line
`[Etc2] textures= cache_hits= encoded= bc= etc2= encode= ms` follows the other load-end lines.

Budget: the first boot pays the encode, later boots a hash and a read. The encode runs where the decode
runs today (the async-create workers); the target is the first-boot load no more than ~2 s longer than
today's (the town's ~11.5 s load), and a cached boot no slower than today's decode boot. Measured on the
S10 (§6); if the first boot is over budget the encode of large levels is split across a worker pool as
compress_ao's `bc4_chain.cpp` does.

## 6. Half textures on low-memory phones

- **Hook**: `Platform::LowMemoryDevice()` (`src/platform/common/platform_memory.h`), whole-file twins:
  Windows and POSIX (Linux, macOS) answer false; Android reads the total RAM (`sysconf` physical pages)
  and applies the pure rule `Platform::IsLowMemory(totalBytes)` in the header (unit-tested).
- **Threshold: total RAM below 6.5 GiB as the kernel reports it.** A phone sold as 8 GB reports ~7.2-7.6
  GiB (the S10: 7.6), one sold as 6 GB ~5.4-5.7 GiB, 4 GB ~3.5-3.8 GiB (the rest is firmware carve-outs);
  6.5 GiB sits between the 6 and 8 GB classes with margin on both sides. With the transcode the app holds
  ~0.55-0.6 GB; on a 6 GB phone that is still ~10% of RAM and on a 4 GB one ~15%, where the low-memory
  killer reaps a background app of that size first; Half takes ~0.1 GB more off (Half's measured -228 MB
  was of RGBA8; at ETC2's size it is a quarter of the transcoded bytes, estimated, measured where possible).
- **How it becomes a default**: the phone seed (`app_android.cpp`, the mechanism every phone default
  uses) gains `ext.texture_quality=1` and `ext.texture_quality_drop=1` when the hook answers true. The
  seed only adds a key the file lacks, so the player's later choice in Options > Enhanced (Texture
  quality) is kept, as for every other phone default. The S10 (7.6 GB) gets no such lines: Full.

## 7. Verification plan

- `tools/build.sh unit` (BUILD_JOBS=2); the Windows Vulkan run (`BANISHED_RENDER_BACKEND=vulkan
  tools/build.sh run`) unchanged: the BC path (the RTX 5080 has BC and no ETC2).
- S10, release + PGO apk: menu and town screenshots against the current RGBA8 decode (banding on sky and
  terrain, alpha edges on foliage); load time before / after (first boot and cached boot); GPU memory
  before / after by the P13_PROFILE method (kgsl `page_alloc` with the app running minus stopped); the
  stress town fps in one ABBA pair (battery <= 32 C, thermal status 0 at the start).
- Half on low memory: the rule unit-tested; on the S10 the seed adds nothing (Full).

## 8. Results

(Filled in after the runs.)
