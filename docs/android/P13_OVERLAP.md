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
- **Gate**: open by default. THE FENCE (`kExtBeforeSteps`), when the pending packet carries a snapshot
  (`Packet::overlap`) and its emit is on the render thread, **claims** it instead of waiting
  (`Handoff::ClaimSteps`): granted only while the emit has not reached it; an emit already past it is
  fenced as before. Claimed, the render thread drops the device lock at the gate, parks until the main
  thread opens it, and takes the lock again. Unclaimed (a Frame that does not step), the emit goes straight
  through. Sequential split or no snapshot: a no-op.
- **E2** (`SaveLoadDraw` .. `PostDraw`, `Released`, the tail): unchanged code, reading the UI, the tool and the
  post-draw state live. After claimed steps it runs **after the steps of the next Frame**: the Frame tail
  (`kExtFrameTail`) opens the gate and then waits for the release there (`FenceReleased`, the fence moved
  after the steps), before the main half. `Handoff::WaitIdle` opens it too.

The steps and E2 never run together. The main half still runs with nothing pending after a Frame that
stepped, exactly as after today's fence, so fixes 4 and 5 (the sun and the camera, written only while
nothing is pending) keep writing on those Frames. *Revised during the implementation:* the first cut opened
the gate at the tail without waiting; nothing was ever "not pending" at speed 10 then, the sun was never
written (the RenderPipeIdem shadow caught it: every sun check a mismatch) and the camera's frustum would have
frozen in normal play.

The frame becomes, at speed 10: main `[steps N+1 | wait for E2 N | main half N+1 | hand-off]`, render `[E1 N
(during the steps) | gate | E2 N | tail (during the main half)]`. The fence's wait shrinks from "the rest of
the emit up to its release" to "E2", whenever E1 finishes within the steps.

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
*Since section 10:* the static is set only while a packet with copies is in flight, and the answer uses no
thread-local state.

