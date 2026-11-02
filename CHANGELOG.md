# Changelog

One section per release, newest first. The section of the version being tagged is the GitHub
release's notes (`.github/workflows/release.yml` reads it with `tools/release/changelog_section.py`).
Headings are `## vX.Y.Z (YYYY-MM-DD)`; the version is the CMake `VERSION` in `CMakeLists.txt`.

## Unreleased

**Extensions**
- Natural tree growth (`ext.better_trees`): in spring the deciduous trees now flower, each on its own
  schedule for a week or two. Plum, cherry, peach, pear and apple carry white and pink blossom, and the
  early ones flower on bare branches. Oak, birch, pecan and chestnut carry catkins. Modded trees get a pale
  blossom colour picked from their names. The flowers are drawn in the existing tree shaders, so DX11,
  Vulkan and Metal all get them (`docs/extensions/BLOSSOM.md`).
- `ext.better_water` (on, the Enhanced tab's "Better water" Off / Plain / Fancy, Plain by default): a shore
  edge on rivers and lakes. Plain: the ground under the shallows darkens as wet ground does, the waterline
  wanders a little instead of following the terrain contour exactly, and a thin line runs along it, white on
  the outside of bends. Fancy adds the bank side (a wet, darker bank and a foam fringe where the water
  visibly starts) and foam where the water meets piers, bridge feet and boats (DX11 and Vulkan; Metal draws
  none yet). Extra passes over the water with our own shaders; retail's water shader is not touched
  (`docs/extensions/handins/BETTERWATER.md`). Off under `/profile:faithful`.

**Fixes**
- `fix.stranded_wade` (on): a citizen stuck on land cut off from the town (he crossed a stream where it
  touches a mountain or shore, or a builder was left on a finished dock or bridge piece) no longer stands
  there "going somewhere to get warm" until he starves. Once a walk of his toward home was refused because
  the place is cut off, he wades or climbs back: to his home, or to the house he just lost (the game takes
  a house from a citizen who cannot reach it), else to the town hall or a storage building, as retail
  already lets other walks do (`docs/retail_bugs.md` section 19). Off under `/profile:faithful`.
- `fix.mods_persist` (on): a change in the Mods window is saved to `settings.cfg` as soon as it is
  confirmed, so a crash no longer brings back the old mod list. Mods are changed from the main menu only:
  the pause menu's Mods button is greyed out, since retail dropped an in-game change at the next menu
  anyway (`docs/retail_bugs.md` section 23). Off under `/profile:faithful`.
- `fix.nomad_arrival` (on): nomads who cannot walk to the town hall (cut off by a river, for example) no
  longer count as arrived where they are stuck. Retail let such a group be allowed in, and the new citizens
  appeared where they stood and starved there. With the fix a nomad must reach the hall; it tries once more,
  then leaves (`docs/retail_bugs.md` section 22). Off under `/profile:faithful`.
- `fix.input_fetch_room` (on): a brewer, smith or other production worker no longer walks back and forth
  between a barn and a nearly full building. Retail sends the worker for a full load of an input whatever
  room the building has left, the building refuses a load it cannot take whole, and the worker carries it
  back to storage and comes for it again; with the fix the worker fetches only what fits
  (`docs/retail_bugs.md` section 25). Off under `/profile:faithful`.
- ConsumeProduce workers (brewers, smiths, tailors...) no longer set off for an input while already
  carrying something or with no room to carry it; as in retail, they re-plan instead.
- `fix.autotrade_free_goods` (on): a trading post set to trade automatically no longer hands the merchant
  whole stacks of goods he does not buy. Retail pays for an automatic purchase by selling stored goods one
  unit at a time until the price is covered, and a good the merchant values at nothing never covers it, so
  the whole stack went for nothing; with the fix such goods are not used to pay (`docs/retail_bugs.md`
  section 24). Off under `/profile:faithful`.
- `fix.firefighter_wells` (on): the Firefighter achievement ("Build 20 wells in a single town") is
  awarded at 20 wells. The game's data asks for 40, a long-known mismatch with the description
  (`docs/retail_bugs.md` section 20). Off under `/profile:faithful`.
- `fix.crop_planting_exit` (on): a crop field no longer stays in "planting" forever. Retail never finishes
  planting a field with a row the farmers cannot reach (a steep or fenced-off edge), so the field is never
  tended or harvested and its crop rots; nor does it move on when every crop was lost before planting
  finished. With the fix, the unreachable slots are left bare and the field goes on to tending and harvest,
  and a field with nothing left growing is cleared for replanting (`docs/retail_bugs.md` section 11). Off
  under `/profile:faithful`.
- `fix.unresolved_seeds` (on): selecting a crop field, orchard or pasture no longer crashes when the
  town's list of crops, trees and animals names one that no installed mod provides: a starting condition
  naming a missing resource (one mod's "Fig" where the package has "Fig Tree"), or a save made with a
  mod's crop, loaded with that mod off. Retail puts an empty entry in the list and the panel's menu reads
  it; the fix leaves the entry out (`docs/retail_bugs.md` section 21). Off under `/profile:faithful`.
- `fix.save_failure_notice` (on): a save that cannot be written (a read-only or OneDrive save folder,
  Windows Controlled Folder Access, an antivirus block, a full disk) shows a message naming the save
  folder, where the game used to fail without a word. The save is written to a temporary file and moved
  over the old one only when complete, so a failed save no longer leaves the previous save of that name
  broken (`docs/retail_bugs.md` section 27). Off under `/profile:faithful`.
- `fix.boat_reach_notice` (on): when a merchant's boat finds no water route to a trading post's dock (a
  pond, a lake, shallows between the dock and the river), the event log says so, at the post. The game
  used to send the boat past without a word, for ever, so the post never traded and nothing said why
  (`docs/retail_bugs.md` section 28). Where boats sail is unchanged. Off under `/profile:faithful`.
- Better trees (`ext.better_trees`): the light and shadow on growing trees' crowns no longer shimmer. The
  billboard self-shadow picked each leaf card's cut from a hash of its world position, which includes the
  tree's growth scale, and retail eases that scale a little every frame (also while paused with Space, and
  most after a spell at high speed), so each step re-rolled the cut. The hash now reads the card's model
  position and the tree's map position, which growth never changes (`receiver_rewrite.h` "THE HASH").
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
- Log size: diagnostic lines that printed on every game tick or every window update no longer reach
  `standalone_error.log` in released builds. A tester's 2-hour session had written a 1 GB log. The
  plague, tornado and citizen-vitals traces are now off unless `BANISHED_PLAGUE_TRACE=1`,
  `BANISHED_TORNADO_TRACE=1` or `BANISHED_VITALS=1` is set.
- Construction after a load: a building that was under construction when the game was saved now
  finishes properly. Before, it kept its construction storage, so a house finished that way took no food
  or firewood. Buildings already finished that way in an existing save are not repaired
  (`docs/known_port_issues.md` row 1345).
- Merchant boats: a boat leaving a trading post no longer sometimes stops on the river and stays there.
  Its departure now ends when it reaches its waypoint, as retail's does (`BoatComponent::State3Tick`,
  `docs/known_port_issues.md` row 1344).
