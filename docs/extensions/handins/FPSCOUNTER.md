# FPSCOUNTER hand-in (FPS counter)

`ext.fps_counter`: the frame rate the game draws at, on an FPS page of the extension window. One class A row,
**off by default**, live, no state of its own (nothing is saved in the town, no sim effect).

## 1. What it does

With the row on, the toolbar's extension button (the Reports group, `src/ext/toolbar/`) opens the extension window,
and its FPS tab shows:

| Line | Meaning |
| --- | --- |
| Current | the frame rate over the last whole second |
| Average | every counted frame over every counted second (not a mean of the per-second rates) |
| Minimum / Maximum | the lowest and highest one-second rate since the count started |
| Measured for | how long the count covers (whole seconds of closed windows) |

A Reset button starts the count over. Before the first second has closed the four rates read `-`.

Frames are counted whether or not the window is open, from the moment the row comes on (the count is reset then).
A gap of more than two seconds between two frames (a load, a breakpoint, the process held off the CPU) drops the
window that was open instead of counting a sub-1-fps second, so loading a town does not pull the minimum to zero.
Only in-game frames are counted; the menu is not.

## 2. Where it hooks

- **The count.** `FrameLimiter::g_extGameFrame` (`src/app/frame_limiter.h`), a null-by-default function pointer
  called first thing in `FrameLimiter::TickInGame`, the one per-frame chokepoint both `GameUpdateLoop::Draw` and
  ext.render_pipe's main half pass. `Ext::FpsCounter::Sync` (boot, and every toggle through `SetAndRecord`) sets it
  while the row is on and clears it while it is off; under the faithful profile it stays null, so the faithful
  frame pays one null test. `frame_limiter.cpp` is our own code (no retail body), so no retail function changes.
- **The sums.** `src/ext/fps_counter/fps_stats.h`, standard C++, one-second windows over
  `Platform::PerfCounter` ticks. Main thread only (the count and the page both run there).
- **The page.** `src/ext/fps_counter/toolbar_page.cpp` + `src/ext/ui/Dialog/FpsCounterPage.rsc` (our own text
  `.rsc`, strings `FpsCounter*` in `ExtensionsStringTable.rsc`). It re-reads the sums each frame it shows (the
  host's `tick`) and sets a text's parameter only when its figure changed.

Why a page and not a button of its own: the toolbar gets ONE extension button and every in-game control is a page of
its window (`docs/extensions/DESIGN.md`, Surfaces, ruled 2026-10-20). Why off by default although class A: turning it
on adds that button to a toolbar that is otherwise retail's (the same call as `show_grid`).

## 3. Tests

`tests/unit/ext_fps_counter_test.cpp`: a steady rate gives every figure; minimum and maximum come from the windows and
the average from the frames; a long gap is dropped, not counted; Reset starts over; a clock running backwards is a gap.
`ext_rsc_text_parse_fps_counter_page`, the toolbar page list (7 pages), the registry's default-off rule and the
row icon (`tools/ext_icons/fpscounter.py`, baked into `ext_sprites_data.h`) are covered by the existing suites.

## 4. Not done

- Not yet seen in a running game (the page's layout, the figures updating, Reset).
- No stress A/B: the cost with the row on is one indirect call and one `QueryPerformanceCounter` per drawn frame.
