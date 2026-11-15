#!/usr/bin/env python3
r"""Enumerate every in-DLL reference to the Wave 23 lane VIDVT subjects.

WHAT THIS IS AND WHY IT IS NOT rt_singleton_refs.py
---------------------------------------------------
``tools/rt_singleton_refs.py`` (Wave 15) answers exactly the question rule 6
asks -- "who READS this static in the DLL, in a body we have NOT ported?" -- but
its population is hard-wired to the twenty ``?_singleton@`` rows, and it asserts
that count.  Lane VIDVT's subjects are

  * 4 ``?_first*@`` statics  (heads of intrusive lists of live GPU resources) and the
    2 ``?_singleton@`` statics of Video::Configuration and Video::Device,
  * 12 ``??_7*Data@Video@@6B@`` base vtables (whose triage question is not
    "who reads it" but "who STAMPS it" -- i.e. LEA references),

so this is the same MECHANISM aimed at a different population, with LEA
promoted from a footnote to the headline.  It renders NO verdicts.

METHOD -- deliberately identical to rt_singleton_refs.py's, for comparability
  route SCAN     candidate generation: for every byte offset p in .text and
                 every trailing-immediate size t in {0,1,2,4},
                 base + p + 4 + t + disp32(p) is a candidate target.  No
                 RIP-relative instruction form can escape this; it
                 over-generates, which is the safe direction.
  route BACKWARD confirmation: decode one instruction at every start in
                 [p-15, p-1] with capstone and keep the decode whose own
                 RIP-relative target is the subject.  This is what makes the
                 trailing-imm32 quirk a non-issue (the decoder computes the end
                 of the instruction rather than assuming it) -- see
                 rt_singleton_refs.py's CONTROL 1 negative control, which this
                 tool re-runs as its own control 1.
  route SWEEP    independent linear disassembly of .text, restarting one byte
                 later on an undecodable byte.  Set differences with BACKWARD
                 are printed per row.

A candidate no route can decode is emitted as UNKNOWN-FORM, never dropped: a
dropped reference is a false SAFE.

CONTROLS
  control 1 (positive, borrowed)  the five known ``?_singleton@SaveLoad@Save@@``
      references at their five known VAs, one of which carries a trailing imm32.
      Re-run here because this tool has its own copy of the scan, and a control
      that only ever ran against the OTHER copy proves nothing about this one.
  control 2 (negative)  the same run with candidate generation restricted to
      trailing-imm=0.  It MUST lose the imm32 reference; if it does not, the
      quirk handling is decoration and every count here is suspect.
  control 3 (blind-spot)  a subject address chosen to have NO references
      (a .rdata address in the middle of a string table).  It must report 0,
      which is what distinguishes "this row has no stampers" from "the parser
      is dead" -- the failure recorded in
      reference_absence_screen_must_be_handed_a_shape.md.

PORT STATE of an owning body is read from a runtime_census.py --csv, passed
with --census.  That is screen R1 only; this tool does NOT re-implement screen
R2, and says so in its output rather than implying a two-screen answer.

USAGE
    python tools/video_data_refs.py --census <census.csv>
Needs capstone + pefile: run with the SYSTEM python (the project venv has
pefile but not capstone), exactly as rt_singleton_refs.py requires.
"""
import argparse
import bisect
import csv
import struct
import sys

import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DLL = retail("Runtime-x64.dll")
BASE = 0x180000000

