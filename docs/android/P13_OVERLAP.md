# Android P13: the render thread's emit overlapping the sim steps (design)

Owner rulings of 2026-10-28: (1) the render thread's emit may overlap the next frame's sim steps, the sim
staying bit-identical, only the render timing and the snapshot the emit reads may change; (2) fewer frames
queued for input latency, net latency in normal play no worse than today. This note is the design, written
before the code; the measured outcome is appended at the end (section 9).

## 1. Where the time goes, and why the fence exists
`P13_PROFILE.md` section 3: at speed 10 every Frame steps, and `ext.render_pipe`'s THE FENCE
(`kExtBeforeSteps`, `pipe_driver.h` `FenceReleased`) makes the main thread wait until the render thread's emit
of the previous packet released itself (after the post-draw callbacks). The main thread spends 14.3 ms a frame
on its own work and **4.45 ms waiting in that fence**: the frame is `steps + main half + emit up to its
release`, serial.

The fence exists because the emit reads live state the steps write (RENDERPIPE0.md section 3, RENDERPIPE1.md
section 2). The packet carries values only for the context writes (fix 3), the camera matrices, the tool
pointer and flags; everything else is read live, through pointers:

| emit step | what it reads live | who writes it during the steps |
|---|---|---|
| `Pass2`: `Renderer::DrawItemList` / `ext.draw_batch` over the packet Renderer's lists | each item's **Geometry**: `_transform` pair, `_opacity` pair, a MeshInstance's `_nodes` pairs, `_color`, `_mesh`, `_animation` (the instanced pose in `AnimationManager::_instances`), a ParticleInstance's `_boundsPtr` pool (live records), StatusIconGeometry's sprite mask / size, WeatherParticleGeometry's `_particles` / `_indices`, WorldGeometry's `_decals` / `_roadBatches` vertex arrays, an EdgeMeshInstance's `_followed` MeshInstance | `AnimationManager::Update` (every iteration rolls every pose pair in place), `EntityList::Update` (component ticks: `SetTransform`, opacity, animations, entities created and **destroyed**, which destroys their embedded MeshInstances and particle pools), `Weather::Update`, `Scene::Clean` (GameUpdateLoop::Update step 15), the decal / road writers (tools, `DecalComponent`, the road action) |
| `Pass2` | the item's `MaterialInstance`, `GraphicsMesh`, textures, render buffers, the SunLight | nothing: factory-owned resources (`ExternalPtr` is a raw pointer, the factory owns them), freed only when the loop is torn down; GPU companions are created / destroyed only under `Video::Device::Lock`, which the emit holds; the sun is written only by the main half while nothing is pending (fix 4) |
| `SaveLoadDraw`, `ToolDraw`, `UIDraw`, `PostDraw` | the UI tree (`UserInterface::Draw` via `SpriteContext`), the current tool and the map / grid it reads to colour its footprint, SaveLoad's capture state, the post-draw callbacks | the message pump (each step's first action), the UI and tool updates, the sim (map occupancy), a loop change |
| `ClearResourcesIfDirty`, `BeginFailed` | `MainLoop::_contextDirty`, `_currentLoop` | a loop change inside `MainLoop::Update` |

So the overlap needs two things: what pass 2 reads must come from a snapshot the steps cannot touch, and the
UI-side draws must not run while the steps do.

## 2. The design
### 2.1 The emit, cut in two at a gate
`packet.h` `Emit` gains one op, `Gate`, between `Pass2` and `SaveLoadDraw`:

- **E1** (`FrameTailTime` .. `Pass2`): reads only the packet, its Renderer slot and the **snapshot** (2.2).
  It runs while the main thread runs the next Frame's steps.
- **Gate**: when the packet carries a snapshot (`Packet::overlap`) and the emit is on the render thread, the
  render thread drops the device lock, parks until the main thread opens the gate, and takes the lock again.
  Sequential split or no snapshot: a no-op.
