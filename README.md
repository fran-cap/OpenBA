<h1><img src="images/ba_icon.png" width="64" align="center" alt="OpenBA icon"> OpenBA: An open source reimplementation of Banished's Engine</h1>

OpenBA aims to rebuild Banished as an open source, readable, C++ codebase.    
This project is designed along the lines of OpenRCT2, DevilutionX etc where the aim is    
to have baseline parity with the game's 1.0.7 release, but allowing for under the hood changes that still match retail.  
The project is designed to drop into the game's directory and run, and uses no game assets or code.  
Optional extensions are supported (see below).  

Project goals: readable C++ with classes, decent comments, and no "function is byte identical" slop.  
The engine also supports basic multithreading, higher performance, and various rendering fixes.  
OpenBA delivers a sharp experience even on 4k displays, and aims to double retail's performance:
<p align="left">  
<img src="images/fpscomp.jpg">  
</p>  
OpenBA supports x64 Windows, x64 Linux, MacOS (ARM), and Android (tested on 12+) with a custom mobile interface:
<p align="left">  
<img src="images/androidui.jpg" width="800">
</p> 

---  

> **NOTE: You need your own copy of Banished.**  
> OpenBA has no game assets, shaders, sounds, models or .rsc resource files.  
> At a baseline it needs the retail 1.0.7 `WinData/*.pkg` packages.  
> Grab a zip from releases, extract it to your banished folder, then run OpenBA's executable.  
> The GOG and Steam versions should both work for data.  

## Extensions

Extensions can be enabled in the new **Extensions** button in the main menu.

Types:  
**Fixes**   Base game bugs: Issues in the base game that are logical or programming errors.  
**Class A** Basic functionality: Performance, graphics, and basic gameplay changes.  
**Class B** Advanced/Divergent functionality: Aggressive performance modifications, breaks from base game behavior.  
**Class C** New gameplay features: Terrain modification, tool priority assignment, detail brushes.
Fixes default **on**; non-gameplay changing class A extensions default **on**, class B/C extensions default **off**

Extensions include performance tweaks, graphical enhancements, and bug fixes for the base game.  
(i.e. Advanced graphics settings can be found in the **Enhanced** tab of Options).  
For a list of extensions see: [docs/extensions/LIST.md](docs/extensions/LIST.md).  
For extension design see: [docs/extensions/DESIGN.md](docs/extensions/DESIGN.md).  

## Versioning

The main menu's build line shows retail's version on the left and OpenBA's on the right, e.g.
retail: `1.0.7 17Se10` OpenBA: `OpenBA v1.0.0 2424a9093 2026-10-19`:

- **`1.0.7` and `17Se10`** are retail's. `1.0.7` is the game version OpenBA targets.    
   `17Se10` is retail's build stamp.    
- **`OpenBA vX.Y.Z`** is this project's release version: the `VERSION` in `CMakeLists.txt`  
  (`project(banished_reimpl VERSION X.Y.Z ...)`), compiled in as `OPENBA_VERSION`.     
  Releases are tagged `v<version>` in git (ie `v1.0.0`) and the zip on the releases page matches the tag.    
- **`2424a9093 2026-10-19`** is the git commit the exe was built from and the build date (UTC).  

A release bumps the CMake `VERSION`, adds its section to `CHANGELOG.md` and pushes the tag `v<version>`;   
the Release workflow (`.github/workflows/release.yml`) then builds the Windows zip, the Linux tarball and the   
macOS app zip, and publishes them under the "Releases" link with that section as the notes.  

## Building
Prerequisites on Windows:
the pinned WinLibs GCC 16.2 (`tools/toolchain/install_winlibs.sh`), MSYS2 for its shell, CMake 3.20+ and Ninja.
OpenBA also requires Python 3, and the game installed.

```bash
git clone --recurse-submodules https://github.com/fran-cap/OpenBA
cd OpenBA
export BANISHED_DIR="C:/GOG Games/Banished" # your install; this is the default
tools/build.sh unit # OPTIONAL: build + run the unit tests (needs the install for a few)
tools/build.sh run # build, deploy into BANISHED_DIR and launch a new game
```

`tools/build.sh` is what runs the show:  
(`build | unit | deploy | run | run-menu | stress | clean`, build type: `debug | og | perf | release`).  
The deploy step copies `OpenBA.exe` and required resources to the game's folder.  
More setup info, the Linux stuff, and the debugger setup are in [docs/reference/environment.md](docs/reference/environment.md).  

## Code Layout

| Path | Desc |
|---|---|
| `src/game/` | Game logic: components, dialogs, AI, tools |
| `src/engine/` | Engine runtime: objects, I/O, UI, audio, math |
| `src/platform/` | Platform seams: Windows, POSIX (Linux), macOS, Android, shared SDL code |
| `src/render/` | Renderers: DX11 (Windows), Vulkan (Linux, Android), Metal (macOS), shared code in `common/` |
| `src/ext/` | Fixes and extensions |
| `src/app/`, `src/diag/` | Launcher and the diagnostics / harness code |
| `tests/unit/` | Unit tests; `tools/` the build, gate, capture and analysis scripts |
| `docs/` | Design, status and reference; `/history/` has the full development history |
| `third_party/` | Vendored dependencies; the full list and licences are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |

## How it was made

The retail binaries were decompiled with Ghidra and rewritten function by function into  
C++, with binary-compatible class layouts so saves and the game's own data files load unchanged.  
The whole process is recorded under `docs/` if you want to peruse.  
Most of the code was written with AI agents (Claude mostly) under human direction and review.  

## Legal

OpenBA is an independent project with no affiliation to or endorsement by Shining Rock  
Software. Banished is their work and their trademark. This repository holds only OpenBA's own  
source code and documentation. There is no game content and no copies of the retail binaries.  
OpenBA is not designed for profit, the project is designed as an engineering exercise.  
If you are the rights holder and have a concern, please open an issue or reach out to me.  

**License:** OpenBA is free software under the **GNU General Public License v3.0**   
([LICENSE](LICENSE)). The vendored third-party code keeps its own GPL-compatible licence; every  
component, its licence and how it is linked are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).  
Portions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved.  

**Typeface:** the user-interface typeface is **FrancophilSans** by Manfred Klein (1932-2018), used  
under his "Simple Terms of Use": free for private and commercial use, with a donation to  
[Doctors Without Borders](https://www.msf.org/) asked for when a use makes a profit.   
The two `.ttf` files are kept unchanged under `tools/release/fonts/` and ship beside `OpenBA.exe`   
in the release package (with `FrancophilSans_README.txt`), are loaded privately by the exe when the face   
is not installed, and are not part of OpenBA's code or of its GPL-3.0 licence.  
