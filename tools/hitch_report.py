#!/usr/bin/env python3
"""Read a stress run's hitch rows (HITCH, perf/README.md "Hitches") and say where the hitches came from.

usage: hitch_report.py <results_dir | standalone_stress_hitch.csv>
                       [--stress standalone_stress.csv] [--samples standalone_samples.txt]
                       [--exe build/cmake-perf/OpenBA.exe] [--top 25] [--list 20] [--min-excess MS]

The harness (src/diag/stress_harness.cpp, BANISHED_STRESS_HITCH_MS) logs one row per Draw-to-Draw
period that exceeds the rolling median by more than the threshold, with the period's split
(update / draw / outside, the SIMPROF phases, the main-thread pathfinding brackets), the rings'
medians for each of those, the number of Updates the period ran and the harness's own markers.

This report:
  1. the per-window overview from the stress CSV (frame_ms_max, hitch_n per 600-frame row);
  2. the hitch distribution (excess over the median: count, p50/p95/max, the histogram);
  3. ATTRIBUTION: for every hitch, which part grew most over its median --
       cascade      extra Update(s) in the period (the previous frame was long, so the fixed-step
                    accumulator ran k > 1 Updates: a FOLLOW-ON hitch, not a new cause)
       ph_*         one SIMPROF phase of Update grew (PairCache, Animation, EntityList, Weather,
                    CitizenList, PostSim); update-other = Update grew outside every phase
       draw         GameUpdateLoop::Draw (CPU submission) grew
       outside      the time between Draw and the next Update grew (Present / vsync, the pump,
                    the limiter, the OS, another thread holding the main thread)
       harness      the stress harness's own work (the CSV row, the staffing pass): not the game
     and the harness markers (staff pass, CSV row, frame % 1800, new world) on those frames;
  4. with --samples: the sampler lines (BANISHED_SAMPLE) whose [LoadTime] stamp falls inside a hitch
     period, main thread, aggregated by function (self and inclusive) and compared with the whole
     run: a function whose share inside hitches is far above its overall share is the hitch.
     Symbolised like tools/sample_report.py (addr2line on --exe, nearest Runtime export).

Acceptance (the session's criterion): no period more than one 140 fps frame (7.14 ms) over the
median. The report prints PASS / FAIL against the CSV's own threshold.
"""
import argparse, collections, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PHASES = ["paircache", "animation", "entitylist", "weather", "citizenlist", "postsim"]
MARKERS = [(1, "staff"), (2, "row"), (4, "f1800"), (8, "multi"), (16, "newworld")]


