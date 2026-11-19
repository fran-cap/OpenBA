#!/usr/bin/env python3
"""tools/stress_report.py -- summarise a BANISHED_STRESS metrics CSV, optionally against a baseline.

    python tools/stress_report.py standalone_stress.csv [--baseline perf/baselines/current.csv]
                                  [--compprof standalone_stress_compprof.txt] [--warmup N] [--json]

The CSV is written by src/diag/stress_harness.cpp: one row per BANISHED_STRESS_INTERVAL
frames, '#' meta lines first. Every timing column is a per-frame mean over that window
(ms), except *_p95 (95th percentile of the per-frame samples in the window) and fps
(rendered frames per wall second).

What the summary means:
  update ms      GameUpdateLoop::Update wall time per sim frame (all speed iterations).
  per-iter ms    update ms / sim iterations -- the sim cost of ONE 60 Hz tick; the number
                 to compare across speeds and across builds.
  draw ms        GameUpdateLoop::Draw CPU time (command submission; the GPU is not in it).
  draw split     ext.render_pipe (RENDERPIPE0.md): with the extension on, draw ms is split into
                 main ms (the Draw prelude + Renderer::Draw pass 1, on the main thread) and
                 packet ms (pass 2 + the post-scene draws); emit ms is the whole emitted packet
                 (BeginCommands .. Present). main share = main / (main + packet), the `a` of
                 THREADRESEARCH section 1.8. Printed only when the CSV has the columns and a
                 steady row is non-zero.
                 RENDERPIPE-1 (RENDERPIPE1.md): wait us = the main thread's hand-off / fence
                 waits on the render thread per draw frame; latency us = a packet's hand-off
                 to its Present done (mean). Both 0 in the sequential split.
  frame ms / fps render-frame period at the Draw entry -- what a player would see.
  phases         the SIMPROF split of update ms (PairCache, Animation, EntityList,
                 Weather, CitizenList, PostSim=UI/camera/audio).
  world          entities / citizens / employed (work248) / housed (home238) / busy AI,
                 memory working set + private bytes. Growth over the run is a leak tell.
  PATHFINDING    the pf_* columns (PFBENCH, docs/extensions/handins/PFBENCH.md): worker
                 search time, expansions, queue wait, queue depth, fails, worker busy %,
                 path length, end-to-end latency, the reserved shadow tallies, and the
                 main-thread PathDistance / FillZones / sync-spin ms. Window percentiles
                 are averaged over the steady windows. "n/a" = the CSV (or the baseline)
                 predates these columns.

  WORKLOAD       (with --baseline) the two runs' population, entities and sim digest
                 (sim_hash) matched by frame. Two runs of one seed are not one town unless both
                 ran with the repeatable town and the digest agrees; when the population differs
                 by more than 2% at a compared row, the timing deltas are labelled workload and
                 no REGRESSION / PLAN verdict is given (perf/README.md "Repeatable town").

The header's `cpu` meta line (block CPULIMIT, docs/extensions/handins/CPULIMIT.md) is the
leg's process affinity mask and the logical / physical cores inside it, with how settings.cfg's
`sys.cpu_limit` was applied (applied / clamped / none): every A/B leg proves its mask there.

Steady state = every row after the first --warmup rows (default 2: the town is still
settling and the staffing pass is converting laborers). Comparisons use steady state.
Exit code 0 always -- this is a reporter, not a gate; grep the 'REGRESSION' lines if you
want one.
"""
import argparse
import json
import math
import os
import sys


def load(path):
    meta, rows = [], []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        header = None
        for line in f:
            line = line.rstrip("\r\n")
            if not line:
                continue
            if line.startswith("#"):
                meta.append(line[1:].strip())
                continue
            if header is None:
                header = line.split(",")
                continue
            vals = line.split(",")
            if len(vals) != len(header):
                continue
            row = {}
            for k, v in zip(header, vals):
                if k.startswith("sim_hash"):   # hex digests stay strings ("1e40" would parse as a float)
                    row[k] = v
                    continue
                try:
                    row[k] = float(v)
                except ValueError:
                    row[k] = v
            rows.append(row)
    return meta, rows


def mean(xs):
    xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return sum(xs) / len(xs) if xs else float("nan")


def pct(xs, p):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return float("nan")
    k = int(round((len(xs) - 1) * p))
    return xs[k]