# --- subjects -------------------------------------------------------------
VTABLES = [
    (0x180158A28, "??_7RenderBufferData@Video@@6B@"),
    (0x180158A48, "??_7VideoProgramData@Video@@6B@"),
    (0x180158A60, "??_7RenderStateData@Video@@6B@"),
    (0x180158A78, "??_7PixelProgramData@Video@@6B@"),
    (0x180158A90, "??_7VertexProgramData@Video@@6B@"),
    (0x180158AA8, "??_7VertexLayoutData@Video@@6B@"),
    (0x180158AC0, "??_7VertexBufferData@Video@@6B@"),
    (0x180158AF8, "??_7IndexBufferData@Video@@6B@"),
    (0x180158B30, "??_7ConstantBufferData@Video@@6B@"),
    (0x180158B50, "??_7ImageBufferData@Video@@6B@"),
    (0x180158B90, "??_7ContextData@Video@@6B@"),
    (0x180158C30, "??_7DeviceData@Video@@6B@"),
]
STATICS = [
    (0x1801FE780, "?_firstSurface@ImageBuffer@Video@@0PEAV12@EA"),
    (0x1801FE788, "?_firstBuffer@IndexBuffer@Video@@0PEAV12@EA"),
    (0x1801FE790, "?_firstRenderBuffer@RenderBuffer@Video@@0PEAV12@EA"),
    (0x1801FE798, "?_firstBuffer@VertexBuffer@Video@@0PEAV12@EA"),
    (0x1801FE7A0, "?_singleton@Configuration@Video@@0PEAV12@EA"),
    (0x1801FE7A8, "?_singleton@Device@Video@@0PEAV12@EA"),
]
CONTROL1 = [
    (0x1800C3D9B, "WRITE"), (0x1800C3CA6, "WRITE"),
    (0x1800DC3E9, "READ"), (0x1800DCC1D, "READ"), (0x180030D40, "READ"),
]
CONTROL1_SLOT = 0x1801FC6F0  # ?_singleton@SaveLoad@Save@@0PEAV12@EA (read out of the export table)


