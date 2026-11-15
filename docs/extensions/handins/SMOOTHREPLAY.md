# SMOOTHREPLAY -- `ext.render_pipe`: replayed frames during a long sim step

Addendum to RENDERPIPE1.md and `docs/android/P13_OVERLAP.md`. Row `ext.render_pipe_replay`
("Smooth fast speed", Experimental tab, off by default by the ruling of 2026-11-15; it does nothing without the
render_pipe row's thread). Class A: presentation only, nothing saved, the sim
untouched. First pass: the replay loop, the display clock and the recorded interface. Section 6 lists what is
not built yet.

## 0. Short version

- Before: a Frame that runs a sim step presents once. At high game speed a step takes several display
  frames, so the picture holds still for the step and then jumps.
- Now, with the key on: the render thread's emit, parked at its gate while the claimed steps run
  (P13_OVERLAP), presents the frame it has drawn and draws pass 2 again from the same frozen packet at a
  later blend, for as long as one more frame is predicted to fit before the steps end. The stress town at 50x
  shows about 4.5 to 5 replayed frames per step.
- Every packet with frozen copies is drawn at the **display clock's** alpha: the wall clock a constant 8 ms
  behind the sim, read when the frame is drawn. A replayed frame therefore shows a pose the packet's pose
  pair already holds, and the packet's own frame and its replays move at one pace.
- The interface is not in the frozen packet. The emit **records** each real frame's sprite batches (finished
  vertices and the context state of each chunk) and a replayed frame draws that record over the world: the
  same pixels as the last real frame.
- Off (the default), with the faithful profile, or without a render thread: no seam is set and every frame
  is what it was.

## 1. The replay loop (`render_pipe.cpp`, `EngineOps::Gate` / `Replays`)

`Emit` (packet.h) is unchanged but for `Gate`, which now answers whether a frame is still begun. A claimed
gate, with the display clock on and a usable interface record, runs `Replays` before it parks:

    while one more frame fits (Handoff::ReplayFits) and no free waits for a pin (PinFreeWaiting):
        stop if the device's settings changed or a step waits for the device lock
        draw the recorded interface            (UiLayerDraw; stop if the record was dropped)
        EndCommands, FinishPresent             (Present + Unlock; a loss ends the emit as Finish's would)
        BeginCommands under Device::_extReplayBegin   (refuses instead of rebuilding; a refusal ends the emit
                                                       as a failed BeginCommands does, once the steps are over)
        the context writes of steps 2 and 5 at the clock's alpha, pass 2

The frame the loop leaves holds pass 2 only; the real save-load, tool and interface draws follow once the
gate opens, as before. `ReplayFits` (handoff.h): the gate still closed and the claim's age plus the smoothed
span of one replayed frame under 95% of the last claimed steps' span. The frame index is not advanced (fix 2
counts packets); `_frameUpdateTime` and the post-draw callbacks belong to the real frame.

The copies: the overlap governor turns the snapshot off where it does not shorten the frame. While the key
is on and the last steps ran at least `kReplayStepUs` (4 ms), every main half takes the copies
(`Driver::_replaySnapshot`, pipe_driver.h).

ext.better_trees' falling leaves used to refuse the snapshot (an uncovered geometry type), so an autumn frame
kept the fence and got no replays. The snapshot now leaves that one geometry's draw items at the live object
(`Builder::keepLive`, `BetterTrees::LeavesLiveGeometry`): its Draw reads only the card set it shares with
`LeavesFrame` under their lock, which no sim step writes. For that to hold beside a main half, the geometry
is no longer freed when the autumn ends: it leaves the scene's list with its cards emptied and is freed at
`LeavesShutdown`. A replayed frame draws the leaves where the last main half put them.

## 2. The display clock (`display_clock.h`)

    alpha(now) = packet alpha + (now - Frame start - delay) / step, clamped to [0, 1]