def summarise(rows, warmup):
    ss = rows[warmup:] if len(rows) > warmup else rows
    if not ss:
        return {}
    g = lambda k: [r.get(k, float("nan")) for r in ss]
    s = {
        "rows": len(rows),
        "steady_rows": len(ss),
        "wall_s": rows[-1]["wall_s"] if rows else 0,
        "frames": rows[-1]["frame"] if rows else 0,
        "speed": ss[-1].get("speed", 0),
        "fps_mean": mean(g("fps")),
        "fps_min": min(g("fps")),
        "frame_ms_mean": mean(g("frame_ms_mean")),
        "frame_ms_p95": pct(g("frame_ms_p95"), 0.95),
        "update_ms_mean": mean(g("update_ms_mean")),
        "update_ms_p95": pct(g("update_ms_p95"), 0.95),
        "update_ms_max_window": max(g("update_ms_mean")),
        "per_iter_ms": mean(g("update_ms_per_iter")),
        "draw_ms_mean": mean(g("draw_ms_mean")),
        # RENDERPIPE: absent in older CSVs (NaN); 0 in a window with no pipelined frame.
        "draw_main_ms": mean([v for v in g("draw_main_ms") if v == v and v > 0]),
        "draw_packet_ms": mean([v for v in g("draw_packet_ms") if v == v and v > 0]),
        "draw_emit_ms": mean([v for v in g("draw_emit_ms") if v == v and v > 0]),
        # RENDERPIPE-1: absent in older CSVs (NaN).
        "pipe_wait_us": mean([v for v in g("pipe_wait_us") if v == v]),
        "pipe_latency_us": mean([v for v in g("pipe_latency_us") if v == v and v > 0]),
        "ph_paircache": mean(g("ph_paircache_ms")),
        "ph_animation": mean(g("ph_animation_ms")),
        "ph_entitylist": mean(g("ph_entitylist_ms")),
        "ph_weather": mean(g("ph_weather_ms")),
        "ph_citizenlist": mean(g("ph_citizenlist_ms")),
        "ph_postsim": mean(g("ph_postsim_ms")),
        "entities_first": rows[0]["entities"],
        "entities_last": rows[-1]["entities"],
        "citizens_first": rows[0]["citizens"],
        "citizens_last": rows[-1]["citizens"],
        "work248_last": rows[-1]["work248"],
        "home238_last": rows[-1]["home238"],
        "busy_last": rows[-1]["busy"],
        "hunger_last": rows[-1]["hunger_mean"],
        "ws_mb_first": rows[0]["ws_mb"],
        "ws_mb_last": rows[-1]["ws_mb"],
        "private_mb_first": rows[0]["private_mb"],
        "private_mb_last": rows[-1]["private_mb"],
    }
    # WORKLOAD (perf/README.md "Repeatable town"): the sim cost follows the population, so a
    # comparison of two towns that differ is read per 1000 citizens beside the raw number.
    s["citizens_mean"] = mean(g("citizens"))
    s["per_iter_per_kcit"] = mean([r["update_ms_per_iter"] / r["citizens"] * 1000.0
                                   for r in ss if r.get("citizens", 0) > 0])
    dt_min = (rows[-1]["wall_s"] - rows[0]["wall_s"]) / 60.0 if len(rows) > 1 else 0
    s["private_mb_per_min"] = (s["private_mb_last"] - s["private_mb_first"]) / dt_min if dt_min > 0 else 0
    s.update(summarise_pf(ss))
    return s


# PFBENCH (docs/extensions/handins/PFBENCH.md): the pathfinding columns. A CSV written
# before they existed has none of them: every key below is
# then NaN and prints as "n/a"; nothing here may raise on a missing column.
NAN = float("nan")


def _col(ss, k):
    return [r[k] for r in ss if isinstance(r.get(k), float) and not math.isnan(r[k])]


def _mean_or_nan(ss, k):
    xs = _col(ss, k)
    return sum(xs) / len(xs) if xs else NAN


def _max_or_nan(ss, k):
    xs = _col(ss, k)
    return max(xs) if xs else NAN


def _sum_or_nan(ss, k):
    xs = _col(ss, k)
    return sum(xs) if xs else NAN


