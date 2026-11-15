#!/usr/bin/env python3
"""Lane SERIAL: dump the shipped bytes of the IO scalar-serialize dispatch family.

Reads Runtime-x64.dll directly (no Ghidra) and prints, for each of the 75
BinaryReader / BinaryWriter / Visitor vtable slots, the raw bytes between its
entry point and the next one.  A family of near-identical bodies is exactly
where a uniform-looking pass hides a wrong one, so this prints the bytes rather
than a summary and lets the eye do the grouping.
"""
import sys

import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DLL = retail("Runtime-x64.dll")
IMAGE_BASE = 0x180000000


def load_sections(data):
    pe_off = int.from_bytes(data[0x3C:0x40], "little")
    assert data[pe_off:pe_off + 4] == b"PE\0\0"
    coff = pe_off + 4
    nsec = int.from_bytes(data[coff + 2:coff + 4], "little")
    opt_size = int.from_bytes(data[coff + 16:coff + 18], "little")
    sec_off = coff + 20 + opt_size
    secs = []
    for i in range(nsec):
        s = sec_off + i * 40
        name = data[s:s + 8].rstrip(b"\0").decode()
        vsize = int.from_bytes(data[s + 8:s + 12], "little")
        vaddr = int.from_bytes(data[s + 12:s + 16], "little")
        rsize = int.from_bytes(data[s + 16:s + 20], "little")
        raddr = int.from_bytes(data[s + 20:s + 24], "little")
        secs.append((name, vaddr, vsize, raddr, rsize))
    return secs


def rva_to_off(secs, rva):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None


def read_at(data, secs, va, n):
    off = rva_to_off(secs, va - IMAGE_BASE)
    if off is None:
        return None
    return data[off:off + n]


TYPES = ["char", "bool", "float", "u64", "i64", "uint", "int",
         "ushort", "short", "uchar", "schar"]

READER = dict(
    cls="BinaryReader",
    getver=0x180023930, skip=0x1800a83f0,
    arr=[0x1800a8410, 0x1800a8460, 0x1800a84b0, 0x1800a8510, 0x1800a8570,
         0x1800a85d0, 0x1800a8630, 0x1800a8690, 0x1800a86f0, 0x1800a8750,
         0x1800a87b0],
    sca=[0x1800a8810, 0x1800a8830, 0x1800a8850, 0x1800a8870, 0x1800a8890,
         0x1800a88b0, 0x1800a88d0, 0x1800a88f0, 0x1800a8910, 0x1800a8930,
         0x1800a8950],
    bytes_=0x1800a8970, end=0x1800a8980)

WRITER = dict(
    cls="BinaryWriter",
    getver=0x180023ab0, skip=0x1800a8b00,
    arr=[0x1800a8b10, 0x1800a8b60, 0x1800a8bb0, 0x1800a8c10, 0x1800a8c70,
         0x1800a8cd0, 0x1800a8d30, 0x1800a8d90, 0x1800a8df0, 0x1800a8e50,
         0x1800a8eb0],
    sca=[0x1800a8f10, 0x1800a8f30, 0x1800a8f50, 0x1800a8f70, 0x1800a8f90,
         0x1800a8fb0, 0x1800a8fd0, 0x1800a8ff0, 0x1800a9010, 0x1800a9030,
         0x1800a9050],
    bytes_=0x1800a9070, end=0x1800a9090)

VISITOR = dict(
    cls="Visitor",
    getver=0x18002b670, skip=0x1800bacc0,
    arr=[0x1800bacd0, 0x1800bace0, 0x1800bacf0, 0x1800bad00, 0x1800bad10,
         0x1800bad20, 0x1800bad30, 0x1800bad40, 0x1800bad50, 0x1800bad60,
         0x1800bad70],
    sca=[0x1800bad80, 0x1800bad90, 0x1800bada0, 0x1800badb0, 0x1800badc0,
         0x1800badd0, 0x1800bade0, 0x1800badf0, 0x1800bae00, 0x1800bae10,
         0x1800bae20],
    bytes_=0x1800bae30, end=0x1800bae40)


def rows(t):
    out = [(f"{t['cls']}::GetVersion", t["getver"]),
           (f"{t['cls']}::Skip", t["skip"])]
    for i, ty in enumerate(TYPES):
        out.append((f"{t['cls']}::Ser[]<{ty}>", t["arr"][i]))
    for i, ty in enumerate(TYPES):
        out.append((f"{t['cls']}::Ser<{ty}>", t["sca"][i]))
    out.append((f"{t['cls']}::SerializeBytes", t["bytes_"]))
    return out


def main():
    data = open(DLL, "rb").read()
    secs = load_sections(data)
    for t in (READER, WRITER, VISITOR):
        r = rows(t)
        starts = [a for _, a in r]
        print(f"\n=== {t['cls']} " + "=" * 60)
        for i, (nm, a) in enumerate(r):
            if nm.endswith("GetVersion"):
                n = 16
            else:
                nxt = starts[i + 1] if i + 1 < len(starts) else t["end"]
                n = nxt - a if 0 < nxt - a <= 0x80 else 16
            b = read_at(data, secs, a, n)
            print(f"{a:#011x} {nm:<34s} {b.hex()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
