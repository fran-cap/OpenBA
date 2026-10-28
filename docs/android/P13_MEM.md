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

## 8. Results (2026-10-28)

Builds: `tools/android.sh build release --jobs 2` (-O3, ThinLTO, the P13 PGO profile), signed with the
debug key. **B** = this branch; **A** = the same tree with `caps.etc2Transcode` forced false (the RGBA8
decode), built for the comparison only and never committed. S10 (SM-G973U, Adreno 640, driver 1.1.128).

**The device.** `cmd gpu vkjson`: `textureCompressionBC` 0, `textureCompressionETC2` 1,
`textureCompressionASTC_LDR` 1; the device line reads `bc=0 etc2=1 (transcode 1)`. MemTotal 7,640,540 kB
(7.29 GiB): not low memory, the seed adds nothing, `[TextureQuality] off: drop=0` (Full). The S25 has BC
(`PLAN.md` §2.1, read 2026-10-24): it never takes the transcode.

**GPU memory** (kgsl `page_alloc` with the app running minus stopped, the P13_PROFILE method):

| | A (RGBA8) | B (ETC2) | change |
|---|---|---|---|
| main menu (22 s after launch) | 589.6 MB | 394.4 MB | -195 MB |
| new 1x town, camera pin at 25, 45 s after load | 656.6 MB | 390.0 MB | -267 MB |
| stress town, t = 70 s (two runs each) | 657.9 / 660.8 MB | 391.7 / 393.2 MB | -267 MB |

`[Etc2]` at the stress town's end: 127 textures, 66.3 MB of ETC2 where RGBA8 held 397.6 MB (P13_PROFILE's
397 MB); no BC1 texture has a transparent texel (`punch_through=0`), so RGB8A1 is unused by the retail data.

**Stress fps, one ABBA pair** (6000 frames, cap 60, battery 31.8-32.0 C and thermal 0 at each start, 33.3-33.5
C at the end; means of t >= 20 s):

| run | fps | frame ms (p95) | update | draw |
|---|---|---|---|---|
| A | 51.7 | 19.43 (28.21) | 9.16 | 8.04 |
| B | 52.2 | 19.24 (27.94) | 9.22 | 7.88 |
| B | 52.6 | 19.13 (27.32) | 9.12 | 7.93 |
| A | 52.7 | 19.06 (27.34) | 9.11 | 7.90 |

Neutral (A 52.2, B 52.4 mean): within run-to-run noise.

**Load time.** Launch to the menu's first update: A 2.15-2.19 s, B 2.16-2.23 s. Stress town, loader done:
A 12.17 / 12.12 s, B 12.20 / 12.06 s (cache warm). ext.create_on_demand makes most textures on its
prefetch workers after the load, so the encode lands there: on the **first boot** (no ETC2 entry in the
derived cache) the 1x town's load took 5.01 s against A's 4.91 s, the encode summed 4.5 s of worker time,
and the prefetch fell behind: 166 textures were made on use (527 ms in all, **worst 89.8 ms**, a
first-boot hitch; A: 0 on use). Every later boot finds them: the stress runs had 115 of 127 from the
cache (the 12 others were the stress town's own, first seen there), 0 made on use, and the makes took
844-908 ms against A's 1534-1612 ms (a sixth of the bytes to upload). The cache holds ~66 MB more.

**Image.** Screenshots (scratch, not in the tree): menu, the 1x town at distance 25 and the stress town,
A against B side by side. No banding on terrain or water, foliage and status-icon alpha edges the same,
roofs and wood the same at this zoom. The encoder's error on 40 real levels (top levels of the menu and
town textures, read on the Windows Vulkan build by a probe never committed): **35.7 dB PSNR** against
the BC decode, pixel-weighted (BC1 37.0, BC3 35.3); the worst level 25.5 dB (a BC3 atlas whose
transparent texels' colour counts in that figure). Throughput 12 Mpx/s a core on the desktop (from 3 Mpx/s
in the first version: the table pick by the luminance identity, planar only when competitive, EAC over
the distinct values).

