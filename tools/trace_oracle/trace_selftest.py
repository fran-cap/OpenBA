#!/usr/bin/env python3
"""trace_selftest — the control harness for trace_oracle.

A brand-new instrument with no track record cannot be trusted because its output
agrees with the answer you expected. This harness corrupts a REAL trace in
known ways and scores whether the oracle notices.

Two directions, both of which have produced false verdicts on this project:

  POSITIVE controls corrupt a subject the oracle *does* compare. A positive
  control the oracle misses is a blind spot in the oracle.

  NEGATIVE controls corrupt a subject the oracle *must not* flag -- the things
  that genuinely vary run to run in a non-deterministic simulation (event ids,
  draw counts, constant-buffer contents, which dynamic buffer object happened to
  be allocated, how many instances of a configuration were drawn). A negative
  control the oracle flags is a false-positive generator, and this project has
  been bitten by a control that corrupted a byte the probe should not compare
  and then EXONERATED a broken instrument.

EFFECTIVENESS COUNTER
---------------------
Every mutation reports how many records it changed and how many field VALUES it
actually altered. A mutation with changed=0 is not a passing control, it is a
mutation that did nothing -- it is reported INERT and excluded from the score,
because "the oracle did not notice a change that never happened" is not evidence
about the oracle.

PER-SCREEN SCORING, NEVER A UNION
---------------------------------
Each mutation records WHICH of the six screens caught it. A union score reads
3/3 while hiding that one screen caught 1/3. The summary therefore prints a
per-screen recall row, and names any screen that caught nothing at all.

Usage:
    trace_selftest.py <real-trace.jsonl> [--json out.json] [--verbose]
"""

from __future__ import annotations

import argparse
import copy
import json
import os
import random
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)

import trace_oracle as TO  # noqa: E402

SCREENS = ["S1_configSet", "S2_sigSet", "S3_fieldDiff", "S4_invariants",
           "S5_srvPool", "S6_strideSet"]


# ── mutation helpers ─────────────────────────────────────────────────────────

def _first_where(recs, pred):
    for r in recs:
        if pred(r):
            return r
    return None


def _count_changed(before, after) -> int:
    """Number of top-level record fields whose canonical form changed."""
    n = 0
    for k in set(before) | set(after):
        if TO.CS.canon(before.get(k)) != TO.CS.canon(after.get(k)):
            n += 1
    return n


class Mutation:
    """kind is one of:

      positive  the oracle MUST catch it (it corrupts a subject the oracle compares)
      negative  the oracle MUST NOT flag it (it corrupts a subject that genuinely
                varies run to run in this non-deterministic simulation)
      open      its run-to-run stability is NOT KNOWN and cannot be settled without
                a same-build control pair, which needs the game-directory lock.
                Scored separately and never counted as a pass or a failure.

    `exempt` names the screens a NEGATIVE control is entitled to trip, with the
    reason recorded. An exemption is a statement about that screen's domain, not
    a tolerance widened to make a control green -- the only one here says S3
    cannot run unfloored, which is exactly why trace_oracle suppresses it.
    """

    def __init__(self, name, kind, note, fn, exempt=(), why_open=None):
        self.name, self.kind, self.note, self.fn = name, kind, note, fn
        self.exempt, self.why_open = set(exempt), why_open

    def apply(self, recs):
        recs = copy.deepcopy(recs)
        snapshot = copy.deepcopy(recs)
        touched = self.fn(recs)
        changed_records = 0
        changed_fields = 0
        for b, a in zip(snapshot, recs):
            c = _count_changed(b, a)
            if c:
                changed_records += 1
                changed_fields += c
        if len(snapshot) != len(recs):
            changed_records += abs(len(snapshot) - len(recs))
        return recs, changed_records, changed_fields, touched


# ── POSITIVE controls: subjects the oracle DOES compare ──────────────────────

def m_cull(recs):
    r = _first_where(recs, lambda r: (r.get("raster") or {}).get("cull"))
    if not r:
        return 0
    cur = r["raster"]["cull"]
    r["raster"]["cull"] = "Front" if cur != "Front" else "Back"
    return 1