def load(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    secs = []
    off = pe + 24 + optsz
    for i in range(nsec):
        s = data[off + i * 40: off + (i + 1) * 40]
        name = s[:8].rstrip(b"\0").decode()
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", s, 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    return data, secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--census")
    args = ap.parse_args()

    try:
        import capstone
    except ImportError:
        print("capstone missing -- run with the SYSTEM python, not the venv")
        return 3
    sys.path.insert(0, __file__.rsplit("\\", 1)[0])
    from pe_exports import exports

    data, secs = load(DLL)
    text = [s for s in secs if s[0] == ".text"][0]
    tname, tva, tvs, tra, trs = text
    tbase = BASE + tva
    blob = data[tra: tra + trs]

    ex = exports(DLL)
    byaddr = {}
    for rva, n in ex:
        byaddr.setdefault(BASE + rva, []).append(n)
    eaddrs = sorted(byaddr)

    def owner(va):
        i = bisect.bisect_right(eaddrs, va) - 1
        return byaddr[eaddrs[i]][0] if i >= 0 else "?"

    # port state, screen R1 only
    ported = {}
    if args.census:
        with open(args.census, newline="") as f:
            for r in csv.DictReader(f):
                ported[r["key"]] = r["ported"] == "1"

    def pstate(sym):
        if sym in ported:
            return "PORTED(R1)" if ported[sym] else "UNPORTED(R1)"
        return "NOT-IN-CENSUS"

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    # ---- route SWEEP: linear disassembly of .text, restarting one byte later
    # on an undecodable byte.  Its product used here is the BOUNDARY MAP.
    # It is load-bearing, not decoration: a disp32 at p is typically reachable
    # from three or four different starts in [p-15, p-1] that all decode and
    # all compute the same RIP target (an over-long REX form, a 32-bit-operand
    # form, ...).  Taking the first was wrong for 1 of control 1's 5 known
    # references -- the control is the only reason that was visible.
    sweep_starts = set()
    _off = 0
    _n = len(blob)
    while _off < _n:
        try:
            _ins = next(md.disasm(blob[_off:_off + 16], tbase + _off))
        except StopIteration:
            _off += 1
            continue
        sweep_starts.add(_off)
        _off += _ins.size
    ambiguous = [0]

    def scan(target, imms=(0, 1, 2, 4)):
        """route SCAN -> route BACKWARD. Returns list of (va, kind, disasm)."""
        cands = set()
        n = len(blob)
        for p in range(0, n - 4):
            d = struct.unpack_from("<i", blob, p)[0]
            for t in imms:
                if tbase + p + 4 + t + d == target:
                    cands.add(p)
        out = []
        for p in sorted(cands):
            hits = []
            for s in range(max(0, p - 15), p):
                try:
                    ins = next(md.disasm(blob[s:s + 16], tbase + s))
                except StopIteration:
                    continue
                # does this instruction's own RIP-relative operand hit target?
                # the disp32 field ends at p+4, so the instruction must not end
                # BEFORE that.  `<=` here was a real off-by-one: the commonest
                # form (7-byte mov, disp at s+3, no trailing immediate) ends at
                # exactly p+4 and was silently rejected, which turned 4 of
                # control 1's 5 known references into UNKNOWN-FORM.  The control
                # caught it; without the control the row counts would have read
                # as plausible small numbers.
                if s + ins.size < p + 4:
                    continue
                found = False
                for op in ins.operands:
                    if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                        if tbase + s + ins.size + op.mem.disp == target:
                            found = True
                if not found:
                    continue
                mn = ins.mnemonic
                # classification from the DECODED operand list, not from string
                # shape: LEA takes the address, a RIP-relative DESTINATION
                # operand is a store, anything else that touches it is a load.
                # cmp/test write no memory however their operands are ordered.
                dst = ins.operands[0] if ins.operands else None
                dst_is_slot = (dst is not None
                               and dst.type == capstone.x86.X86_OP_MEM
                               and dst.mem.base == capstone.x86.X86_REG_RIP)
                if mn == "lea":
                    kind = "LEA"
                elif mn in ("cmp", "test"):
                    kind = "READ"
                elif dst_is_slot:
                    kind = "WRITE" if mn in ("mov", "movq", "movsd", "movss") else "RMW"
                else:
                    kind = "READ"
                hits.append((s, tbase + s, kind, "%s %s" % (mn, ins.op_str)))
            # AMBIGUITY RESOLUTION against route SWEEP's boundary map.  If more
            # than one start decodes to a reference, keep the one route SWEEP
            # also calls an instruction start; if SWEEP agrees with several (or
            # none), fall back to the LAST, and count it as ambiguous rather
            # than hiding it.
            if len(hits) > 1:
                on = [h for h in hits if h[0] in sweep_starts]
                if len(on) == 1:
                    hits = on
                else:
                    ambiguous[0] += 1
                    hits = [hits[-1]] if not on else [on[-1]]
            if hits:
                out.append(hits[0][1:])
            else:
                out.append((tbase + p, "UNKNOWN-FORM", "(no decode)"))
        return out

    print("subject population: %d vtables + %d statics" % (len(VTABLES), len(STATICS)))
    print("denominator: .text %s .. %s (%d bytes), ONE section; .rdata/.data are"
          % (hex(tbase), hex(tbase + trs), trs))
    print("  NOT scanned for references -- a vtable referenced only from another")
    print("  vtable or a relocation table would be invisible here.")
    print()

    # ---- control 1 / 2 ----
    c1 = scan(CONTROL1_SLOT)
    got = {v for v, k, d in c1}
    want = {v for v, k in CONTROL1}
    print("CONTROL 1 (positive, SaveLoad ground truth): found %d/%d known VAs  %s"
          % (len(want & got), len(want), "GREEN" if want <= got else "RED missing " + str([hex(x) for x in want - got])))
    c2 = scan(CONTROL1_SLOT, imms=(0,))
    got2 = {v for v, k, d in c2}
    lost = want - got2
    print("CONTROL 2 (negative, imm=0 only): lost %d of the %d  %s"
          % (len(lost), len(want), "GREEN (quirk handling is load-bearing)" if lost else "RED (imm handling is decoration)"))
    c3 = scan(0x180165000)
    print("CONTROL 3 (blind-spot, an arbitrary .rdata address): %d refs  %s"
          % (len(c3), "(a 0 here is only meaningful because controls 1/2 are green)"))
    print()

    for label, rows in (("VTABLE (the question is who STAMPS it: LEA)", VTABLES),
                        ("STATIC (the question is who READS it: rule 6)", STATICS)):
        print("=" * 78)
        print(label)
        print("=" * 78)
        for va, sym in rows:
            refs = scan(va)
            kinds = {}
            for _, k, _ in refs:
                kinds[k] = kinds.get(k, 0) + 1
            print("%-52s %s  total=%d %s" % (sym, hex(va), len(refs), kinds))
            unp = 0
            for rva, k, dis in refs:
                o = owner(rva)
                st = pstate(o)
                if st.startswith("UNPORTED") or st == "NOT-IN-CENSUS":
                    unp += 1
                if len(refs) <= 24:
                    print("     %s  %-5s  %-46s %-14s %s" % (hex(rva), k, o[:46], st, dis))
            print("     -> %d of %d references sit in a body this tree has NOT ported"
                  " (screen R1 only; owner = nearest preceding export, which for an"
                  " unexported internal is an OVER-attribution)" % (unp, len(refs)))
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
