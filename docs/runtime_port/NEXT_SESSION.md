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

### 2.12 Android P11c outcome (2026-10-26 night, three opus worktree agents on `android/p11c`, stacked on PR #61)

All §2.11 rulings are code (`docs/android/P11C_CAMERA.md`, `P11C_DIALOGS.md`, `P11C_TOOLTIP.md`); DX11 gate PASS,
resave PASS == golden, unit 1551/0/0; the P11c debug apk is on the S25.
- **Camera (touch_mode):** rotate no ramp (90 deg/s from frame 1); pan ramp 0.10 s in / 0.09 s out; analogue D-pad
  (finger direction at any angle, dead zone 0.10 of the side ~36 px, FULL speed outside it — proportional measured
  48-73 % of key speed on the arrows, kept in code behind one constant); held zoom continuous at 40 units/s from
  frame 1, no drift on release, clamped to retail's / better_camera's range, moves the eye under eye_camera
  (`ApplyHeldZoom` seam in UpdateCamera). Pinch still zooms in steps (follow-up).
- **Dialogs (phone_menu):** Options broke from the 3rd opening because the records (seen / list height) were keyed on
  the dialog's address and never dropped on close -> `TouchForgetClosed` / `TouchScrollForgetClosed` once a frame
  (6-7 reopenings of Options, Fixes and Extensions, Professions verified); safe margin 4.5 % -> 3.375 % of the screen
  height (15 units at UI 42; UI floor at render 50: 70 -> 65); **fix.dialog_position_drift** ("Steady panels at the
  edge", default on, `src/ext/dialog_position_drift/`, retail_bugs §13) on the `kExtPlaceUpperLeft` seam on every
  platform (S25 with phone_menu off: 101 -> 106 for the first frame; desktop trigger = a panel growing against the
  bottom edge, rare).
- **Tooltip:** a held tooltip floats the lift distance (160 px, touch_pointer's value when on) above the finger,
  inside the safe area, flipped below when there is no room (161-164 px measured); mouse = retail.

Rulings owed: margin 3.375 % is just under the S25's corner radius (3.5 %; no clipping seen); the fix row's name and
icon; zoom rate constant in distance (close end feels faster) vs proportional; full vs proportional pad speed; the
tooltip with lift 0 sits on the finger (a 160 floor is one line) and keeps retail's horizontal position.

**RULED 2026-10-26 (owner, real-finger test of the P11c apk):** Options sizing fixed, tooltips good, margin good,
D-pad feels good. Still open: Options, and the Save and Load dialogs, RESIZE themselves 1-2 ticks after opening
(the first-frame rule does not reach them); the Mods menu's buttons are NOT scaled, nor the Save / Load / Cancel
buttons (the touch size misses them); the camera should zoom IN closer, to about the size of two houses.

### 2.13 Android P11d outcome (2026-10-26 night, three opus worktree agents on `android/p11d`, stacked on PR #62)

All three §2.12 items are code (`docs/android/P11D_FIRST_FRAME.md`, `P11D_BUTTONS.md`, `P11D_ZOOM.md`); DX11 gate
PASS, resave PASS == golden, unit 1553/0/0; the P11d debug apk is on the S25.
- **First frame:** Options grew 24 units on frame 2 (the scroll frames added at first placement brought bars the
  thickening pass only reached a frame later); Save / Load widened ~230 ms after opening (they open on a hidden
  "Please wait" group whose list, with the 4x bar, is wider than retail's page). The bar pass runs at first
  placement and a thickened list inside a hidden group is measured as shown; its page holds that width. Every dialog
  traced holds one rect / scale from its first drawn frame.
- **Buttons:** the Mods and Save / Load dialogs were drawn SHRUNK (k 0.79 / 0.91) because their lists were never
  capped (the cap read an uninitialised height from a list on the hidden Browse page; SaveLoad's `mainGroup`
  minimum height) -> capped, drawn at the player's scale, Ok / Cancel / Load / Delete 85 px. Yes / No modal already
  right. New Game stays at 0.933 (78 px): no scrolling list to give height back.
- **Zoom:** `ext.touch_mode_zoom_min` (6..20, settings.cfg only), every zoom input on the phone stops at the nearer of
  it and the frame's limit; the pinch is continuous now; desktop / faithful untouched. Measured 8 = ~2.5 houses but a
  roof behind the focus cuts open below ~10; RULED default **10** (~3.2 houses, roofs closed).

Rulings owed: New Game at 0.933 (accept, or frame its page in a scrolling list with the last rows below the fold);
the Mods `...` Details button is 36 high but retail-narrow (needs the Load Order column wider); below 20 the tilt
flattens slightly and the pan slows (retail's formulas continuing; holding the 20 values is one line); the pinch
still stops at 20 with touch_mode off and touch_pointer on (both are on in the Android seed).

**Next: the owner's real-finger test of the P11d apk; PRs #60 -> #61 -> #62 -> #63 in order.**

### 2.14 Android P12: the mobile-platform pass (2026-10-27, one agent on `claude/vigilant-meitner-yjnlrv`, UNTESTED on a phone)

Branch `claude/vigilant-meitner-yjnlrv` (commit `android: release-grade apk build, display frame rate, big-core
roster, NEON cull`), written from a Linux container WITHOUT the NDK, a phone or the Windows gate: the Linux unit
build is 1561/0/0 and the Android-only sources were cross-syntax-checked with clang for aarch64-android, nothing
more. `docs/android/README.md` (Build, Release, Frame rate cap, Audio, Threads) and `perf/pgo/android/README.md`
describe each change. **The next agent's first job is to build it (`tools/android.sh build release`, sign with the
debug key, install) and run the checks below on the S25; nothing here is verified until then.**
- **Release apk = release flavour** (-O3, Gradle's RelWithDebInfo keeps -g in the unstripped copy) + ThinLTO +
  hidden visibility (`libmain.so` exports `main` alone, `-Wl,--exclude-libs,ALL`); `--flavour perf` keeps the P7
  -O2 build for A/B. Check: the apk boots (a symbol SDL's Java glue needs besides `main` would show as an
  UnsatisfiedLinkError in logcat), `standalone_crash.log` frames symbolise through `llvm-addr2line` as before
  (function names are gone from the live dump), then the P7 stress run (`docs/android/PERF_P7.md` §3.1) against
  `--flavour perf` for the delta.
- **Clang PGO for the apk**: `build release --pgo generate`, the two training towns, `tools/android.sh pgo-merge`,
  commit `perf/pgo/android/openba.profdata`, `build release` reads it (`BANISHED_PGO=auto`). The instrumented
  library writes `<data>/pgo/openba-<hash>.profraw` at `Platform::App::Shutdown` through the weak
  `__llvm_profile_write_file` (Android skips atexit). Check: the `.profraw` appears after a pause-menu quit; a
  recents-kill writes nothing (expected).
- **Display frame rate**: `Platform::Window::SetPreferredFrameRate` (new hook, identity off Android) is called by
  `FrameLimiter::SetCeiling` and re-applied by the Vulkan backend after every `CreateSwapchain`
  (`ANativeWindow_setFrameRate`, dlsym'd from libandroid.so, FIXED_SOURCE). Check: `[Window] preferred frame rate
  60 -> ANativeWindow_setFrameRate = 0` in `standalone_error.log`, `dumpsys SurfaceFlinger --timestats` showing
  presents at 16 ms (the panel at 60 Hz, not 120), and the rate surviving a trip to the background and back. If
  SurfaceFlinger refuses the switch (some OEM builds pin the rate), the limiter still paces as before.
- **Audio**: `SDL_HINT_ANDROID_LOW_LATENCY_AUDIO=0` in `PreSdlInitHints`. Check: music and effects play, no
  underruns in logcat (`AAudio`), the audio thread's CPU share in a simpleperf run lower than P7's.
- **Big-core roster**: `Platform::CpuCoreCapacity` (sysfs `cpu_capacity`) -> `Cpu::BigCoreMask` (keeps a CPU
  whose capacity is MORE than half the largest in the mask; unit-tested) -> the topology's `physical` counted
  inside that mask, `[Cpu] ... big=0x..` on a heterogeneous SoC. The S25 (2 prime + 6 performance) counts all
  eight, so the S25 shows NO change; a 1+3+4 or 2+6-little phone is where it acts. Nothing is pinned: pinning the
  process to the big mask (`sys.cpu_limit` in the phone seed) is the next lever if the helpers still land on little
  cores.
- **NEON frustum cull** (`src/engine/volume/frustum_cull.h`, the AArch64 arm) and the decode batch's FPCR: the
  existing bit-identical frustum tests run the NEON body on CI's macOS arm64 runner; the stress town's
  `BANISHED_STRESS_COMPPROF=4` shadow is the on-device check.
- **Looked at, not changed**: the swap chain already ASKS for 3 images (the S25's 5 is the driver's answer; the
  `[Vulkan] swap chain ... images (asked N, surface min M)` line will say which; 2 frames in flight is the real
  lever and a larger change); `Transform::AsMatrix` (nine scalar products in a fixed order, no NEON win);
  `VK_EXT_vertex_input_dynamic_state` (already not enabled).
- **Still open from the same survey, in order of value**: ADPF (`APerformanceHint` sessions for the main and render
  threads with the frame target, a thermal-status listener stepping the render scale / cap down; both API 31+,
  dlopen under minSdk 29) and a game-mode config (`appCategory="game"` is in since P12 on the S25: `category=manifest: 0`; Samsung GOS still says non-game, it keys on its own list); the gather helpers' spin (10 % of
  all samples in P7, `src/ext/gather_threads/`); WinData / the caches on external (FUSE) storage for Android 10-11
  devices (internal `getFilesDir` or a first-run copy); transient + lazily-allocated depth / MSAA attachments on
  the tiler (`draw.cpp` MakeMainImage); the NDK's stack protector / fortify defaults (a measured A/B before
  touching); the 30-minute thermal soak at the 60 cap that P7 never ran.

**P12 on the S25 (2026-10-27, branch `android/p12` on top of the above).**
- **Release apk boot: FAILED as written, fixed.** `[FactoryReg] FATAL: no exe type record`: under ThinLTO
  `used` does not survive the NDK's `--gc-sections` (0 `exe_type_records` entries). The ELF slots now carry
  `retain` (`platform_exe_type_section.h`; GCC 13 takes it too). Also ported from the local PGO attempt:
  `-fno-delete-null-pointer-checks` on every clang build (the instrumented apk crashed at the menu without it).
  After the fix: boots, new game, `libmain.so` exports `main` alone, no UnsatisfiedLinkError.
- **Frame rate: VERIFIED.** `ANativeWindow_setFrameRate resolved`, `60 -> = 0`; SurfaceFlinger active mode
  60 Hz with `frameRateOverride {uid=<app> 60}`; home screen 120 Hz, back in the game 60 Hz again (re-applied
  after the swap chain is remade). The swap chain answers 4 images (asked 4, surface min 4).
- **Crash symbolise: VERIFIED** on the boot-abort dump: `ExeTypeFatal` (factory_registration.cpp:64),
  `RunGameMain` (game_main.cpp:394, the `VerifyExeTypeInfos` call, at offset - 1), `main` (main.cpp:1165).
- **Stress A/B, `[Stress]` sim ms / iteration, 6000 frames, ABBA, 90 s cool-down + thermal 0 before each run,
  every run ends at thermal 2 (~43 °C battery):** perf 0.642 / 0.612, release 0.657 / 0.637: **no gain from
  -O3 + ThinLTO + hidden visibility**, inside the noise. PGO release 0.611 / 0.605 against plain release
  0.634 / 0.648: **-5% sim**, both PGO runs under both plain ones; draw CPU +7% (5.1 vs 4.8 ms, the training
  towns weight the sim).
- **PGO: DONE**, `perf/pgo/android/openba.profdata` committed (recipe corrected in its README: the flags as
  they exist, and a build dir configured before the file existed stays plain until removed).
- Not checked here: audio underruns under load (none in logcat at boot), the big-core roster (`[Cpu] mask=0xff
  logical=8 physical=6`, no `big=` on the S25 as predicted), the NEON cull shadow (`BANISHED_STRESS_COMPPROF=4`).

**P12 power pass (2026-10-27/28, same branch).** The four survey items, then the soaks:
- **Gather-helper spin: already done** (`2e76704d4`, 2026-10-25); the P7 profile predates it.
- **Two Vulkan frames in flight: MERGED** (`kFramesInFlight` 2, swap chain still asks 3: `kSwapchainImages`).
- **ADPF: MERGED.** Main + render APerformanceHint sessions at the cap's frame time (thread CPU time per frame),
  preferPowerEfficiency on, `[Thermal]` log only. Owner's policy: hold the cap, never step below it.
- **Tiler depth DONT_CARE: CLOSED, no change.** The sun-map and spot depths are the sampled shadow maps; the
  minimap depth is loaded across the frame-copy split on purpose; the reflection depth (~2.4 MiB/frame) cannot be
  proven dead when its pass begins. A learned discard costs a wrong frame (seen once): rejected.
- **Keep-screen-on: FIXED.** main.cpp allows the screensaver as retail does; Android `PostSdlInit` now calls
  `SDL_DisableScreenSaver` (the phone's 30 s timeout darkened a game in play).
- **Critical threads on the fastest cores: MERGED** (`3ccbba8f6`). The scheduler kept the main thread (92% of a
  3.53 GHz core) off the 4.47 GHz pair in 120/120 samples, with or without ADPF or its efficiency flag.
  AttachThread pins main + render to the largest-capacity CPUs when there are >= 2 (main alone: the render
  thread's shared cluster clock fell, no gain); `-Wl,--wrap=pthread_create` starts every new thread on the
  process mask (S25: 2 threads on 6-7, 41 on 0-7).
- **Soaks** (30 min, stress town capped by `BANISHED_FPS_CAP=60`, speed 10, food top-up, 803 -> ~1305 citizens;
  start at battery <= 32 C, fixed brightness, USB on; A = no ADPF, B = ADPF, C = ADPF + pin):

  | | A | B | C |
  |---|---|---|---|
  | 10 s windows at 60 fps | 88% | 84% | **99%** |
  | frame p95 last 10 min | 19.37 ms | 19.30 ms | **17.58 ms** |
  | update / draw last 10 min | 8.26 / 6.59 ms | 8.12 / 6.55 ms | **5.53 / 5.03 ms** |
  | thermal status 1 / 2 | 17.6 min / never | 17.3 / 29.9 min | **14.3 / 21.6 min** |
  | battery at 30 min | 41.4 C | 42.0 C | 42.7 C |
  | battery current, last 10 min (net of USB) | -58 mA | -54 mA | -441 mA |

  ADPF alone changed nothing measurable on this town (no slack for it to use). The pin holds the cap through its
  own throttling, at a heat and power cost. **Open:** a soak past 30 min (does deeper throttling cost frames?),
  battery drain in normal play (speed 1-3, where ADPF has slack), and a PGO retrain on the final code.
- **Where the frame goes** (last 10 min of the soaks, ~1305 citizens, speed 10 = 10 sim iterations a frame;
  `[SIMPROF]`, `[COMPPROF]`, `[Stress::pipe]` in standalone_error.log), A (not pinned) -> C (pinned):

  | per frame | A | C |
  |---|---|---|
  | sim (update) | 8.30 ms | 5.55 ms |
  | ... EntityList: tree walk (~18.4k nodes) / bucket ticks | 5.85 / 1.21 | 3.88 / 0.81 |
  | ... PostSim (UI, camera, audio) | 0.70 | 0.50 |
  | main-thread draw work (gather, cull, sort) | 3.14 | 3.07 (clock-insensitive: memory-bound?) |
  | main thread waiting on the render thread | 3.85 | 0.59 |
  | render thread: packet / emit (Vulkan recording) | 3.45 / 4.34 | 1.96 / 2.80 |

  At 1x speed the sim is one iteration (~0.55 ms pinned): normal play is render-bound (~3 ms main-thread draw
  work + ~5 ms on the render thread); the tree walk dominates at high speed and in large towns.

**Next session (2026-10-28), in order:**
1. **The three open checks above:** a 60+ min capped soak with the pin; battery drain in normal play (speed 1-3,
   camera pinned on the town, 30 min, pinned vs unpinned); retrain the PGO profile on the final code
   (`perf/pgo/android/README.md`; remove the release `.cxx` dir before the rebuild).
2. **Two investigations, in parallel (opus, one worktree each from `android/p12`, the scratchpad phone lock):**
   - **The render thread** (`src/ext/render_pipe/`, `src/render/vulkan/`): packet ~2 ms + emit ~2.8 ms a frame for
     ~400 draws. Profile it first (simpleperf needs `<profileable android:shell="true"/>` in a build-only
     manifest, as P7 did, `docs/android/PERF_P7.md` §3.1). Suspects to check against the profile, not assume:
     descriptor-set allocation per draw (P7: 93-99 sets a frame; the device has push_descriptor), redundant
     pipeline / vertex-buffer binds, the constant ring's writes, the packet build's copies. Goal: less render
     thread CPU per frame = power, and room for the main thread at 1x.
   - **The EntityList tree walk** (`EntityList_Update` treeWalk, `src/game/Entity/`): 3.9 ms a frame pinned, 70%
     of the sim at speed 10, ~1830 nodes per iteration. It is faithful sim code: a change must give identical
     results (class A, an `ext.*` row under `src/ext/`, the faithful path untouched and exactly as fast) or it is
     class B (locked in the save; owner ruling needed). **The desktop already did this walk** (2026-10-12/13,
     `docs/extensions/STATUS.md` those dates, handins PREFETCH / SMMEMO / STILLPATH / STILLTERRAIN / DIRECTTICK /
     PARTREPEAT / OFFSCREEN / WALKBATCH): faithful treeWalk 2.57 -> ~1.9 ms, the ideas list closed. Do not redo it;
     test what may not transfer to ARM: the IBS verdict "the sim is not memory-bound" was taken on a 96 MB X3D L3
     (the S25: ~12 MB L2 per cluster + a small SLC; the main-thread draw work did not speed up with a 27% faster
     clock either), the prefetch distance (`kWalkPrefetch`, "one entity apart", tuned on x86), and the class B
     `walk_batch_grid` (desktop speed-50 everything-on fps +8..+20%) never measured on the phone.
   Both: findings and a ranked plan first, prototypes only where the profile justifies them, measured on the S25
   from the same start (battery <= 32 C, capped stress town); nothing merges without review.

### 2.15 Android P13 (Galaxy S10) (2026-10-28, opus worktree agents merged on `android/p12`, then one combined verification)

The S10 (SM-G973U, Snapdragon 855, Adreno 640, Vulkan 1.1.128: the backend's 1.1 path, `src/render/vulkan/vulkan11.h`)
stood in for the S25 for the whole pass. Every number is release + PGO, debug-key signed, battery <= 32.0 C and
thermal status 0 at each start; the per-pass docs carry the protocols.
- **Profile** (`docs/android/P13_PROFILE.md`): PGO retrained on the P13 base, stress town (cap 60, speed 10, ~803
  citizens) 53.6 fps, treeWalk -6.5% from the retrain alone. The app holds ~0.9 GB (`dumpsys meminfo` under-reports the
  GPU ~4x; the kgsl `page_alloc` difference is the method), ~330 MB of it the BC textures decoded to RGBA8. A 20-min
  soak throttles from ~155 s with Android's thermal status still 0.
- **Render thread** (`P13_RENDER.md`): the 1.1 path's per-pass VkFramebuffer kept in a cache (emit -1.9 ms) and the
  draw_batch instancing prefetch (packet -0.8 ms): stress 35.6 -> 51.3 fps, render-thread CPU ~6.8 -> ~3.9 ms a frame.
- **Walk** (`P13_WALK.md`): the S10's walk is memory-bound (unlike the desktop's X3D verdict); the prefetch names a
  walking character's MoveTo lines on ARM: treeWalk -5.2%, update -2.5%. Main thread on the prime: blocked by
  core_ctl, dropped; `ext.walk_batch_grid` no gain at speed 10.
- **Memory** (`P13_MEM.md`): BC1 / BC3 transcoded to ETC2 at load on a GPU without BC (in-tree encoder, 35.7 dB
  PSNR against the BC decode; ETC2 encodes cached in the derived-data cache): GPU memory -195 MB at the menu,
  -267 MB in town, fps neutral; the load waits for its prefetches so no texture is made on use with its transcode
  pending (first-boot hitch 117 ms -> 0); texture Half by default below 6.5 GiB RAM.
- **Thermal** (`P13_THERMAL.md`): no ADPF HAL on the S10; the frame's two threads pinned to the fastest tier of two
  or more CPUs (gold here): throttled soak +11% fps, the saturated phase keeps real time (59.1 against 54.0 sim
  steps/s). How to read a soak: update ms/step AND sim steps/s at equal t, never fps alone.
- **Overlap** (`P13_OVERLAP.md`): `ext.render_pipe`'s emit overlaps the next Frame's steps from a per-packet
  snapshot of the drawn geometry; sim untouched (resave gates). Stress 52-53 -> 60 fps capped, fence wait 4.2 ->
  0.7 ms, snapshot ~2 ms of main thread a frame; normal-play post-to-present 44 -> 40 ms.
- **CPU** (`P13_CPU.md`): the 1.1 path's pooled pixel descriptor set with dynamic offsets, the ARM gather (flag test
  + MaterialInstance prefetch): 53.5 -> 58.1 fps before the overlap.
- **Tooling:** `docs/android/LLDB.md` (native debugging from the Windows host).

Owner rulings of 2026-10-28: the render_pipe overlap APPROVED provided the sim stays retail; BC -> ETC2 / ASTC
transcode on devices without BC; texture Half on low-memory devices; fewer frames in flight approved, but 1 frame
REJECTED (the stress town halved: 32 fps); the ETC2 encode runs on the loading side ahead of first use.

**Combined verification** (`android/p13-final`: android/p12 `65f3ac6e8` + the PGO profile retrained on it, the 10x
and 50x towns, 89006 blocks):
- **Boot** (1x town by autostart, Quit from the pause menu; `EXITED clean-shutdown`, crash log empty every time):

  | | cold (derived cache deleted) | warm 1 | warm 2 |
  |---|---|---|---|
  | launch to the menu's first update | 3.26 s | 2.16 s | 2.20 s |
  | town load (loader run to done) | 4.74 s | 4.93 s | 4.84 s |
  | launch to the town loaded | 9.62 s | 8.44 s | 8.31 s |
  | load-end wait (menu / town) | 1117 / 68 ms | 0 / 72 ms | 0 / 10 ms |
  | made on use | 0 | 0 | 0 |
  | `[Etc2]` at quit | 103 textures, 90 encoded (3962 ms), 53.4 MB (RGBA8 310.3) | 101 cache hits, 2 encoded | 101 hits, 2 encoded |

  `path 1.1 ... bc=0 etc2=1 (transcode 1)`; `[Pin] the largest capacity 1024 has one CPU: the tier of capacity 871
  (3 CPUs)`, both threads `-> CPUs 0x70: pinned`.
- **Stress town, cap 60** (medians from t = 20 s; pipe = `[Stress::pipe]` windows after the first):

  | run | start / end C | fps | frame ms (p95) | update | draw | pipe main / packet / emit | wait | latency | refused |
  |---|---|---|---|---|---|---|---|---|---|
  | 1 | 31.9 / 33.7 | 59.9 | 16.70 (17.05) | 9.24 | 12.41 | 3.21 / 9.20 / 10.21 | 0.50 ms | 10.46 ms | 0 / 5980 |
  | 2 | 32.0 / 34.0 | 59.9 | 16.70 (17.50) | 9.25 | 12.51 | 3.21 / 9.33 / 10.25 | 0.47 ms | 10.50 ms | 0 / 5977 |
  | 3 | 32.0 / 34.0 | 59.9 | 16.70 (17.21) | 9.21 | 12.44 | 3.25 / 9.19 / 10.29 | 0.48 ms | 10.55 ms | 0 / 5970 |
  | uncapped | 32.0 / 34.8 | **82.1** | 12.18 (16.59) | 8.56 | 10.31 | 3.16 / 7.13 / 9.03 | 0.62 ms | 9.21 ms | 0 / 8158 |

  Snapshot 2.0-2.1 ms a frame (2.9-3.1 MB a packet); every packet overlapped, none refused. P13_OVERLAP's B runs:
  60.0 / 59.9 fps, p95 16.7, update 8.6-8.7; the combination holds the cap with a p95 0.3-0.8 ms higher.
- **Normal play** (autostart, `BANISHED_CAMERA_PIN=town`, 1x, 180 s after 40 s): SurfaceFlinger 10830 presents,
  **0 dropped, 0 janky**, present-to-present 16 ms, post-to-present mean 40.2 / median 40 / p95 44 ms (P13_OVERLAP:
  39.6 / 40 / 40). CPU % of one core: main 56, render 29, process 92 (P13_THERMAL's pin run: 35 / 30 / 73, without
  the overlap; the snapshot is the assumed difference, no `[Stress::pipe]` counters at 1x, not split further). Gold 822 MHz mean, GPU 324 MHz 74%
  busy. **GPU memory 391 MB** (kgsl 518.6 - 127.6 MB; P13_MEM's ETC2 town 390 MB), PSS 424 MB, battery 32.2 -> 33.0 C.
  Screenshots: the town right (rain, smoke, roofs, foliage), citizens and chickens drawn whole at the town
  camera's distance (~10 px figures; no torn or missing meshes).
- **19-min soak** (cap 60, 66000 steps; against P13_THERMAL's soak B, the pin alone, at equal t):

  | phase | fps | sim steps / s | update / step | wait / frame | gold MHz (cap) | battery |
  |---|---|---|---|---|---|---|
  | 0-240 s | **54.5** (B 46.5) | 60.0 (60.0) | 10.20 (9.89) | 0.47 (4.22) | 2225 | 32.0 -> 35.2 C (B -> 34.4) |
  | 240-600 s | **33.0** (30.0) | 59.9 (59.9) | 12.49 (11.90) | 0.56 (4.52) | 1708 (B 2016 from min 6) | -> 35.9 (35.1) |
  | 600-1100 s | 24.8 (24.2) | **56.6 (59.1)** | **14.23 (12.97)** | 0.60 (4.62) | 1708 (2016) | -> 36.3 (35.8) |
  | whole | 33.7 (30.8) | 58.4 (59.5) | 12.77 (11.96) | 0.54 (4.46) | | end 36.3 C (35.8) |

  More frames early (minutes 1-5 +5 to +12 fps), equal at minutes 6-12, -1 to +2 fps after; but the phone runs
  0.5-1.1 C hotter from minute 2, the gold cap settles at 1708 MHz instead of 2016, and in the saturated
  phase update per step is +10% and **the sim falls behind real time** from minute 13 (52-58 steps/s against
  55.6-59.5). Thermal status 0 throughout, main thread on 4-6 in every sample, `EXITED clean-shutdown` at 1134 s.
  One run against one earlier run (another session, another hour): the heat is not separated from ambient.
- **Windows:** `tools/build.sh unit` 1574 tests, 0 failures, 3 skipped (the kit-font ones); `tools/build.sh run`
  (DX11) and `BANISHED_RENDER_BACKEND=vulkan tools/build.sh run`: in game, crash logs empty (Vulkan: 0 validation
  errors, the documented depth-only `SV_Target` warning); resave `Gate` **PASS**, golden unchanged (md5
  `971aaec2...`); acceptance 0 lines off target.
- **Desktop cost of the overlap** (perf flavour, stress town speed 50, 6000 frames, faithful profile + the
  extensions named, DX11, RTSS stopped, nothing else running; base `2a5c2c481` = android/p12 before the overlap):

  | run | tree | extensions | sim ms / iter | update ms / frame | fps | frame p95 | overlap |
  |---|---|---|---|---|---|---|---|
  | A1 | base | render_pipe | 0.14 | 7.03 | 715 | 8.64 | - |
  | B1 | final | render_pipe | 0.16 | 8.06 | 619 | 9.38 | 600 steps, 5948 snapshots |
  | B2 | final | render_pipe | 0.16 | 8.09 | 619 | 9.66 | 600 steps, 5520 snapshots |
  | A2 | base | render_pipe | 0.14 | 7.19 | 713 | 8.78 | - |
  | C1 | final | render_pipe + terrain_edit (every snapshot refused) | 0.16 | 7.86 | 675 | 9.58 | 0, 6441 refused |
  | D1 | base | render_pipe + terrain_edit | 0.14 | 7.12 | 711 | 8.79 | - |
  | E1 | final | none | 0.15 | 7.38 | 526 | 8.89 | - |
  | F1 | base | none | 0.14 | 7.14 | 546 | 8.61 | - |

  With render_pipe on the final tree costs the desktop **~13% fps (714 -> 619) and +13% update** at speed 50.
  The overlap itself (B against C, same tree) is ~8% fps (675 -> 619) and draw CPU 1.06 -> 1.77 ms (copy 0.18 ms a
  snapshot plus the park at the gate); the rest (+10% update, -5% fps) is there with every snapshot refused, so it
  is not the copy. The sim-side change since the base that runs whenever render_pipe is installed is the
  `MeshInstance::_extInstancedState` seam on the four pose lookups (`FrozenInstancedState`, a `thread_local`
  read; under MinGW GCC a `thread_local` is an emutls call): the likely cause, **not profiled**. render_pipe off:
  final 7.38 / 526 against 7.14 / 546 (one run each, within spread or a small layout cost). Desktop fps at speed
  50 is far above any display, and ext.* is off by default there, so this is not a player-visible loss today; the
  overlap pays where the fence wait is the limit (the S10's 4.2 ms) and costs where it is not (a 0.17 ms desktop
  wait). Recommendation, no change made: profile the seam first; limit the snapshot to where it pays (e.g. a fence
  wait above a threshold, or the platforms whose wait was measured), and keep the seam uninstalled when no
  snapshot will run.

Open:
- ~~The S25 has run no P13 commit~~ DONE: "P13 on the S25" below, no regression.
- LLDB attach crashes the NDK 30 host lldb; `LLDB.md` recipe 1 is untried.
- 2 textures re-encoded on every warm boot: CLOSED, not reproducible. With a temporary miss log, 5 warm boots on `664c4045b` (60 s x3, 210 s x2, some ended by force-stop) had 0 misses, 103 of 103 hits, the cache steady at 294 entries; the earlier +2 a boot (251 -> 253 -> 282 entries, different textures each run) was the cache filling in, not a key that changes per boot.
- ~~The desktop cost above (the seam, the snapshot)~~ DONE on `android/p13-adapt` (P13_OVERLAP section 10): the
  seam's `thread_local` (an emutls call per pose lookup, 4.4% of the main thread's samples, profiled) replaced by
  an address test, the seam installed only while a packet with copies is in flight, and a governor that takes the
  snapshot only when the frame is main-thread bound and the removable fence wait beats the copy. Desktop speed 50
  726 fps against the base's 735 (6 runs, within spread; 619 before); S10 stress 59.9 fps capped, normal play 0
  snapshots, 60 locked, 0 dropped.
- Left from that question: normal-play post-to-present without the snapshot is 39-44 ms (mean of three 41.4)
  against 40 with it (~1-2 ms back towards the base's 44); the main thread's CPU in normal play varies 29-41%
  between runs of one build (the 56% of 2.15 was not reproduced, 41% with the overlap on throughout). The soak is
  main-thread bound throughout, so the governor keeps the overlap on and the soak's heat (gold cap 1708 MHz, sim
  behind real time from minute 13) is unchanged; whether it is the overlap's extra early frames or ambient is not
  separated (one run against one earlier run).
- The RenderPipe shadows (COMPPROF=4) slow the render thread enough that the governor never switches on under them;
  the copy shadow was run with the governor forced on in a local build (0 mismatches). A harness way to force it
  (without a flag for the feature) is not there.
- The low-memory Half path is untested on a real low-memory phone.
- Candidates not done (P13_CPU §2 items 3-4, §4.4): the walk prefetch leftovers, more prefetch in draw_batch's
  instanced loop, the vertex / index bind de-dup.

**P13 on the S25** (2026-10-28/30; Galaxy S25 SM-S931U, Snapdragon 8 Elite, Adreno 830, Vulkan 1.3.284, Android
17). Tip = `android/p12` `e8a0f6a52` (the whole P13 pass + the adaptive overlap), base = `fb9190a26` (the last commit
before the P13 merges, P12's profile); both release + PGO, debug-key signed, USB attached, screen kept on. Every
run started at battery <= 32.0 C and thermal status 0, cooled with the app stopped; the phone lock held.
- **Checks: all PASS.**
  - `path 1.3 ... bc=1 etc2=1 (transcode 0)`; no `[Etc2]` line in any of the 19 runs (the BC path never transcodes).
  - Pipelines: every `[Vulkan]` frame line reads `warmed 0 failed`, `failed=0` (1667 lines).
  - Validation (debug apk, Khronos layer 1.4.363): 0 errors. The only warning is the documented depth-only
    `SV_Target` one: 10 of it, then the duplicate limit.
  - Pin: `[Pin] main thread ... -> CPUs 0xc0 (of 0xff): pinned`, and the same for the render thread, in every run.
    Main and render were on the prime pair (6-7) in every sample of every run.
  - ADPF: `[ADPF] main session` and `render session` (`target 16666666 ns, preferPowerEfficiency on`) in every
    capped run. The uncapped runs, tip and base alike, create no session (no frame target).
  - Memory: `[Memory] total RAM 10.85 GiB: not low memory` (logcat).
  - Frame rate: `preferred frame rate 60 -> ANativeWindow_setFrameRate = 0` in every capped run, and the
    SurfaceFlinger `activeMode` is 60 Hz.
  - Shutdown: `EXITED clean-shutdown` after the pause-menu Quit and at the end of every stress run and soak; the
    normal-play runs are force-stopped by their script. Every crash log is empty; there is no hang log.
- **The overlap governor never switches on on the S25.** No `[RenderPipe] overlap on|off` line appears in any
  release run, and every `[Stress::pipe]` window reads `governor=off snapshots=0 refused=0`. That is the rule of
  P13_OVERLAP §10.2 working as built:
  - Capped stress: the frame idles 5-7 ms (the cap's sleep), and the removable wait is 1.7-2.3 ms.
  - Uncapped stress: idle 2.7-3.4 ms against a removable wait of 0.4-0.7 ms.
  - Normal play: no switch line.
  The debug apk under the layer switched it on while loading (`removable fence wait 8139 us, idle 498 us`) and
  off once in town. On this phone the P13 overlap code is installed and idle.
- **Stress town, cap 60** (6000 frames, ABBA; medians from t = 20 s; pipe = `[Stress::pipe]` windows after the
  first; update / step from the CSV after t = 40 s):

  | run | start / end C | fps | p95 | update | update / iter | draw | pipe main / packet / emit | wait | latency |
  |---|---|---|---|---|---|---|---|---|---|
  | 1 tip | 32.0 / 35.0 | 60.0 | 16.67 | 4.30 | 0.427 | 4.90 | 3.11 / 1.78 / 6.17 | 2.04 | 6.22 |
  | 2 base | 32.0 / 35.0 | 60.0 | 16.67 | 4.45 | 0.436 | 5.09 | 3.15 / 1.94 / 5.75 | 2.25 | 5.81 |
  | 3 base | 32.0 / 35.1 | 60.0 | 16.67 | 4.50 | 0.447 | 4.99 | 3.13 / 1.87 / 5.47 | 2.14 | 5.58 |
  | 4 tip | 32.0 / 34.9 | 60.0 | 16.67 | 4.32 | 0.439 | 5.18 | 3.27 / 1.79 / 5.50 | 2.10 | 5.56 |

  **No regression.** Update is 3% lower on the tip in both pairs (4.30 / 4.32 against 4.45 / 4.50 ms; the walk
  prefetch and the retrained profile are the assumed cause, not profiled). Draw and packet are equal. The emit
  includes the wait for a swap-chain image at the display's pace and is not separated from noise (tip 6.17 / 5.50,
  base 5.75 / 5.47).
- **Stress town, uncapped** (same, ABBA; fps = median, then the run mean and the mean after t = 40 s):

  | run | start / end C | fps | p95 | update | update / iter | draw | pipe main / packet / emit | wait |
  |---|---|---|---|---|---|---|---|---|
  | 1 tip | 31.9 / 40.5 | 132.9 (128.5, 120.6) | 14.65 | 3.64 | 0.367 | 3.53 | 2.30 / 1.27 / 6.95 | 2.95 |
  | 2 base | 31.9 / 40.7 | 132.2 (128.8, 121.8) | 15.03 | 3.86 | 0.379 | 3.66 | 2.32 / 1.31 / 6.57 | 3.57 |
  | 3 base | 31.3 / 41.3 | 132.4 (123.7, 116.5) | 14.91 | 3.83 | 0.380 | 3.52 | 2.25 / 1.32 / 6.57 | 3.74 |
  | 4 tip | 31.7 / 41.5 | 130.5 (125.0, 116.8) | 14.88 | 3.79 | 0.373 | 3.62 | 2.27 / 1.26 / 6.68 | 3.80 |

  fps is equal within the pairs' spread. Update per iteration is 2-3% lower on the tip. Every run ends at
  thermal status 2.
- **Normal play** (autostart, `BANISHED_CAMERA_PIN=town`, 1x, 180 s after 40 s; tip-base-base-tip-tip-base;
  SurfaceFlinger timestats of the game layer; CPU % of one core from `/proc` ticks; GPU memory = kgsl `page_alloc`
  in game less after the stop, the P13_PROFILE method):

  | run | presents | dropped / janky | post-to-present mean / median / p95 | CPU main / render / process | GPU MHz, busy | GPU memory | battery |
  |---|---|---|---|---|---|---|---|
  | 1 tip | 10984 | 0 / 0 | 35.3 / 36 / 46 ms | 15 / 13 / 35 | 351, 76% | 420.5 MB | - |
  | 2 base | 11027 | 0 / 0 | 29.4 / 29 / 38 | 16 / 13 / 37 | 587, 81% | 420.3 | - |
  | 3 base | 10956 | 0 / 0 | 47.3 / 48 / 58 | 16 / 13 / 37 | 386, 83% | 424.5 | 33.3 -> 34.6 C |
  | 4 tip | 10928 | 0 / 0 | 44.5 / 44 / 58 | 13 / 13 / 33 | 512, 80% | 421.6 | 33.7 -> 35.0 |
  | 5 tip | 10959 | 0 / 0 | 35.2 / 36 / 46 | 15 / 13 / 36 | 495, 79% | 421.4 | 33.7 -> 35.2 |
  | 6 base | 10998 | 0 / 0 | 36.2 / 36 / 46 | 13 / 12 / 33 | 646, 80% | 419.4 | 34.6 -> 37.2 |

  **No regression.**
  - Frames: 60 locked (present-to-present 16 ms), 0 dropped, 0 janky in all six runs.
  - Post-to-present: a mean of three of 38.3 ms on the tip against 37.6 on the base, with a 29-47 ms spread
    within each build.
  - CPU: equal (main 13-16%, render 12-13%). The prime pair sits at its 1017 MHz floor throughout.
  - GPU memory: equal (419-425 MB; no transcode on either build). PSS 772-794 MB (`dumpsys meminfo`).
  - Battery current: net of USB, -47 to +1 mA, not comparable at this size (runs 1-2 recorded none).
- **Soak** (P12 soak C's protocol: 30 min stress town at cap 60, 108000 frames, brightness manual 100, sensors
  every ~10 s; equal t, P13_THERMAL's columns):

  | phase | 60 fps windows | update / step | update / step per 1000 citizens | citizens | draw | p95 | thermal | battery |
  |---|---|---|---|---|---|---|---|---|
  | 0-300 s | 100% (base 97%) | 4.36 (4.16) | 4.81 (4.59) | 991 (1007) | 4.82 (4.74) | 16.67 (16.67) | 0 (0) | 37.1 (35.8) C |
  | 300-600 s | 100% (100%) | 4.75 (4.82) | 4.68 (4.67) | 1048 (1062) | 4.99 (5.01) | 16.67 (16.99) | 0 (0) | 39.1 (37.9) |
  | 600-900 s | 100% (100%) | 4.96 (5.57) | 4.54 (4.99) | 1140 (1170) | 4.96 (5.28) | 16.92 (16.67) | 1 (0) | 40.7 (39.5) |
  | 900-1200 s | 100% (100%) | 5.87 (6.06) | 5.00 (5.04) | 1199 (1227) | 5.15 (5.24) | 17.36 (17.07) | 1 (1) | 41.9 (41.2) |
  | 1200-1500 s | 100% (100%) | 6.06 (6.05) | 4.97 (4.80) | 1242 (1285) | 5.18 (5.08) | 16.82 (17.42) | 2 (2) | 42.5 (42.2) |
  | 1500-1800 s | 100% (100%) | 6.15 (6.06) | 4.91 (4.68) | 1258 (1297) | 5.06 (5.05) | 17.56 (16.67) | 2 (2) | 42.9 (42.8) |

  Tip run 2 (base in brackets). The details:
  - Sim: 60.0 steps / s in every phase of both runs.
  - Last 10 min: update 6.11 against 6.05 ms (per 1000 citizens 4.94 against 4.74, +4%), draw 5.12 against 5.06,
    p95 17.20 against 17.04, wait 2.53 against 2.56 ms.
  - Thermal status 1 / 2 at 662 / 1360 s against 916 / 1452 s.
  - Battery: 32.0 -> 42.9 C against 31.4 -> 42.8 C.
  - Current (net of USB, charging in both): +234..+348 against +233..+303 mA.
  - The 600-900 s phase is the base's throttle (CPU caps 2009 / 1974 MHz, GPU 525); the tip throttled later and
    less there.
  - Both runs: main and render on 6-7 in every sample, `EXITED clean-shutdown`, crash log empty.

  **No regression in frames or sim.** Thermal status came 4.2 / 1.5 min earlier on the tip, but its start was
  0.6 C warmer, its battery was at 70% against 58% (a different charge current), and the end temperature is equal.
  This is one pair, so that difference is not separated from the start; nothing points at P13 code, since the
  overlap stays off here.

  Tip run 1 (`p13s25/soak_tip`, 2026-10-29) is **not comparable**:
  - A fire at ~35 s burned 10 buildings.
  - The fire-fighting walks swamped the pathfinder: 99.9% busy from 50 to 230 s, a queue of up to 103k, path
    length 15 -> 70.
  - The town starved from 947 to 633 citizens and ended at 1056.
  - Thermal 2 came at 231 s, with the clocks capped from 51 s and -950 mA drawn.
  Fires are random in the stress town (P12's soak A had one; even the 100 s runs differ by a few citizens between
  identical runs), so a soak whose log shows `[Collapse] ... burning` is re-run, not compared.

  Against P12 soak C (the same protocol, 2026-10-27):
  - C: 99% at 60 fps, update 5.53 ms in the last 10 min at ~1290 citizens, thermal 1 / 2 at 855 / 1297 s,
    42.7 C at 30 min, -433 mA (not charging).
  - This session's base: update 6.05 ms (+9%) with the same code minus P13, so the session (ambient, the phone
    charging, 2026-10-30) costs ~9% update.
  - Tip and base agree with each other.
- **Verified:** every check above. Equal frames and sim on the S25 in capped and uncapped stress, normal play
  and the 30-min soak. The overlap governor stays off on this phone. GPU memory unchanged (no transcode).
  **Assumed:** the cause of the tip's 2-3% lower update (walk prefetch / profile, not profiled). The earlier
  thermal status of tip soak 2 being the start conditions (one pair). The emit difference being display pacing.
- Data: the scratchpad's `p13s25/` (the earlier runs) and `p13s25b/` (both soaks of 2026-10-30, `soak_ab.sh`,
  `soak2.py`, `sf.py`).

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