- **E2** (`SaveLoadDraw` .. `PostDraw`, `Released`, the tail): unchanged code, reading the UI, the tool and the
  post-draw state live, **after the steps of the next Frame**: the gate opens at the Frame tail
  (`kExtFrameTail`, before the main half) and before every wait on the render thread (`Handoff::WaitIdle`).

E2 therefore overlaps only the next main half, which is what the whole emit overlaps today on every Frame that
does not step (RENDERPIPE1 section 3 audit: the main half writes nothing the emit reads, except the camera and
the sun, which it writes only when nothing is pending). The steps and E2 never run together.

The frame becomes, at speed 10: main `[steps N+1 | main half N+1 | hand-off]`, render `[E1 N (during the
steps) | gate | E2 N + tail (during the main half)]`. The main thread waits only at the hand-off, for E2 and
the tail, which are shorter than the main half (~1.5-2 ms against ~5 ms in the stress town).

**What the frame shows changes, the sim does not.** The scene of packet N is the state the main half of N
gathered, as today. The UI, the tool's ghost and footprint and the save-thumbnail capture of packet N are
drawn from the state after the steps of N+1, up to one step newer than the scene under them (16.7 ms at 1x,
one Frame at speed 10). Visible only in motion: a tool ghost leads the scene by one frame while the camera
pans; text and counters update one frame earlier. At a paused frame the image is identical.

### 2.2 The snapshot: frozen copies of the drawn geometry
At the end of the main half (main thread, the sim quiescent, after pass 1 and the RenderPipe shadow hash),
`Snapshot::Build` walks the packet Renderer's draw-group item lists and its drawables, and for every distinct
Geometry an item references makes a **frozen copy** in a per-slot arena, then points the item at the copy.
Pass 2's code (faithful `Renderer::DrawItemList`, `SunLight::DrawLight` / `DrawShadowMap`, `ext.draw_batch`,
each Geometry's `Draw` / `GetInstanceTransform` / `SetSharedInstanceData`) is unchanged and simply runs on
the copies. The copy keeps the vptr; its arrays point at arena copies:

| exact dynamic type (`typeid`) | copy | deep copies |
|---|---|---|
| `MeshInstance` | `sizeof(MeshInstance)` | `_nodes`, `_materials`, `_animation._layers`; instanced: the `InstancedAnimationState` record and its `_nodes` / `_pose` (2.3) |
| `EdgeMeshInstance` | `sizeof` | `_followed` re-pointed at the followed MeshInstance's copy (copied too if it is not drawn) |
| `ParticleInstance`, `ParticleSystemInstance`, `TornadoInstance` | `sizeof` of the type | the `_boundsPtr` record and its `_records` up to the live count |
| `StatusIconGeometry` | `sizeof` | none (inline fields, the sprite sheet is a resource) |
| `WeatherParticleGeometry` | `sizeof` | `_particles`, `_indices` |
| `WorldGeometry`, decal / road items only (key mode 0x2000 / 0x4000) | `sizeof` | the `_decals` / `_roadBatches` headers; the vertex / index arrays of the batches the packet draws (the others emptied) |
| `WorldGeometry`, every other mode (chunks, water, contours, `ext.terrain_batch` runs) | **not copied**: the live object | these draws read only fields fixed after the world build (grid and chunk sizes, the contour ranges) and the embedded GPU buffers (written only under the device lock) |
| a drawable that is the scene's `SunLight` | not copied | written only by the main half while nothing is pending |

**Any other type, any other drawable, or `ext.terrain_edit` active (its brush rewrites the contour ranges and
water buffers in a step) makes the packet ineligible**: no copies, `overlap = false`, and the frame runs with
today's fence. Correctness never depends on the list being complete; only the gain does.

Why copies of whole objects and not a list of values: the draw code is faithful and reads `this`; a copy is
the one way to give it a frozen `this` without editing a dozen Draw bodies. The copies are never destroyed
(trivial bytes in an arena), never registered anywhere, and never seen by the main thread's code except the
snapshot builder. Their `_attached` / `_attachParent` still point at live objects; pass 2 never reads them
(it is `UpdateAttached` / `Clean` that do, both main-thread).

