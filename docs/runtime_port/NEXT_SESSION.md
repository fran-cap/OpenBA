# Next session (written 2026-10-11, evening: no phase open)

Endgame B closed at B9 (2026-10-10). Phase D closed 2026-10-11: D.1-D.3 + CMake (the restructure), D.4's four waves (the
portable layer: settings file, `Platform::` seams, SDL3 window / input / cursor / audio with the vendored WMAv2 decoder,
DXVK, the ABI audit; the first Linux game build boots to the menu and into a new game under WSL2 with the world and the
resave byte-identical), then the post-close audio hunt and the PORT GAP hunt, every merge gated at 0 problems. The user
looked at and listened to every group the same day and found no issue. **Nothing is in progress.** What follows is the
complete list of what is left, each item with its owner and the condition that closes it. There is no other list.

The history: `STATUS.md` (the close log, newest first), `phase_d/GATE_TABLE.md` (every gated merge), `phase_d/handins/`
(every block's hand-in), `docs/history/endgame_b/` (the B-phase rule sheets, kits and the old `NEXT_SESSION.md`).

## 1. Where it stands

| | |
|---|---|
| main | the night-of-2026-10-11 commits (CMake gate, Linux release / perf, housekeeping) and after |
| tree | `src/game` (the exe's logic), `src/engine` (the Runtime DLL), `src/platform/{win,posix,sdl,common}`, `src/render/dx11`, `src/app`, `src/diag`; `tests/`; **the build is CMake + Ninja through `tools/build.sh`** (exes in `build/cmake-<flavour>`; the Makefile is deleted) |
| `PORT GAP` lines | **1**: a comment that says "no PORT GAP" (`citizen_lifeevent_handler.cpp:180`). The device-lost pair was the last body and is ported (§2.4) |
| known_port_issues | **0 rows OPEN**; 1292 PARTLY FIXED (§2.1); 890 FIXED (§2.4); 1295 and 1162 RULED; 1277-1284, 1320-1322 RECORDED (platform / bed facts, not defects); every duplicate row collapsed |
| invariants (every gate) | new game `[ExtLoad] calls=5694 openFileFail=0`, tutorial 5773 / 12, typetable `965b527a`, resave 5/5 == golden `971aaec2cf95` (23,453,453 bytes), unit 1162 / 0 / 0 skipped (2026-10-18 KITFREE; 1010 / 0 on Linux at wave 4, re-count owed), castmetric 2 (the two ruled casts), `tools/abi_audit.py` OK, `tools/acceptance/acceptance.sh` 0 off target, 0 new warnings |
| perf | sa-stress 139 fps, update p95 2.54 ms (baseline 2.40; recorded, not chased: the user's rule) |
| Linux | WSL2 Ubuntu 24.04 (WSL 3.0.1), lavapipe (CPU Vulkan: dzn / the host GPU is Vulkan 1.2, the renderer wants 1.3 -- row 1322), the Vulkan backend as the Linux renderer since Vulkan P5 (2026-10-25; it replaced the Phase D.4 D3D11-over-Vulkan route); source copy `~/banished-main` (rsync of main; ccache), builds `~/banished-main/build/linux-{release,perf}`, game dirs `~/banished_{release,perf}`, runs `~/lx_runs/`; **release and perf flavours boot, play and stress-test clean** (unit 1012 / 0 each); `tools/linux_run.sh` routes menu / newgame / world / resave / stress |
| worktrees / branches | none besides main (the 64 block branches and `oldbase` deleted 2026-10-11 at the user's word) |

## 2. What is left

### 2.1 Code: one item (an agent can take it; `behaviour:` commit, gated on main)

**Row 1292, the region build fold.** The map's region build is retail's one member at 0x14009e8c0, called from the map
create 0x14009ef20 (between `FillZones` 0x14009c480 and the shore-link loop 0x140088b80) and from `Map::Serialize`'s load
tail (0x14009f2a6). The tree spells it as two port-only free functions, `Pathfinder_Seed` (`src/game/Game/pathfinder_seed.cpp`)
and `Region_Seed` (`region_seed.cpp`), declared in `Game/exe_bodies.h`, each with an early return when its object exists
and a `[PfSeed]` / `[RegionSeed]` stderr line. The hunt already deleted `GameUpdateLoop::Load`'s post-load pair (DRAW-2)
and wired the map create; what is left is cosmetic: one Map member, no early return, no print, the two files and the two
declarations gone. Done when: the gate at 0 problems with the resave golden unchanged (the fold writes no state), and
`worldcmp.sh` differs from the last run by the `[PfSeed]` wording only.

### 2.2 By hand, the user: the checks no route or look has covered yet

The 2026-10-11 looks covered every group in `STATUS.md`'s hunt line. These rows still carry "look owed" in their cell
with a subject that list did not name; each closes with a sentence on its row (or a new row if it is wrong):

Seen good by the user on 2026-10-11 night: the sprite-capture button (row 1296), a bridge dragged over water (row
1180), a worker added and a worker removed at the town hall (rows 1141 / 1142), a drawn road (row 1240), livestock
sounds (cows). Still unseen: a look after a forced device loss (§2.4) and the three WSLg looks (§2.3).

The END click heard once on a debug exe is dropped from the list (the user, 2026-10-11 night): row 1289 proved every
sound tail clean in unit tests and the SDL pull harness; if it is ever heard again, the sound and the moment make a row.

### 2.3 Linux (rows 1277-1285 and 1320-1322 are RECORDED facts; these are the follow-ups)

Done 2026-10-11: the release and perf flavours (built, unit 1012 / 0, menu / new game / back-to-back boot / stress
clean); row 1282 re-tested clean 2 of 2 (stays RECORDED; no stall seen); the stress bed fixed (row 1320: GameAutoPause
off for the run) and dzn tried (row 1322: Vulkan 1.2, DXVK wants 1.3; the bed stays lavapipe). Left:

- **Stress numbers**: DONE the same night. The Windows baseline is re-cut from the CMake perf exe on a quiet machine
  (`perf/baselines/current.csv`: sim 0.17 ms / iteration, 0.19 before, 24820 entities); the quiet Linux perf exe reads
  0.46 (row 1321): 2.7x slower per sim iteration on the same tree and machine. Recorded, not chased; if the user ever
  wants it chased, the first read is the allocator (the standalone allocator's lock on glibc vs Win32) and the WSL2 VM,
  with `perf` under WSL2 on the perf exe. The Linux fps / draw columns are lavapipe's (and WSLg throttles an unfocused
  window to 5 fps) and never compare.
- **Fullscreen / alt-tab / a forced device loss on Linux** (one look each under WSLg, by the user at the WSLg window;
  the forced loss with `EXTRA_ENV=BANISHED_FORCE_DEVICE_LOST=12000` on a newgame route).
- **The POSIX diagnostics** (rows 1281 / 1283: BLOCKED_DIALOG, redzone, sampler, hw_watch are Windows-only): each is its
  own task, taken only if a Linux debugging session needs it.
- **A GPU-speed Linux bed** only if wanted: a native Linux boot, or dzn once it exposes Vulkan 1.3 (`LINUX_VK_ICD`
  in `linux_run.sh` is ready for it).

### 2.4 Device-lost: DONE (2026-10-11 night; re-opened by the user that evening, deferred since 2026-09-27)

Row 890 FIXED by merge `8b89fcfd5` (opus block d5-devlost, hand-in `phase_d/handins/DEVLOST.md`, gate row DEVLOST at
0 problems): `GameUpdateLoop::OnVideoError / OnVideoRestart` are retail 0x140095970 / 0x140095930 (the terrain's four
buffers, height image, mesh and water mesh; the minimap's two images), with the menu loop's twin pair and the engine's
`ImageBuffer::Release`. The forced route (`BANISHED_FORCE_DEVICE_LOST=12000`, `runs/DEVLOST1`) now runs ONE round trip,
no later device removal, working set flat at ~315 MB for the 220 s after it (was ~100 MB per frame), and the run quits
itself. What is left of device-lost: a look by the user after a forced loss (terrain, water and minimap drawn after the
trip -- the route's logs say the uploads happened, eyes say the picture is right), and device-lost / fullscreen /
alt-tab under WSLg (§2.3). The retail crash window (a loss between the loop becoming current and its first Draw,
`DEVLOST.md` section 5) is retail's and is not guarded.

### 2.5 Housekeeping

**PGO on the other platforms (the user, 2026-10-26: later, mechanism agreed).** The Windows release exe is
profile-guided (`perf/pgo/README.md`); Linux (g++, its own `.gcda` set trained in WSL2 on lavapipe), macOS and
Android (clang: `-fprofile-instr-generate`, `llvm-profdata merge`, `-fprofile-instr-use`; Android trains on the
device, `adb pull` the `.profraw`) each need their own committed set and a `train` step in `perf/pgo/`. None can
train in CI (no game data).

**The compiler-vtable aliases.** DONE 2026-10-26 (STATUS entry VTABLE ALIASES RETIRED): `Object::ClassVptr`,
recorded by the ctors, replaced `DEFINE_EXE_VTABLE`; every TU of the release exe is LTO.

**CI duration.** DONE 2026-10-26 (STATUS entry CI WINDOWS SHORTER): the Windows job's vulkan configure builds its
backend's own objects (`tools/build.sh backend`) and `unit`, and ccache now shares the two configures. Splitting
the vulkan build into its own job was not needed; it would add a required check to branch protection.

**Before the repo goes public (the user, 2026-10-14): a final content-cleanup pass.** DONE 2026-10-17 in two
rounds (STATUS entries REPO REVIEW + CLEANUP and CONTENT PURGE + HISTORY REWRITE): the 14 screenshots and the Sheep1
xWMA fixture out; the docs sweep (two opus agents, every tracked file against the local ToolKit) found no REMOVE-level
content; the user then ruled on every borderline item and all are done: `Extensions.rsc` re-authored (0 lines shared
with the kit's `Mod.rsc`), the SharpFont variants derive from the game's Font objects by reference, the DXBC grammar /
fire-chance table / RawMaterial + template values / award text / 276 DiseaseList log rows elided, the xWMA fingerprint
replaced by structural checks, the five Ghidra hash indexes untracked + gitignored (`tools/ghidra_mcp/regen_hash_index.py`
regenerates them), and **the git history rewritten** (git filter-repo: those paths, the 59 old .rsc blobs and the quoted
lines purged from every commit; pack 142 -> 47 MB).

Still open after the rewrite:

- **The remote: DONE 2026-10-17 evening.** `main` + tags force-pushed, the 48 stale remote branches deleted; origin
  holds only the rewritten history. Any other clone must re-clone (the WSL copy `~/banished-main` is an rsync of the
  working tree, unaffected). GitHub keeps unreachable objects until its own GC; a support request purges them sooner.
  The pre-rewrite backup bundle lives in the 2026-10-17 session scratchpad (`lead/pre_rewrite_backup.bundle`), not in
  the repo.
- Seen by the user 2026-10-17 evening: the re-authored Extensions dialog and ext.sharp_ui text both look right.
  **Noticed on the way (pre-existing, not from the rewrite -- the compiled variants decode identical to the old template's
  but for dependency records):** with ext.sharp_ui on, dragging the UI-scale slider changes the text's letter spacing at
  each step; off, it does not. First read as inherent (each variant hinted at its own height). SHARP-SPACING.md
  (2026-10-17, branch worktree-agent-a44e6e8d26aba3553, not yet merged): the variants' advances were never read -- the
  cause was the per-glyph pen snap `round(pen * px)`, a stray pixel per letter pair that reshuffles with the ratio;
  fixed with word runs (the base pen anchors the word, the variant's advances step inside it, held within 1 px) and
  a self-swap in the native band (text at 99 % was resampled). What remains per step is the hinter's ink widths
  (measured, the hand-in section 3); the user's look at the live slider is owed.
- `decomp/` (the per-binary function inventories, derived from the retail binaries like the hash indexes): the user's
  call, not ruled.
- The FAA-SPIKE.md group table still lists the 24 matched blobs' checksum keys (hashes, same class as the indexes).
- The regeneration script was written without a live Ghidra; first run it against `Banished_RE.gpr`.
- Legal notes (2026-10-17, not legal advice): the install's EULA.txt is the GOG User Agreement (9.1(b) defers to the
  rights holder; no Shining Rock EULA found in the install, the ToolKit or online; the kit README grants modding and
  says nothing about RE); US: Sega v. Accolade / Sony v. Connectix cover the intermediate copying; EU Directive
  2009/24/EC art. 6(2) limits decompilation to interoperability and bars giving the information to others.

Done 2026-10-11 at the user's word: the 64 merged block branches and the `oldbase` worktree deleted (`worldcmp.sh`'s
`oldbase` comparison is gone with it; the Phase D world baseline `runs/POSTBLOCKS_worldcmp` is what it compares against
now). The ignored run logs (2+ GB under `docs/history/endgame_b/b*_session/runs/` and `phase_d/runs/`) stay, by the
user's choice; delete at will. The Makefile and its build directories were deleted the same night at the user's word.
Still the user's call:

- **Perf (2026-10-13 night close, `docs/extensions/STATUS.md` top entry):** the three next ideas are DONE: SORTMEMO shipped (the
  draw-item sort memo, draw -14% heavy / -7% compact), WALKGRID merged as the opt-in class B row `ext.walk_batch_grid` (speed-50
  everything-on fps +8..+20%), XFORM2 measured neutral and NOT merged (its tag deleted 2026-10-18, the code kept off the repo; the world-matrix
  memo idea is closed). Faithful draw CPU: heavy 0.97 ms, compact 0.65 ms (morning 1.66 / 1.03). No perf idea is queued; the next
  collect would be a fresh IBS pass on the merged exe. Before any uncapped fps number: Afterburner, RTSS AND RTSSHooksLoader64
  stopped (they are stopped now; restart them). The hypervisor is still OFF (`bcdedit /store ... hypervisorlaunchtype auto` + reboot
  restores WSL2 / Docker).
- **Loading speed: DONE 2026-10-14 afternoon** (`docs/extensions/handins/LOADPLAN.md` §6, `docs/extensions/STATUS.md` top entry): new
  game 9.86 -> 3.23 s (P4 3.72 s), save load 4.07 -> 3.10 s (1.19 s with `ext.quick_screens`), boot 4.74 -> 4.46 s (2.89 s with it);
  every world byte-identical (`[WorldHash]`, kit `docs/extensions/handins/loadkit/`). Boot left as is by the user (the D3D device in the
  NVIDIA driver, the fades, the menu scene). Not queued: the start town's Perlin (~0.43 s; the noise is per tile and could be precomputed in parallel ahead of the
  RNG-ordered spawn), the blend AO pass (~0.22 s), `wg.paths` (150-320 ms by seed, never profiled), cold-cache loads (every number
  is warm; package read-ahead only pays there), a real 1025^2 field in the erosion bench. The census routes randomise the seed, so
  new-game times vary ~0.1 s by map. By hand, the user: `ext.quick_screens` on -- boot, a save load, an in-game save, quit to menu, the tutorial (no flash or hang).
- **Multithreading: DONE 2026-10-14** (`docs/extensions/handins/THREADPLAN.md`, hand-ins CPULIMIT / RENDERPIPE0 / CULLPAR / RENDERPIPE1,
  `docs/extensions/STATUS.md` top entry): `sys.cpu_limit` + the roster (`src/platform/common/cpu/`), `ext.render_pipe` (the render thread, class A) and
  `ext.gather_threads` (the parallel cull, class A) shipped at gate 0 with every shadow 0; 4-core heavy speed 10 +67%, speed 50 +35%;
  speed-50 close-out 752 / 618 / 425 fps everything on. **The user (2026-10-14): the in-game fps improvements are COMPLETE. The next
  phase is SPEEDING UP THE LOADING** (new game / load game / boot times): not yet planned; the first step is a timed census of the
  load route (`tools/boot_state.sh` stamps, the loader thread, `[ExtLoad] calls=5694`, the world build, `PaintTerrain`) before any design.
  Threading leftovers (by hand, the user): an alt-tab in exclusive fullscreen with `ext.render_pipe` on (DXGI presents off the window
  thread; a hang reads exit 4), a toggle off / on of `render_pipe` in one session, a road-tool drag with the row on; row 1328 (one
  pre-fix quit hang, not reproduced). Deferred: a ThreadSanitizer Linux flavour (needs the hypervisor back on).
- **2026-10-16 night (closed): the user's two complaints** -- town chatter (`ext.seamless_loops`) and the deciduous
  canopy's rotating shadow edge (`ext.billboard_shadow`, TS-DAPPLE) -- shipped and ruled; `docs/extensions/STATUS.md`
  has the close and the two optional follow-ups.
- **2026-10-17: `ext.better_trees`** (foliage_aa + billboard_shadow merged, keys migrated) with the per-texel
  edge dither -- shipped and RULED by the user (noise 0.05, scale 32 = `kDefaultParams`); session closed.
  `docs/extensions/STATUS.md` top entry, `handins/BETTER-TREES.md`. Open only if asked: the DETAIL v2 pixel
  variants (no dither), a redrawn row icon.
- **The next phase: CHOSEN 2026-10-11 night -- fixes and extensions** (`docs/extensions/DESIGN.md`, status in
  `docs/extensions/STATUS.md`, the classified deviations in `docs/extensions/INVENTORY.md`; gate rows EXT1 / EXT2).
  The other candidates stay on record, not chosen: macOS through MoltenVK under DXVK (ENDGAME §E item 3);
  `docs/retail_bugs.md` as a "fixed" flavour; a Linux gate (the five routes + resave under WSL2 on every merge, as the
  Windows gate does).

### 2.6a Extensions in progress (owned by `docs/extensions/`)

**Hitch hunt** (`handins/HITCH.md`, 2026-10-18, branch `hitch`, NOT merged to main): DONE to the bar (one 140 fps
frame over the class median): 10x PASS, 50x PASS with the four hitch extensions on (spread_warmth B, drain_split B,
background_save A, async_save_compress A), the autosave frame included; faithful 50x keeps only retail's cold-wave
scans. Faithful fixes merged: the PathGraph drain (two `behaviour:` commits), one-write stdout/stderr tree-wide,
PathLength memo + PathCostIndex, the save's GetReferenceId memo + Write fast path; the stress town is fed; the
Extensions sheet is 512 x 512. Merged to main (7cc480104); the `ext:` notice crash under `/profile:faithful` FIXED
(row 1330); the fed 10x baseline RE-CUT 2026-10-18 (`perf/baselines/README.md`, sim 0.16 ms / iteration); AUTOLOAD now
quits from the menu when a modal declines the load (f21e70449). Defaults RULED 2026-10-18: fixes and class A ship
ON, class B / C OFF, `eye_camera` OFF (it changes play; DESIGN.md section 2); the resize-during-save case driven
(HITCH_BGSAVE.md section 2: IDENTICAL x5, reload exact). Left: a tighter save writer (faithful, optional).

**Ambient occlusion** (`ext.ambient_occlusion`, OFF by default): `docs/extensions/AO.md` §7 is closed but 7.5 (Linux /
DXVK: blocked while the hypervisor is off), 7.7 (the next full gate's new-game count and worldcmp with a row on) and 7.8 (AO
on the menu backdrop, the user's call). The look was ruled 2026-10-16 (overview radius 5.0); the GPU cost per tier is in
`handins/AOTUNE.md` §7.2 (Low / Medium / High -4 / -7 / -13% fps uncapped).

**Foliage edge smoothing + max shadow resolution** (`ext.foliage_aa` + `ext.foliage_aa_mode`, `ext.max_shadow_res` +
`ext.max_shadow_size`; class A, OFF by default): DONE 2026-10-16, every block merged at gate 0, the look ruled by the user
(k 0.15, floor 0.15, mode 2; the shadow entry Off / 4096 / 8192). `docs/extensions/FOLIAGE_AA.md`, hand-ins FAA-*.md.
Nothing left.

**Debug tweak overlay** (`docs/reference/tweak_overlay.md`): Dear ImGui, debug flavour only, Alt+F10. Nothing left but use.

**Device-lost memory climb (row 1329)**: CLOSED 2026-10-16, not a port defect (`handins/DEVLOSTMEM.md`).

**Branches**: none besides `main` (every agent worktree and branch of the 2026-10-16 session removed at the user's word;
no tags: the seven old tags were deleted before the repo went public, 2026-10-18; the unmerged spikes' code is kept off the repo in a git bundle).

### 2.6 Tracked, not tasks

The 28 `stays free` names of LEFT_FREE §A (typed overloads over one template core, the raw-triple holders, the harness
rows; acceptance 08 at 0 against them); row 1230 (the element-loop template's residue, unobservable); row 1295 (the
menu loop's `_loadBit = -1`, RULED kept with its reason); the comment debt of `docs/history/endgame_b/b9_session/handins/RT2.md`
(banners naming deleted helpers). None of these changes behaviour; each has its row or line.

### 2.7 Android P8 (ruled 2026-10-25, after PR #50): several opus agents in parallel, one phone

P7 (`docs/android/P7_FIRST_PASS.md`, PR #50) left these; the owner ruled each on 2026-10-25. The phone (S25) is one
device: device runs are serialised between agents, builds at jobs = 8 / agents building.

New, seen by the owner on the S25:
- **In-game buttons under the edge mask.** The in-game toolbar / HUD buttons at the screen edge sit under the S25's
  curved edge; move them further left (ext.phone_menu's safe area covers the front-end dialogs only).
- **Middle-mouse release lost (real mouse over a KVM):** the button-up never fires, so camera rotate sticks on. Find
  where it is lost (SDL's Android mouse source, our pump / input hooks, the touch-as-mouse path) and fix it.
- **Citizen shadows flicker heavily** on the phone (suspects: the 512 sun map, depth precision, the msrtss path;
  compare the DX11 look).
- **Options entries:** render scale Full / Half / Quarter (`VideoRenderScale` 100 / 50 / 25, live) and an fps cap
  with **60 fps as the default (ruled)**; an in-game cap must be built first (`BANISHED_FPS_CAP` paces the menu loop
  only; the cap is a settings key, never an env flag).

Verdicts on the P7 "Left" list: depth-discard re-arm rule: **fix**. Frame-copy pass split (2 of 3 depth passes still
store): **investigate**. Shadows: ~~retail's three settings stay, plus an Off (a class A `ext.*` row)~~ **retail's Shadow Quality = None is
the Off** (`VideoShaderDetail=0`: no shadow pass, no casters, the unshadowed pixel variant; verified 2026-10-25 on
DX11 and the S25, `docs/android/P8_SHADOWS_RELEASE.md`); no ext row (ruled 2026-10-25); the phone seed keeps shadows on. Combined image samplers in the
converter: **implement**. Release: a **signed Android apk in `release.yml`** like the three desktop packages (done in P8:
job `android` + `tools/android.sh sign`; the first real run is the next tag or a dispatch). The release keystore exists (made 2026-10-25, PKCS12, alias `openba`, RSA 4096, kept off-tree in the owner's `~/.android/`) and the repo's Actions secrets `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` are set; the job decodes the keystore to a temp file and signs with apksigner (or Gradle's signingConfig fed from the environment); nothing of it enters the tree. HWASan and the 30-minute soak: **skip**. Font rasteriser on Android (the TTF from the data folder):
**add**. Still unverified from P7: ext.phone_menu on the phone itself, the oversized push-descriptor pair path, the
in-game trim-memory save after the latch fix (verified in P8: `Auto.sav` the same second, and it loads), rotation / display change at render scale 50, the Windows Vulkan boot
state left RUNNING after a clean exit.

**P8 outcome (2026-10-25, four opus agents, merged on `android/p8`; the per-agent logs are `docs/android/P8_UI.md`,
`P8_INPUT_OPTIONS.md`, `P8_VULKAN.md`, `P8_SHADOWS_RELEASE.md`).** Done: the in-game HUD / panels / tooltips inside
ext.phone_menu's safe area (seen on the S25); ext.phone_menu verified on the phone; the Android font rasteriser
(`font_raster_android.cpp`, the TTF in the data folder); the lost middle-mouse release (SDL's Java glue drops captured
pointer events: `OpenBASurface.java` forwards them; verified with a uinput mouse only); Options > Enhanced rows Render
scale (live, Android) and Frame rate cap (`ext.fps_cap` class A + `ext.fps_cap_rate`, the in-game limiter; phone seed
60); rotation at render scale 50; the depth discard re-armed by frame signature and the main pass 3 -> 2; combined image
samplers (all 465 permutations, caches re-versioned); the oversized push-pair path exercised; boot state EXITED on a
clean exit (Windows + Linux); the signed apk job in `release.yml` (never run yet) and a non-required Android job in
`ci.yml`; the trim-memory save verified; shadows off = retail's Shadow Quality None. Gate + resave PASS on the merged tree.

Left / for the owner: the citizen shadow flicker is NOT reproduced (pinned camera, 512 map, half res: no frame-to-frame
change; needs a recording or the settings in force, then a RenderDoc capture on the phone); a real KVM mouse test of the
middle-button fix; a phone whose `settings.cfg` predates P8 is NOT capped (the seed runs on a first launch only: the
coordinator's merged-tree run on the S25 logged `game: not paced`; decide whether the seed should add missing keys);
UI scale at render scale Quarter pushes the Options tab row off the top (known_port_issues); the 60 cap sleeps, the
panel stays at 120 Hz (a panel-rate request was not built); the frame-copy split stays (the remaining redesigns are
unfaithful or large); the sun map is still allocated at Shadow Quality None (unmeasured); the Android CI job as a fifth
required check or not; a dispatch dry run of `release.yml` before the next tag; whether the apk bundles the font.

**P8 rounds 2-4 (2026-10-25/26, after the owner's own S25 test; same branch, same four logs).** Done: the pause / main
menu no longer jump on their first frame (laid out from the current frame); modals shown through
`UserInterface::ShowDialog` take the UI scale (`fix.ui_scale_layout`, retail_bugs §5); the phone main and pause menus are
a row of square buttons (logo top, version bottom), wrapping to two rows as overflow, icons about 45 % of the square,
the UI-scale slider floored at the lowest scale where both menus fit (35 on the S25 at render scale 100, computed,
clamped at boot); the rotate drag at full speed (Android batches mouse samples: all summed) and no cursor snap on
release; retail's Video rows (Resolution, Refresh rate, Fullscreen, VSync on) disabled on a fixed surface under
ext.phone_menu; the phone seed appends missing keys to an existing settings.cfg; AO on the S25 (the Adreno driver
refused `OpBitFieldInsert`: lowered to shifts and masks) and on the tiler MSAA path (about 2.1 ms GPU at AO Low);
anisotropy reaches the samplers (the Texture Filter choices say "(next start)", Enhanced-tab host); the OpenBA
launcher icon. Closing checks: DX11 gate + resave PASS; DX11 unit 1492/0, Vulkan 1512/0; the RELEASE apk (signed with
the debug key through `tools/android.sh sign`) boots a new game on a zeroed phone and writes the full seed.

Left / for the owner: the citizen shadow flicker is still NOT reproduced (120 fps recordings at three zooms: walking
citizens' shadows step one sun-map texel, retail's look; suspects to toggle live: `ext.render_culling`,
`ext.render_pipe`; the owner's recording + whether DX11 on the PC shows it); the KVM mouse: a jump to the screen
CENTRE was not reproduced (a KVM re-announcing the mouse starts its pointer at the centre); below about UI 31-33 % on
the phone the column returns (a third row or a smaller logo if ever wanted); the enlarged 16 px icons read as pixel
art; "Anisotropic 16x (next start)" touches the combo's drop arrow at UI 100; the fifth CI check; a dispatch dry run
of `release.yml`; whether the apk bundles the font; the sun map still allocated at Shadow Quality None (unmeasured).

### 2.8 Android P9 (the owner's rulings after the P8 release-apk test, 2026-10-26)

Rulings on the P8 opens: the citizen shadow flicker is GONE on the final P8 build (no further work); the enlarged
16 px icons in the phone menu squares are fine for now; PR #53 merged as is.

Next session, in the owner's order:
- **Texture Filter drop-down:** REMOVE the "(next start)" suffix again (`src/ext/enhanced_tab/texture_filter_note.cpp`
  and its string-table entry, the `OptionsDialog::PopulateFilterCombo` seam).
- **AO setting changes the mipmap level:** on the S25 adjusting the AO quality / strength visibly changes texture
  mip selection. Find why (the AO pass's sampler state leaking into the main pass's samplers under the
  combined-sampler conversion? the texture_quality Half path? a sampler cache keyed without the anisotropy / LOD
  bias?) and fix it; compare DX11.
- **Loading screen:** the bottom black edging image sits too low / off-screen on the phone, and the "Loading" text is
  drawn over the rendered scene / loading image instead of over the black edging as the Banished logo is. Fit both
  under ext.phone_menu (same backdrop rule as the logo).
- **Scroll bars about 4x wider on the phone** (finger scrolling), under ext.phone_menu: lists in Options, Fixes and
  Extensions, Mods, the in-game panels.
- **Render scale and UI scale coupled:** changing the render scale must re-derive the UI scale value so the interface
  keeps the same size on screen (UI scale is in render pixels today: Half -> the UI doubles). Keep the slider floor
  rule in step.
- **Options and Fixes and Extensions dialogs cut off at the top** below UI 45 at render scale Full (35 at Half): the
  tab row and the top of the list go off-screen, and in Options the other buttons become unreachable. A phone layout
  for the tall dialogs: cap the dialog to the safe height (scroll the list, keep the tab row and the button row on
  screen), or scale the dialog down to fit.
- **Icons for the new extension rows** (`fps_cap`, and any row added in P8 without one) in the Extensions sprite
  sheet (`src/ext/ui/`, `tools/ext_resources.sh` recompiles the kit output; the sheet cross-check unit test).
- **In-game version text overlaps the bottom row of buttons** at render scale Full, UI 40 on the S25 (the pause menu's
  two rows vs the version line: the rows must be placed above the version line, or the version line hidden when the
  rows need the space), and the black fringe image is missing there too.
- **Bundle the font in the apk (ruled 2026-10-26):** the FrancophilSans files the desktop packages ship
  (`tools/release/fonts`, with the notice; `THIRD_PARTY_NOTICES.md`) go into the apk as assets and are copied to the
  app's data folder at first run when absent (`font_raster_android.cpp` reads them there); `push-data` no longer
  needs to push them.
- Ruled 2026-10-26: the rotate drag is good on the real KVM mouse (centre jump and speed: closed). Waiting, no action:
  the fifth CI check, a dispatch dry run of `release.yml`, the sun map still allocated at Shadow Quality None.

### 2.9 Android P9 outcome (2026-10-26, run as five opus worktree agents + one research agent) and what is next

Every §2.8 item is DONE on `android/p9` (session docs `docs/android/P9_DIALOGS.md`, `P9_LOADING_SCALE.md`, `P9_RENDER.md`,
`P9_ASSETS.md`): Texture Filter note removed; AO-changes-mip NOT a sampling defect (RULED: AO identical to DX11 on every
platform); loading screen fitted under ext.phone_menu; scroll bars 4x and close buttons 2x; render scale / UI scale
coupled (the setting is in screen pixels at Full; `src/platform/common/ui_render_scale.h`); tall dialogs scale the
interface down while open (Options 44 %, Fixes and Extensions 52 % at UI 40); icons for every row (a unit test now
fails a row without one); pause menu rows above the version line, both torn borders drawn (new engine seam
`kExtPlaceBackdropLower`); FrancophilSans in the apk, copied to the data folder at first start (`push-data` no longer
pushes it).

Rulings taken during the session: loading picture's TOP border is the one cut on 19.5:9 (keep); no migration for a
settings.cfg saved at Half / Quarter before the coupling (desktops are always 100).

Open after P9 (follow-ups, not blockers):
- At render scale Quarter with UI 42 the Options text squashes (sharp_ui rasterises at ~0.6 size in a 585x270 buffer).
- While Options is shown scaled down, Apply on the UI-scale slider saves the value but the dialog keeps its fitted size
  until closed; the scale-down is per interface, not per dialog (the engine has one scale per interface).
- The 4x scroll bars stretch retail's 12 px bar art (the thumb reads as a wide rounded block; no wider art exists).
- Tutorial glow ring around a toolbar button is drawn at the unscaled place while ext.touch_mode is on (tutorial only).
- The Enhanced tab is now 11 rows on a dialog that is already tall on the phone.

**P10 touch mode (RULED 2026-10-26, built on `android/p10-touch`, `docs/android/TOUCH_MODE.md`, handin TOUCHMODE.md):**
`ext.touch_mode` class A, Android seed only: bottom-left D-pad (pan) + rotate L/R + zoom in/out in the toolbar's own
frame, the toolbar as one bar beside it, scaled independently (`ext.touch_mode_toolbar_scale`, requested value then
SHRUNK TO FIT beside the cluster: ~1.49x on the S25); per-dialog scale seam `UI::UserInterface::_extDialogScale` (RULED,
null in faithful); a press captures the control it started on until release (RULED). Rulings owed: a pressed look for
the D-pad; the empty corner above the zoom buttons.

**P11 touch pointer (RULED 2026-10-26 from `docs/android/TOUCH_PLACEMENT_IDEAS.md`, all nine recommendations
accepted, NOT built):** one class A `touch_pointer` ext, Android seed only, reading SDL finger events through the pump
hook and injecting pointer / button edges after the poll (tools untouched): one finger AIMS while a tool is active
(retail's hover shows the ghost), pan = D-pad or two fingers, PINCH = zoom, two-finger SPIN = rotate (through
touch_mode's camera hold hook); Place / Rotate / Flip / Cancel action bar on the right above the toolbar; tap-twice as a
setting; lift offset on, distance tuned on the phone; line and rectangle tools drive retail's single-click mode (tap
start, drag preview, Confirm); edge auto-pan only in drag tools with a finger down; pick snap ~50 px + tap-to-cycle;
last device wins (a pinch / spin ends on release, then the mouse or the D-pad takes over).

**Next session (the owner's list after the P9 / P10 release-apk test, 2026-10-26 late), in this order:**
1. **P11 touch pointer**: the placement tool / road tool changes and "clicking on things with a finger" per the
   rulings above (`docs/android/TOUCH_PLACEMENT_IDEAS.md`): aim-then-Place, the action bar, retail's single-click mode
   for line / rectangle tools, pick snap ~50 px + tap-to-cycle.
2. **Touch-size pass on every control** (ext.phone_menu, extending the 2x close [x]): everything clickable gets about
   the height of the [x] / camera buttons: the buttons at the top of a dialog (pin, follow, ...), the `>` buttons on the
   citizens in a house, Ok / Accept / Cancel, the Options buttons and sliders.
3. Bugs seen on the phone: the loading screen still shows a small sliver of the picture at the bottom where the
   border strip sits too high (P9_LOADING_SCALE.md: the clean route keeps the menus' overlay sprite alive past the
   menu loop); the toolbar tooltip pops up over the toolbar button ABOVE the one tapped (finger only); the version text
   gets smaller when Options opens (the tall-dialog scale-down applies to the whole interface, P9_DIALOGS.md); the
   in-game pause menu is no longer centred; opening the tools and reports menu makes every dialog jump (the toolbar,
   the navigation panel and any open dialog such as a house), maybe the same per-interface scale-down.

Waiting, no action: the fifth CI check, a dispatch dry run of `release.yml`, the sun map at Shadow Quality None.

### 2.10 Android P11 outcome (2026-10-26, three opus worktree agents, merged on `android/p11`)

All three items of the §2.9 list are DONE (session docs `docs/android/P11_TOUCH_POINTER.md`, `P11_TOUCH_SIZE.md`,
`P11_PHONE_BUGS.md`); DX11 gate PASS (town up, empty crash log), resave PASS == golden, unit 1537/0/0.
- **ext.touch_pointer** (class A, Android seed): finger events through `Platform::Touch::SetSink` (SDL's
  touch-to-mouse emulation off), pointer states + action edges written after the poll through the one engine seam
  `Input::Device::_extAfterUpdate`; aim with lift offset, Place / Rotate / Flip / Cancel bar, pinch / spin / two-finger
  pan with last-device-wins, line and rectangle tools on retail's own single-click mode (`DragTool::OnKey`), edge
  auto-pan, pick snap + tap-to-cycle, `ext.touch_pointer_lift` / `_pick_radius` / `_tap_twice` keys. Checked on the
  S25 with SYNTHETIC input only (`tools/android_multitouch.py`); no real finger yet.
- **Touch-size pass** (ext.phone_menu `touch_size.cpp`): every clickable control about the enlarged [x]'s 36 units
  (85 device px at UI 42): title-bar buttons, the citizens' `>`, text buttons, check boxes, drop-downs, tabs,
  spinners, sliders (engine seam `UI::SliderElement::_extSpriteScale`, 1 = retail). Options' non-scrolling tab pages
  (Video, Audio) stay at retail size (enlarging them shrinks the whole fitted dialog).
- **Phone bugs**: version-text shrink, off-centre pause menu and the Tools-and-Reports jump were ONE cause (P9's fit
  scaled the whole interface) -> the tall-dialog fit now runs on the per-dialog seam `_extDialogScale` (installed by
  phone_menu, `TouchMode::Sync` folded in; the hover clip region is given in the dialog's units); loading-screen
  sliver = clip through the torn edge + strip on the truncated layout height -> the menus' overlay drawn to the real
  bottom edge (`_extBackdropOver` takes the sprite); tooltip after a tap RULED hide on finger lift
  (`Platform::Input::PointerIsTouch`, a finger event counts since touch_pointer eats the mouse emulation).

Rulings owed (each session doc §"Rulings owed"): touch_pointer lift default (160 vs 240), cycle window 1.5 s and pick
radius 50, Enhanced-tab rows for its three keys, spin direction, the bar's rotate / flip glyphs; touch size: Video /
Audio pages at retail size, the stretched 2x slider thumb, 3 house rows before scrolling (was 5), spinner arrows 18
units each; a finger never sees a tooltip now (building costs unreachable by touch: a press-and-hold tooltip would need
a hook in `DialogInstance::Update`'s hover rule). Not built from the ideas sheet: long-press tooltips, two-finger list
scrolling, the road route-modifier toggle, Android back as Cancel. Not verified: the tutorial's forced placement,
render scale Half with touch_pointer, a real KVM mouse on the phone with the tooltip hide.

**RULED 2026-10-26 (owner, after the real-finger test of the P11 apk; "great first pass"):** lift 160 KEPT; pick
radius / cycle window KEPT; the three touch_pointer keys get ONE "Touch controls" combo on the Enhanced tab; spin
sense KEPT; the Rotate / Change-model glyphs REUSE the undo / redo menu's left / right rotate icons, and the housing
icon sits too far left / right in its button and overlaps the edge (spacing to fix); the D-pad camera pan should be
SMOOTH (ramped) like the two-finger pan; the Enhanced tab's fit (a scrolling list) is the model: REPEAT it for the other
Options pages (Game / Video / Audio / Input) instead of keeping them at retail size; slider thumb KEPT; house rows
(3) KEPT; spinner scale KEPT but the Professions list must SCROLL (mods add professions); press-and-hold shows the
tooltip (a seam in `DialogInstance::Update`'s hover rule); fitted-dialog text KEPT. Two more: the Cancel / Ok and
Options / Default buttons of Fixes and Extensions, and the Game / Video / Audio / Input / Enhanced tab buttons of
Options, are still TOO SMALL to tap (target the [x]'s height); a dialog opened on a building appears ABOVE its place
and snaps down a few frames later. The remaining ideas-sheet items (two-finger list scroll, route-modifier toggle,
Android back = Cancel) are DROPPED: not wanted.

Also RULED the same evening: with a placement tool open, tapping a toolbar button made the ghost flicker -> the ghost
stays SOLID until Place / Cancel or another tool; Flip keeps the `<` `>` arrows (the two-way rotate glyph confused),
the new Rotate icons (undo / redo arrows) are good.

### 2.11 Android P11b outcome (2026-10-26 late, three opus worktree agents on `android/p11b`, stacked on PR #60)

All rulings of §2.10 are code (session docs `docs/android/P11B_POINTER.md`, `P11B_SCROLL_SIZE.md`,
`P11B_DIALOG_SNAP.md`); DX11 gate PASS, resave PASS == golden, unit 1545/0/0; the P11b debug apk is on the S25.
- **Pointer:** Rotate = ext.terrain_edit's undo / redo arrows by name, Flip = `<` `>`, glyphs fit 0.72 of the button;
  press-and-hold tooltip via engine seam `UI::UserInterface::_extHoverWhileDown` (phone_menu `touch_hold.cpp`, cursor
  held still within ~13 px of the press); "Touch pointer" combo on the Enhanced tab (Off / Default / Close aim / Far
  aim / Tap twice / Custom; radius steps of 25); D-pad pan + rotate ramp 0.30 s in / 0.20 s out through `panGain`
  (keys = 1); the ghost held while a finger presses a dialog (`HoldToolAim` / `ToolAimHeld` / `ToolCursor` seams in
  PlayerToolManager::Update, GameUpdateLoop::Draw, the tools' cursor ray — all behind `Ext().touch_pointer`).
- **Scroll / size:** round 1's buttons WERE 36 units — the P9 scale-down shrank the whole dialog (Fixes and Extensions
  at 0.78). One rule: a tall window gives height back from its scrolling lists first (floor 3 touch rows), the
  scale-down only below that; Video / Audio / Professions moved at run time into our `PhoneScroll.rsc` frame (retail
  descriptions untouched); Options back at the player's scale, Cancel / Ok 84 px.
- **Snap:** three causes (size walk a frame late; `SetUpperLeft` clamps to the screen while the layout uses the safe
  area; retail's stored-position drift at the bottom edge) -> new call point `kExtPlaceUpperLeft` + the walk and the
  fit run at a dialog's first placement. Panels stable from the first drawn frame.

Rulings owed: combo caption "Touch pointer" vs renaming touch_mode's row "Touch toolbar"; whether Off belongs in the
combo and the Close / Far presets; ramp constants and the coast after release (synthetic input only); the ~13 px hold
slop (a slider drag starts after 13 px); hotkeys ignored during a dialog press over an aiming tool; a `fix.*` row for
retail's stored-position drift on desktop; a dragged panel now stops at the safe margin; the Audio page's empty
scroll-bar track; the cap applies to every tall window (New Game still shrinks to 0.47, nothing in it scrolls).

**RULED 2026-10-26 (owner, real-finger test of the P11b apk):** placement icons good, flicker gone, touch pointer
combo fine (Off stays), slider slop fine, hotkey ignore good, drift fix good -> a `fix.*` row for desktop YES;
BUG: opening Options several times in a row breaks its scaling; tooltips work but sit under the finger -> they float
ABOVE it by about the lift distance (160 px); camera ROTATE ramp 0 (no ramp, too laggy); pan ramp kept but ~100 ms;
the D-pad is not "smooth": it locks to the cardinals -> analogue direction from the finger's position on the pad
(a circle of directions, the rectangular graphic stays); ZOOM smooth and continuous with zero delay (no steps);
the safe margin can shrink ~25 % on the edges.

**Next (P11c, three opus agents stacked on `android/p11b`):** the rulings above as code.

## 3. Rules that hold (do not relax)

- CLAUDE.md's hard rules: no null guard on a faithful crash (find what handed us the null); no `getenv`, flags through
  `Opt()`; poll a run with `tools/boot_state.sh --wait 300`; read `standalone_crash.log` first; no pixel diffs
  (`tools/trace_oracle/`); no doors (`tools/acceptance/acceptance.sh` at target); wiring a body is `behaviour:` and the
  consumer is read first; never `find /`; never `pacman -S`.
- The build is `tools/build.sh` (CMake + Ninja); `--jobs 4` for the gate, 2 for agents; a warning baseline is cut only
  from a clean build (`tools/build.sh clean` first); park the `.pkm` mods before a gate (a mounted mod is +1 load).
- The block shape of `phase_d/D4_RULES.md` §4 / §5 and `phase_d/GAPHUNT_RULES.md`: one `behaviour:` commit per retail body,
  B11 per touched TU, hand-in at `phase_d/handins/<BLOCK>.md`; agents never run the game, `-j2`, at most four builders;
  the coordinator alone gates (`gate.sh` / `mergegate.sh` on the MERGED tree before the merge commit; the gate table
  committed first); `RESOLVE_CACHE_GONE` stays beside the kit; kill an orphan by PID, never `taskkill /IM`; a fresh
  worktree is reset to a named commit; `git stash` forbidden; any text with a backslash goes through the Write / Edit tool.
- A resave DIFF is a finding, never a flake; a golden re-cut needs the payload / table analysis the gate prints.
- Perf numbers are recorded, not chased (the user's rule): a regression is a row, not a bisect.
- Lessons that still bite: memory `feedback_gap_hunt_team_lessons`, `feedback_agent_team_coordination_lessons`,
  `reference_d4_kit_traps` (an agent idle "waiting for a build" never resumes by itself: check ListAgents, message it).