def summarise_pf(ss):
    """Steady-state pathfinding summary: window means of the per-window percentiles,
    the worst max, summed failures, shadow percentages weighted by each window's count."""
    p = {
        "pf_present": bool(_col(ss, "pf_requests")),
        "pf_requests": _mean_or_nan(ss, "pf_requests"),
        "pf_req_per_sim_s": _mean_or_nan(ss, "pf_req_per_sim_s"),
        "pf_search_us_p50": _mean_or_nan(ss, "pf_search_us_p50"),
        "pf_search_us_p95": _mean_or_nan(ss, "pf_search_us_p95"),
        "pf_search_us_max": _max_or_nan(ss, "pf_search_us_max"),
        "pf_expanded_mean": _mean_or_nan(ss, "pf_expanded_mean"),
        "pf_expanded_p95": _mean_or_nan(ss, "pf_expanded_p95"),
        "pf_wait_us_p50": _mean_or_nan(ss, "pf_wait_us_p50"),
        "pf_wait_us_p95": _mean_or_nan(ss, "pf_wait_us_p95"),
        "pf_queue_max": _max_or_nan(ss, "pf_queue_max"),
        "pf_fail": _sum_or_nan(ss, "pf_fail"),
        "pf_worker_busy_pct": _mean_or_nan(ss, "pf_worker_busy_pct"),
        "pf_len_mean": _mean_or_nan(ss, "pf_len_mean"),
        "pf_e2e_us_p50": _mean_or_nan(ss, "pf_e2e_us_p50"),
        "pf_e2e_us_p95": _mean_or_nan(ss, "pf_e2e_us_p95"),
        "pf_ready_us_p50": _mean_or_nan(ss, "pf_ready_us_p50"),
        "pf_ready_us_p95": _mean_or_nan(ss, "pf_ready_us_p95"),
        "pf_poll_us_p50": _mean_or_nan(ss, "pf_poll_us_p50"),
        "pf_poll_us_p95": _mean_or_nan(ss, "pf_poll_us_p95"),
        "pf_frames0_pct": _mean_or_nan(ss, "pf_frames0_pct"),
        "pf_frames1_pct": _mean_or_nan(ss, "pf_frames1_pct"),
        "pf_frames2_pct": _mean_or_nan(ss, "pf_frames2_pct"),
        "pf_frames3p_pct": _mean_or_nan(ss, "pf_frames3p_pct"),
        "pf_shadow_n": _sum_or_nan(ss, "pf_shadow_n"),
        "pf_shadow_worse": _sum_or_nan(ss, "pf_shadow_worse"),
        "pf_shadow_speedup": _mean_or_nan([r for r in ss if r.get("pf_shadow_n", 0) > 0], "pf_shadow_speedup"),
        "pf_sync_wait_ms": _mean_or_nan(ss, "pf_sync_wait_ms"),
        "ph_pathdistance": _mean_or_nan(ss, "ph_pathdistance_ms"),
        "ph_fillzones": _mean_or_nan(ss, "ph_fillzones_ms"),
    }
    for key in ("pf_shadow_equal_cost", "pf_shadow_same_cells"):
        num = den = 0.0
        for r in ss:
            n, v = r.get("pf_shadow_n"), r.get(key)
            if isinstance(n, float) and isinstance(v, float) and n > 0:
                num += n * v
                den += n
        p[key] = num / den if den > 0 else NAN
    return p


# Pathfinding metrics compared against the baseline: (key, label, higher_is_better, threshold %).
# The plan's decision metrics are search p95, wait p95 and worker busy %; the rest are context.
PF_COMPARE = [
    ("pf_req_per_sim_s", "requests / sim s", None, 0),
    ("pf_search_us_p95", "search us p95", False, 15),
    ("pf_wait_us_p95", "wait us p95", False, 15),
    ("pf_worker_busy_pct", "worker busy %", False, 15),
    ("pf_expanded_mean", "expanded mean", False, 15),
    ("pf_e2e_us_p95", "end-to-end us p95", False, 15),
    ("ph_pathdistance", "PathDistance ms/frame", False, 15),
    ("ph_fillzones", "FillZones ms/frame", False, 15),
    ("pf_sync_wait_ms", "sync spin ms/frame", False, 15),
]