def m_depth_writes(recs):
    r = _first_where(recs, lambda r: "depthWrites" in (r.get("depthState") or {}))
    if not r:
        return 0
    r["depthState"]["depthWrites"] = not r["depthState"]["depthWrites"]
    return 1


def m_blend_enable(recs):
    r = _first_where(recs, lambda r: (r.get("blend") or {}).get("blends"))
    if not r:
        return 0
    r["blend"]["blends"][0][1] = not r["blend"]["blends"][0][1]
    return 1


def m_blend_factor(recs):
    r = _first_where(recs, lambda r: (r.get("blend") or {}).get("blends"))
    if not r:
        return 0
    b = r["blend"]["blends"][0]
    b[3] = "One" if b[3] != "One" else "Zero"   # colorBlend.source
    return 1


def m_sampler_address(recs):
    r = _first_where(recs, lambda r: r.get("sampler"))
    if not r:
        return 0
    s = r["sampler"][0]
    s[4] = "ClampEdge" if s[4] != "ClampEdge" else "Wrap"   # addressU
    return 1


def m_sampler_filter(recs):
    r = _first_where(recs, lambda r: r.get("sampler"))
    if not r:
        return 0
    r["sampler"][0][3] = "Point/Point/Point/Normal"
    return 1


def m_ps_shader(recs):
    r = _first_where(recs, lambda r: r.get("ps") and r["ps"] != "-")
    if not r:
        return 0
    r["ps"] = r["ps"][:-4] + "dead"
    return 1


def m_srv_format(recs):
    r = _first_where(recs, lambda r: r.get("srv"))
    if not r:
        return 0
    r["srv"][0][4] = "R8G8B8A8_UNORM_SRGB_MUTATED"
    return 1


def m_drop_depth_target(recs):
    r = _first_where(recs, lambda r: (r.get("om") or {}).get("depth", "-") != "-")
    if not r:
        return 0
    r["om"]["depth"] = "-"
    return 1


def m_viewport(recs):
    r = _first_where(recs, lambda r: r.get("viewports"))
    if not r:
        return 0
    r["viewports"][0][2] = float(r["viewports"][0][2]) / 2.0
    return 1


def m_topology(recs):
    r = _first_where(recs, lambda r: r.get("topology"))
    if not r:
        return 0
    r["topology"] = "Line_List" if r["topology"] != "Line_List" else "Point_List"
    return 1


def m_layout_offset(recs):
    r = _first_where(recs, lambda r: r.get("layout"))
    if not r:
        return 0
    r["layout"][0][4] = int(r["layout"][0][4]) + 4
    return 1


def m_vb_stride(recs):
    """Vertex-buffer STRIDE is configuration (identity is not) -- must be caught.

    This control is why S6 exists. On its first run it was caught by S3 alone,
    and S3 is suppressed without a control pair, so the instrument could not see
    a stride difference in the configuration a lane actually has. S6 was added in
    response; this control is what scores whether that fix works.
    """
    r = _first_where(recs, lambda r: r.get("vb"))
    if not r:
        return 0
    r["vb"][0][2] = int(r["vb"][0][2]) + 4
    return 1


def m_cb_bind_size(recs):
    """Constant-buffer bind SIZE -- the one subject only S3 can see.

    This control exists because trace_selftest itself reported "S3 caught 0/18,
    this screen is unvalidated by this run" after `cbBufferSizes` and `vbStrides`
    were moved to NON_VERDICT. A screen with no positive control is a screen whose
    silence means nothing.

    byteSize is the shader's declared constant-buffer size: configuration. It is
    NOT the ring allocation's length (that is `cbBufferSizes`, measured unstable
    at 30.9-37.4% between identical runs and now non-verdict). Measured stable:
    cbBindings differed on 0.0% of aligned pairs across the ctrlA/B/C pairs.
    """
    r = _first_where(recs, lambda r: r.get("cb") and len(r["cb"][0]) >= 6)
    if not r:
        return 0
    r["cb"][0][5] = int(r["cb"][0][5]) + 16
    return 1