### 2.4 Loop changes: one more seam point
A step can end the loop (`MainLoop::Update`: `Shutdown`, `delete` the GameUpdateLoop and with it Renderer
slot 0, the scene, the loop's resources). E1 reads slot 0 and resources, E2 the loop's UI. A third point on
the existing seam, `kExtBeforeTransition`, is called by `MainLoop::Update` before `Shutdown`; the hook joins
the render thread there (opens the gate, waits idle, collects the result). The Frame's tail then sees the new
loop and runs unpipelined, as today. Nothing else in a step frees what E1 reads (2.2's resource row).

### 2.5 Ordering (happens-before pairs; `handoff.h` gets the gate)
- publish (unchanged): the packet, the copies and the arena are written before `Publish` (unlock = release);
  the render thread's `WaitJob` lock acquires them.
- gate: `ClaimSteps` (lock; refused when the render thread already passed the gate, else `gate = false`;
  unlock) before the steps; `OpenGate` (lock, `gate = true`, unlock, notify) after them; the render thread's
  `PassGate` / `WaitGate` (lock, wait for `gate`, mark passed) before E2: a claim and a pass are ordered by
  the one mutex, so either the steps run entirely before E2 (every step write happens-before E2's reads) or
  the claim is refused and the fence waits as today.
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
`[Stress::pipe]` keeps its fields; `drains step` falls to the packets fenced before the steps (ineligible, or
past their gate), and the fence wait (`waits (fence N)`, `wait=`) now includes the moved fence at the tail.
Added after a `| overlap:` separator: `steps` (fences claimed: the steps ran beside an E1), `snapshots`,
`refused` (packets the snapshot refused), `copy` (main-thread ms per snapshot) and the arena KB per packet;
since section 10, the governor's state and measures.

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

## 9. Results (2026-10-28)
Code: `77f1fffff` on `android/p13-overlap` (base `2a5c2c481`, `android/p12`). Builds: Android release, -O3
ThinLTO with the committed `perf/pgo/android/openba.profdata` (cut on the base code: both legs share the
profile), signed with the debug key; Windows debug (DX11 and Vulkan).

### 9.1 S10 stress town, 60 cap (`BANISHED_STRESS=1 BANISHED_FPS_CAP=60`, speed 10, 6000 frames), ABBA
Each run started at battery <= 32.0 C, thermal status 0, one run per phone lock; medians of the windows from
20 s on (fps, frame) and of the `[Stress::pipe]` windows.

| run | start / end C | fps | frame ms (p95) | update | draw | pipe main / packet / emit | fence wait | latency |
|---|---|---|---|---|---|---|---|---|
| 1 A base | 31.7 / 33.3 | 53.0 | 18.88 (27.83) | 9.10 | 7.91 | 4.20 / 3.70 / 5.28 | 4.21 ms | 5.50 ms |
| 2 B overlap | 32.0 / 34.1 | **60.0** | **16.67 (16.73)** | 8.70 | 12.49 | 3.59 / 8.87 / 9.92 | **0.69 ms** | 10.25 ms |
| 3 B overlap | 32.0 / 34.1 | **59.9** | **16.70 (16.67)** | 8.58 | 12.20 | 3.58 / 8.58 / 9.62 | **0.71 ms** | 9.97 ms |
| 4 A base | 32.0 / 33.5 | 52.0 | 19.22 (27.72) | 9.11 | 7.97 | 4.24 / 3.69 / 5.30 | 4.22 ms | 5.52 ms |

B: every packet overlapped (`overlap: steps=596 snapshots=596 refused=0`), the snapshot **2.0-2.1 ms** of
main-thread time a frame (3.0 MB a packet) -- above section 2.7's estimate, paid inside the 60 cap. The fence
wait falls from 4.2 to 0.7 ms (E2 only) and the frame holds the cap. `draw` and `packet` grow because they
now include the copy and the emit's park at the gate; `latency` (hand-off to Present done) grows by the steps
E2 waits for.

### 9.2 Normal play (`BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town`, 1x, 180 s), SurfaceFlinger timestats
| build | presents | dropped / janky | present-to-present | post-to-present mean / median / p95 |
|---|---|---|---|---|
| A base | 10799 | 0 / 0 | 16 ms | 44.2 / 44 / 46 ms |
| B overlap | 10773 | 0 / 0 | 16 ms | **39.6 / 40 / 40 ms** |
| C = B + FIFO asks the surface minimum (2) | 10984 | 0 / 0 | 16 ms | 39.6 / 40 / 40 ms |
| D = B + `kFramesInFlight` 1 | 10987 | 0 / 0 | 16 ms | 36.8 / 38 / 38 ms |

Section 3's candidates: (a) **no effect** on the S10: asked for 2 images under FIFO, the driver still creates
3 (`3 images (asked 2, surface min 2)`); not kept. (b) cuts another ~3 ms in normal play but **costs the stress
town half its frame rate** (one run: 32.0 fps, emit 25.4 ms: the CPU waits for the GPU's previous frame
before recording); rejected by the ruling's "not costing stress fps". Kept: neither; the overlap alone moves
post-to-present from 44 to 40 ms at a locked 60 (0 dropped, 0 janky), so normal-play latency improves. The
app-side part (gather to post) is not in timestats; at 1x it is bounded by the same frame.

Screenshots of B in normal play (three over the window, the town camera): citizens and cattle drawn whole, no
torn or flickering animated meshes, the season advancing (the sun and weather keep updating).

### 9.3 Windows
- `tools/build.sh unit`: 1561 tests, 0 failures (5 new / changed render_pipe tests: the gate, the overlap
  scripted runs with and without device loss, the transition join, the emit order with `Gate`).
- The render_pipe tests built with `-fsanitize=thread` (WSL GCC 13, the driver and hand-off with the fake
  engine): 15 tests pass, 0 ThreadSanitizer reports. The engine itself was not run under TSan.
- `tools/build.sh run` (faithful) and `BANISHED_RENDER_BACKEND=vulkan tools/build.sh run`: RUNNING, crash log
  empty.
- Resave gate: `Gate` faithful **PASS** (golden unchanged); `GATE_PROFILE=` (render_pipe on) resaved image
  byte-identical to the base commit's under the same profile (md5 `ba7d73e0...`).
- COMPPROF=4 stress (debug, render_pipe + draw_batch on DX11; + terrain_batch on Vulkan), 2500 frames:
  `RenderPipeCopy` 1111 / 1138 checks, `RenderPipeFrame`, `RenderPipe` and `RenderPipeIdem` 0 mismatches.

### 9.4 Sim identity
No sim code is changed or reordered (section 2.6). Evidence: the resave gates above, the shadows at 0, and the
stress town's citizen counts (803 every window in all four runs). The stress town is not bit-reproducible
run to run (entity counts differ by tens between the two A runs as between A and B; the final window's
citizens 958 / 960 for A, 958 / 957 for B), so it cannot show bit identity by itself.

### 9.5 Risks left
1. The snapshot costs ~2 ms of main thread a frame in the stress town on the S10; uncapped, or on a phone whose
   main thread is the limit, it is a trade against the fence wait it removes.
2. An unlisted live read in pass 2 remains possible (section 6.1); the shadows cover what they hash.
3. Packets refused by the snapshot (an unlisted Geometry type, `ext.terrain_edit` on) keep the fence; none
   were refused in these runs. The first refusal of each reason is logged once (`[RenderPipe] snapshot refused`).
4. The UI of a packet drawn after the steps of the next Frame (2.1) is visible only in motion; not checked by
   eye beyond the screenshots.

## 10. The adaptive gate and the seam fix (2026-10-28)
The combined verification (NEXT_SESSION 2.15) found two costs: the snapshot ran every frame, also where no fence
wait was left to remove (S10 normal play, the desktop at speed 50), and with render_pipe installed the desktop's
update was +10 % even with every snapshot refused. Branch `android/p13-adapt` (on `android/p12` `39e42fa13`).

### 10.1 The seam: a `thread_local` per pose lookup
Profiled on the desktop (perf build at `39e42fa13`, stress town speed 50, render_pipe on, `BANISHED_SAMPLE=2000`,
120171 main-thread samples): `FrozenInstancedState` holds **4.38 %** of the main thread's samples, 3.96 % of them in
`__gthread_getspecific` and 0.22 % in `__emutls_get_address` (MinGW GCC's emulated TLS: a `TlsGetValue` call per
read of `t_arena`). Its callers are `RtInstancedPoolSlot` from `MeshInstance::RootAnimationOffset` in
`CharacterComponent_MoveTick` (4.19 %) and from `GetNodeToWorld` (0.72 %): sim code, every walking character every
step. The update phases are ~45 % of the samples, so the seam was ~10 % of update: the measured +10 %. Confirmed.

The fix, two parts:
- `FrozenInstancedState` decides by the address alone: a MeshInstance lying in either slot's arena
  (`snapshot.cpp`: `[begin, end)` of the last snapshot, atomics written by the main thread only) is a copy, and its
  header holds the frozen record. No `thread_local`; `SnapshotScope` is gone. A live object never lies in an
  arena; the render thread only asks for its own packet's copies, whose slot the main thread does not rewrite
  while that packet is pending; a racing read of the other slot's bounds can at most send a copy to its own
  header, which answers the same.
- The seam `MeshInstance::_extInstancedState` is set per packet: `Host::BeforePublish`, with the render thread idle
  just before the publish, installs it when the packet carries copies and clears it otherwise. Without a snapshot
  in flight the four pose lookups test one null global and run retail's code.

### 10.2 The governor (`overlap_governor.h`)
The snapshot buys the main thread's fence wait with main-thread time. It shortens the frame only when the main
thread is the limit; in a frame that idles (the cap's sleep, the display, a slower render thread) a removed wait
only moves. Every Frame tail feeds the governor two measures (`pipe_driver.h`):
- **removable**: the fence wait the snapshot takes off the main thread -- without a snapshot the wait THE FENCE had,
  with one the emit's run beside the steps (from the claim to its gate; `Handoff::OpenGate` returns when it
  parked) -- in both cases less the part the emit spent before its `BeginCommands` returned
  (`Handoff::NoteBegun`: a free swap-chain image or a frame in flight, the display's or the GPU's pace);
- **idle**: the main thread's other waits -- the frame cap's sleep (`FrameLimiter::LastWaitUs`, reported by the
  main half in `Packet::idleUs`), the hand-off's wait for the render thread, and the fence's waits for an emit short
  of `BeginCommands`.

Both are smoothed over ~16 Frames, the copy's time per snapshot too. Off at install (today's fence). **On** when the
frame idles under 0.5 ms and the removable wait is above 1 ms and above the copy. **Off** when, without the
snapshot, the frame would idle more than 4 ms (idle + copy - removable), or the removable wait falls under 0.5 ms
or half the copy. Any switch holds for 120 Frames. No flag: the thresholds are constants with their rationale in the
header. Every switch prints one `[RenderPipe] overlap on|off` line with the three measures, and `[Stress::pipe]`
appends `governor=on|off removable= idle= (without the copies )`.

Tuning, on the S10 (stress runs at the cap, normal play):
- First cut (removable = the whole fence wait, no idle): stress 59.7 / 59.9 fps, but normal play turned it on too
  (an 11 ms fence wait at 1x).
- Idle added, the fence wait taken whole: normal play flapped every 120 Frames (24 switches in 180 s): at 1x ~7 of
  the 11 ms fence wait is the emit waiting at `BeginCommands` for the display, which a snapshot only moves to the
  hand-off.
- The wait split at `BeginCommands` (as built): normal play never switches on; the stress town switches on 2-3 s
  after install. The 4 ms off threshold: with the copies the stress town idles 0-2.5 ms from window to window,
  estimated -1.2 to +0.8 ms without them; a 2 ms threshold switched it off four times in a minute (58.9 fps median).

### 10.3 Results
Desktop (perf build, stress town speed 50, 6000 frames, faithful profile + render_pipe, DX11; MSI Afterburner and
RTSS could not be stopped from this session (elevated processes), their hook loader was; base `2a5c2c481`):

| run | tree | sim ms / iter | update ms / frame | fps | frame p95 | overlap |
|---|---|---|---|---|---|---|
| A1 | base | 0.14 | 6.96 | 734 | 8.39 | - |
| B1 | adapt | 0.15 | 7.30 | 706 | 8.98 | 0 snapshots |
| B2 | adapt | 0.14 | 6.89 | 741 | 8.42 | 0 snapshots |
| A2 | base | 0.14 | 7.01 | 732 | 8.55 | - |
| B3 | adapt | 0.14 | 7.07 | 732 | 8.76 | 0 snapshots |
| A3 | base | 0.14 | 6.97 | 738 | 8.54 | - |

Base 6.98 ms / 735 fps, adapt 7.09 ms / 726 fps (B1 the low run, B2 the best of all six): within the run spread,
against 8.06 ms / 619 fps for `39e42fa13`. The governor stays off (removable 17-24 us a frame).

S10 (release + the committed PGO profile, debug key, battery 32.0 C and thermal 0 at each start):

| run | fps | frame ms (p95) | update | draw | pipe main / packet / emit | wait | latency | snapshots / packets |
|---|---|---|---|---|---|---|---|---|
| stress 1 (cap 60) | 59.9 | 16.70 (16.94) | 9.11 | 12.50 | 3.47 / 9.06 / 9.97 | 0.49 ms | 10.24 ms | 5029 / ~5990 |
| stress 2 (cap 60) | 59.9 | 16.70 (17.16) | 9.21 | 12.62 | 3.40 / 9.22 / 10.17 | 0.52 ms | 10.43 ms | 4962 / ~5990 |

The packets without copies are the first seconds (the hold) and the shutdown frames. NEXT_SESSION 2.15's combined
runs: 59.9 fps, p95 17.05-17.50.

Normal play (`BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town`, 1x, 180 s after 40 s; SurfaceFlinger timestats):

| build | snapshots | CPU % of a core main / render / process | presents | dropped / janky | post-to-present mean / median / p95 |
|---|---|---|---|---|---|
| adapt, run 1 | 0 | 29 / 26 / 61 | 10842 | 0 / 0 | 41.7 / 42 / 44 ms |
| adapt, run 2 | 0 | 39 / 28 / 76 | 10836 | 0 / 0 | 43.5 / 44 / 46 ms |
| adapt before the tail split of 10.2 (0 snapshots too) | 0 | 40 / 31 / 77 | 10826 | 0 / 0 | 39.1 / 40 / 42 ms |
| first cut, the overlap on throughout | every frame (~1 ms copy at 1x) | 41 / 27 / 76 | 10865 | 0 / 0 | 39.9 / 40 / 42 ms |
| `39e42fa13` (2.15) | every frame | 56 / 29 / 92 | 10830 | 0 / 0 | 40.2 / 40 / 44 ms |

60 locked in every run. The main thread's CPU varies 29-41 % between runs of one build; the snapshot's own share at
1x is ~1 ms a frame (6 % of a core), and the 56 % of 2.15 was not reproduced even with the overlap on throughout
(41 %). Post-to-present without the snapshot is 39.1-43.5 ms (mean of the three 41.4), with it 39.6-40.2 (P13_OVERLAP
9.2, 2.15, the first cut): back towards the base's 44 ms by ~1-2 ms, inside this spread; the overlap's 40 ms is not
kept in normal play.

19-min soak (cap 60, 66000 steps), at equal t against 2.15's run (`39e42fa13`) and P13_THERMAL soak B (no overlap):

| phase | fps (2.15 / B) | sim steps / s | update / step | wait / frame | gold MHz | battery |
|---|---|---|---|---|---|---|
| 0-240 s | 53.2 (54.5 / 46.5) | 60.0 (60.0 / 60.0) | 10.11 (10.20 / 9.89) | 0.68 (0.47 / 4.22) | 2141 (2225 / 2246) | 32.0 -> 35.0 C (35.2 / 34.4) |
| 240-600 s | 33.1 (33.0 / 30.0) | 59.9 (59.9 / 59.9) | 12.34 (12.49 / 11.90) | 0.55 (0.56 / 4.52) | 1708 (1708 / 1921) | -> 35.8 (35.9 / 35.1) |
| 600-1100 s | 24.7 (24.8 / 24.2) | 55.6 (56.6 / 59.1) | 14.32 (14.23 / 12.97) | 0.58 (0.60 / 4.62) | 1708 (1708 / 2011) | -> 36.4 (36.3 / 35.8) |
| whole | 33.4 (33.7 / 30.8) | 57.9 (58.4 / 59.5) | 12.73 (12.77 / 11.96) | 0.61 (0.54 / 4.46) | 1800 (1818 / 2031) | end 36.4 C |

The stress town is main-thread bound from the first minute (below the cap, no idle), so the governor keeps the
overlap on for the whole soak (one switch, at 3 s) and the run matches 2.15's: the adaptive gate does not change
the soak. Its heat (the gold cap at 1708 MHz, the sim behind real time from minute 13) comes with the extra frames
the overlap draws in the first minutes, or with ambient; one run against one earlier run does not separate them.
`EXITED clean-shutdown`, crash log empty.

Windows: `tools/build.sh unit` 1579 tests, 0 failures, 3 skipped (5 new governor tests; `BeforePublish` checked in
the threaded fake); `tools/build.sh run` (DX11) and `BANISHED_RENDER_BACKEND=vulkan tools/build.sh run` in game,
crash logs empty (Vulkan: only the documented depth-only `SV_Target` warning); resave `Gate` PASS, golden md5
`971aaec2...` unchanged; `GATE_PROFILE=` (render_pipe on) resaved image md5 `ba7d73e0...`, the base commit's under
the same profile; acceptance 0 lines off target. Shadows: neither the desktop debug stress nor the S10 under
`BANISHED_STRESS_COMPPROF=4` switches the governor on (the shadow's hashes make the render thread the slower one,
so the frame idles at the hand-off). With the governor forced on in a local build (not committed), the debug
COMPPROF=4 stress (render_pipe + draw_batch, DX11, 2500 frames): `RenderPipeCopy` 1091, `RenderPipeFrame` 1091,
`RenderPipe` 1090 checks, 0 mismatches.