A copy of the embedded `Video::VertexBuffer` members of WorldGeometry would be bound by pointer, cached by the
Context across frames and could alias a later arena object; that is why the chunk / water modes keep the live
WorldGeometry. The decal / road modes draw through the Context's dynamic buffers only.

Copy order is the items' order, so instanced and batched runs (`ext.draw_batch` groups consecutive items by
geometry, instance and material pointer) group exactly as before: every item of one geometry points at its
one copy, and the sort ran before the swap.

### 2.3 The instanced pose: one engine seam
An instanced MeshInstance does not own its pose: `GetBones`, `InstancedPoses` (meshinstance_draw.cpp),
`RtInstancedPoolSlot` (mesh.cpp: GetNodeToWorld, RootAnimationOffset, GetInstanceTransform) and
`SkinMemo`'s `PoseArray` look it up in `AnimationManager::_singleton->_instances` by the last layer's handle,
and `AnimationManager::Update` rolls that record in place every iteration. One null-by-default static,
`Graphics::MeshInstance::_extInstancedState` (DESIGN.md hook rule 5), is consulted by those four on the
instanced branch only: non-null and answering for a copy, it returns the copy's frozen record; null (always
under the faithful profile, and for every live object) the faithful lookup runs. The answer is decided by
the address (the copy lies inside a snapshot arena), so the main thread's live objects never take it.