**Windows.** `BANISHED_RENDER_BACKEND=vulkan tools/build.sh run`: `bc=1 etc2=0 (transcode 0)`, the BC
path unchanged, a new game reached, 0 validation errors. `tools/build.sh unit` (BUILD_JOBS=2): 1570 tests,
0 failures, 3 skipped (the kit-font ones, as before).

**Not done / open.** The Half default was not run on a low-memory phone (none attached); its rule and
seed lines are unit-tested and the S10 confirms the Full side. The first-boot hitch (worst 90 ms) could
be taken off by encoding on the prefetch worker ahead of use or by a faster (SIMD) encoder; T / H modes
would lift two-colour blocks (unit test 29 dB) if a texture ever shows it.

### Verified versus assumed
- Verified: the S10's features and MemTotal; the GPU memory, fps, load and on-use numbers above; the
  decoders against an independent decoder (`texture2ddecoder`, 256 random words per format, every mode);
  the encoder's PSNR on real levels; the Windows BC path; the unit tests.
- Assumed: the RAM figures of 4 / 6 / 8 GB phone classes (vendor carve-outs vary); that a low-memory
  phone has ETC2 (every Android Vulkan device should); Half's memory at ETC2 size (estimated a quarter of
  the transcoded bytes, not measured).

## 9. The encode before the load ends (ruled 2026-10-28)

The ruling: no texture is made at its first use because its transcode is still pending; the encode runs
on the loading side, ahead of first use. ext.create_on_demand already queues every texture of a mesh
instance for its prefetch workers when the instance gets its mesh (during the load, for the town), so
the transcode was always on those workers; on a cold cache they fell behind the first frames. Now, on a
device that transcodes, the loader bracket's close (`DeviceData::Slot4(false)`) waits for the prefetch
queue (`CreateOnDemand::FinishPrefetches`, over the new `LazyTable::DrainQueueUnlessPaused`, which also
returns when the table is paused by a device going away) before the load ends. A load whose prefetches
are already made waits nothing; `[CreateOnDemand] load end waited N ms` says when it did.

S10, stress town, release build: **before** = the §8 build, **after** = this one. "Cold" = the whole
derived cache deleted before the run (shader conversions included, so both cold runs also convert
their shaders); "warm" = the cache of the previous run.

| | before cold | after cold | before warm (2 runs) | after warm |
|---|---|---|---|---|
| launch to the menu's first update | 2.24 s | 3.27 s | 2.17 / 2.16 s | 2.08 s |
| stress town load (loader run to done) | 8.73 s | 8.62 s | 8.71 / 8.61 s | 8.62 s |
| launch to the town's loader done | 13.13 s | 13.48 s | 12.20 / 12.06 s | 12.02 s |
| textures made on use (worst) | 131 (117 ms) | **0** at the load's end, 1 in play (0.9 ms) | 0 | 0 at the load's end, 1 in play (0.4 ms) |
| load-end wait | - | menu 1178 ms, town 61 ms | - | 59 ms |
| stress fps | 52.8 | 51.8 | 52.2 / 52.6 | 53.5 |

- The first boot's hitch is gone: no texture is made on use while its transcode is pending. The one
  object made on use in play (both after-runs, under 1 ms) is not a transcode: no `[Etc2]` encode is
  counted after the load's end.
- The cold cost moved to the menu: the main menu's scene textures (~43 MB of ETC2) are encoded before
  the menu shows, 1.0 s later on the first boot only. The town's own textures are mostly done by its
  load end (61 ms wait) because the town load is long (8.6 s) next to its encode.
- Warm boots are unchanged (menu 2.08 s, town 8.62 s), and the fps of the one after-run is inside the
  earlier runs' spread (no regression; one run, not an A/B pair).
- The warm runs still encode 2 textures (the stress town's own, first met in the cold run, whose cache
  writes a force-stop at the run's end can cut off; assumed, not traced).