def m_neg_buffer_offsets(recs):
    """Byte offsets into dynamic vertex/index buffers are ring allocation.

    Where a draw's data landed depends on how much was drawn before it, which is
    the non-determinism this tool exists to survive. Stride is configuration;
    offset is not. S6 must therefore flag a stride change and ignore this.
    """
    n = 0
    for r in recs:
        ib = r.get("ib")
        if ib and len(ib) >= 3:
            ib[2] = int(ib[2]) + 2
            n += 1
        for e in (r.get("vb") or []):
            if len(e) >= 4:
                e[3] = int(e[3]) + 2
                n += 1
    return n


def m_srv_nummips(recs):
    """Breaks the S4 texture-derived invariant srv.numMips == texture mips."""
    r = _first_where(recs, lambda r: any(len(e) >= 10 and ":m" in (e[3] or "")
                                         for e in (r.get("srv") or [])))
    if not r:
        return 0
    for e in r["srv"]:
        if len(e) >= 10 and ":m" in (e[3] or ""):
            e[6] = int(e[6]) + 1
            return 1
    return 0


def m_sampler_maxlod(recs):
    """Breaks the S4 invariant maxLOD == mips-1 (S2 excludes maxLOD by design)."""
    n = 0
    for r in recs:
        srv_slots = {(e[0], e[1]) for e in (r.get("srv") or []) if len(e) >= 4}
        for s in (r.get("sampler") or []):
            if len(s) >= 12 and (s[0], s[1]) in srv_slots:
                s[11] = float(s[11]) + 3.0
                n += 1
                if n >= 1:
                    return n
    return n


def m_bound_texture(recs):
    """A different texture bound -- S5's subject."""
    r = _first_where(recs, lambda r: r.get("srv") and (r["srv"][0][3] or "").startswith("tex:"))
    if not r:
        return 0
    r["srv"][0][3] = r["srv"][0][3] + ":MUTATED"
    return 1


def m_rt_format(recs):
    r = _first_where(recs, lambda r: (r.get("om") or {}).get("rts"))
    if not r:
        return 0
    r["om"]["rts"][0][2] = "R10G10B10A2_UNORM_MUTATED"
    return 1


def m_scissor(recs):
    r = _first_where(recs, lambda r: r.get("scissors"))
    if not r:
        # Scissors are commonly all-disabled; synthesise one so the screen is
        # HANDED a positive shape rather than being asked to find an absence.
        recs[0]["scissors"] = [[0, 0, 64, 64]]
        return 1
    r["scissors"][0][2] = int(r["scissors"][0][2]) + 7
    return 1


# ── NEGATIVE controls: subjects the oracle MUST NOT flag ─────────────────────

def m_neg_eid(recs):
    for r in recs:
        r["eid"] = int(r.get("eid", 0)) + 100000
    return len(recs)


def m_neg_idx(recs):
    for r in recs:
        r["idx"] = int(r.get("idx", 0)) + 7
    return len(recs)


def m_neg_name(recs):
    for r in recs:
        r["name"] = "MutatedActionName(...)"
    return len(recs)


def m_neg_cb_contents(recs):
    """Per-object transforms genuinely differ run to run."""
    n = 0
    for r in recs:
        for e in (r.get("cb") or []):
            if len(e) >= 7:
                e[6] = "deadbeefdeadbeef"
                n += 1
    return n


def m_neg_draw_counts(recs):
    """Index/instance counts follow what is on screen, not what the port does."""
    n = 0
    for r in recs:
        d = r.get("draw")
        if d:
            d[0] = int(d[0]) + 3
            n += 1
    return n


def m_neg_dynamic_buffer_identity(recs):
    """Which dynamic buffer object got allocated is allocation order, not config."""
    n = 0
    for r in recs:
        for e in (r.get("vb") or []):
            if len(e) >= 2 and isinstance(e[1], str):
                e[1] = "buf:999999"
                n += 1
        ib = r.get("ib")
        if ib and isinstance(ib[0], str):
            ib[0] = "buf:999999"
            n += 1
    return n


