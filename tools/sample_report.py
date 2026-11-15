#!/usr/bin/env python3
"""Aggregate standalone_samples.txt (BANISHED_SAMPLE sampling profiler) into a report.

usage: sample_report.py <samples.txt> [--exe build/perf/OpenBA.exe]
                        [--tag N[,N..]] [--top 40] [--callers SYMBOL]
                        [--thread ROLE|TID|all] [--from MS] [--to MS]

Each line (block LOADSTAMP): "@<role><tid> <ms> <tag> <n> <frame0> <frame1> ...",
frame0 = leaf; <role> is one letter (M main, L loader, S persist, A audio,
F pathfinder, P path worker, R render, C cull helper, J parallel worker, T other System::Thread),
<ms> the sample time in ms since process start (the [LoadTime] clock). The older
"<tag> <n> <frames...>" lines (main thread only, no time) are still read.
Frames are E:0x140... (exe, symbolized with addr2line), R:<export>+off (Runtime),
V:..., ?:mod+off. Tags: 0 outside Update, 1 Update (no phase), 2 PairCache,
3 Animation, 4 EntityList, 5 Weather, 6 CitizenList, 7 PostSim, 8 EntityList buckets,
99 not the main thread.

--thread picks the samples of one thread: a role letter (M, L, ...), a role name
(main, loader, persist, audio, pathfinder, path, render, cull, parallel, thread), a numeric
tid, or "all". The default is the main thread (what the report always showed).
--from / --to keep samples with from <= ms <= to (old-format lines carry no time and
are dropped by either).
"""
import argparse, collections, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe_exports  # noqa: E402
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished

RUNTIME_DLL = retail("Runtime-x64.dll")

TAGS = {0: "outside-Update", 1: "Update/other", 2: "PairCache", 3: "Animation",
        4: "EntityList", 5: "Weather", 6: "CitizenList", 7: "PostSim", 8: "EntityList/buckets",
        99: "not-main-thread"}

ROLES = {"M": "main", "L": "loader", "S": "persist", "A": "audio", "F": "pathfinder",
         "P": "path", "R": "render", "C": "cull", "J": "parallel", "T": "thread"}


def parse_line(parts):
    """-> (role, tid, ms, tag, frames) or None. Old-format lines: role M, tid/ms None."""
    if parts[0].startswith("@"):
        if len(parts) < 5:
            return None
        head = parts[0][1:]
        role = head[:1]
        tid = int(head[1:]) if head[1:].isdigit() else None
        return role, tid, float(parts[1]), int(parts[2]), parts[4:]
    if len(parts) < 3:
        return None
    return "M", None, None, int(parts[0]), parts[2:]


def thread_matcher(spec):
    if spec is None:
        spec = "M"
    if spec.lower() == "all":
        return lambda role, tid: True
    if spec.isdigit():
        want = int(spec)
        return lambda role, tid: tid == want
    if len(spec) == 1:
        return lambda role, tid: role == spec.upper()
    letters = [k for k, v in ROLES.items() if v == spec.lower()]
    if not letters:
        sys.exit("unknown --thread %r (role letter, role name, tid or all)" % spec)
    return lambda role, tid: role == letters[0]