def print_pathfinding(s, b=None):
    print("PATHFINDING", end="")
    if not s.get("pf_present"):
        print("  n/a (this CSV has no pf_* columns: written before PFBENCH)")
    else:
        print(f"  requests {fmt(s['pf_requests'])}/window ({fmt(s['pf_req_per_sim_s'])} per sim s)  "
              f"worker busy {fmt(s['pf_worker_busy_pct'])}%")
        print(f"  search us   p50 {fmt(s['pf_search_us_p50'])}  p95 {fmt(s['pf_search_us_p95'])}  "
              f"max {fmt(s['pf_search_us_max'])}      expanded mean {fmt(s['pf_expanded_mean'])} "
              f"p95 {fmt(s['pf_expanded_p95'])}")
        print(f"  wait us     p50 {fmt(s['pf_wait_us_p50'])}  p95 {fmt(s['pf_wait_us_p95'])}  "
              f"queue max {fmt(s['pf_queue_max'])}   failed {fmt(s['pf_fail'])}   "
              f"path len mean {fmt(s['pf_len_mean'])}")
        print(f"  e2e us      p50 {fmt(s['pf_e2e_us_p50'])}  p95 {fmt(s['pf_e2e_us_p95'])}  "
              f"(enqueue -> TakePath, includes the walker's poll)")
        if not math.isnan(s.get("pf_poll_us_p95", NAN)):
            print(f"  e2e split   enqueue->ready p50 {fmt(s['pf_ready_us_p50'])} p95 {fmt(s['pf_ready_us_p95'])}   "
                  f"ready->taken (poll) p50 {fmt(s['pf_poll_us_p50'])} p95 {fmt(s['pf_poll_us_p95'])}")
            print(f"  frames crossed  0: {fmt(s['pf_frames0_pct'])}%  1: {fmt(s['pf_frames1_pct'])}%  "
                  f"2: {fmt(s['pf_frames2_pct'])}%  3+: {fmt(s['pf_frames3p_pct'])}%")
        if s.get("pf_shadow_n", 0) and not math.isnan(s["pf_shadow_n"]) and s["pf_shadow_n"] > 0:
            print(f"  shadow      equal cost {fmt(s['pf_shadow_equal_cost'])}%  same cells "
                  f"{fmt(s['pf_shadow_same_cells'])}%  worse {fmt(s['pf_shadow_worse'])}  "
                  f"speedup {fmt(s['pf_shadow_speedup'])}x  ({fmt(s['pf_shadow_n'])} paired requests)")
        else:
            print("  shadow      not run (no extension called StressHarness_PathShadow)")
        print(f"  main        PathDistance {fmt(s['ph_pathdistance'])} ms/frame  FillZones "
              f"{fmt(s['ph_fillzones'])}  sync spin {fmt(s['pf_sync_wait_ms'])} ms")
    if b is None:
        return
    print("  vs baseline (n/a = the baseline CSV has no pf_* columns)")
    print("  {:<22} {:>10} {:>10} {:>8}".format("metric", "baseline", "now", "delta"))
    for key, label, hib, thr in PF_COMPARE:
        bv, nv = b.get(key, NAN), s.get(key, NAN)
        if isinstance(bv, float) and isinstance(nv, float) and bv and not math.isnan(bv) and not math.isnan(nv):
            d = (nv - bv) / abs(bv) * 100.0
            tag = ""
            if hib is not None:
                worse = (d < -thr) if hib else (d > thr)
                better = (d > thr) if hib else (d < -thr)
                tag = "  REGRESSION" if worse else ("  improved" if better else "")
            print("  {:<22} {:>10} {:>10} {:>+7.1f}%{}".format(label, fmt(bv), fmt(nv), d, tag))
        else:
            print("  {:<22} {:>10} {:>10} {:>8}".format(label, fmt(bv), fmt(nv), "n/a"))


# WORKLOAD (perf/README.md "Repeatable town"). Two runs of one exe with one seed are NOT one town
# by default: the entity update order follows heap addresses (EntityList's update tree is keyed by
# pointer, as in retail) and retail's path worker answers on wall-clock time, so the population
# drifts apart from the first sim frame and the sim cost follows it. A delta between two towns that
# differ is workload, not code or noise. This check matches the rows by frame and says so.
WORKLOAD_POP_TOL_PCT = 2.0


def paths_mode(meta):
    for m in meta:
        if m.startswith("paths "):
            return m.split()[1]
    return "async"   # a CSV from before the line: retail's worker, the only mode there was


