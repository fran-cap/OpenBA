#!/usr/bin/env python3
"""Flag a quoted count in a wave document that names no population.

WAVE 21 ITEM 13.  READ §"VERDICT" BELOW BEFORE USING THIS.

WHAT IT IS FOR
--------------
WAVE20_CLOSED §5.10: the denominator failure appeared at FOUR levels in one
wave -- in an instrument, in a lane, in the close, and inside the close's own
self-verification -- and **every denominator caught that wave was caught by a
person asking.**  That is a structural gap rather than a run of bad luck, and
Wave 20 named the cheapest possible probe for it: a screen that flags a quoted
count with no visible population.  This is that probe, built in order to find
out whether the gap is mechanisable.

WHAT "NEARBY" MEANS, AND WHY IT IS NOT A LINE COUNT
---------------------------------------------------
The window is the enclosing BLOCK: a blank-line-delimited paragraph, a whole
markdown table, or a whole fenced code block.  It is NOT "N lines above".

That choice is the direct lesson of
``reference_documenting_a_check_can_delete_it.md`` -- adding a 12-line comment
above a call site pushed it out of a checker's fixed 6-line window and deleted
the check, which then read as a pass -- and of
``reference_checker_pairs_across_function_boundaries.md``, where scanning back
N raw lines paired a number with a population from an unrelated function and
manufactured confident wrong findings.  A block is derived from the document's
own structure, so it grows when the author writes more and it never crosses
into the next paragraph.  It can still be wrong (an author may legitimately
state a population one paragraph earlier), and that is measured below rather
than assumed.

WHAT IT READS
-------------
Markdown text.  It has no idea what any number MEANS.  It cannot tell a count
from a date, a slot index, an address, a section number or a price, beyond the
syntactic filters listed in SKIP below -- and it cannot tell a TRUE denominator
failure from a number that needs none.

VERDICT -- THIS SCREEN DOES NOT WORK, AND THAT IS THE FINDING
--------------------------------------------------------------
Scored by Wave 21 lane CLASS against a HAND-READ sample: WAVE21_KICKOFF.md in
full, plus WAVE20_CLOSED.md §5.7-§5.10 and §6-§8 in full.

    flags raised inside the hand-read sample     16
    of those, real denominator failures           0    PRECISION  0 / 16
    real denominator failures found by hand       3
    of those, flagged by this screen              0    RECALL     0 / 3

Tree-wide it flags 6094 of 11622 numeric claims across 207 WAVE*.md files --
52%.  A screen that flags half its input is not a work list, and an INFLATING
blind spot MANUFACTURES work
(reference_a_screen_that_cannot_name_its_subject).

**Why it fails is the useful part, and it is not a tuning problem.** All three
real failures were quantities spelled as ENGLISH WORDS -- "paid for that three
times", "three items in this position were stale", "the transitive half was
four short" -- with no digit anywhere for a numeric screen to catch.  And all
sixteen false positives were digits whose population IS stated, in prose, one
clause away ("the 29 unported Graphics rows", "ours 5 / slots 10", "Items 7, 9
and 10").  The discriminating signal is not syntactic in either direction, so
no filter, window or skip-list moves these numbers: the screen would have to
understand the sentence.

KEEP THIS FILE.  Deleting it would leave the verdict as prose, which is the
exact decay this lane exists to fight.  Re-run it before proposing a screen of
this shape again.

WHERE THE MECHANISABLE VERSION LIVES INSTEAD
--------------------------------------------
Enforce the denominator where the number is PRODUCED, not where it is QUOTED.
A tool's output is machine-readable and its author is one place; a wave doc is
neither.  That approach has already worked in this tree -- Wave 18's six
instruments were repaired by making each print what it could not see.  Note
the asymmetry: Wave 20 §5.10's four denominator failures were in an
instrument, a lane, a close, and a self-verification -- and only the FIRST of
those four is reachable by a tool at all.

Usage:  python tools/denominator_screen.py [--root <repo root>]
                                           [--glob docs/history/runtime_port/WAVE*.md]
                                           [--list]
"""

import argparse
import glob as globmod
import os
import re
import sys

# A candidate numeric claim: a bare integer of 2+ digits (scan_file drops a bare
# single digit) or a percentage.
NUM_RE = re.compile(r"(?<![\w.$])(\d{1,3}(?:,\d{3})+|\d+)(%?)(?![\w.])")