def symbolize_exe(addrs, exe):
    out = {}
    addrs = sorted(addrs)
    if not addrs:
        return out
    CH = 2000
    for i in range(0, len(addrs), CH):
        chunk = addrs[i:i + CH]
        p = subprocess.run(["addr2line", "-e", exe, "-f", "-C", "-s"] + [hex(a) for a in chunk],
                           capture_output=True, text=True)
        lines = p.stdout.splitlines()
        for j, a in enumerate(chunk):
            fn = lines[2 * j].strip() if 2 * j < len(lines) else "??"
            loc = lines[2 * j + 1].strip() if 2 * j + 1 < len(lines) else "??"
            if fn == "??":
                fn = "exe+%x" % (a - 0x140000000)
            out[a] = (fn, loc)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--exe", default="build/perf/OpenBA.exe")
    ap.add_argument("--tag", default=None)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--callers", default=None, help="show caller breakdown for this symbol")
    ap.add_argument("--callees", default=None, help="show what this symbol spends time in")
    ap.add_argument("--thread", default=None,
                    help="role letter / role name / tid / all (default: the main thread)")
    ap.add_argument("--from", dest="t_from", type=float, default=None, help="keep samples at ms >= this")
    ap.add_argument("--to", dest="t_to", type=float, default=None, help="keep samples at ms <= this")
    a = ap.parse_args()
    want = None if a.tag is None else set(int(x) for x in a.tag.split(","))
    thread_ok = thread_matcher(a.thread)
    thread_counts = collections.Counter()

    samples = []
    exe_addrs = set()
    tag_counts = collections.Counter()
    with open(a.path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            rec = parse_line(parts)
            if rec is None:
                continue
            role, tid, ms, tag, frames = rec
            thread_counts["%s%s" % (role, "" if tid is None else tid)] += 1
            if not thread_ok(role, tid):
                continue
            if a.t_from is not None and (ms is None or ms < a.t_from):
                continue
            if a.t_to is not None and (ms is None or ms > a.t_to):
                continue
            tag_counts[tag] += 1
            if want is not None and tag not in want:
                continue
            samples.append((tag, frames))
            for fr in frames:
                if fr.startswith("E:"):
                    exe_addrs.add(int(fr[2:], 16))
    print("samples by thread (whole file):")
    for k, c in thread_counts.most_common():
        print("  %-12s %-10s %7d" % (k, ROLES.get(k[:1], "?"), c))
    total_all = sum(tag_counts.values())
    print("\nsamples by tag (selected thread / window):")
    for t in sorted(tag_counts):
        print("  %-16s %7d  %5.1f%%" % (TAGS.get(t, str(t)), tag_counts[t], 100.0 * tag_counts[t] / max(1, total_all)))
    sym = symbolize_exe(exe_addrs, a.exe)
    rt = pe_exports.Resolver(RUNTIME_DLL) if os.path.exists(RUNTIME_DLL) else None
    name_cache = {}

    def name(fr):
        r = name_cache.get(fr)
        if r is not None:
            return r
        if fr.startswith("E:"):
            r = sym.get(int(fr[2:], 16), ("?", "?"))[0]
        elif fr.startswith("?:Runtime-x64.dll+") and rt:
            nm, _ = rt.nearest(int(fr.split("+0x")[1], 16))
            r = "R:" + (nm or "?")
        elif fr.startswith("R:") or fr.startswith("V:"):
            r = fr[:2] + re.sub(r"\+0x[0-9a-f]+$", "", fr[2:])
        else:
            r = re.sub(r"\+0x[0-9a-f]+$", "", fr)
        name_cache[fr] = r
        return r

    n = len(samples)
    print("\nselected samples: %d" % n)
    if not n:
        return
    self_c = collections.Counter()
    incl_c = collections.Counter()
    for tag, frames in samples:
        names = [name(fr) for fr in frames]
        self_c[names[0]] += 1
        for nm in set(names):
            incl_c[nm] += 1

    print("\nSELF (leaf) time, top %d:" % a.top)
    for nm, c in self_c.most_common(a.top):
        print("  %6.2f%%  %6d  %s" % (100.0 * c / n, c, nm))
    print("\nINCLUSIVE (on stack) time, top %d:" % a.top)
    for nm, c in incl_c.most_common(a.top):
        print("  %6.2f%%  %6d  %s" % (100.0 * c / n, c, nm))

    if a.callers:
        cc = collections.Counter()
        for tag, frames in samples:
            names = [name(fr) for fr in frames]
            for i, nm in enumerate(names):
                if nm == a.callers and i + 1 < len(names):
                    cc[names[i + 1]] += 1
                    break
        print("\nCALLERS of %s:" % a.callers)
        for nm, c in cc.most_common(a.top):
            print("  %6.2f%%  %6d  %s" % (100.0 * c / n, c, nm))
    if a.callees:
        cc = collections.Counter()
        for tag, frames in samples:
            names = [name(fr) for fr in frames]
            for i, nm in enumerate(names):
                if nm == a.callees:
                    cc[names[i - 1] if i > 0 else "<self>"] += 1
                    break
        print("\nCALLEES of %s (direct):" % a.callees)
        for nm, c in cc.most_common(a.top):
            print("  %6.2f%%  %6d  %s" % (100.0 * c / n, c, nm))


if __name__ == "__main__":
    main()
