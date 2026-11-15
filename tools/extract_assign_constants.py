#!/usr/bin/env python3
"""
tools/extract_assign_constants.py -- recover the DECLARATION TABLE that
?AssignConstants@Context@Video@@AEAAXXZ (0x1800ef3d0) builds.

Same rule as tools/extract_shader_constants.py and for the same reason: the
table is ~20 AddConstant rows over 45 lifted literals, and a row transcribed by
eye is a shader parameter that silently never binds. This walks the SHIPPED
BYTES and prints the sequence of ConstantBuffer calls with the string literals
and immediates that reached them, so the port is transliterated from a
machine-produced listing rather than from a human reading 1975 instructions.

NO DISASSEMBLER IN THE LOOP. Byte patterns are matched directly, among them:

    LEA r64,[rip+disp32]   48/4C  8D  modrm(mod=00,rm=101)  disp32
    MOV R9D,imm32          41 B9 imm32
    CALL rel32             E8 disp32, accepted only when the target is one of
                           the seven addresses in CALLS

so a listing's typo cannot be inherited, and the same tool works with Ghidra
shut down. The AddConstant row count is asserted against the DLL, so a scan that
silently under-reads (the defect the shader-literal tool shipped with first)
fails instead of reporting a short table.

USAGE
    tools/extract_assign_constants.py            # print the table
    tools/extract_assign_constants.py --check    # exit non-zero if it changed
"""

import argparse
import sys

try:
    import pefile
except ImportError:
    sys.exit("pefile is required: use tools/ghidra_mcp/.venv/Scripts/python.exe")

import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DLL = retail("Runtime-x64.dll")
BASE = 0x180000000
FN = 0x1800EF3D0

CALLS = {
    0x1800EED10: "BeginConstants",
    0x1800EEC40: "AddConstant",
    0x1800EEBF0: "EndConstants",
    0x1800EF010: "PlatformCreate",
    0x1800C95D0: "String::Set",
    0x1800C9730: "String::Format",
    # *** THE INLINED BeginConstants. *** MSVC inlined `?BeginConstants@` for
    # some of the seven buffers: the two String assignments and the five scalar
    # stores appear in line, and the only thing left of the call is its TAIL —
    # `LEA RCX,[buffer+0x28] ; CALL 0x18001cab0` (the entry-array clear). Without
    # this row the scan reported four Begins for seven buffers and folded three
    # buffers' literals into one row, which read like a plausible table.
    0x18001CAB0: "BeginConstants[inlined]",
}

# `?AssignConstants@` makes exactly this many AddConstant calls in the shipped
# DLL. Asserted so a scan that silently under-reads fails loudly.
EXPECT_ADDCONSTANT = 20


def read_cstr(pe, va, limit=128):
    rva = va - BASE
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize,
                                                            s.SizeOfRawData):
            data = s.get_data()
            off = rva - s.VirtualAddress
            end = data.find(b"\x00", off)
            if end < 0 or end - off > limit:
                return None
            raw = data[off:end]
            if raw and all(32 <= b < 127 for b in raw):
                return raw.decode("ascii")
            return None
    return None


