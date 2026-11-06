# Leaf-card / billboard optimisation: research note (issue #253, 2026-11-05)

A literature, vendor-guidance and shipped-game survey, ranked against the S25 measurements in
`docs/android/I253_RENDER.md`. Nothing here is built yet; it is the plan for a later pass. Measured on the
S25 before this note: halving deciduous card area gains +9% with 2x MSAA and nothing without; an early
low-coverage discard under MSAA gains only 2-3% (every rasterised fragment still launches); front-to-back
order inside a batch gains nothing. Any method must be generated at load from the user's installed (and
mod) tree assets and keep seasons, tint and wind working.


## Summary

The S25 forest at max zoom is limited by fragment work. It runs about 10 leaf fragments per pixel, and only about 3.4% of crown fragments are kept. The extra cost shows up under 2x MSAA with A2C. Three methods fit that profile and can be generated automatically from installed or mod assets:

1. **Card trimming.** Each card is drawn as a tight polygon around its leaf texture's opaque texels instead of a full quad. The image is identical, and it can work in faithful mode.
2. **Fewer, bigger cards with distance**, SpeedTree style. It works in the vertex shader only, and seasons and tint keep working.
3. **A leaf depth pre-pass, then a colour pass with no discard.** This attacks the Better Trees shader cost under MSAA.

Impostors are the largest possible win at far zoom, but they take the most work and change the image. They rank 4th.

One check on our own code: the Vulkan backend already keeps MSAA in tile memory through `VK_EXT_multisampled_render_to_single_sampled` (`src/render/vulkan/draw.cpp:416`, `:619`, `:653`). The vendor "transient MSAA attachment" advice is therefore already done wherever `msrtss` is live. The only thing left to check is that it is live on the S25: the boot log or LiveCaps should show `msrtss=1`.

## Verification status of the load-bearing claims