- `fix.cancel_refund` (on): a building site cancelled after its materials arrived now leaves all of them in
  piles on and around its ground. Retail lays piles only on the few free cells of the site's own ground and
  drops the rest, a third of a town hall's delivery in one test (`docs/retail_bugs.md` section 26). A site
  that builders had already started still gives back half, as in retail. Off under `/profile:faithful`.
- `fix.commute_cost` (on): the job allocator no longer treats the walk from a newly built home to a
  workplace (or from a home to a newly built workplace) as no walk at all the first time it asks for it, so
  such a citizen is no longer sent to a far workplace for that reason. Retail answers 0 for a commute it has
  not measured yet (`docs/retail_bugs.md` section 6). Off under `/profile:faithful`.
- `fix.path_query_first` (on): in a large town the game no longer freezes for 15 to 20 seconds when a
  merchant boat sets off, or while a road is drawn. Retail queues that path query behind every citizen
  route search already waiting; with the fix it is answered first, and those citizens get their routes a
  few ticks later (`docs/retail_bugs.md` section 18). Off under `/profile:faithful`.
- A crash in large towns at high game speed (`ext.render_culling`): a citizen or building removed while
  offscreen no longer leaves entries behind that the next frame without a game tick writes through. Seen
  after 7.8 hours in a 1588-citizen town (`docs/known_port_issues.md` row 1346).
- Render thread (`ext.render_pipe`): no more long lines flickering across the screen, and no more crash of
  the render thread, most often seen while placing a building with a radius circle (a well, a market)
  and moving the camera. The frame copy the render thread draws from wrote two parts of itself over each
  other (`docs/known_port_issues.md` row 1347).

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
