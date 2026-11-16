#!/usr/bin/env python3
r"""In-DLL rel32 CALL/JMP scan -- "who still reaches this body WITHOUT a door?"

WAVE 25 LANE DATA.  Written because a DATA row's reader/writer census was about
to produce a WRONG verdict, and this is the half that was missing.

THE QUESTION IT ANSWERS, and why a census cannot
    `tools/runtime_census.py` marks an export `ported` when a DOOR is registered
    for it.  That is not the same as "retail's copy of this function no longer
    runs".  We intercept only at doors, and an in-DLL `rel32` call is not one:
    every call site INSIDE Runtime-x64.dll still lands in retail's body, which
    still touches retail's statics.

    Measured specimen: `?SetText@Error@System@@` and `?Display@Error@System@@`
    are both `ported`, and between them they are the ONLY in-DLL referencers of
    `?_errorString@`, `?_stopApplication@` and `?_hideDisplay@`.  Read from the
    census alone, all three rows look like "every consumer is ours -- promote".
    This tool finds **18** in-DLL rel32 sites reaching retail's `SetText` and
    **2** reaching retail's `Display`.  All three rows are HARMFUL.

WHAT IT EXAMINES
    The `.text` section of `C:\GOG Games\Banished\Runtime-x64.dll`, scanning
    every byte offset for an `E8` (call) or `E9` (jmp) whose rel32 lands on the
    target.  It does NOT see indirect calls through a vtable or a pointer, so a
    `0` means "no DIRECT in-DLL call", never "unreachable".

CONTROL
    Pass a target with known in-DLL callers as the first argument.  A scanner
    that returns 0 for everything is indistinguishable from a true 0 unless it
    is handed a positive shape first.
    `?StrAppend@String@System@@SAPEADPEADHPEBD@Z=0x1800c9020` is a good one: it
    has exactly one in-DLL caller, inside `?SetText@Error@`.

USAGE
    python tools/rt_indll_callers.py NAME=0xVA [NAME=0xVA ...]
"""
import struct, sys
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DLL = retail("Runtime-x64.dll")
BASE = 0x180000000
data = open(DLL, 'rb').read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
off = pe + 24 + optsz
for i in range(nsec):
    s = data[off + i * 40: off + (i + 1) * 40]
    if s[:8].rstrip(b"\0") == b".text":
        vsz, vad, rsz, rad = struct.unpack_from("<IIII", s, 8)
blob = data[rad:rad + rsz]
tbase = BASE + vad
n = len(blob)


def callers(target):
    out = []
    for p in range(n - 5):
        op = blob[p]
        if op in (0xE8, 0xE9):
            d = struct.unpack_from("<i", blob, p + 1)[0]
            if tbase + p + 5 + d == target:
                out.append((tbase + p, "call" if op == 0xE8 else "jmp"))
    return out


for name, va in [(a.split('=')[0], int(a.split('=')[1], 0)) for a in sys.argv[1:]]:
    c = callers(va)
    print("%-40s %s : %d in-DLL rel32 site(s)" % (name, hex(va), len(c)))
    for v, k in c[:12]:
        print("      ", hex(v), k)
