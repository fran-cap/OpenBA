---
name: camera-pin
description: Frame a specific building or map location with BANISHED_CAMERA_PIN (world-coordinate math from startTown log lines), take a presentable screenshot (BANISHED_SCREENSHOT_AT, the showroom harness, tools/capture_window.ps1, season and extensions, side-by-side and before/after composites, where the image goes), and verify rendering changes with tools/trace_oracle instead of pixel diffs. Use for any visual or rendering verification, and for "get me a screenshot of ...", "can I get a nice fall screenshot of the perf map", "a group image of all the fruit trees in bloom", "a side by side", "attach an image of the before/after", "get an image of the extension", "a screenshot or two for the release notes".
---

# Camera pinning and visual verification

## Pixel diffs between runs are meaningless
Pin + seed do NOT give a reproducible frame. Measured: five runs at identical seed, pin
and command line split into two clusters ~98% apart, independent of the flag under test
(two *armed* runs differed by 97.98%; an armed and an unarmed run by 0.79%). Details:
`docs/history/runtime_port/WAVE23_CLOSED.md` §3. Screenshots are for *looking at*, not *diffing*.

**To compare rendering between runs use `tools/trace_oracle/`**: it compares the
draw-call / pipeline-state command stream, so it needs no frame determinism. Read the
measured noise-floor table in its README before quoting any of its six screens — only
three are verdict-bearing.

## Pinning
`BANISHED_CAMERA_PIN=x,y[,dist]` (`src/game/Game/camera_input.cpp`) pins the camera focus
to a world position every frame, overriding pan/follow. Without it the autostart camera
roams and lands differently every run. `BANISHED_CAMERA_PIN=town[,dist]` pins the town.

- **World coords**: `world = buildCell − mapDim/2` (small map: cell − 128). For a placed
  structure take the tile from its `[startTown::…] tile=(x,y) fp=WxH` log line and pin the
  footprint center: `x + W/2 − 128, y + H/2 − 128`.
- The 512 terrain grid has a 128-cell non-playable border; build cells map 1:1 onto its
  center. There is NO 2-cells-per-tile scaling.
- `dist` defaults to 40; ~50 frames a 21x17 building with context.
- Flags are read once at boot: relaunch to move the pin. Placement is deterministic per
  seed+flags, so a wrong guess costs one probe run — read the tile from the log, relaunch.

Example (demo quarry):
```
BANISHED_SEED=12345 BANISHED_BUILD_DEMO_QUARRY=1 BANISHED_CAMERA_PIN=9.5,-0.5,50
```

## Taking a presentable screenshot
For a picture the user will show (a PR comment, release notes, the README), not for a verdict.

**Extensions on.** `tools/build.sh run` and `tools/stress_run.sh` force `/profile:faithful`,
which turns every extension off: a first fall shot came out in retail's even yellow instead of
the multicolour trees. Run the stress town with `STRESS_PROFILE=settings` (any name but
`faithful` / `all` reads the rows from settings.cfg), or launch the exe by hand. Check the
`[Ext]` lines in `standalone_error.log` for the rows that matter before looking at the image.

**Pick the moment and the place.**
- Scene only, no UI: `BANISHED_SCREENSHOT_AT=<frame>` raises retail's screenshot request at that
  game frame; `<frame>,ui` takes the with-interface shot. It renders in the engine, so it needs no
  focus and no visible window. The file is `<user dir>/Screenshots/Screenshot<N>.bmp`
  (`Documents/Banished`, or the functional dir's `_userdata` when `BANISHED_USER_DIR` is set).
- Season: there is no season flag for a normal game. In the stress town the calendar follows the
  frame count (the multicolour autumn shot was frame 13100 at the default 10x speed, with
  `--frames` a few hundred past it; one probe run per guess, and say which frame was used). Weather follows the seed, so try a nearby frame if
  it is raining.
- A tree or building at a chosen month: the showroom harness (`tools/showroom/README.md`):
  `BANISHED_SHOWROOM=<list>` places the listed templates or trees, sets the calendar with
  `month` lines, pins and screenshots each one into `<game dir>/showroom/`. Fruit species
  blossom at different months, so "all in bloom" is a grid of each at its own peak.
- Frame it with the pin (above): `town,<dist>` for the whole town, `x,y,dist[,yaw]` for one spot.
- Use the functional game dir (`tools/game_dirs.sh env <lane> functional`): screenshots are
  runs nobody quotes numbers from.

**UI shots** (a dialog, a panel, a hover) need the window itself:
`powershell -ExecutionPolicy Bypass -File tools/capture_window.ps1 -Out <scratchpad>/shot.png`
captures the screen region of the OpenBA window, so it brings that window to the front first:
it takes the focus from the user. Say so before a series of captures, never open other visible
windows (Notepad, Explorer, an image viewer) to help, and never minimize the game (it may stop
presenting). `BANISHED_PANEL_HOLD=<ComponentTypeName>` opens one building's panel and leaves it
up for the capture (skill `menu-driver`).

**Convert and compose** in the scratchpad: `python tools/bmp2png.py <bmp> <png> 1` (last
argument = pixel step), or PIL. A side-by-side is two shots of the same pin at the same size,
cropped and pasted onto one canvas with a short label each; a split down the middle is the left
half of one and the right half of the other. Say when the two halves come from different runs:
the world differs run to run, so a composite shows a difference in kind, never a pixel diff.
Read the result back (Read the PNG) before handing it over.

**Where it goes.** The scratchpad, with its full path in the reply. Nothing is committed unless
it is a docs image the tree keeps. For a PR or issue comment, an image cannot be attached through
`gh`: a secret gist works (`gh gist create` a README, `git clone` the gist, add the image, push,
link `https://gist.githubusercontent.com/<owner>/<id>/raw/<file>`); that publishes the image, so
the user agrees first. Never put retail assets themselves (textures, icons cut from the `.pkg`)
in the tree as images (AGENTS.md rule 1).

**Offer the user the camera.** A showroom or season hunt can take several runs; the user has said
they would rather take the shots themselves than wait. When the first probe misses, say what it
would take and offer to launch the game at the right place for them instead (skill `hand-test`).