| Claim | Source opened | Status |
|---|---|---|
| Persson trimming algorithm. Area vs the full quad: aligned rect 69.23%, 3 verts 70.66%, 4 verts 60.16%, 5 verts 55.60%, 6 verts 53.94%, 7 verts 52.31%, 8 verts 51.90% (one smoke-puff texture) | humus.name ID=266 | **verified** |
| Just Cause 2 used 4 vertices (clouds) and 8 (GPU particles) | not on ID=266; cited from PDF speaker notes | unverified |
| SpeedTree leaf LOD: "fewer and bigger leaf instances" with distance; some shrink away while the rest are scaled up; per-vertex LOD scales in the VS; billboard fade smooth under A2C; overhead billboard cross-faded by camera angle; no numbers | Unity SpeedTree SDK LOD page | **verified** |
| Epic impostor: sprite card of 8 tris / 9 verts vs 8 billboard cards of 72 tris / 81 verts; "billboards still have worse overdraws for their masked materials"; mobile samples a single frame; Fortnite uses Upper Hemisphere impostors for trees when Nanite is off; the capture switch strips world colouring so impostors can be tinted after baking | Epic Impostor Baker doc | **verified** (the page does not use the word "octahedral") |
| MJP, RX 7900 XT: back-to-front 648,000 PS invocations; front-to-back 440,640; with a never-taken discard about 582,000. Discard is fully compatible with early-Z when depth writes are off | therealmjp | **verified** (no measured count for discard with depth writes off) |
| Mesa: discard draws cannot feed LRZ in the binning pass, but can in the rendering pass through LRZ feedback | Mesa lrz.html | **verified** (page says nothing about A2C, EQUAL or blending) |
| Igalia: EQUAL / NEVER temporarily disable LRZ; fragment depth writes disable it completely; blending, logic ops and colour write masks disable it temporarily | Igalia adreno-lrz | **verified** |
| Adreno writes no LRZ for A2C draws (Mesa MR 18083) | not opened (bot-blocked) | unverified |
| Qualcomm: 2x MSAA has no "significant additional cost", above 2x "it can become expensive"; resolve happens in GMEM | Qualcomm anti-aliasing page | **verified** (only the first 100k characters were read) |
| Qualcomm FAQ: alpha test under MSAA "only get[s] a late Z test" | FAQ redirects; seen only in search snippets | **unverified**. Our measurements fit it, but it is not proven |
| Arm G925 fragment pre-pass: late-Z work runs in both passes and "can potentially mean doing more work overall"; keep the A2C alpha computation simple; put discards early in the shader; Fortnite Parachute −6.5% GPU cycles, −16.1% FP instructions | Arm blog | **verified** (other titles' numbers did not come back in my fetch) |
| Khronos/Arm, Mali-G76: write-back resolve costs about +3% bandwidth; a separate resolve pass +5 GB/s (about 500 mW); storing MSAA colour and depth +6.3 GB/s (+302%) | Vulkan-Samples msaa README | **verified** |
| Castano: bisect for a per-mip reference, scale that mip's alpha by A_r / a_r; applies to A2C with a reference near 1; no run-time performance numbers | ludicon.com | **verified** |

## Ranked table

Effort is across DX11, Vulkan and Metal: S is about a day, M a week, L several weeks.

| # | Method | What it attacks here | Expected gain (far zoom, tiler) | Image | Auto from user / mod assets | Seasons / tint / wind / shadows | Effort | Status |
|---|---|---|---|---|---|---|---|---|
| 1 | Card trimming (conservative convex hull, 6-8 verts) | transparent card area; per-sample late-Z under MSAA; shadow pass too | **small-medium**: below the +9% of the half-area test at 2x MSAA; about 0 at 1x | identical if conservative | yes: per leaf texture (or atlas sub-rect) at load, cacheable | unaffected: same cards, same UVs; applies to the shadow pass as well | M | verified |
| 2 | Leaf-card LOD: fewer, bigger cards with distance | overlap between cards (most of the ~10 layers); vertex / binning cost | **medium-large** at max zoom | changes it (coarser crowns); ext row | yes: a rank per card at load plus VS scaling | tint and seasons unchanged; leaf fall must share the rank so the two do not fight; use the same cut in the shadow pass | S-M | verified (mechanism; no numbers anywhere) |
| 3 | Leaf depth pre-pass (cheap alpha-only shader), then a colour pass with LEQUAL, no depth writes, no discard | the Better Trees PS cost of hidden layers; the A2C path | **unknown, possibly medium** with Better Trees on; about 0 when off | identical if pass 2 reproduces pass 1's coverage | yes (render path only) | no asset interaction; season effects stay in pass 2; leaf fall's alpha must also be in pass 1 | M | mechanism verified; no mobile foliage gain found |
| 4 | Hemi-octahedral impostor, or 8 views + top-down billboard, at far zoom | overlap, vertex count; a whole crown drops to about 1 layer | **largest** (about 10 layers down to about 1) | changes it a lot; ext row | yes: offscreen bake at load from installed meshes, derived-data cache | bake untinted albedo and tint per instance (as Fortnite does); leaf fall and blossom need one bake per state or extra channels; wind frozen; shadows need their own view or keep the real mesh; no fragment depth writes on Adreno | L | verified (Epic); no fps numbers |
| 5 | Cheaper Better Trees tier under MSAA (no A2C; plain alpha test plus sharpening, or A2C only at 2x) | the A2C sample path | up to part of the 22% (73 → 57 fps) | more aliased edges; ext setting | yes (shader only) | none | S | sources verified (Arm, Qualcomm 2x) |
| 6 | Leaf shader order: compute the discard decision first; do every derivative-based sample before the discard (explicit LOD after it) | helper-lane / demote work after the discard | small (we measured 2-3% from early discard; this targets why it was small) | identical | yes | none | S | partly verified (Arm advice); helper-lane mechanism from secondary sources |
| 7 | Castano coverage-preserving mips in place of the in-shader distance fill | per-fragment ALU in Better Trees; also defines the region the hull in #1 must enclose | small | ext-only change (distance fill is already Better Trees) | yes: re-mip at load; needs BC / ASTC re-encode | none | M | verified (algorithm) |

## Per-method notes

### 1. Card trimming (Persson / Avalanche "particle trimmer")

- **How it works.** Threshold the alpha, take the convex hull of the set texels (skipping texels that have 5 or more set neighbours), then try hull-edge combinations at the target vertex count. Keep only the polygons that enclose everything, and pick the smallest. In our VS, each card would expand to N hull corners (UV + offset from a per-texture table) instead of 4.
- **Keeping the image identical.**
  - Faithful mode tests alpha ≥ 0.5 after bilinear filtering and mip sampling. Build the hull over the union of texels that pass at every mip that can be sampled, dilated by one texel of the coarsest mip.
  - With Better Trees on, the threshold is the lowest alpha that distance fill plus the sharpened A2C ramp can turn into coverage. Build that hull separately per mode.
  - Clamp the polygon to the sub-rect so it never picks up a neighbouring atlas tile.
- **Expected gain.** The half-area test shrank whole cards, which cut overlap as well as transparent area. A conservative hull removes only empty texels, so the real gain is below +9%, and about 0 without MSAA. Persson's 52-60% figure is for a half-empty smoke texture; our leaf textures may differ.
- **Open question.** Do our cards each use a whole texture or a sub-rect of an atlas? The hull table is keyed on that.
- **Shadow pass.** The same hull applies to the cards when they are re-faced to the sun.
- Source: https://www.humus.name/index.php?ID=266

### 2. Fewer, bigger cards with distance (SpeedTree leaf LOD)

- **How it works.** At load, give each card a stable random or importance rank. In the VS, collapse cards ranked below a cut driven by distance or screen size, to zero area. Scale the survivors by sqrt(N/kept) so crown coverage is unchanged.
- **Seasonal leaf fall** already removes cards. Use one rank for both, so the season cut is the max of the two and they do not fight.
- **Shadows.** Use a fixed cut in the shadow pass so the shadow does not breathe as the camera zooms.
- **Keying.** Key the cut on screen size, not pixels, so phone and desktop behave the same (the lesson from Cities: Skylines II).
- **Expected gain.** Overlap is most of the 10x, so the ceiling is higher than for #1. No source publishes numbers.
- Sources: https://docs.unity3d.com/speedtree-runtime-sdk/manual/level-of-detail.html , https://docs.speedtree.com/doku.php?id=lod

### 3. Leaf depth pre-pass

- **Pass 1** is depth only. It samples alpha (plus leaf fall) and discards; with MSAA it uses A2C so per-sample depth is right.
- **Pass 2** runs the full Better Trees shader with depth writes off, no discard and no A2C. The per-sample depth test then gives the coverage.
- **Use LEQUAL, not EQUAL**, in pass 2: EQUAL temporarily disables LRZ (Igalia).
- **On A7xx**, pass 1's discard draws can still feed LRZ during rendering (Mesa feedback).
- **Costs.** Leaf geometry is binned twice, at about 122-240 triangles per tree. Pass 1 still pays the full fragment count, so the method only wins by the gap between the full shader and the alpha-only shader. With Better Trees off it is likely a loss.
- **Mali.** Arm's G925 hardware pre-pass already does this. Arm advises against app pre-passes on Mali, so make it Adreno-only or measure per GPU.
- Sources: https://therealmjp.github.io/posts/to-earlyz-or-not-to-earlyz/ , https://docs.mesa3d.org/drivers/freedreno/hw/lrz.html , https://blogs.igalia.com/dpiliaiev/adreno-lrz/

### 4. Impostors at far zoom

- **Layout.** Our camera is steep, so use the hemisphere layout, or the cheaper SpeedTree form of 8 azimuth views plus an overhead billboard.
- **Frame blending.** Mobile samples a single frame (Epic), which avoids 3x the fetches.
- **Depth.** Keep the impostor alpha-tested inside a trimmed hull (#1) and never write fragment depth (LRZ is disabled completely).
- **Bake per tree type and per season state**, untinted, and apply per-tree tint at draw time.
- **Transition.** A cross-fade band doubles fragment cost, so keep it narrow. With no TAA, a dithered cross-fade shows its pattern.
- **Next step once this exists.** Density-driven switching (Ghost Recon Wildlands): trees inside dense stands switch sooner.
- Sources: https://dev.epicgames.com/documentation/en-us/unreal-engine/impostor-baker-plugin-in-unreal-engine , https://discussions.unity.com/t/hlod-streaming-octahedral-impostors-using-megacity-subscenes/742223 , https://80.lv/articles/vegetation-generation-in-ghost-recon-wildlands/

### 5. A2C tiering

- Qualcomm says 2x MSAA has no significant additional cost on Adreno. Our 22% therefore comes from the leaf path (A2C and per-sample late Z), not from the MSAA resolve. The 22% is our own measurement; the late-Z cause is unverified.
- Arm, verified: keep the A2C alpha computation simple.
- **Option:** a Better Trees setting of A2C / plain alpha test / auto (A2C off when far zoom is GPU-bound).
- Sources: https://docs.qualcomm.com/bundle/publicresource/80-71528-1/topics/anti_aliasing_with_adreno.md , https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/immortalis-g925-the-fragment-prepass

### 6. Shader ordering

- Do the alpha fetch and compare first, with every implicit-derivative sample placed before the discard. Seasonal tint, dapple and lighting come after, with explicit-LOD sampling, so the compiler can terminate lanes instead of demoting them.
- This also helps Mali's G925 pre-pass (Arm: put discards early in the shader).

### 7. Castano mips

- Replaces the run-time `CalcMipLevel`-style distance fill with load-time mips. It saves ALU only.
- It needs re-encoding: BC on desktop, ASTC or ETC2 on Android.
- Its output defines the conservative hull for #1 when Better Trees is on.
- Source: https://ludicon.com/castano/blog/articles/computing-alpha-mipmaps/

## Bound tests to run first on the S25

Same scene for all: planted 100x100 forest, max zoom-out, full resolution, each test at 2x MSAA and at 1x, with Better Trees on and off.

1. **Trimming.**
   - **(a)** Log, at load and on CPU, the conservative hull area as a fraction of the quad for every deciduous leaf texture, at 6 and 8 vertices. If the fractions are above about 0.8, stop.
   - **(b)** As a throwaway, scale each card's corner offsets in the VS toward the card centre by sqrt(hull fraction), keeping the UV mapping. This removes about that share of card area, as the half-area test did. That is an upper bound, because it also removes some covered texels.
   - **Build only if (b) gives at least +4% at 2x MSAA.**
2. **Card LOD.**
   - In the VS, collapse every card whose `hash(cardIndex) < 0.5` to zero area.
   - Run two variants: no scale-up, and survivors scaled by sqrt(2).
   - The scaled variant measures the overlap gain at the same coverage. If it gives under +5%, overlap is not the cost and #2 drops.
   - Repeat at a keep fraction of 0.25 to find the slope.
3. **Pre-pass ceiling.** Swap the Better Trees leaf pixel shader for a trivial one (alpha sample + discard/A2C, constant colour). The fps gap between the two is the most a pre-pass can recover; the trivial pass's own cost is roughly what the pre-pass adds back. Build only if the gap is clearly larger than about one trivial-pass cost.
   - Second measurement: Better Trees with A2C forced off and everything else on. This separates #5 (A2C sample path) from #3 (shader cost).
4. **Impostor ceiling.** Optional, not one of the required three. At max zoom, draw each deciduous crown as one camera-facing card of crown size, using any leaf texture. The fps here is the ceiling of #4.

## Not worth it for us

- **Hashed / stochastic alpha** (Wyman & McGuire): an image fix that costs extra ALU and is noisy without TAA. We have no TAA.
- **Visibility buffer / Nanite masked / Nanite voxels:** an architecture change across three backends, with no mobile path. It does show the industry direction: away from alpha cards at distance.
- **Forest shader-map** (Bruneton & Neyret 2012): research-grade; per-tree seasons and tint would have to go into an aggregate term.
- **Billboard clouds** (Decoret / Colditz / Simplygon): L effort with clustering plus offscreen renders. Impostors plus trimming cover the same ground more simply.
- **Opaque concave cut-out leaf meshes:** tens of vertices per card times 60-120 cards per tree adds a lot of binning cost. Revisit only if #1 pays and leaves a large concave remainder.
- **Alpha blend instead of alpha test:** blended leaves need sorting, cannot occlude, and change the image.
- **Dithered LOD cross-fades on their own:** they only add fragments. They matter only as part of #4, and then with a narrow band.
- **Front-to-back sorting within batches and further draw-call cuts:** already measured, with no gain.
- **MSAA attachment / resolve rework:** already done through MSRTSS. Only confirm it is active on the S25.
- **Cascade update-rate tricks:** we have one shadow map. Only the fixed shadow-LOD idea carries over, and it is already folded into #1 and #2.