def scan(pe):
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data()
    tbase = BASE + text.VirtualAddress

    addrs = sorted({BASE + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols
                    if e.name and BASE + e.address > FN})
    if not addrs:
        sys.exit("no export above the function -- cannot bound it")
    fn_end = addrs[0]

    events = []
    i = FN - tbase
    end = min(fn_end - tbase, len(data) - 8)
    while i < end:
        va = tbase + i
        b = data[i]

        # *** EVERY PATTERN IS A PEEK AND THE CURSOR ALWAYS ADVANCES BY ONE. ***
        # The first version of this scan advanced by the matched instruction's
        # length, which is a linear sweep: one false `B8..BF` byte inside
        # another instruction's modrm/displacement desynchronises the stream and
        # the walk then steps OVER real calls. It did exactly that — the first
        # four `BeginConstants` calls vanished and their literals were folded
        # into the next row, and the AddConstant count still came out right, so
        # nothing complained.
        #
        # Peeking cannot desynchronise. It can over-report, which is why every
        # pattern below is narrow enough that a false positive is
        # distinguishable: a CALL is kept only when its target is one of seven
        # exact addresses, an immediate only in the narrow encodings matched
        # below, and a literal only when the bytes at the
        # target really are a NUL-terminated ASCII string.

        # LEA r64,[rip+disp32]
        if b in (0x48, 0x4C) and data[i + 1] == 0x8D and (data[i + 2] & 0xC7) == 0x05:
            disp = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
            s = read_cstr(pe, va + 7 + disp)
            if s is not None:
                events.append((va, "lit", s))

        # MOV R9D,imm32 — AddConstant's 4th argument (the size), and
        # BeginConstants' 4th.
        elif b == 0x41 and data[i + 1] == 0xB9:
            imm = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
            events.append((va, "r9d", imm))

        # MOV dword ptr [rsp+disp8],imm32 — BeginConstants' stack arguments.
        elif b == 0xC7 and data[i + 1] == 0x44 and data[i + 2] == 0x24:
            imm = int.from_bytes(data[i + 4:i + 8], "little", signed=True)
            events.append((va, "stk", (data[i + 3], imm)))

        # ---- patterns added for the PORT (the first version only had to make
        # the table readable; the port needs every immediate that reaches a
        # ConstantBuffer field, and inferring "a float4x4 is obviously 4" is
        # precisely the retyping this file exists to avoid) ----

        # MOV qword ptr [RBP-0x79],imm32 — the SIZE of an INLINED AddConstant's
        # stack element. RBP is RSP+0xc9 here, so RBP-0x79 is RSP+0x50, which
        # is element+0x10; the qword store also zeroes element+0x14 (offset).
        elif b == 0x48 and data[i + 1] == 0xC7 and data[i + 2] == 0x45 \
                and data[i + 3] == 0x87:
            events.append((va, "size", int.from_bytes(data[i + 4:i + 8],
                                                      "little", signed=True)))

        # MOV qword/dword/word ptr [R15+disp32],imm — the scalar fields of an
        # INLINED BeginConstants. R15 is `this` for the whole function.
        elif b == 0x49 and data[i + 1] == 0xC7 and data[i + 2] == 0x87:
            events.append((va, "q[r15]", (
                int.from_bytes(data[i + 3:i + 7], "little"),
                int.from_bytes(data[i + 7:i + 11], "little", signed=True))))
        elif b == 0x41 and data[i + 1] == 0xC7 and data[i + 2] == 0x87:
            events.append((va, "d[r15]", (
                int.from_bytes(data[i + 3:i + 7], "little"),
                int.from_bytes(data[i + 7:i + 11], "little", signed=True))))
        elif b == 0x66 and data[i + 1] == 0x41 and data[i + 2] == 0xC7 \
                and data[i + 3] == 0x87:
            events.append((va, "w[r15]", (
                int.from_bytes(data[i + 4:i + 8], "little"),
                int.from_bytes(data[i + 8:i + 10], "little"))))

        # The same three against RSI, which MSVC parks a buffer base in for the
        # third buffer (LEA RSI,[R15+0x1f78]).
        elif b == 0x48 and data[i + 1] == 0xC7 and data[i + 2] == 0x46:
            events.append((va, "q[rsi]", (
                data[i + 3],
                int.from_bytes(data[i + 4:i + 8], "little", signed=True))))
        elif b == 0x66 and data[i + 1] == 0xC7 and data[i + 2] == 0x46:
            events.append((va, "w[rsi]", (
                data[i + 3], int.from_bytes(data[i + 4:i + 6], "little"))))

        # MOV byte ptr [RSP+disp8],imm8 — BeginConstants' two bool arguments.
        elif b == 0xC6 and data[i + 1] == 0x44 and data[i + 2] == 0x24:
            events.append((va, "stkb", (data[i + 3], data[i + 4])))

        # MOV RCX,[R15+disp32] / MOV EAX,[R15+disp32] — the SLOT CHAIN. Each
        # buffer's +0x14 is `previous.+0x14 + 1`, read from the PREVIOUS
        # buffer's fields (or from its companion's vtable slot 3 when it has
        # one), so these two reads are what say which buffer precedes which.
        elif b == 0x49 and data[i + 1] == 0x8B and data[i + 2] == 0x8F:
            events.append((va, "prev.companion",
                           int.from_bytes(data[i + 3:i + 7], "little")))
        elif b == 0x41 and data[i + 1] == 0x8B and data[i + 2] == 0x87:
            events.append((va, "prev.slot",
                           int.from_bytes(data[i + 3:i + 7], "little")))
        # ADD R15,imm32 — MSVC rebases `this` for the last buffer.
        elif b == 0x49 and data[i + 1] == 0x81 and data[i + 2] == 0xC7:
            events.append((va, "this+=",
                           int.from_bytes(data[i + 3:i + 7], "little")))

        # LEA RCX/RSI/RBX,[R15+disp32] — which BUFFER the next call acts on.
        # An entry-array LEA is buffer+0x28; a plain buffer LEA is the base.
        elif b == 0x49 and data[i + 1] == 0x8D and data[i + 2] in (0x8F, 0xB7,
                                                                  0x9F):
            reg = {0x8F: "rcx", 0xB7: "rsi", 0x9F: "rbx"}[data[i + 2]]
            events.append((va, "lea[r15]", (
                reg, int.from_bytes(data[i + 3:i + 7], "little"))))

        # CALL rel32, kept only when it lands on one of the seven.
        elif b == 0xE8:
            disp = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
            target = va + 5 + disp
            if target in CALLS:
                events.append((va, "call", CALLS[target]))

        i += 1

    return events


def build(events):
    """Fold the event stream into rows: each ConstantBuffer call, with the
    literals and the last immediate seen since the previous one."""
    rows = []
    lits = []
    r9 = None
    stk = {}
    for _va, kind, val in events:
        if kind == "lit":
            if not lits or lits[-1] != val:
                lits.append(val)
        elif kind == "r9d":
            r9 = val
        elif kind == "stk":
            stk[val[0]] = val[1]
        elif kind == "call" and val in ("String::Set", "String::Format"):
            pass  # the literal is already captured by the preceding LEA
        elif kind == "call":
            rows.append((val, list(lits), r9, dict(stk)))
            lits = []
            r9 = None
            stk = {}
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--events", action="store_true",
                    help="print the raw event stream in address order")
    a = ap.parse_args()

    pe = pefile.PE(DLL)
    if a.events:
        for va, kind, val in scan(pe):
            print("0x%x  %-9s %r" % (va, kind, val))
        return
    rows = build(scan(pe))

    n_add = sum(1 for r in rows if r[0] == "AddConstant")
    for name, lits, r9, stk in rows:
        stxt = " ".join("[rsp+0x%x]=%d" % (k, v) for k, v in sorted(stk.items()))
        print("%-15s %-44s r9d=%-6s %s"
              % (name, ", ".join(repr(x) for x in lits),
                 "-" if r9 is None else r9, stxt))
    print("--- %d rows, %d AddConstant ---" % (len(rows), n_add))

    if n_add != EXPECT_ADDCONSTANT:
        print("*** AddConstant count is %d, expected %d -- the scan changed or "
              "under-read; do NOT transcribe from this output ***"
              % (n_add, EXPECT_ADDCONSTANT))
        sys.exit(1)


if __name__ == "__main__":
    main()
