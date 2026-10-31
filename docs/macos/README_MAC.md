# OpenBA on macOS (Apple Silicon)

OpenBA is a free re-implementation of the Banished engine. It contains **no game data**:
you need your own copy of Banished (bought on Steam). OpenBA only *reads* from it.

## Requirements
- A Mac with Apple Silicon (M1 or newer), macOS 14 or newer (see deployment limitations below).
- Your own Banished install on this Mac (see "Where is my game?" below).
- To build it yourself: full Xcode, the Metal toolchain, CMake, Ninja, Meson, Python 3 and Git.

## Build from source
Prerequisites (Apple Silicon Mac): full Xcode selected with `xcode-select`, its Metal toolchain
(`xcodebuild -downloadComponent MetalToolchain` if needed), and
`brew install cmake ninja meson python git`. A fresh clone also needs the SDL and FreeType
submodules: `git submodule update --init third_party/SDL third_party/freetype`. Then, in the OpenBA
source folder:

    tools/macos/make_app.sh

Every run checks dependencies through `tools/macos/setup_deps.sh`. On the first run it
clones DXMT (pinned commit, into `third_party/dxmt_src`; override with `DXMT_SRC=...`),
builds LLVM 15.0.7 and the shader converter. Completed dependency steps are skipped;
build directories with missing or incompatible deployment-target stamps are rebuilt automatically.
`setup_deps.sh --dry-run` shows the planned steps; `--jobs N` sets dependency build parallelism.

The script builds the engine incrementally through `tools/build.sh build release` into
`build/mac-release` and creates the ad-hoc signed `dist/OpenBA.app` (release flavour, -O3).
`BUILD_JOBS` controls engine build parallelism, with the default from `tools/build.sh`.
`make_app.sh OUT.app` selects an alternate app output;
`FLAVOUR=perf tools/macos/make_app.sh` selects `build/mac-perf` with symbols.

The game is never shipped or copied: OpenBA finds your own Banished install at run time (see "Where is my game?").

Licence note: the shader converter is DXMT (LGPL-2.1), a separate `libairconv.dylib` loaded dynamically; you can
replace it by setting `BANISHED_AIRCONV=/path/to/libairconv.dylib`. Source: https://github.com/3Shain/dxmt,
commit `50102f994b406f58f56d5d09f2ad20f9b90390a2`. See the root `THIRD_PARTY_NOTICES.md`; full licence texts ship inside
`Contents/Resources/licenses/`. The engine also statically links LGPL wmadec;
the root notices describe rebuilding and relinking it. ImGui is absent from the Mac app.

## Package for distribution
To produce the distributable, run:

    tools/macos/make_release_zip.sh

This creates `dist/OpenBA-mac-arm64.zip` using `ditto`. It refuses `.pkg`, `.pkm`, `WinData` and `.ttf` entries,
including symlink targets. Unzip and drag the app to Applications.
For a packaging dry run without changing dist, use
`tools/macos/make_release_zip.sh dist/OpenBA.app "$(mktemp -d)"`.

The engine, launcher, LLVM and converter builds target macOS 14.0. The converter's
shader generator targets Metal 3.1 (`air64-apple-macos14.0`). Inspect recorded minimums
with `xcrun vtool -show-build`. Packaging rejects incompatible recorded minimums.

## Run it
- Double-click `OpenBA.app`, or run `tools/macos/run_openba.sh`.
- First launch: if OpenBA cannot find Banished by itself, a window asks you to pick the
  folder. Choose the `Banished` folder (the one that holds `WinData`) or `WinData` itself.
  Only the folder-picker choice is remembered; command-line and environment
  overrides are temporary. An invalid `--game-dir` or `BANISHED_DIR` fails and names the path.
- Options for `run_openba.sh`: `--game-dir <folder>`, `--background` (small window that does
  not grab focus; for testing).

