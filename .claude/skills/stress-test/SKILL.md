---
name: stress-test
description: Run and read the performance stress test (tools/build.sh stress, BANISHED_STRESS) and the profilers (BANISHED_SAMPLE sampling profiler, per-component Update profile), and measure GPU time (PresentMon GPUBusy, RenderDoc captures with tools/trace_oracle/gpu_timing.py and the renderdoc MCP, Nsight Graphics GPU Trace). Use for any perf measurement, perf regression check, A/B comparison or baseline update, and for "where is the GPU time going", "zoomed out with all fancy extensions on", "DX11 vs Vulkan", "is it GPU bound", "renderdoc capture", "nsight".
---

# Stress test / performance baseline

Full details: [perf/README.md](../../../perf/README.md). Read it before changing a baseline.

## Run
```bash
tools/build.sh stress                  # perf exe (-O2 -g, logs stripped, build/cmake-perf/)
STRESS_ARGS="--label x --speed 5 --citizens 600 --sets 3 --frames 6000" tools/build.sh stress
tools/build.sh stress debug            # same town on the -O0 exe: functional check only
bash <(tr -d '\r' < tools/linux_run.sh) stress <gamedir> <outdir> 3600   # the Linux exe (WSL2, lavapipe)
```
`BANISHED_STRESS=1` builds a medium map with 3 sets of every placeable building, 800
citizens in their own houses, 10x speed, every workplace staffed through the real seat
cascade, camera pinned on the town, FPS cap off, and a clean self-quit after 6000 sim
frames (60k ticks at 10x).

## Output: `perf/results/<stamp>_<label>/`
- `standalone_stress.csv` — one row per 600 frames: update/draw ms, SIMPROF phase split,
  population/employment/entities, memory
- the per-component-type Update profile
- `report.txt` from `tools/stress_report.py`, auto-diffed against `perf/baselines/current.csv`

**Headline metric: `sim ms / iteration`** (speed- and GPU-independent).

**Pathfinding** (PFBENCH): every row also carries `pf_*` worker columns (requests, search
us p50/p95/max, expanded, wait us, queue max, fails, worker busy %, path length, the
reserved `pf_shadow_*`) and the main-thread `pf_sync_wait_ms`, `ph_pathdistance_ms`,
`ph_fillzones_ms`; the report prints them under PATHFINDING (`n/a` against a baseline that
predates them). Path-heavy town: `STRESS_ARGS="--spread"` (= `BANISHED_STRESS_SETS=-3`).
Columns: perf/README.md "Pathfinding columns"; A/B recipe: `docs/extensions/handins/PFBENCH.md`.

**Hitches** (HITCH): every Draw-to-Draw period more than `BANISHED_STRESS_HITCH_MS` (default
7.14 ms = one 140 fps frame) over the rolling median of its class is a row of
`standalone_stress_hitch.csv`; `tools/hitch_report.py <results dir>` (run automatically, in
`report.txt` under `== hitches`) attributes each to the part that grew (phase / draw / outside /
harness) and, for a `BANISHED_SAMPLE=4000` run, lists the functions enriched inside the hitches.
Measure at 10x, 20x AND 50x: sim-periodic and frame-periodic causes differ. perf/README.md "Hitches".

## Profiling order
1. `BANISHED_SAMPLE=1000` — sampling profiler (`src/diag/win/sampler.cpp`), report with
   `tools/sample_report.py`. Use this first.
2. The per-component table second: it cannot see libm, runtime or walk time.
   `BANISHED_STRESS_COMPPROF=2` adds the `/tree` rows and the `/char` rows (characters by
   on/off-screen x walk/pathwait/timed/idle/other, with the D1/D2/E1 ceilings; how to read
   them: `docs/extensions/handins/OFFSCREEN.md` §3).

## GPU time
The stress CSV's draw columns are CPU submission time only; the engine has no GPU timer and emits
no debug markers. What has worked, in the order to reach for it:

**The scene.** Stress town at speed 1 so both arms sit at the same sim time:
`BANISHED_STRESS=1 BANISHED_STRESS_SPEED=1 BANISHED_STRESS_FRAMES=100000 BANISHED_CAMERA_PIN=town,130`
(130 = retail's max zoom). The extension rows come from settings.cfg (`ext.no_fog=1` for no fog,
each visual row at its max), so launch without `/profile:faithful`: back up settings.cfg and the
game-dir exe when the lock is granted and restore exactly those copies (an older backup restored
over a newer settings.cfg has lost the user's changes). Borderless fullscreen at a fixed size:
windowed runs came out 2560x1377 or 2528x1345 from run to run. Snow cover in this town rises in
the first seconds of game time, so compare frames taken at the same game time (about 45 s in),
not the same frame number.

1. **Is it GPU-bound, and by how much: PresentMon.** API-neutral, live, no capture overhead:
   `"C:/Program Files/Intel/PresentMon/PresentMonConsoleApplication/PresentMon-<ver>-x64.exe" --process_name OpenBA.exe --timed 20 --terminate_after_timed --no_console_stats --v2_metrics --output_file <csv>`
   after `boot_state.sh --wait` plus a settle; the median of `GPUBusy` is the number to quote
   (the clean DX11 / Vulkan A/B of the shadow-depth work used it). Sample
   `nvidia-smi --query-gpu=utilization.gpu,clocks.sm,pstate --format=csv -lms 500` beside it.
   The version in the exe name changes with reinstalls: `ls` the folder.
2. **Where it goes, per pass and per shader: a RenderDoc capture.**
   `tools/trace_oracle/capture_arm.sh <tag> --frame N --out <ABSOLUTE dir> --expect-log RE [--exe-args '...'] -- ENV=V ...`
   (or `renderdoccmd capture --opt-ref-all-resources` with `BANISHED_CAPTURE_INGAME=<frame>`; the
   first `.rdc` is the menu's own capture, the in-game one is the second). RenderDoc captured both
   the DX11 and the Vulkan perf exe this way. Then
   `RENDERDOC_MODULE_PATH="$(pwd -W)/tools/renderdoc_mcp/runtime" tools/renderdoc_mcp/.venv/Scripts/python.exe tools/trace_oracle/gpu_timing.py <rdc> <out.json>`
   (an absolute module path: the script hands it to `os.add_dll_directory`)
   prints GPU duration per pass and per (VS, PS) pair. Its numbers are **relative only**: the replay
   is serialised and every draw pays a few-µs floor, so scale the shares to the live PresentMon
   `GPUBusy`. The `mcp__renderdoc__*` tools (`open_capture`, `get_pass_timing`,
   `analyze_render_passes`, `analyze_overdraw`, `get_pipeline_state`, `get_cbuffer_contents`,
   `save_render_target`) answer what a pass draws and with which state; close the capture after.
   The game can render broken (terrain, roofs) under RenderDoc until its second capture; that is
   RenderDoc, not a finding, unless it reproduces without it.
3. **What limits a pass in hardware: Nsight Graphics GPU Trace, Vulkan only.** What did and did not
   work on this machine (RTX 5080):
   - Nsight **Systems** (`nsys`) has no per-pass or per-draw counters: not the tool.
   - `ngfx.exe` (Nsight Graphics CLI) crashed at about 2 s (`0xC0000409`, no output) until run with
     `NSIGHT_SUGGEST_GRAPHICS_CAPTURE=0` and `NSIGHT_REPORT_REPLAY_WINDOW_INTERFERENCE=0`, multi-pass
     metrics OFF (with auto-export on, multi-pass writes a trace nothing can open); the wrapper
     that worked was github.com/FrostyLeaves/nsight-graphics-analyzer's `gputrace-capture`
     (metric set "Top-Level Triage", `--start-after-ms 45000 --limit-to-frames 4`), kept in the
     scratchpad, not the repo.
   - **DX11: GPU Trace never attached** to our D3D11 device (port search, then "Failed to
     connect"). DX11 stays on RenderDoc timings.
   - **Counters**: "GPU Performance Counters unavailable" until Claude Code itself ran elevated.
     The Control Panel "allow all users" setting, a driver restart and a full reboot did not unlock
     them. Elevation is the user's step; ask, never work around it.
   - With no markers, Nsight exports whole-frame counters only; per-pass limiters came from
     differential runs (one ext row off per run, against the base). Nsight locks clocks to base, so
     its frame times run above live ones; ~5% run-to-run noise.
   - Stop after a failed attempt or two and report: each failed ngfx start can put a dialog in
     front of the user.

`tools/dx11_state_diff/` (per-draw pipeline-state equality between two DX11 captures) and the
trace oracle's screens answer "did the command stream change", not "what does it cost".

Before any GPU A/B: stop MSI Afterburner, then RTSS, then RTSSHooksLoader64 (the limiter caps
fps and survives killing RTSS alone) and check they did not come back after a reboot; kill the
game by PID only.

## Related flags
`BANISHED_MAP_SIZE=small|medium|large`, `BANISHED_CLIMATE=mild|fair|harsh`,
`BANISHED_CAMERA_PIN=town[,dist]`. All flags: `src/app/options.h`.

## Keep the measurement clean
Nothing else heavy may run during an A/B (no repo-wide searches, no second build). Trace
flags are a struct load via `Opt()`, so a per-call `if (Opt().someTrace)` in tick code is
cheap — but never add a `getenv`.
