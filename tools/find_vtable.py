#!/usr/bin/env python3
"""Find which .rdata vtable contains a given .text address, and at which slot.

This finds the exe .rdata address of a class's vtable.  What we usually know
instead is one
reimplemented method's retail address (e.g. "ScenarioDialog::Slot4 was 0x140078010").
This walks .rdata for that qword and, for each hit, walks BACKWARDS while the
qwords still look like .text pointers to find the table's base.

Usage:  python tools/find_vtable.py 0x140078010 [0x...]
"""
import struct
import sys

import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
EXE = retail("Application-x64.exe")
IMAGE_BASE = 0x140000000
TEXT_LO = 0x140001000
TEXT_HI = 0x140150200


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    off = pe + 24 + optsz
    out = []
    for i in range(nsec):
        s = data[off + i * 40:off + (i + 1) * 40]
        name = s[:8].rstrip(b"\0").decode()
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", s, 8)
        out.append((name, vaddr, vsize, raddr, rsize))
    return out


def main():
    data = open(EXE, "rb").read()
    secs = sections(data)
    rd = [s for s in secs if s[0] == ".rdata"][0]
    _, vaddr, _, raddr, rsize = rd
    for arg in sys.argv[1:]:
        target = int(arg, 16)
        print("== searching .rdata for 0x%x" % target)
        found = False
        for off in range(raddr, raddr + rsize - 8, 8):
            if struct.unpack_from("<Q", data, off)[0] != target:
                continue
            found = True
            base = off
            while base - 8 >= raddr:
                q = struct.unpack_from("<Q", data, base - 8)[0]
                if not (TEXT_LO <= q < TEXT_HI):
                    break
                base -= 8
            va = IMAGE_BASE + vaddr + (base - raddr)
            print("   hit at slot %d of vtable 0x%x" % ((off - base) // 8, va))
        if not found:
            print("   no hit")


if __name__ == "__main__":
    main()