## Where is my game? (how OpenBA looks, in this order)
1. `--game-dir <folder>` on the command line.
2. The `BANISHED_DIR` environment variable (a Banished or WinData folder).
3. The folder you picked before (`~/Library/Application Support/OpenBA/gamedir.txt`).
4. Common places, checked automatically:
   - `~/Library/Application Support/Steam/steamapps/common/Banished`
   - Windows-style prefixes under `drive_c/Program Files (x86)/Steam/steamapps/common/Banished`
     (also `Program Files`, and `GOG Games/Banished`) for Porting Kit (Desktop, Applications
     and Application Support), Whisky, CrossOver, Kegworks/Wineskin and `~/.wine`.
5. The folder picker.

A folder counts only if it contains `WinData/data0.pkg`. Nothing is copied or modified in it.
To forget the choice, delete `~/Library/Application Support/OpenBA/gamedir.txt`.

## Where do saves and logs go?
- Saves: `~/Documents/Banished/Save`. Settings (`settings.cfg`): `~/Documents/Banished`.
  Same folders as the original game, so existing saves are found.
- Logs: `~/Library/Application Support/OpenBA/Run/standalone_*.log` (send `standalone_error.log`
  with any bug report). That folder also holds two shortcuts: `WinData` (your game) and
  `OpenBA` (the engine inside the app). `standalone_boot_state.txt` is in this
  Run folder too; `tools/boot_state.sh` uses it by default on Darwin.
  For a launcher `--run-dir DIR` test, poll with `tools/boot_state.sh --run-dir DIR`;
  `BANISHED_STATE_DIR` can also name a directory containing the state file.
- Compiled shader cache: `Library/Caches/OpenBA/shaders-<fingerprint>` under the profile
  (`~/Library/Caches/OpenBA/...` normally; a `--run-dir` test run redirects it under that
  folder). It is rebuilt automatically if it disappears (first start after that is slower).
The app itself is never written to, so its signature stays valid.

## Quit
Cmd+Q or OpenBA menu, Quit OpenBA.

## Testing without touching your home (developers)
`OpenBA --run-dir <folder>` keeps the Run folder, remembered game folder, saves/settings
(via USERPROFILE) and shader cache under `<folder>`. `BANISHED_BACKGROUND=1` gives a small window
that does not take focus.

## How to report a problem
Send `standalone_error.log` (and `standalone_crash.log` if it is not empty) from the Run folder,
the macOS version and Mac model, and what you did just before.

## Gatekeeper and distribution (honest status)
- The app is **ad-hoc signed** (`codesign --sign -`), not notarised. `spctl --assess` says "rejected" (expected for any non-notarised app); run from the Mac that
  built it, it still opens normally (no quarantine flag).
- If you move it to another Mac by download, AirDrop or chat, macOS marks it as quarantined
  and says it cannot be verified. On macOS 14, right-click the app and choose Open.
  On macOS 15 or later, try opening it once, then choose System Settings >
  Privacy & Security > Open Anyway. See https://support.apple.com/102445.
  The terminal route is `xattr -dr com.apple.quarantine OpenBA.app`.
  Do this only for builds you trust.
- Giving it to other people without that warning needs an Apple Developer ID (paid
  developer account), signing with it and notarising (`notarytool`). Not done, not possible
  without that account. With Developer ID and hardened runtime, include
  `com.apple.security.cs.disable-library-validation` so library validation permits
  a user-replaced LGPL converter dylib. The current ad-hoc build does not enable
  hardened runtime. Sign nested binaries first, then the app; do not use `--deep`.

## Known gaps
- The Metal renderer implements ambient occlusion, MSAA and the Better Trees rewrite
  (`docs/reference/macos_metal_passes.md`), but visual equivalence with the Windows game
  has not been verified: depth uses D32F+S8 instead of retail D24S8, and whether the
  Better Trees mode 2 haze returns with the shared values has not been compared.
  Crisp text on Retina needs the separately obtained FrancophilSans.ttf. Resizing the
  window and changing video modes are not fully handled. See `docs/known_port_issues.md`
  for tracked divergences.
- Gameplay, saves and long sessions have had little testing on macOS.
- The app icon is upscaled (Lanczos) from a 48x48 picture, so it is still soft.
- Apple Silicon only (the shader converter is arm64).
- The launcher passes unchanged any extra arguments to the engine (for example `-nomods`).