`delay` is `kDisplayDelayUs` = 8000 (`ReplayLook::delayUs`, replay_params.h; the debug tweak overlay's
"Replayed frames" panel moves it live, 0 to 16 ms). Read once after BeginCommands for the packet's own frame
(`EngineOps::SetTime`) and once per replayed frame; it drives `Renderer::_alphaT` and `SetTime`'s shader
time. Not applied to a paused frame (`Packet::alphaFixed`), to a packet without copies, or under COMPPROF=4
(the RenderPipe shadows check the packet's own alpha). Spike measurements (2026-11-15, stress town 50x):
8 ms: 99 to 100% of replayed frames show new motion, mean picture age +2.7 ms; 12 ms stalled 22% of packet
frames at alpha 0.

## 3. The recorded interface (`ui_layer.h`, engine seam `UI::SpriteContext::_extRecord`)

Every interface and save-load pixel goes through `UI::SpriteContext::Flush`. While `_extRecord` points at a
`UI::SpriteRecord` (`src/engine/ui/SpriteRecord.h`), Flush appends each chunk it draws: the vertices, the
material instance, the sampler choice, the scissor, transform 0 and the colour; `UserInterface::Draw` adds
the camera it set. The emit sets the seam around its save-load and interface draws on the render thread.

- **The tool.** Every tool's draw is one function, `PlayerTileRender_DrawOverlay_08c0`: footprint tiles on
  the terrain, drawn with `Terrain/Selection.rsc` through `Context::BeginVertices` .. `EndVertices`. It reads
  the live map (placement tests, road paths), so a replayed frame cannot run it beside a step. Around
  `Tool::Draw` the emit sets `Video::Context::_extVertexTap`: each stream's vertices are copied into the same
  record, between the save-load screen's batches and the interface's, under the packet's camera. A replayed
  frame draws the same tiles. A draw the tool submits that is not such a stream (the context's draw count
  against the recorded streams) leaves the frame's record unusable.
- A frame whose **save-load screen** drew leaves no usable record: the next claimed steps get no replayed
  frames.
- Only the frame drawn last may be replayed: an emit that recorded nothing, a faithful tail and a loop
  transition drop the record.
- **Freed materials.** The record names material instances by pointer, and a step may free one (the map
  window closed, ext.sharp_ui's sheets rebuilt). `Graphics::MaterialInstance::_extBeforeDestroy` runs first in
  the destructor: under the layer's mutex it drops a record that names the instance. `UiLayerDraw` holds the
  mutex for its draw, so the free waits out a draw in progress and the replays stop for that step.
- The interface on a replayed frame is the last real frame's: a panel that follows an entity trails the
  world by the replay's age.

## 4. What a replayed frame may not do beside a step

- **Rebuild anything in BeginCommands.** `Video::Device::_extReplayBegin` is true only around a replayed
  frame's begin: DX11 refuses on `settingsChanged`; Vulkan also on a lost device, a missing surface or a
  dirty swap chain; Metal also on a drawable size change. The ordinary begin that follows, with the steps
  over, does the rebuild.
- **Starve a step of the device lock.** The lock is an unfair critical section the loop drops and retakes
  back to back. `Video::Device::_extLockWaiters` counts threads blocked in `Lock()` while the render thread
  is up; the loop stops when it is not zero (1 to 5 times per 600 steps in the stress town).
- **Read a pinned EdgeMesh a step is freeing.** `PinGate::FreeWaiting` (resource_pin.h): the loop stops, the
  reads are declared over, the free goes on.

## 5. Checks run (2026-11-15, Windows, DX11)

- Unit: `tests/unit/ext_render_pipe_replay_test.cpp` (the clock, the record's chunk bookkeeping,
  `ReplayFits`, the pin's waiting free, the driver's snapshot rule) and the emit-order test's new gate case.
- Gate (faithful): a new town runs, empty crash log. Resave gate (faithful): PASS == golden.
- Stress town, 50x, perf exe, key on: 4.6 to 5.3 replayed frames per step, none without an interface record,
  empty crash log; with a building panel held open under ext.sharp_ui off, on, and on with the magnified
  sheets, the panel is complete in every window capture.
- Falling leaves on (stress town, 50x, autumn reached): snapshots refused 0, 6.3 to 6.9 replayed frames per
  step, empty crash log; COMPPROF=4: `RenderPipeSnapshotLean checks=2086 mismatches=0`.
- The tool (hand test, stress town, 50x, tools picked and dragged): 10177 real frames carried the overlay
  into the record, 11253 replayed frames over 4798 steps, none without a record, none stopped, empty crash
  log; the tiles and the motion behind them judged good by eye.
- Gate.sav at 50x with the key on (per-entry profile): load, the retail autosave path at frame 1500 with
  replayed frames running, quit through the pause menu: exit 0, empty crash log.

## 6. Not built yet

- The delay derived from the last step's span (today a constant, tunable in the debug overlay only).
- A look on Vulkan and on the phone; PresentMon numbers on a quiet machine; the user's ruling on the default.
- Camera pans stay at the step rate (the camera has no interpolation, in retail too): a separate question.