# SKIP -- syntactic shapes that are not counts.  Each one is a deliberate
# blind spot in the HIDING direction (it can only lose a real failure, never
# manufacture one), which is the safer direction for a screen nobody audits.
SKIP_CONTEXT = [
    re.compile(r"0x[0-9a-fA-F]+"),          # addresses
    re.compile(r"§\s*[\d.]+"),               # section numbers
    re.compile(r"\bWave\s+\d+", re.I),       # wave numbers
    re.compile(r"\b20\d\d-\d\d-\d\d"),       # dates
    re.compile(r"\bv?\d+\.\d+\.\d+"),        # versions
    re.compile(r"`[0-9a-f]{7,40}`"),         # commit shas
    re.compile(r"\.(md|py|cpp|h|inc|txt|csv):\d+"),   # file:line
    re.compile(r"\bslot\s*\d+", re.I),       # slot indices
    re.compile(r"\bitem\s*\d+", re.I),       # item numbers
]

# A population is NAMED if any of these appear in the same block.
POP_RE = [
    re.compile(r"\b\d+\s*(?:of|/|out of)\s*\d+\b", re.I),
    re.compile(r"\bof\s+(?:the\s+)?\d+\b", re.I),
    re.compile(r"\b(?:denominator|population|subject|coverage|tracked|scanned|"
               r"examined|sample|out of|total|corpus)\b", re.I),
    re.compile(r"\b\d+\s*/\s*\d+\b"),
]


def blocks(text):
    """(start line, block text) for each paragraph / table / fenced block."""
    out, cur, start, fence = [], [], 1, False
    for i, line in enumerate(text.splitlines(), 1):
        if line.strip().startswith("```"):
            fence = not fence
            cur.append(line)
            if not fence:
                out.append((start, "\n".join(cur)))
                cur, start = [], i + 1
            continue
        if fence:
            cur.append(line)
            continue
        if not line.strip():
            if cur:
                out.append((start, "\n".join(cur)))
            cur, start = [], i + 1
            continue
        if not cur:
            start = i
        cur.append(line)
    if cur:
        out.append((start, "\n".join(cur)))
    return out


def scan_file(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    flags, considered = [], 0
    for start, body in blocks(text):
        named = any(p.search(body) for p in POP_RE)
        for line_off, line in enumerate(body.splitlines()):
            if any(s.search(line) for s in SKIP_CONTEXT):
                continue
            for m in NUM_RE.finditer(line):
                val, pct = m.group(1), m.group(2)
                if not pct and len(val.replace(",", "")) < 2:
                    continue          # a bare 0..9 is almost never a claim
                considered += 1
                if not named:
                    flags.append((start + line_off, line.strip(), val + pct))
    return flags, considered


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    ap.add_argument("--glob", default="docs/history/runtime_port/WAVE*.md")
    ap.add_argument("--list", action="store_true",
                    help="print every flagged line, not just the per-file tally")
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    print("SUBJECT ROOT : %s" % root)
    print("GLOB         : %s   (a FREE PARAMETER -- this screen can be aimed)"
          % args.glob)
    print("WINDOW       : the enclosing markdown BLOCK (paragraph / table /")
    print("               fenced block).  Not a line count -- see the module")
    print("               docstring for the two recorded failures that rules out.")
    print("EVIDENCE     : syntax only.  This screen does not know what a number")
    print("               MEANS and cannot tell a claim that NEEDS a denominator")
    print("               from one that does not.  Read VERDICT in the docstring.")
    print("=" * 78)

    paths = sorted(globmod.glob(os.path.join(root, args.glob)))
    tot_f = tot_c = 0
    for p in paths:
        flags, considered = scan_file(p)
        tot_f += len(flags)
        tot_c += considered
        if flags:
            print("%-52s %4d flagged of %4d numeric claims"
                  % (os.path.relpath(p, root), len(flags), considered))
            if args.list:
                for ln, line, val in flags:
                    print("    %5d  [%s]  %s" % (ln, val, line[:100]))
    print("=" * 78)
    print("FILES        : %d matched the glob" % len(paths))
    print("POPULATION   : %d numeric claims survived the SKIP filters" % tot_c)
    print("FLAGGED      : %d (%.0f%%) sit in a block that names no population"
          % (tot_f, 100.0 * tot_f / tot_c if tot_c else 0.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