def m_neg_drop_repeated_draws(recs):
    """Simulate the measured non-determinism: fewer things on screen.

    Only draws whose CONFIGURATION still occurs elsewhere are dropped, because
    a set-based screen is entitled to notice a configuration that vanished
    entirely -- that is a real set difference, not noise.
    """
    from collections import Counter
    counts = Counter(TO.CS.config_hash(r) for r in recs)
    rng = random.Random(1234)
    keep, dropped = [], 0
    for r in recs:
        h = TO.CS.config_hash(r)
        if counts[h] > 1 and dropped < max(1, len(recs) // 10) and rng.random() < 0.25:
            counts[h] -= 1
            dropped += 1
            continue
        keep.append(r)
    recs[:] = keep
    return dropped


def m_neg_cb_buffer_size(recs):
    """Constant-buffer byte SIZE of a dynamic ring allocation is not config.

    Recorded and printed, and listed in trace_oracle.NON_VERDICT as
    `cbBufferSizes`, so S3 never promotes it to a finding. If another screen
    catches it, that is a real finding about the oracle and is reported as such
    rather than silently tolerated.
    """
    n = 0
    for r in recs:
        for e in (r.get("cb") or []):
            if len(e) >= 4:
                e[3] = "buf:424242"
                n += 1
    return n


POSITIVES = [
    Mutation("raster.cull", "positive", "cull mode flipped on one draw", m_cull),
    Mutation("depth.writes", "positive", "depth writes flipped on one draw", m_depth_writes),
    Mutation("blend.enable", "positive", "blend enable flipped on one draw", m_blend_enable),
    Mutation("blend.srcFactor", "positive", "colour blend source factor changed", m_blend_factor),
    Mutation("sampler.addressU", "positive", "address mode changed on one sampler", m_sampler_address),
    Mutation("sampler.filter", "positive", "filter changed on one sampler", m_sampler_filter),
    Mutation("shader.ps", "positive", "a different pixel shader bound", m_ps_shader),
    Mutation("srv.format", "positive", "SRV view format changed", m_srv_format),
    Mutation("om.depthTarget", "positive", "depth target unbound on one draw", m_drop_depth_target),
    Mutation("viewport.width", "positive", "viewport halved on one draw", m_viewport),
    Mutation("topology", "positive", "primitive topology changed", m_topology),
    Mutation("layout.byteOffset", "positive", "input layout element offset +4", m_layout_offset),
    Mutation("vb.stride", "positive", "vertex buffer stride +4", m_vb_stride),
    Mutation("cb.bindSize", "positive", "constant-buffer bind size +16 "
             "(S3's only positive control)", m_cb_bind_size),
    Mutation("srv.numMips", "positive", "S4 invariant broken: numMips != tex mips", m_srv_nummips),
    Mutation("sampler.maxLOD", "positive", "S4 invariant broken: maxLOD != mips-1", m_sampler_maxlod),
    Mutation("srv.texture", "positive", "a different texture bound", m_bound_texture),
    Mutation("om.rtFormat", "positive", "render target view format changed", m_rt_format),
    Mutation("scissor.rect", "positive", "scissor rectangle changed", m_scissor),
]

NEGATIVES = [
    Mutation("eid renumber", "negative", "every event id shifted", m_neg_eid),
    Mutation("idx renumber", "negative", "every record index shifted", m_neg_idx),
    Mutation("action name", "negative", "RenderDoc action labels rewritten", m_neg_name),
    Mutation("cb contents", "negative", "every constant-buffer hash changed", m_neg_cb_contents),
    Mutation("draw counts", "negative", "index counts changed on every draw", m_neg_draw_counts),
    Mutation("dynamic buffer id", "negative", "vb/ib buffer identity rewritten",
             m_neg_dynamic_buffer_identity),
    Mutation("buffer byte offsets", "negative",
             "vb/ib byte offsets shifted (dynamic ring allocation)",
             m_neg_buffer_offsets, exempt=["S3_fieldDiff"]),
    Mutation("fewer draws", "negative", "repeated-config draws dropped (the measured "
             "non-determinism)", m_neg_drop_repeated_draws,
             exempt=["S3_fieldDiff"]),
    # SETTLED BY MEASUREMENT, and it went the other way from the first guess.
    # This began as an `open` control on the argument that a D3D11 constant
    # buffer's byte length is fixed at creation, so two agreeing renderers must
    # allocate the same size. Three same-build captures (ctrlA/B/C, identical
    # seed, pin, command line and exe md5) then measured cbBufferSizes differing
    # on 30.9-37.4% of aligned pairs between IDENTICAL runs. It is a dynamic ring
    # allocation. NEGATIVE, and `cbBufferSizes` is now in trace_oracle.NON_VERDICT.
    Mutation("cb buffer size", "negative", "constant-buffer allocation size rewritten",
             m_neg_cb_buffer_size),
]


# ── harness ──────────────────────────────────────────────────────────────────

def findings_for(base, mutated):
    """S3 is run UNFLOORED here, and only here.

    trace_oracle suppresses S3 without a `--control` pair. In this harness the
    control pair IS the trace against an exact copy of itself, so the floor is
    provably zero -- the identity baseline below asserts exactly that. That makes
    S3 scoreable here and nowhere else.
    """
    sc = TO.run_screens(base, mutated)
    f, flagged = TO.screen_findings(sc, control=None, margin=0.0, s3_unfloored=True)
    return f, sc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="a REAL trace.jsonl from dump_state.py")
    ap.add_argument("--json", dest="json_out", default=None)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    _, base, errs = TO.load(args.trace)
    print(f"=== trace_selftest on {os.path.basename(args.trace)} ===")
    print(f"subject: {len(base)} draw records"
          + (f" ({len(errs)} extraction errors excluded)" if errs else ""))
    if not base:
        print("REFUSING: the trace is empty. Every control would 'pass' trivially.")
        return 3

    cen = TO.examined_census(base, "subject")
    dead = TO.dead_fields(cen)
    print(f"fields present: {len(cen['fields'])}"
          + (f";  NOT-EXAMINED (0 populated): {', '.join(dead)}" if dead
             else ";  all populated in >=1 draw"))

    # ── identity baseline: the oracle must be silent on a trace vs itself ────
    base_f, _ = findings_for(base, copy.deepcopy(base))
    print("\n-- identity baseline (trace vs an exact copy) --")
    print("  " + "  ".join(f"{k}={base_f[k]}" for k in SCREENS))
    if any(base_f.values()):
        print("  !! NON-ZERO on identity. Every score below is meaningless.")
        return 3
    print(f"  all {len(SCREENS)} screens silent, as required.")

    rows = []
    for m in POSITIVES + NEGATIVES:
        mutated, ch_rec, ch_fld, touched = m.apply(base)
        if ch_rec == 0 and ch_fld == 0:
            rows.append({"name": m.name, "kind": m.kind, "note": m.note,
                         "changedRecords": 0, "changedFields": 0,
                         "inert": True, "caughtBy": [], "verdict": "INERT"})
            continue
        f, _sc = findings_for(base, mutated)
        caught = [k for k in SCREENS if (f[k] or 0) > (base_f[k] or 0)]
        unexempt = [k for k in caught if k not in m.exempt]
        if m.kind == "positive":
            verdict = "CAUGHT" if caught else "MISSED"
        elif m.kind == "open":
            verdict = "OPEN"
        else:
            verdict = "clean" if not unexempt else "FALSE-POSITIVE"
        rows.append({"name": m.name, "kind": m.kind, "note": m.note,
                     "changedRecords": ch_rec, "changedFields": ch_fld,
                     "inert": False, "caughtBy": caught,
                     "exempt": sorted(m.exempt), "whyOpen": m.why_open,
                     "verdict": verdict,
                     "findings": {k: f[k] for k in SCREENS}})

    # ── report ──────────────────────────────────────────────────────────────
    print("\n-- POSITIVE controls (a subject the oracle DOES compare) --")
    print(f"  {'mutation':<20}{'recs':>6}{'flds':>6}  {'verdict':<14} caught by")
    for r in rows:
        if r["kind"] != "positive":
            continue
        print(f"  {r['name']:<20}{r['changedRecords']:>6}{r['changedFields']:>6}  "
              f"{r['verdict']:<14} {','.join(r['caughtBy']) or '-'}")

    print("\n-- NEGATIVE controls (a subject the oracle MUST NOT flag) --")
    print(f"  {'mutation':<20}{'recs':>6}{'flds':>6}  {'verdict':<14} flagged by")
    for r in rows:
        if r["kind"] != "negative":
            continue
        ex = f"   [exempt: {','.join(r['exempt'])}]" if r.get("exempt") else ""
        print(f"  {r['name']:<20}{r['changedRecords']:>6}{r['changedFields']:>6}  "
              f"{r['verdict']:<14} {','.join(r['caughtBy']) or '-'}{ex}")

    opens = [r for r in rows if r["kind"] == "open"]
    if opens:
        print("\n-- OPEN controls (stability unknown; NOT scored either way) --")
        for r in opens:
            print(f"  {r['name']:<20}{r['changedRecords']:>6}{r['changedFields']:>6}  "
                  f"{'OPEN':<14} would be flagged by: {','.join(r['caughtBy']) or '-'}")
            for line in (r["whyOpen"] or "").split(". "):
                if line.strip():
                    print(f"      {line.strip().rstrip('.')}.")

    pos = [r for r in rows if r["kind"] == "positive" and not r["inert"]]
    neg = [r for r in rows if r["kind"] == "negative" and not r["inert"]]
    opn = [r for r in rows if r["kind"] == "open" and not r["inert"]]
    inert = [r for r in rows if r["inert"]]
    caught = [r for r in pos if r["verdict"] == "CAUGHT"]
    fp = [r for r in neg if r["verdict"] == "FALSE-POSITIVE"]

    print("\n-- PER-SCREEN RECALL (the union score is deliberately not printed alone) --")
    for k in SCREENS:
        n = len([r for r in pos if k in r["caughtBy"]])
        flag = "  <- CAUGHT NOTHING: this screen is unvalidated by this run" if n == 0 else ""
        print(f"  {k:<16} caught {n}/{len(pos)} positive controls{flag}")

    nos3 = [r for r in pos if [k for k in r["caughtBy"] if k != "S3_fieldDiff"]]
    print(f"\n  CAUGHT WITHOUT S3: {len(nos3)}/{len(pos)}")
    print("    S3 is suppressed unless a same-build control pair is supplied, so this")
    print("    is the recall a lane with ONE capture per arm actually gets. A positive")
    print("    control caught by S3 alone is invisible in that configuration.")
    only_s3 = [r["name"] for r in pos if r["caughtBy"] == ["S3_fieldDiff"]]
    if only_s3:
        print(f"    CAUGHT BY S3 ALONE (invisible without --control): {', '.join(only_s3)}")

    print("\n-- SUMMARY --")
    print(f"  positive controls: {len(caught)}/{len(pos)} CAUGHT"
          + (f"   MISSED: {', '.join(r['name'] for r in pos if r['verdict'] == 'MISSED')}"
             if len(caught) != len(pos) else ""))
    print(f"  negative controls: {len(neg) - len(fp)}/{len(neg)} clean"
          + (f"   FALSE POSITIVES: {', '.join(r['name'] for r in fp)}" if fp else ""))
    print(f"  EFFECTIVENESS: every scored mutation changed >=1 field value; "
          f"{len(inert)} inert mutation(s) excluded"
          + (f" ({', '.join(r['name'] for r in inert)})" if inert else ""))
    print(f"  open controls:     {len(opn)} unresolved; each needs a same-build "
          f"control pair")
    print(f"  total field values altered across all controls: "
          f"{sum(r['changedFields'] for r in rows)}")

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as fh:
            json.dump({"trace": os.path.basename(args.trace), "draws": len(base),
                       "baseline": base_f, "rows": rows}, fh, indent=1)
        print(f"\nwrote {args.json_out}")

    bad = (len(caught) != len(pos)) or bool(fp)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