def read_csv(path):
    meta, rows, header = [], [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                meta.append(line[1:].strip()); continue
            if header is None:
                header = line.split(","); continue
            vals = line.split(",")
            if len(vals) != len(header):
                continue
            row = {}
            for k, v in zip(header, vals):
                try: row[k] = float(v)
                except ValueError: row[k] = v
            rows.append(row)
    return meta, rows


def pct(xs, p):
    xs = sorted(xs)
    if not xs: return float("nan")
    return xs[int(round((len(xs) - 1) * p))]


def markers_str(m):
    m = int(m)
    s = [name for bit, name in MARKERS if m & bit]
    return "+".join(s) if s else "-"


def attribute(h):
    """-> (dominant, dict of deltas). Deltas are ms over the ring medians, scaled by the Updates run."""
    k = max(1, int(h["updates"]))
    med_u = h["med_update_ms"]
    d = {}
    d["cascade"] = (k - 1) * med_u if k > 1 else 0.0
    ph_sum = 0.0
    for p in PHASES:
        v = h["ph_%s_ms" % p] - k * h["med_%s_ms" % p]
        d["ph_" + p] = v
        ph_sum += v
    d["update-other"] = (h["update_ms"] - k * med_u) - ph_sum
    d["draw"] = h["draw_ms"] - h["med_draw_ms"]
    harness = h.get("harness_ms", 0.0)
    d["harness"] = harness
    d["outside"] = (h["outside_ms"] - harness) - h["med_outside_ms"]
    dom = max(d.items(), key=lambda kv: kv[1])[0]
    return dom, d


def run_frames(errlog, from_frame):
    """Draw-to-Draw periods the harness closed, from the [Stress::hitch] window lines (one per CSV row,
    `frame=` = the row's world frame) of rows after `from_frame`. None when the log has none."""
    import re
    total, seen = 0, False
    with open(errlog, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "[Stress::hitch]" not in line:
                continue
            m = re.search(r"\bframes=(\d+)", line)
            if not m:
                continue
            seen = True
            total += int(m.group(1))
    return total if seen else None


def tail_class(h):
    """HITCH_TAIL: the class of one tail frame and its split (ms). Classes, first match wins:
       harness    the stress harness's own row / staffing work in the period (> 1 ms)
       catch-up   two or more fixed steps in the period (the previous frame was long)
       draw-only  no step: Draw, the render-pipe tail (join / hand-off wait) and Present only
       step       one fixed step (sim + draw): the plain stepping frame"""
    k = int(h["updates"])
    fence = h.get("fence_ms", 0.0) or 0.0
    tail = h.get("pipe_tail_ms", 0.0) or 0.0
    tail_wait = max(0.0, tail - h["draw_ms"]) if tail > 0 else 0.0
    harness = h.get("harness_ms", 0.0)
    split = {
        "update": h["update_ms"],
        "draw": h["draw_ms"],
        "fence": fence,
        "tailwait": tail_wait,
        "harness": harness,
    }
    split["other"] = h["period_ms"] - sum(split.values())
    if harness > 1.0 or (int(h["markers"]) & 3):
        return "harness", split
    if k >= 2:
        return "catch-up", split
    if k == 0:
        return "draw-only", split
    return "step", split


def tail_report(hitches, total, label):
    """HITCH_TAIL: the slowest 5% / 1% of ALL periods, classified. Needs a run whose threshold is
    near 0 (BANISHED_STRESS_HITCH_MS=0.01: every period above its class median is a row), so the
    tail is complete in the rows; `total` is the run's period count."""
    print("\nTAIL %s (%d periods in the run, %d rows above their class median)" % (label, total, len(hitches)))
    rows = sorted(hitches, key=lambda h: -h["period_ms"])
    steps = [h for h in hitches if int(h["updates"]) >= 1]
    for frac, name in ((0.05, "p95"), (0.01, "p99")):
        n = max(1, int(round(total * frac)))
        if n > len(rows):
            print("  %s: the rows do not cover the tail (%d needed, %d rows): rerun at BANISHED_STRESS_HITCH_MS=0.01" % (name, n, len(rows)))
            continue
        tail = rows[:n]
        cut = tail[-1]["period_ms"]
        over = sum(max(0.0, h["period_ms"] - 1000.0 / 140.0) for h in tail)
        print("  slowest %s: %d periods, period >= %.2f ms; ms over 7.14 summed %.1f" % (name, n, cut, over))
        print("    %-10s %6s %6s %8s %8s %7s %7s %7s %8s %7s %7s  %s" % (
            "class", "count", "share", "over7ms", "period", "update", "draw", "fence", "tailwait", "harness", "other", "update>med"))
        byc = collections.defaultdict(list)
        for h in tail:
            c, s = tail_class(h)
            byc[c].append((h, s))
        for c in ("step", "catch-up", "draw-only", "harness"):
            xs = byc.get(c, [])
            if not xs:
                continue
            m = lambda key: sum(s[key] for _, s in xs) / len(xs)
            ov = sum(max(0.0, h["period_ms"] - 1000.0 / 140.0) for h, _ in xs)
            grew = sum(h["update_ms"] - max(1, int(h["updates"])) * h["med_update_ms"] for h, _ in xs) / len(xs)
            print("    %-10s %6d %5.1f%% %7.1f%% %8.2f %7.2f %7.2f %7.2f %8.2f %7.2f %7.2f  %+.2f" % (
                c, len(xs), 100.0 * len(xs) / n, 100.0 * ov / over if over else 0.0,
                sum(h["period_ms"] for h, _ in xs) / len(xs), m("update"), m("draw"), m("fence"), m("tailwait"),
                m("harness"), m("other"), grew))
    if steps:
        per = [h["period_ms"] for h in steps]
        print("  step-class rows: %d; period p50 %.2f p90 %.2f max %.2f; update p50 %.2f; class median period %.2f" % (
            len(steps), pct(per, 0.5), pct(per, 0.9), max(per), pct([h["update_ms"] for h in steps], 0.5),
            pct([h["med_period_ms"] for h in steps], 0.5)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", help="results dir or standalone_stress_hitch.csv")
    ap.add_argument("--stress", default=None)
    ap.add_argument("--samples", default=None)
    ap.add_argument("--exe", default="build/cmake-perf/OpenBA.exe")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--list", type=int, default=20, help="worst hitches to list")
    ap.add_argument("--min-excess", type=float, default=0.0, help="ignore hitches with less excess (ms)")
    ap.add_argument("--errlog", default=None, help="standalone_error.log (period totals for the TAIL section)")
    a = ap.parse_args()

    hitch_path = a.path
    if os.path.isdir(a.path):
        hitch_path = os.path.join(a.path, "standalone_stress_hitch.csv")
        if a.stress is None and os.path.exists(os.path.join(a.path, "standalone_stress.csv")):
            a.stress = os.path.join(a.path, "standalone_stress.csv")
        if a.samples is None and os.path.exists(os.path.join(a.path, "standalone_samples.txt")):
            a.samples = os.path.join(a.path, "standalone_samples.txt")

    thr = None
    hitches = []
    if os.path.exists(hitch_path):
        meta, hitches = read_csv(hitch_path)
        for m in meta:
            if "more than" in m:
                try: thr = float(m.split("more than")[1].split("ms")[0])
                except ValueError: pass
    hitches = [h for h in hitches if h["excess_ms"] >= a.min_excess]

    # 1. per-window overview
    if a.stress and os.path.exists(a.stress):
        smeta, srows = read_csv(a.stress)
        for m in smeta: print("# " + m)
        if srows and "frame_ms_max" in srows[0]:
            print("\nWINDOWS (per %d-frame row):" % (int(srows[1]["frame"] - srows[0]["frame"]) if len(srows) > 1 else 0))
            print("  %6s %5s %8s %8s %8s %7s %9s %8s %8s" % ("frame", "speed", "frame_ms", "p95", "max", "hitches", "worst_ms", "citizens", "hunger"))
            for r in srows:
                print("  %6d %5d %8.2f %8.2f %8.2f %7d %9.2f %8d %8.1f" % (
                    r["frame"], r["speed"], r["frame_ms_mean"], r["frame_ms_p95"], r["frame_ms_max"],
                    r["hitch_n"], r["hitch_excess_max_ms"], r["citizens"], r["hunger_mean"]))
            worst = max(r["frame_ms_max"] for r in srows[1:] or srows)
            print("  longest period after the first row: %.2f ms" % worst)
        else:
            print("\n(stress CSV predates the hitch columns)")

    # 2. distribution
    print("\nHITCHES: %d rows%s" % (len(hitches), "" if thr is None else "  (threshold %.3f ms over the rolling median)" % thr))
    if not hitches:
        print("  none -> PASS" if thr is not None else "  no hitch file")
        return
    ex = [h["excess_ms"] for h in hitches]
    print("  excess ms: p50 %.2f  p95 %.2f  max %.2f  (periods: median %.2f .. max %.2f ms)" % (
        pct(ex, 0.5), pct(ex, 0.95), max(ex), pct([h["med_period_ms"] for h in hitches], 0.5), max(h["period_ms"] for h in hitches)))
    if thr:
        buckets = collections.Counter()
        for e in ex:
            b = 0
            while b < 7 and e >= thr * (2 << b): b += 1
            buckets[b] += 1
        labels = ["[t,2t)", "[2t,4t)", "[4t,8t)", "[8t,16t)", "[16t,32t)", "[32t,64t)", "[64t,128t)", "128t+"]
        print("  histogram: " + "  ".join("%s=%d" % (labels[b], buckets[b]) for b in range(8) if buckets[b]))
    game = [h for h in hitches if attribute(h)[0] != "harness"]
    print("  verdict: %s (%d periods over the criterion, %d of them the harness's own row / staffing work)" % (
        "PASS" if not game else "FAIL", len(hitches), len(hitches) - len(game)))

    # 2b. HITCH_TAIL: the slowest 5% / 1% of every period (a near-zero threshold run only)
    errlog = a.errlog
    if errlog is None and os.path.isdir(a.path) and os.path.exists(os.path.join(a.path, "standalone_error.log")):
        errlog = os.path.join(a.path, "standalone_error.log")
    if thr is not None and thr < 0.5 and errlog:
        total = run_frames(errlog, 0)
        if total:
            tail_report(hitches, total, "(whole run)")
            # steady state: past the first CSV row (world build, warm-up). By wall time: a draw-only
            # period carries frame 0 (no Update set it).
            first_wall = None
            if a.stress and os.path.exists(a.stress):
                _m, srows0 = read_csv(a.stress)
                if srows0:
                    first_wall = srows0[0]["wall_s"]
            steady = [h for h in hitches if first_wall is not None and h["wall_s"] > first_wall]
            if steady:
                n_first = None
                import re
                with open(errlog, encoding="utf-8", errors="replace") as f:
                    for line in f:
                        m = re.search(r"\[Stress::hitch\].*\bframes=(\d+)", line)
                        if m:
                            n_first = int(m.group(1))
                            break
                if n_first:
                    tail_report(steady, total - n_first, "(steady: after the first CSV row)")

    # 3. attribution
    by_dom = collections.defaultdict(list)
    doms = {}
    for h in hitches:
        dom, d = attribute(h)
        doms[id(h)] = (dom, d)
        by_dom[dom].append(h)
    print("\nATTRIBUTION (the part that grew most over its median):")
    print("  %-14s %6s %10s %9s %9s  %s" % ("dominant", "count", "excess_sum", "excess_max", "delta_med", "example frames"))
    for dom, hs in sorted(by_dom.items(), key=lambda kv: -sum(h["excess_ms"] for h in kv[1])):
        deltas = [doms[id(h)][1][dom] for h in hs]
        frames = " ".join("%d" % h["frame"] for h in sorted(hs, key=lambda h: -h["excess_ms"])[:6])
        print("  %-14s %6d %10.1f %9.2f %9.2f  %s" % (dom, len(hs), sum(h["excess_ms"] for h in hs), max(h["excess_ms"] for h in hs), pct(deltas, 0.5), frames))
    mk = collections.Counter(markers_str(h["markers"]) for h in hitches)
    print("  markers: " + "  ".join("%s=%d" % kv for kv in mk.most_common()))
    per = collections.Counter()
    for h in hitches:
        f = int(h["frame"])
        for m in (1800, 600, 300, 60):
            if f % m == 0: per["frame%%%d==0" % m] += 1; break
        else:
            per["other"] += 1
    print("  periodicity: " + "  ".join("%s=%d" % kv for kv in per.most_common()))

    # worst list
    print("\nWORST %d:" % min(a.list, len(hitches)))
    print("  %7s %6s %5s %8s %8s %8s %3s %8s %8s %8s %-14s %-10s %s" % (
        "wall_s", "frame", "speed", "period", "baseline", "excess", "upd", "update", "draw", "outside", "dominant", "markers", "phase deltas (ms over k*median)"))
    for h in sorted(hitches, key=lambda h: -h["excess_ms"])[:a.list]:
        dom, d = doms[id(h)]
        phd = " ".join("%s%+.1f" % (p[:3], d["ph_" + p]) for p in PHASES if abs(d["ph_" + p]) >= 0.5)
        print("  %7.1f %6d %5d %8.2f %8.2f %8.2f %3d %8.2f %8.2f %8.2f %-14s %-10s %s" % (
            h["wall_s"], h["frame"], h["speed"], h["period_ms"], h.get("baseline_ms", h["med_period_ms"]), h["excess_ms"], h["updates"],
            h["update_ms"], h["draw_ms"], h["outside_ms"], dom, markers_str(h["markers"]), phd))

    # 4. samples inside the hitch periods
    if a.samples and os.path.exists(a.samples):
        import sample_report, pe_exports, re
        windows = sorted((h["t0_ms"], h["t1_ms"], doms[id(h)][0]) for h in hitches)
        starts = [w[0] for w in windows]
        import bisect
        inside, allm = [], []
        exe_addrs = set()
        with open(a.samples, encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.split()
                if not parts: continue
                rec = sample_report.parse_line(parts)
                if rec is None: continue
                role, _tid, ms, _tag, frames = rec
                if role != "M" or ms is None: continue
                allm.append(frames)
                i = bisect.bisect_right(starts, ms) - 1
                if i >= 0 and windows[i][0] <= ms <= windows[i][1]:
                    inside.append((windows[i][2], frames))
                    for fr in frames:
                        if fr.startswith("E:"): exe_addrs.add(int(fr[2:], 16))
        print("\nSAMPLES: %d main-thread samples in the run, %d inside hitch periods" % (len(allm), len(inside)))
        if inside:
            sym = sample_report.symbolize_exe(exe_addrs, a.exe)
            rt = pe_exports.Resolver(sample_report.RUNTIME_DLL) if os.path.exists(sample_report.RUNTIME_DLL) else None
            cache = {}

            def name(fr):
                r = cache.get(fr)
                if r is not None: return r
                if fr.startswith("E:"):
                    r = sym.get(int(fr[2:], 16), ("exe+%x" % (int(fr[2:], 16) - 0x140000000), "?"))[0]
                elif fr.startswith("?:Runtime-x64.dll+") and rt:
                    nm, _ = rt.nearest(int(fr.split("+0x")[1], 16)); r = "R:" + (nm or "?")
                else:
                    r = re.sub(r"\+0x[0-9a-f]+$", "", fr)
                cache[fr] = r
                return r

            def agg(sample_frames):
                self_c, incl_c = collections.Counter(), collections.Counter()
                for frames in sample_frames:
                    names = [name(fr) for fr in frames]
                    if not names: continue
                    self_c[names[0]] += 1
                    for nm in set(names): incl_c[nm] += 1
                return self_c, incl_c

            # the whole run, for enrichment (only frames we symbolised: cheap, exe addrs outside hitches
            # resolve to exe+off -- good enough for a share)
            hs, hi = agg([fr for _, fr in inside])
            as_, ai = agg(allm)
            n_in, n_all = len(inside), len(allm)
            print("\n  SELF (leaf) inside hitches, top %d  [share inside | share in whole run | enrichment]:" % a.top)
            for nm, c in hs.most_common(a.top):
                sh_in = 100.0 * c / n_in; sh_all = 100.0 * as_.get(nm, 0) / max(1, n_all)
                print("    %6.2f%% | %6.2f%% | %5.1fx  %6d  %s" % (sh_in, sh_all, sh_in / sh_all if sh_all else float("inf"), c, nm))
            print("\n  INCLUSIVE inside hitches, top %d:" % a.top)
            for nm, c in hi.most_common(a.top):
                sh_in = 100.0 * c / n_in; sh_all = 100.0 * ai.get(nm, 0) / max(1, n_all)
                print("    %6.2f%% | %6.2f%% | %5.1fx  %6d  %s" % (sh_in, sh_all, sh_in / sh_all if sh_all else float("inf"), c, nm))
            # per dominant category
            for dom in sorted(set(d for d, _ in inside), key=lambda d: -sum(1 for x, _ in inside if x == d)):
                sub = [fr for d, fr in inside if d == dom]
                if len(sub) < 20: continue
                _s_c, i_c = agg(sub)
                print("\n  category %s (%d samples) -- inclusive top %d:" % (dom, len(sub), a.top))
                for nm, c in i_c.most_common(a.top):
                    print("    %6.2f%%  %6d  %s" % (100.0 * c / len(sub), c, nm))
    elif a.samples:
        print("\n(no samples file at %s)" % a.samples)


if __name__ == "__main__":
    main()