def workload_check(rows, brows):
    """Match rows by frame: population, entities and the sim digest of both runs.
    Returns (table, first_hash_diff_frame, first_pop_diff_frame, hashed)."""
    by_frame = {int(r["frame"]): r for r in brows if isinstance(r.get("frame"), float)}
    table, first_hash, first_pop = [], None, None
    hashed = False
    for r in rows:
        f = int(r["frame"])
        b = by_frame.get(f)
        if b is None:
            continue
        ha, hb = r.get("sim_hash"), b.get("sim_hash")
        both = isinstance(ha, str) and isinstance(hb, str) and ha and hb
        hashed = hashed or both
        same_hash = (ha == hb) if both else None
        if both and not same_hash and first_hash is None:
            first_hash = f
        ca, cb = r.get("citizens", NAN), b.get("citizens", NAN)
        dpop = (ca - cb) / cb * 100.0 if cb else NAN
        if not math.isnan(dpop) and abs(dpop) > WORKLOAD_POP_TOL_PCT and first_pop is None:
            first_pop = f
        table.append((f, cb, ca, dpop, b.get("entities", NAN), r.get("entities", NAN), same_hash))
    return table, first_hash, first_pop, hashed


def print_workload(meta, bmeta, rows, brows, s, b):
    """The WORKLOAD block of a baseline comparison. Returns True when the populations differ by
    more than WORKLOAD_POP_TOL_PCT at some compared row: the timing verdicts are then withheld."""
    table, first_hash, first_pop, hashed = workload_check(rows, brows)
    ma, mb = paths_mode(meta), paths_mode(bmeta)
    print("WORKLOAD  (is it the same town? perf/README.md \"Repeatable town\")")
    print(f"  paths     baseline {mb}   now {ma}")
    print("  {:>6} {:>9} {:>9} {:>7} {:>9} {:>9}  {}".format(
        "frame", "cit base", "cit now", "delta", "ent base", "ent now", "sim_hash"))
    for f, cb, ca, dpop, eb, ea, same in table:
        h = "n/a" if same is None else ("same" if same else "DIFFERS")
        print("  {:>6} {:>9} {:>9} {:>+6.1f}% {:>9} {:>9}  {}".format(
            f, fmt(cb), fmt(ca), dpop if not math.isnan(dpop) else 0.0, fmt(eb), fmt(ea), h))
    if hashed and first_hash is None:
        print("  SAME TOWN: the sim digest agrees at every compared row; the deltas below are the code's")
        return False
    if hashed:
        print(f"  DIFFERENT TOWNS from the row at frame {first_hash} (the sim digest differs)")
    else:
        print("  the sim digest is missing from one CSV: whether the towns are the same is UNKNOWN")
    if first_pop is not None:
        print(f"  !! population differs by more than {WORKLOAD_POP_TOL_PCT:.0f}% from frame {first_pop}: "
              f"the steady-state deltas below are WORKLOAD, not code or noise")
    if ma != mb:
        print(f"  !! the path modes differ ({mb} vs {ma}): the main thread pays the searches in an inline run")
    print("  (no two runs repeat a town today: the entity update order follows heap addresses; "
          "--inline-paths removes only the worker race)")
    pb, pn = b.get("per_iter_per_kcit", NAN), s.get("per_iter_per_kcit", NAN)
    if not math.isnan(pb) and not math.isnan(pn) and pb:
        print(f"  per 1000 citizens: sim ms/iter {fmt(pb)} -> {fmt(pn)} ({(pn - pb) / pb * 100.0:+.1f}%) "
              f"at mean population {fmt(b.get('citizens_mean', NAN))} -> {fmt(s.get('citizens_mean', NAN))}"
              f"  (a first-order correction only: the cost is not linear in the population)")
    return first_pop is not None


def fmt(v, unit=""):
    if isinstance(v, float):
        if math.isnan(v):
            return "n/a"
        if abs(v) >= 100:
            return f"{v:.0f}{unit}"
        if abs(v) >= 10:
            return f"{v:.1f}{unit}"
        return f"{v:.2f}{unit}"
    return f"{v}{unit}"


def print_timeline(rows):
    print("  frame    t(s) spd    fps  frame  upd/f  /iter   draw  entities  cit  work home busy  hunger  wsMB")
    for r in rows:
        print("  {:>6} {:>6} {:>3} {:>6} {:>6} {:>6} {:>6} {:>6} {:>9} {:>4} {:>5} {:>4} {:>4} {:>7} {:>5}".format(
            int(r["frame"]), int(r["wall_s"]), int(r["speed"]), fmt(r["fps"]), fmt(r["frame_ms_mean"]),
            fmt(r["update_ms_mean"]), fmt(r["update_ms_per_iter"]), fmt(r["draw_ms_mean"]),
            int(r["entities"]), int(r["citizens"]), int(r["work248"]), int(r["home238"]), int(r["busy"]),
            fmt(r["hunger_mean"]), int(r["ws_mb"])))


