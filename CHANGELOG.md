# Changelog

One section per release, newest first. The section of the version being tagged is the GitHub
release's notes (`.github/workflows/release.yml` reads it with `tools/release/changelog_section.py`).
Headings are `## vX.Y.Z (YYYY-MM-DD)`; the version is the CMake `VERSION` in `CMakeLists.txt`.

## Unreleased

**Fixes**
- `fix.foundation_piles` (on): a building placed over trees or rocks no longer stalls forever when the
  town's storage is full. Retail leaves the felled logs and stone on the site, because no store will take
  them, and the site never finishes (the piles rot a year later). With the fix, citizens carry those piles
  to free ground just outside the foundation, and a stockpile collects them later as usual
  (`docs/retail_bugs.md` section 17). Off under `/profile:faithful`.
- Clearing road or field ground under a new building: a citizen given that task is no longer counted as
  still free, so the next job can no longer overwrite the task (`ClearArea::Execute` returns 2 there, as
  retail does).
- Missing game data (issue #72): when the game folder's `WinData/` is missing, holds no `.pkg` files or has
  no `data0.pkg`, OpenBA now shows one message that names the folder it looked in and says what to copy
  there (on Android: the USB / MTP steps into `Android/data/org.openba.game/files/WinData/`), then exits
  with code 6. Before, the boot failed with no explanation on every platform.

## v1.0.2 (2026-10-30)

**Major Changes:**
1. First Android build (a signed apk)
2. Vulkan renderer that replaces dxvk-native on Linux
3. Profile-guided Windows exe
4. Faster loading again, and a round of fixes checked against retail, several of them contributed.

**Platforms and packages**
- Android arm64 (`OpenBA-v1.0.2-android-arm64.apk`, signed; Vulkan 1.1 or later; the game data comes from
  the player's own install, `docs/android/README.md`). Its version follows the game's. Details below.
- A Vulkan renderer (`src/render/vulkan/`): the default on Linux and the renderer on Android. The Linux
  package no longer carries dxvk-native (`OpenBA-v1.0.2-linux-x64.tar.gz`: the exe, libSDL3, `OpenBA.sh`).
  Windows stays on DirectX 11; macOS on Metal.
- The release assets carry no `.sha256` files any more: GitHub shows each asset's SHA-256 on the release page.

**Build**
- Windows is built with GCC 16.2 (the pinned WinLibs build, `tools/toolchain/install_winlibs.sh`) instead of MSYS2
  GCC 13.1, and the release exe is profile-guided and link-time optimised (`perf/pgo/`): on the stress town the
  sim runs about 20% faster at 50x, drawing about 10% faster, in a 14% smaller exe. World generation's `powf` now uses the ported MSVC CRT body
  everywhere (`MsvcLibm::Powf`), so a generated world no longer depends on which C runtime the exe was built with.

**Loading (caching round 2)**
- `ext.menu_entry` (on): returning to the main menu, or reaching it at boot, no longer rescans every mod
  file and remounts every package when nothing changed (MegaMod: return to the menu 2.2 s down to 0.8 s).
- `ext.visit_replay` (on): the loader's later passes reach the resources they marked before from a record
  instead of walking every link again (MegaMod: 0.6 s off the post-load passes, 0.4 s off a new game).
- `ext.find_fastpath` (on): resource lookups a load already answered are answered from a table (save load
  entity stage 229 ms down to 129 ms).
- `ext.texture_quality` (off): a Texture quality choice on the Enhanced tab, Full or Half; Half makes
  large world textures without their top mip level (MegaMod: 3 GB never decoded, 10.1 GB down to 7.1 GB
  of deferred texture data). Sprite sheets, fonts and cursors are never affected.
- `ext.stream_cache`: a cache hit no longer reads anything from the package file (each record carries its
  resource's block directory; a package is identified by its size, date, header crc and file table, checked once
  at mount). On slow storage this matters: a cold MegaMod boot from a USB flash drive 126 s down to 58 s; on an
  SSD 0.2 s. The cache is rebuilt once (format 3).
- `ext.heap_slack` (on): the engine allocator keeps a little freed heap committed instead of returning it to
  the OS and taking it back a moment later, zeroing exactly the pages retail would have received fresh, so every
  byte any allocation sees is unchanged (MegaMod first start with an empty cache folder 15.8 s down to 14.4 s).
- With the four rows on, a MegaMod boot to the menu is 12.9 s down to 11.5 s on the reference machine.

**Fixes**
- The derived cache is rebuilt once after this update (its file format changed). A second running copy of
  the game now reads the cache instead of running without it; a fresh profile whose Documents folder did
  not exist yet now gets a cache on its first run; over the size cap the least recently used entries are
  evicted instead of everything the previous session did not touch; the cache rewrite runs after loading on
  its writer thread and survives a crash between its two file replaces; a lost update of the usage stamps.
- A boot crash on a first-run window size with the texture quality at Half: the menu scene load now waits
  for a pending device reset (the standalone loads that scene on the main thread, retail inside the loader).
- The borderless window no longer keeps a hidden caption frame, so a boot whose device reset is refused
  (another window holding the foreground) does not shrink the window and retry every frame.
- `fix.boot_device_lost` (on): a device removed by a GPU reset during boot is recovered from the reset path, not
  only from Present, and the main loop no longer walks a SaveLoad that does not exist yet during a device loss
  before the menu (a retail bug, `docs/retail_bugs.md` section 10). Off under `/profile:faithful`.
- Remove Roads works again: finished road tiles are marked and a laborer removes them, as in retail. The
  removal marker used to finish the moment it was placed, because nothing stands on a road cell. Thanks
  to Karl for the report.
- Drawn roads show their queued, see-through version at once, as in retail, instead of staying invisible
  until a builder finished the first cell (kralyerg, PR #41).
- Crop fields are replanted after their first harvest. A field planted once and then stayed idle for good,
  because the field's re-clear state (ClearArea state 0x15) was registered with null handlers. A save with
  fields stuck that way recovers on load (myungjoo, PR #33).
- A building marked for demolition shows its demolition status icon again (kralyerg, PR #65), and the
  citizen panel's status line shows the citizen's name again (kralyerg, PR #66).
- The terrain height and normal under a point use retail's terrain ray cast (two triangles per cell)
  instead of a smooth approximation. On the same seed a generated map's buildable ground and the start
  town can differ slightly from v1.0.1's.
- Simulation sorts, `sinf` / `cosf` and the needs queue now order and round exactly as retail does, on every
  platform (they used the host C library before). Marriage respects retail's age window (any younger single
  citizen used to qualify). Guards retail lacks were removed from trade, immigration, nomads, disease,
  quarries and several info panels; the Save / Load list rounds its slot count as retail.
- Returning to the main menu from a heavily modded save no longer crashes on the second mod remount.
- `fix.alloc_tiebreak` (on): when several citizens or houses are equally good for a job or a move, the job
  and housing allocators pick by entity id and profession order instead of by memory address, so a save plays
  out the same way each time it is loaded (a retail bug, `docs/retail_bugs.md` section 14). Off under
  `/profile:faithful`.

**Extensions and interface**
- `ext.item_priority` (class C, kralyerg, PRs #29, #64): rank the tools, fuel, clothing and health items
  citizens fetch from storage, and set an item to Never so citizens never take it.
- `ext.terrain_edit` (class B, off by default, kralyerg, PRs #37, #43, #44, #52): a brush that reshapes the
  ground, a resource tool that plants trees, plants, rocks and ore, with Undo and Redo.
- `ext.town_screenshot` (kralyerg, PR #54): one picture of the whole town from an isometric view.
- `ext.tool_names` (class C, off by default, kralyerg, PR #67): the citizen panel names the tool the citizen carries.
- The Extensions dialog has Optional and Default tabs; the in-game extension window's pages fill the list;
  eleven rows gained icons; the list keeps its size when a mod resizes the Mods window (kralyerg, PR #28).
- `fix.slider_drag_clamp` (on): a dragged slider keeps following the pointer and clamps at its ends.
- The Enhanced options tab fits the retail tab width.

**Android**
- Touch controls: `ext.touch_mode` (an on-screen D-pad, rotate and zoom cluster, the toolbar as one scaled
  bar, pinch zoom), `ext.touch_pointer`, finger-sized dialogs and lists, `ext.phone_menu` (a main menu laid
  out for a phone), safe-area insets, a half-resolution render scale and an fps cap.
- Older and mid-range phones (tested on a Galaxy S10 and S25): a Vulkan 1.1 path, BC textures transcoded to
  ETC2 on a GPU without BC, Half texture quality by default below 6.5 GiB of RAM, the frame's two threads
  pinned to the fastest cores, two frames in flight, ADPF hint sessions.
- Shadows can be switched off on a weak device with the game's own Options > Video > Shadow Quality = None:
  no shadow map is drawn at all.
- The release apk is built like the desktop packages (P12): the release flavour (-O3), ThinLTO, and a
  clang profile when one is committed (`perf/pgo/android/README.md`); `libmain.so` exports `main` alone.
- The frame rate cap is told to the display (`ANativeWindow_setFrameRate`, Android 11 and later): a
  120 Hz panel runs at 60 under the default cap instead of the game sleeping between every other vsync.
- The audio stream no longer asks for low-latency mode; the thread roster counts a heterogeneous SoC's
  big cores only; the fast frustum cull has a NEON body on arm64 (it ran the scalar reference before).

Thanks to kralyerg and myungjoo for their pull requests, and to Karl for reporting the road removal bug.

## v1.0.1 (2026-10-23)

Second release of OpenBA, five days after v1.0.0: Linux and macOS packages for the first time,
mod loading an order of magnitude faster, the first contributed fixes and extensions, and the
release build itself now made by GitHub Actions.

**Platforms and packages**
- Linux x86-64 package (`OpenBA-v1.0.1-linux-x64.tar.gz`): the exe with SDL3 and dxvk-native 3.1.1
  beside it, a launcher script and a README. Unpack next to the game's `WinData` folder and run `OpenBA.sh`.
- macOS Apple-silicon package (`OpenBA-v1.0.1-mac-arm64.zip`): an app bundle with a Metal renderer and a
  DirectX-to-Metal shader converter, Retina-aware UI (LucaSammartino, PRs #5, #6, #7).
- Windows x64 zip unchanged in layout (`OpenBA-v1.0.1.zip`: `OpenBA.exe`, the FrancophilSans files, README, licences).
- Every package is built by the Release workflow from the tag; CI builds and runs the unit tests on
  Windows, Linux and macOS and the no-doors acceptance check on every pull request. `main` is PR-only.

**Loading and memory (issue #13, MegaMod)**
- Main menu with the MegaMod loaded: about 60 s down to 14 s; memory 13.5 GB down to 2.5 GB.
- Exact, faster package reading: bulk array reads and memcpy LZ decode, packed reads served from a
  read-only mapping of the package, a hash index beside the resource tree and the package directory,
  package remount and mod sort done once and reused.
- The loading screen shows during the boot's loader frames and names the mod being loaded
  (`ext.load_progress`); the loader loops return at once and draw while the loader works.
- New class A extensions, on by default unless noted: `ext.decode_ahead` (next compressed blocks decode
  on worker threads), `ext.async_gpu_create` (GPU buffers and textures created on worker threads),
  `ext.create_on_demand` (GPU objects created at first use, their bytes skipped at load),
  `ext.share_gpu_data` (equal immutable buffers and textures share one GPU object), `ext.compress_ao`
  (baked AO maps as BC4), `ext.package_cache` and `ext.stream_cache` (the merged package directory and
  the consumed part of each resource stream kept in an on-disk derived cache for the next start).
- An on-disk derived-data cache of load-time transforms of the user's own data (never shipped), with
  its size and a Clear cache button on the Enhanced tab.
- `fix.large_packages`: mod packages larger than 2 GiB load (64-bit offsets).
- Present runs outside the device lock, under a backend Present mutex.

**Fixes**
- `fix.terrain_resort`: the red squares. Flat ground no longer stays marked too steep after flattening,
  and a cleared tree, ore or goods pile leaves steep ground buildable. Reproduced from the saves Karl sent in;
  thanks to him for the report.
- Garbled screen after minimize / restore or a fullscreen toggle (kralyerg, PR #17).
- The Town Hall Inventory tab counted every building as a home (kralyerg, PR #15).
- `fix.rock_seat`: rocks and iron ore are drawn seated on the slope instead of floating (render only).
- Decals over a rebuilt terrain chunk are re-meshed with it (retail `RebuildDirtyChunks`).
- The resource-limit UI accepts any ledger index a row's config names, not only 0..10 (#10).

**Extensions**
- `ext.restricted_foods` (class C, kralyerg, PR #11): choose which foods citizens eat. The first class C
  extension; it brings the shared extension toolbar window, a save-state codec, and host records in the
  save tail so a B / C extension's window and state survive a reload.
- `ext.high_limits` (kralyerg, PR #9): limit boxes take up to 999,999,999.
- `ext.no_fog` (kralyerg, PR #14): remove the fog from the picture (off by default).
- `ext.better_camera`: zoom range 1..200 and a steady tilt (class B, off by default).
- `ext.draw_batch` and `ext.terrain_batch` (class A, on): merged decal, road, smoke and terrain draw runs,
  a CPU saver for weak CPUs; the render-pipe fence is released before Present.
- Exact performance work with no behaviour change: start-town scatter Perlin precomputed in parallel
  rows, sound-effect buffers decoded in parallel at load, an ambient-environment scan memo.

**Project**
- The extension standard (`docs/extensions/DESIGN.md` §3): ids, directories, `Ext::<Id>` namespaces,
  hosts, codec, seams, hand-ins and tests on one form; every extension moved onto it.
- Platform and renderer seams: `Platform::` hooks, `BANISHED_RENDER_BACKEND` with a `backend.cmake` per
  renderer, backend-neutral code in `src/render/common/`.
- The main menu's build line shows the git commit and build date beside the version (README "Versioning").
- `AGENTS.md` (the contributor rule sheet), `CONTRIBUTING.md`, `THIRD_PARTY_NOTICES.md`, `.clang-format`.
- The FrancophilSans typeface files are kept unchanged in `tools/release/fonts/` with their notice, so a
  release is built from the repository alone.
- `-ffp-contract=off` everywhere, for identical float results across compilers and ARM.

Thanks to kralyerg and LucaSammartino for their pull requests.

## v1.0.0 (2026-10-18)

First release of OpenBA, the work of eight months (February to October 2026). Matching functionality
to retail Banished 1.0.7 with more performant and extendable code: the main exe, the runtime and the
DX11 video module are all ported and contained in `src/`. Extensions add features such as ambient
occlusion, foliage shadowing and performance tweaks. Windows x64 only; the Linux build was in its
foundational stages.