### 2.4 Loop changes: one more seam point
A step can end the loop (`MainLoop::Update`: `Shutdown`, `delete` the GameUpdateLoop and with it Renderer
slot 0, the scene, the loop's resources). E1 reads slot 0 and resources, E2 the loop's UI. A third point on
the existing seam, `kExtBeforeTransition`, is called by `MainLoop::Update` before `Shutdown`; the hook joins
the render thread there (opens the gate, waits idle, collects the result). The Frame's tail then sees the new
loop and runs unpipelined, as today. Nothing else in a step frees what E1 reads (2.2's resource row).

### 2.5 Ordering (happens-before pairs; `handoff.h` gets the gate)
- publish (unchanged): the packet, the copies and the arena are written before `Publish` (unlock = release);
  the render thread's `WaitJob` lock acquires them.
- gate: the main thread's `OpenGate` (lock, `gate = true`, unlock, notify) after the steps; the render
  thread's `WaitGate` (lock, wait for `gate`) before E2: every step write happens-before E2's reads.
- idle (unchanged): E2 and the tail happen-before the hand-off / drain / transition joins.
- What E1 and the steps share, and why it is safe: the packet Renderer slot (written by pass 1 only, the
  ping-pong keeps the next main half on the other slot), the copies and their arena (written once, before
  publish; the arena of a slot is rewritten only after that slot's packet was joined), `Video::Context` (E1
  only; no step code touches the Context, only device-level uploads under the lock), resources (factory-owned,
  freed only at a loop change, 2.4), GPU companions (created / destroyed under the device lock, which E1
  holds; a step that needs it waits until the gate releases it), the sun (main half only, fix 4),
  `MainLoop::_frameUpdateTime` (written by the previous emit's tail, read by `SetTime`, both render thread).
  Benign word-sized reads kept from today: `Video::Device::backBufferWidth` (EdgeMeshInstance, UI) and
  `GameSettings::statusIconScale`, which a step applying options may rewrite.
- Lock order: the render thread holds the device lock only inside E1 and inside E2 + the tail up to
  `FinishPresent`'s unlock, and never while it waits for the gate; the main thread never waits on the render
  thread while holding it. No cycle.
- A step that takes the device lock (the minimap overlay upload, step 16; terrain chunk uploads; the editors)
  waits for the gate's unlock, then uploads in the middle of the frame being recorded: on DX11 an immediate
  update, on Vulkan the upload batch that is submitted before the frame's command buffer. The frame then draws
  that texture or buffer one step newer (the snapshot ruling); nothing is torn, the copy completes first.

### 2.6 How the sim stays identical
No sim code changes; no sim code is reordered. The steps run the same functions in the same order on the same
data; nothing they read is written by the render thread (the emit writes the Context, the frame index,
`_frameUpdateTime`, the RenderPipe counters, SaveLoad's capture state: the last is consumed by the step after
the next hand-off, the same step as today; the capture's pixels are a thumbnail, never sim input). The only
main-thread change is where it waits. Verification (section 8): the resave gate (faithful profile, golden
unchanged) and again with the settings profile (render_pipe on), unit tests for the new protocol, and the
RenderPipe shadow extended to hash the copies against the live objects at hand-off and again in E1.

### 2.7 Cost
The copies: one pass over the items (a pointer hash for the distinct geometries) and the copy itself, on the
main thread. Estimate for the stress town: a few thousand MeshInstances (~0.3 KB each plus 56 bytes per node),
1-3 MB a frame, 0.3-0.8 ms on the S10; measured in section 9. The arena is kept across frames (two, one per
Renderer slot), grown with headroom. Side effect: pass 2 walks contiguous copies instead of scattered
objects (P13_RENDER section 3.3: the scattered MeshInstance vtable loads were 40 % of `ExtDrawItemList`).

## 3. Latency (ruling 2)
Normal play today: post-to-present 38-42 ms, ~2.4 frames queued (P13_PROFILE section 8). The queue is the
swap chain's: under FIFO the app runs ahead until `vkAcquireNextImageKHR` blocks, so the depth is the image
count. `kSwapchainImages` 3 was raised to the surface minimum on the S10 (the log line says how many).
Candidates, measured with `dumpsys SurfaceFlinger --latency` in normal play and by fps in the stress town:
(a) under FIFO ask for the surface's minimum image count (MAILBOX / IMMEDIATE keep 3: capability-driven, in
the backend, no platform test); (b) `kFramesInFlight` 1; (c) both. The overlap itself moves the Present of a
packet later by the length of the steps (E2 waits for them) while the UI of that packet reflects one step of
newer input; section 9 has the measured net.

## 4. The 1.3 path, desktop, S25
Nothing here is Android- or 1.1-specific: the snapshot and the gate are in `src/ext/render_pipe` and run
wherever the render thread runs (`Cpu::RenderThread`: three physical cores or more), DX11 or Vulkan. The
sequential split (fewer cores) is unchanged: no gate, no copies, the fence as today. The swap-chain change is
in the Vulkan backend and keyed on the present mode and the surface's minimum.

## 5. Counters
`[Stress::pipe]` keeps its fields; `waitsFence` falls to the ineligible packets; the hand-off wait now
includes E2 and the tail. One field is added: `overlapped` (packets emitted with a snapshot), and the
snapshot's copy time joins the main half's draw time (it is main-thread work).

## 6. Risks
1. An unlisted live read in pass 2 (a Draw body reading through a pointer the copy shares with the live object).
   Mitigation: the per-type table above came from reading each Draw / GetInstanceTransform /
   SetSharedInstanceData body; the RenderPipe shadow (COMPPROF=4) hashes, at E1, everything RENDERPIPE0's
   shadow lists, now from the copies.
2. A step that frees a resource outside a loop change. None found (2.2); a crash would point at it.
3. UI one step newer than the scene (2.1): a visible change only in motion.
4. Device-lock waits in steps cost overlap on those steps only (never more than today's fence).

## 7. Not done
Copying the chunk / water WorldGeometry modes, a display list for the UI (drawing it from packet-time state),
a third Renderer slot (depth 2).

## 8. Verification plan
Windows: `tools/build.sh unit`, `tools/build.sh run` + `tools/boot_state.sh --wait 300`, the resave gate
(`Gate`, faithful: PASS, golden unchanged; and `GATE_PROFILE=` with render_pipe on), `BANISHED_RENDER_BACKEND=vulkan
tools/build.sh run`, a COMPPROF=4 stress run (the RenderPipe shadows at 0). S10: stress ABBA at the 60 cap
(release + PGO, debug key; battery <= 32 C, thermal 0 at start), normal play 3 min with SurfaceFlinger
latency, screenshots of animated characters.

## 9. Results
(appended after the measurements)
