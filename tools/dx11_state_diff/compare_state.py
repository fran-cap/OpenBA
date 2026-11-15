#!/usr/bin/env python3
"""Compare two per-draw state dumps produced by dump_state.py.

The problem this has to work around: Banished's simulation is not
frame-deterministic even with the world seed pinned, so two runs draw slightly
different numbers of things. A naive draw-N-vs-draw-N comparison drowns in that.

Two comparisons are made instead, neither of which depends on the draw lists
being the same length:

1. **Configuration set.** Each draw is reduced to a `config` — everything that
   describes *how* the pipeline is set up (shaders, states, targets, layout,
   samplers, texture bindings) with the per-draw *content* (index counts,
   constant-buffer contents, dynamic buffer sizes) removed. Two renderers that
   agree must produce the same set of configurations. A configuration present in
   one frame and absent from the other is a real finding regardless of counts.

2. **Aligned per-field diff.** The two draw sequences are aligned on their
   config hashes (difflib), then every field of every matched pair is compared,
   and the rate of disagreement is reported per field.

Neither output is interpreted here. Run the tool once on two runs of the *same*
renderer to get the noise floor, then on ours-vs-shipped, and compare the two
tables: a field that disagrees no more often than it does in the control is
simulation noise, and a field that disagrees only in the test is a defect.

Usage:
    python compare_state.py <a.jsonl> <b.jsonl> [--label-a A --label-b B]
                            [--show-examples N] [--json out.json]
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import sys
from collections import Counter, defaultdict

# Fields that describe pipeline configuration rather than per-draw payload.
# `vb`/`ib` are split: their strides and offsets go to the per-field diff (strides_only), not the
# configuration; the buffer identity is dropped (dynamic buffers are re-allocated per frame at varying sizes).
CONFIG_FIELDS = [
    "topology", "layout", "vs", "ps", "gs", "hs", "ds",
    "raster", "viewports", "scissors", "blend", "depthState", "om",
    "srv", "sampler", "uav",
]

# Recorded, compared, but excluded from the configuration identity.
CONTENT_FIELDS = ["draw", "cb", "vb", "ib", "flags", "name"]

IGNORED = {"eid", "idx", "_meta"}


def load(path: str):
    meta, recs = {}, []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            d = json.loads(line)
            if d.get("_meta"):
                meta = d
            else:
                recs.append(d)
    return meta, recs


def canon(v) -> str:
    return json.dumps(v, sort_keys=True, separators=(",", ":"))


def config_of(rec: dict) -> dict:
    return {k: rec.get(k) for k in CONFIG_FIELDS}


def config_hash(rec: dict) -> str:
    return hashlib.sha1(canon(config_of(rec)).encode()).hexdigest()[:12]


# A coarser identity than `config`: the pipeline setup with everything derived
# from *which* resource is bound removed. Which texture a given tree uses is the
# game's choice and varies with what is on screen; how the pipeline is
# configured to sample it is the renderer's job.
#
# Three quantities are texture-derived and must come out, or the signature is
# not identity-free and the comparison is circular:
#   sampler.maxLOD    D3D11 clamps it to the bound texture's mip count
#   srv.numMips       the view covers the texture's mips
#   srv.numSlices     the view covers the texture's array slices
# They are not simply discarded — check_maxlod_invariant() verifies
# each against the bound texture's actual description, which is the stronger
# statement: not "both renderers used 7" but "both renderers derived it from
# the texture by the same rule, every time".
SRV_SHAPE_IDX = [0, 1, 2, 4, 5, 7, 9]   # stage, slot, arrayElem, format, firstMip, firstSlice, texType
SAMPLER_DROP = {11}                      # maxLOD


def _sig_parts(rec: dict) -> dict:
    srv_shape = sorted([[e[i] for i in SRV_SHAPE_IDX]
                        for e in (rec.get("srv") or []) if len(e) >= 10])
    samplers = sorted([[v for i, v in enumerate(e) if i not in SAMPLER_DROP]
                       for e in (rec.get("sampler") or [])])
    cb_shape = sorted([e[:3] for e in (rec.get("cb") or [])])
    return {
        "vs": rec.get("vs"), "ps": rec.get("ps"), "gs": rec.get("gs"),
        "hs": rec.get("hs"), "ds": rec.get("ds"),
        "topology": rec.get("topology"), "layout": rec.get("layout"),
        "raster": rec.get("raster"), "blend": rec.get("blend"),
        "depthState": rec.get("depthState"), "sampler": samplers,
        "viewports": rec.get("viewports"), "scissors": rec.get("scissors"),
        "om": rec.get("om"), "uav": rec.get("uav"),
        "srvShape": srv_shape, "cbShape": cb_shape,
    }


def pipeline_sig(rec: dict) -> str:
    return hashlib.sha1(canon(_sig_parts(rec)).encode()).hexdigest()[:12]


def sig_without_sampler(rec: dict) -> str:
    parts = _sig_parts(rec)
    parts.pop("sampler", None)
    return canon(parts)


def strides_only(rec: dict) -> dict:
    """vb/ib with the buffer identity dropped, keeping stride and offset."""
    ib = rec.get("ib") or ["-", 0, 0]
    vb = rec.get("vb") or []
    return {
        "ibStride": ib[1] if len(ib) > 1 else 0,
        "vbStrides": sorted([[e[0], e[2], e[3]] for e in vb if len(e) >= 4]),
    }


def all_fields(rec: dict) -> dict:
    """Flat field map used for the per-field diff."""
    out = {k: v for k, v in rec.items() if k not in IGNORED}
    out.update(strides_only(rec))
    # Compare constant-buffer *bindings* separately from their contents: a
    # different matrix is the simulation moving, a different binding is not.
    cb = rec.get("cb") or []
    out["cbBindings"] = sorted([e[:3] + [e[4], e[5]] for e in cb if len(e) >= 6])
    out["cbContents"] = sorted([e[:3] + [e[6]] for e in cb if len(e) >= 7])
    out["cbBufferSizes"] = sorted([e[:3] + [e[3]] for e in cb if len(e) >= 4])
    out.pop("cb", None)
    out.pop("vb", None)
    out.pop("ib", None)
    return out


def summarize_configs(recs, label):
    counts = Counter(config_hash(r) for r in recs if "error" not in r)
    return counts


def describe_config(rec: dict) -> str:
    om = rec.get("om") or {}
    rts = om.get("rts") or []
    rt = rts[0][1] if rts and len(rts[0]) > 1 else "-"
    vp = (rec.get("viewports") or [[0, 0, 0, 0]])[0]
    blend = rec.get("blend") or {}
    blends = blend.get("blends") or [[0, False]]
    dsx = rec.get("depthState") or {}
    return (f"vs={rec.get('vs', '-')[:20]} ps={rec.get('ps', '-')[:20]} "
            f"topo={rec.get('topology')} rt={rt[:46]} "
            f"vp={vp[2]:.0f}x{vp[3]:.0f} blend={blends[0][1]} "
            f"depth={dsx.get('depthEnable')}/{dsx.get('depthWrites')} "
            f"cull={(rec.get('raster') or {}).get('cull')}")


# Field order of a sampler entry as written by dump_state.sampler_entry().
SAMPLER_FIELDS = ["stage", "slot", "arrayElem", "filter", "addressU", "addressV",
                  "addressW", "mipBias", "maxAniso", "compare", "minLOD",
                  "maxLOD", "border"]


def explain_signature_gap(recs_ref, recs_other, l_ref: str, l_other: str):
    """Classify signatures present in recs_ref but not recs_other."""
    other_sigs = {pipeline_sig(r) for r in recs_other}
    other_by_pair = defaultdict(list)
    for r in recs_other:
        other_by_pair[(r.get("vs"), r.get("ps"))].append(r)

    gap = defaultdict(list)
    for r in recs_ref:
        h = pipeline_sig(r)
        if h not in other_sigs:
            gap[h].append(r)
    if not gap:
        return

    unused_shader, unexplained = 0, []
    for h, rs in gap.items():
        r = rs[0]
        if not other_by_pair.get((r.get("vs"), r.get("ps"))):
            # That material never drew on the other side at all -- content.
            unused_shader += 1
        else:
            unexplained.append(h)

    print(f"  gap {l_ref}->{l_other}: {len(gap)} signature(s); "
          f"{unused_shader} shader-pair-never-drawn (content), "
          f"{len(unexplained)} UNEXPLAINED")
    for h in unexplained:
        print(f"    UNEXPLAINED {h} (x{len(gap[h])}): {describe_config(gap[h][0])}")


def check_maxlod_invariant(recs, label):
    """The texture-derived quantities excluded from pipeline_sig, verified.

    Each is checked against the description of the texture actually bound at
    that slot, so dropping them from the signature does not drop them from the
    comparison -- it strengthens what is claimed about them.
    """
    total, bad = 0, 0
    mip_total, mip_bad, sl_total, sl_bad = 0, 0, 0, 0
    for r in recs:
        srv = {(e[0], e[1]): e[3] for e in (r.get("srv") or []) if len(e) >= 4}
        for e in (r.get("srv") or []):
            if len(e) < 10 or ":m" not in (e[3] or "") or ":a" not in (e[3] or ""):
                continue
            try:
                mips = int(e[3].split(":m")[1].split(":")[0])
                slices = int(e[3].split(":a")[1].split(":")[0])
            except ValueError:
                continue
            mip_total += 1
            if e[6] != mips:
                mip_bad += 1
            sl_total += 1
            if e[8] != slices:
                sl_bad += 1
        for s in (r.get("sampler") or []):
            tex = srv.get((s[0], s[1]))
            if not tex or ":m" not in tex:
                continue
            try:
                mips = int(tex.split(":m")[1].split(":")[0])
            except ValueError:
                continue
            total += 1
            if abs(float(s[11]) - (mips - 1)) > 1e-3:
                bad += 1
    def line(name, ok, tot, bad_n):
        return f"{name} {ok}/{tot}" + ("" if not bad_n else f" ({bad_n} EXCEPTIONS)")

    if total or mip_total:
        print(f"  {label}: texture-derived invariants -- "
              + line("maxLOD==mips-1", total - bad, total, bad) + ", "
              + line("srv.numMips==mips", mip_total - mip_bad, mip_total, mip_bad) + ", "
              + line("srv.numSlices==arraySize", sl_total - sl_bad, sl_total, sl_bad))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--label-a", default=None)
    ap.add_argument("--label-b", default=None)
    ap.add_argument("--show-examples", type=int, default=2)
    ap.add_argument("--json", dest="json_out", default=None)
    args = ap.parse_args()

    meta_a, recs_a = load(args.a)
    meta_b, recs_b = load(args.b)
    la = args.label_a or meta_a.get("capture", args.a)
    lb = args.label_b or meta_b.get("capture", args.b)

    err_a = [r for r in recs_a if "error" in r]
    err_b = [r for r in recs_b if "error" in r]
    recs_a = [r for r in recs_a if "error" not in r]
    recs_b = [r for r in recs_b if "error" not in r]

    print(f"=== {la}  vs  {lb} ===")
    print(f"draws: {len(recs_a)} vs {len(recs_b)}"
          + (f"   (errors {len(err_a)}/{len(err_b)})" if (err_a or err_b) else ""))
    for m, lab in ((meta_a, la), (meta_b, lb)):
        if m:
            print(f"  {lab}: actions={m.get('totalActions')} textures={m.get('textures')} "
                  f"buffers={m.get('buffers')} resources={m.get('resources')}")

    # ── 1. configuration sets ────────────────────────────────────────────────
    ca, cb_ = summarize_configs(recs_a, la), summarize_configs(recs_b, lb)
    by_hash = {}
    for r in recs_a:
        by_hash.setdefault(config_hash(r), r)
    for r in recs_b:
        by_hash.setdefault(config_hash(r), r)

    only_a = sorted(set(ca) - set(cb_))
    only_b = sorted(set(cb_) - set(ca))
    shared = sorted(set(ca) & set(cb_))

    print(f"\n-- pipeline configurations --")
    print(f"distinct configs: {len(ca)} vs {len(cb_)};  shared {len(shared)}, "
          f"only-{la} {len(only_a)}, only-{lb} {len(only_b)}")
    if only_a or only_b:
        for h in only_a:
            print(f"  ONLY in {la} (x{ca[h]}): {h}  {describe_config(by_hash[h])}")
        for h in only_b:
            print(f"  ONLY in {lb} (x{cb_[h]}): {h}  {describe_config(by_hash[h])}")
    else:
        print(f"  IDENTICAL SET: every pipeline configuration occurs in both.")

    cnt_diffs = [(h, ca[h], cb_[h]) for h in shared if ca[h] != cb_[h]]
    if cnt_diffs:
        print(f"  shared configs whose draw COUNT differs: {len(cnt_diffs)}/{len(shared)}")
        for h, x, y in sorted(cnt_diffs, key=lambda t: -abs(t[1] - t[2]))[:8]:
            print(f"    {h}: {x} vs {y}   {describe_config(by_hash[h])}")
    else:
        print(f"  all shared configs occur the same number of times.")

    # ── 1b. pipeline signatures (no alignment, no resource identity) ─────────
    pa = Counter(pipeline_sig(r) for r in recs_a)
    pb = Counter(pipeline_sig(r) for r in recs_b)
    for r in recs_a + recs_b:
        by_hash.setdefault(pipeline_sig(r), r)
    p_only_a = sorted(set(pa) - set(pb))
    p_only_b = sorted(set(pb) - set(pa))
    print(f"\n-- pipeline signatures (bound-resource identity removed) --")
    print(f"distinct: {len(pa)} vs {len(pb)};  shared {len(set(pa) & set(pb))}, "
          f"only-{la} {len(p_only_a)}, only-{lb} {len(p_only_b)}")
    if p_only_a or p_only_b:
        for h in p_only_a:
            print(f"  ONLY in {la} (x{pa[h]}): {h}  {describe_config(by_hash[h])}")
        for h in p_only_b:
            print(f"  ONLY in {lb} (x{pb[h]}): {h}  {describe_config(by_hash[h])}")
    else:
        print(f"  IDENTICAL SET: both renderers configure the pipeline the same "
              f"{len(pa)} ways.")

    # ── 1b-ii. explain the signature gap ─────────────────────────────────────
    # A signature present on one side only is not automatically a defect. The benign
    # cause checked here: the shader pair never ran on the other side at all (that
    # material had nothing to draw), which is content. The sampler's maxLOD, which
    # D3D11 derives from the bound texture's mip count, is already out of the
    # signature (SAMPLER_DROP); `sampler maxLOD == mips-1` is verified below.
    # Anything left over is reported as UNEXPLAINED and is a real finding.
    explain_signature_gap(recs_a, recs_b, la, lb)
    explain_signature_gap(recs_b, recs_a, lb, la)
    check_maxlod_invariant(recs_a, la)
    check_maxlod_invariant(recs_b, lb)

    # ── 1c. the pool of textures ever bound as an SRV ────────────────────────
    def srv_pool(recs):
        pool = Counter()
        for r in recs:
            for e in (r.get("srv") or []):
                if len(e) >= 4:
                    pool[e[3]] += 1
        return pool

    sa_, sb_ = srv_pool(recs_a), srv_pool(recs_b)
    only_sa = sorted(set(sa_) - set(sb_))
    only_sb = sorted(set(sb_) - set(sa_))
    print(f"\n-- textures bound as SRV (content-hashed identity) --")
    print(f"distinct: {len(sa_)} vs {len(sb_)};  shared {len(set(sa_) & set(sb_))}, "
          f"only-{la} {len(only_sa)}, only-{lb} {len(only_sb)}")
    for k in only_sa[:10]:
        print(f"  ONLY {la} (x{sa_[k]}): {k[:110]}")
    for k in only_sb[:10]:
        print(f"  ONLY {lb} (x{sb_[k]}): {k[:110]}")

    # ── 2. aligned per-field diff ────────────────────────────────────────────
    ha = [config_hash(r) for r in recs_a]
    hb = [config_hash(r) for r in recs_b]
    sm = difflib.SequenceMatcher(a=ha, b=hb, autojunk=False)
    pairs = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                pairs.append((recs_a[i1 + k], recs_b[j1 + k]))
    unmatched_a = len(recs_a) - len(pairs)
    unmatched_b = len(recs_b) - len(pairs)

    print(f"\n-- aligned per-field diff --")
    print(f"aligned {len(pairs)} draw pairs "
          f"(unaligned: {unmatched_a} in {la}, {unmatched_b} in {lb})")

    field_diffs: Counter = Counter()
    examples = defaultdict(list)
    for ra, rb in pairs:
        fa, fb = all_fields(ra), all_fields(rb)
        for k in sorted(set(fa) | set(fb)):
            va, vb = canon(fa.get(k)), canon(fb.get(k))
            if va != vb:
                field_diffs[k] += 1
                if len(examples[k]) < args.show_examples:
                    examples[k].append((ra.get("eid"), rb.get("eid"), va, vb))

    n = max(len(pairs), 1)
    print(f"{'field':<16} {'differing pairs':>16}  {'rate':>7}")
    all_keys = sorted(set(all_fields(recs_a[0])) if recs_a else set())
    for k in all_keys:
        d = field_diffs.get(k, 0)
        mark = "   " if d == 0 else " * "
        print(f"{mark}{k:<14} {d:>14}/{len(pairs):<6} {100.0 * d / n:>6.1f}%")

    if args.show_examples:
        for k in sorted(field_diffs):
            print(f"\n  [{k}] examples:")
            for eid_a, eid_b, va, vb in examples[k]:
                print(f"    eid {eid_a} vs {eid_b}")
                print(f"      {la}: {va[:300]}")
                print(f"      {lb}: {vb[:300]}")

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump({
                "a": la, "b": lb,
                "draws": [len(recs_a), len(recs_b)],
                "configs": [len(ca), len(cb_)],
                "configsOnlyA": only_a, "configsOnlyB": only_b,
                "sharedConfigCountDiffs": len(cnt_diffs),
                "alignedPairs": len(pairs),
                "fieldDiffs": dict(field_diffs),
            }, fh, indent=1)
        print(f"\nwrote {args.json_out}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