def compprof_last_window(path, top=12):
    if not path or not os.path.exists(path):
        return None
    blocks, cur = [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "[COMPPROF] world#" in line:
                cur = [line.strip()]
                blocks.append(cur)
            elif cur is not None and "[COMPPROF]" in line:
                cur.append(line.strip())
    if not blocks:
        return None
    return blocks[-1][: top + 1]


# PLAN.md definition-of-done gate on the headline metric (section 5).
PLAN_GATE_PCT = 5.0

# Metrics compared against the baseline: (key, label, higher_is_better, regression threshold %)
COMPARE = [
    ("per_iter_ms", "sim ms / iteration", False, 10),
    ("update_ms_mean", "update ms / frame", False, 10),
    ("update_ms_p95", "update ms p95", False, 15),
    ("draw_ms_mean", "draw ms / frame", False, 10),
    ("frame_ms_mean", "frame ms", False, 10),
    ("fps_mean", "fps", True, 10),
    ("ph_entitylist", "  EntityList ms", False, 10),
    ("ph_citizenlist", "  CitizenList ms", False, 10),
    ("ph_animation", "  Animation ms", False, 10),
    ("ph_paircache", "  PairCache ms", False, 10),
    ("ph_postsim", "  PostSim ms", False, 10),
    ("private_mb_last", "private MB (end)", False, 15),
    ("private_mb_per_min", "private MB / min", False, 50),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--baseline")
    ap.add_argument("--compprof")
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--rows-to", type=int, default=0,
                    help="use only the rows up to this frame, in the run and the baseline (the window "
                         "before two towns drift apart; WORKLOAD says where that is)")
    a = ap.parse_args()

    meta, rows = load(a.csv)
    if a.rows_to > 0:
        rows = [r for r in rows if r.get("frame", 0) <= a.rows_to]
        meta.append(f"rows to frame {a.rows_to} only (--rows-to)")
    if not rows:
        print(f"{a.csv}: no data rows (meta: {meta})")
        return 0
    s = summarise(rows, a.warmup)

    if a.json:
        print(json.dumps({"meta": meta, "summary": s}, indent=2))
        return 0

    print(f"STRESS RUN  {a.csv}")
    for m in meta:
        print(f"  {m}")
    if not any(m.startswith("cpu ") for m in meta):
        print("  cpu (not recorded: the exe predates CPULIMIT -- the leg's CPU mask is unknown)")
    print(f"  {int(s['frames'])} sim frames in {s['wall_s']:.0f}s wall, {s['rows']} rows "
          f"({s['steady_rows']} steady after {a.warmup} warm-up)")
    print()
    print_timeline(rows)
    print()
    print("STEADY STATE")
    print(f"  sim       {fmt(s['per_iter_ms'])} ms per 60Hz iteration   "
          f"update {fmt(s['update_ms_mean'])} ms/frame (p95 {fmt(s['update_ms_p95'])}, worst window {fmt(s['update_ms_max_window'])}) "
          f"at speed {int(s['speed'])}")
    print(f"  phases    EntityList {fmt(s['ph_entitylist'])}  CitizenList {fmt(s['ph_citizenlist'])}  "
          f"Animation {fmt(s['ph_animation'])}  PairCache {fmt(s['ph_paircache'])}  "
          f"Weather {fmt(s['ph_weather'])}  PostSim {fmt(s['ph_postsim'])}  (ms/frame)")
    print(f"  render    draw {fmt(s['draw_ms_mean'])} ms CPU   frame {fmt(s['frame_ms_mean'])} ms "
          f"(p95 {fmt(s['frame_ms_p95'])})   {fmt(s['fps_mean'])} fps (min window {fmt(s['fps_min'])})")
    dm, dp, de = s.get("draw_main_ms", NAN), s.get("draw_packet_ms", NAN), s.get("draw_emit_ms", NAN)
    if not (math.isnan(dm) and math.isnan(dp)):
        share = dm / (dm + dp) * 100.0 if not math.isnan(dm) and not math.isnan(dp) and dm + dp > 0 else NAN
        print(f"  pipe      draw main {fmt(dm)} ms   packet {fmt(dp)} ms   emit {fmt(de)} ms   "
              f"main share {fmt(share)}%  (ext.render_pipe)")
        pw, pl = s.get("pipe_wait_us", NAN), s.get("pipe_latency_us", NAN)
        if not (math.isnan(pw) and math.isnan(pl)):
            print(f"  pipe      hand-off wait {fmt(pw)} us/frame   packet latency {fmt(pl)} us "
                  f"(render thread; 0 / n/a = sequential split)")
    print(f"  world     entities {int(s['entities_first'])} -> {int(s['entities_last'])}   "
          f"citizens {int(s['citizens_first'])} -> {int(s['citizens_last'])}   "
          f"employed {int(s['work248_last'])}  housed {int(s['home238_last'])}  busy {int(s['busy_last'])}  "
          f"hunger {fmt(s['hunger_last'])}")
    print(f"  memory    working set {fmt(s['ws_mb_first'])} -> {fmt(s['ws_mb_last'])} MB   "
          f"private {fmt(s['private_mb_first'])} -> {fmt(s['private_mb_last'])} MB "
          f"({fmt(s['private_mb_per_min'])} MB/min)")

    cp = compprof_last_window(a.compprof)
    if cp:
        print()
        print("COMPONENT UPDATE PROFILE (last window)")
        for line in cp:
            print("  " + line.replace("[COMPPROF]", "").rstrip())

    b = None
    if a.baseline and os.path.exists(a.baseline):
        bmeta, brows = load(a.baseline)
        if a.rows_to > 0:
            brows = [r for r in brows if r.get("frame", 0) <= a.rows_to]
        b = summarise(brows, a.warmup) if brows else {}
    print()
    print_pathfinding(s, b)

    if b is not None:
        print()
        towns_differ = print_workload(meta, bmeta, rows, brows, s, b)
        print()
        print(f"VS BASELINE  {a.baseline}")
        for m in bmeta:
            print(f"  {m}")
        same_shape = (int(b.get("citizens_last", -1)) and int(b.get("speed", 0)) == int(s.get("speed", 0)))
        if not same_shape:
            print("  !! baseline ran at a different speed -- per-frame numbers are not comparable, per-iteration ones are")
        print("  {:<22} {:>10} {:>10} {:>8}".format("metric", "baseline", "now", "delta"))
        regressions = 0
        for key, label, hib, thr in COMPARE:
            bv, nv = b.get(key, float("nan")), s.get(key, float("nan"))
            if isinstance(bv, float) and isinstance(nv, float) and bv and not math.isnan(bv) and not math.isnan(nv):
                d = (nv - bv) / abs(bv) * 100.0
                worse = (d < -thr) if hib else (d > thr)
                better = (d > thr) if hib else (d < -thr)
                tag = "  REGRESSION" if worse else ("  improved" if better else "")
                if towns_differ and tag:
                    tag = "  (workload: different towns)"   # WORKLOAD: no verdict between two towns
                else:
                    regressions += worse
                print("  {:<22} {:>10} {:>10} {:>+7.1f}%{}".format(label, fmt(bv), fmt(nv), d, tag))
            else:
                print("  {:<22} {:>10} {:>10} {:>8}".format(label, fmt(bv), fmt(nv), "n/a"))
        if towns_differ:
            print("  NO VERDICT: the towns differ (WORKLOAD above); compare the rows before the drift "
                  "(--rows-to FRAME), the per-citizen line, or several runs a side")
        else:
            print(f"  {regressions} regression(s) over threshold")

        # PLAN gate (docs/runtime_port/PLAN.md, section 5): `sim ms / iteration`
        # within 5% of the baseline at each wave boundary. That is TIGHTER than
        # the 10% REGRESSION threshold above, so "0 regressions over threshold"
        # is NOT the gate passing -- the Wave 2 boundary breached the gate at
        # +7.8% while this reporter called the run clean. Print the verdict.
        bv, nv = b.get("per_iter_ms", float("nan")), s.get("per_iter_ms", float("nan"))
        if bv and not math.isnan(bv) and not math.isnan(nv):
            d = (nv - bv) / abs(bv) * 100.0
            verdict = "PASS" if d <= PLAN_GATE_PCT else "FAIL"
            if towns_differ:
                verdict = "NOT JUDGED (different towns)"
            print("  PLAN 5%% gate (sim ms/iter): %+.1f%%  %s" % (d, verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
