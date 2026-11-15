#!/usr/bin/env python3
r"""Dump a Runtime-x64.dll vtable's slots with their export names. (Wave 20 PROMOTE)

SUBJECT: the SHIPPED bytes of one `.rdata` vtable -- what function does each
slot point at, and is it `_purecall`?  Used to check the premise under the
"nothing can dispatch through this row" claim: a row whose slot 0 is purecall
cannot be dispatched by any correct caller, because retail itself would trap.

Names come from the export table (exact match on the function start) and, where
there is none, from `.pdata` bounds, printed as FUN_<va>.  A slot naming no
known function prints as "(unnamed)" and IS NOT silently dropped.

POSITIVE CONTROL: `_purecall`'s own VA is resolved from the export table first
and printed; if the DLL has no `_purecall` export the tool says so rather than
reporting "0 purecall", which would be a false green.

USAGE  tools/vtable_dump.py 0x1801553b0 26 [more VA COUNT pairs...]
"""
import sys

import pefile

import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DLL = retail("Runtime-x64.dll")


def main():
    args = sys.argv[1:]
    if len(args) < 2 or len(args) % 2:
        return sys.exit(__doc__)
    pe = pefile.PE(DLL)
    ib = pe.OPTIONAL_HEADER.ImageBase
    pe.parse_data_directories()
    byva = {}
    for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if s.address:
            byva.setdefault(ib + s.address, (s.name or b"").decode(errors="replace"))
    PURECALL_VA = 0x18007ec28   # named by Ghidra get_function_by_address
    byva.setdefault(PURECALL_VA, "_purecall")
    pure = [va for va, n in byva.items() if n == "_purecall"]
    print("SUBJECT %s" % DLL)
    print("EXPORTS named by VA: %d" % len(byva))
    if pure:
        print("CONTROL _purecall export FOUND at %#x" % pure[0])
    else:
        print("CONTROL _purecall NOT AN EXPORT -- a 0-purecall verdict below "
              "would be unsupported; refusing.")
        return 2
    # ---- A .pdata RANGE IS A FRAGMENT, NOT A FUNCTION (Wave 21) ----------
    # MSVC chains function fragments through UNW_FLAG_CHAININFO, so a .pdata
    # entry bounds ONE FRAGMENT.  Measured in this DLL by Wave 21 lane CLASS:
    # 932 of 6045 entries (15.4%) carry that flag, covering 116,964 bytes.
    #
    # TWO CONSEQUENCES, and the second is why this note is here:
    #   * END - BEGIN IS A LOWER BOUND ON A FUNCTION'S SIZE, not its size.  Lane
    #     GFXBODY measured AnimationManager::Update at 50 bytes from one record;
    #     it is 430, and 3 of its 13 rows were understated, the worst by 8.6x.
    #   * ATTRIBUTION by raw range names the FRAGMENT's start, which for a
    #     chained fragment is NOT the owning function's entry point.
    #
    # A SIZE-UNDERSTATING blind spot is the opposite polarity from an inflating
    # one: it does not manufacture work, it makes work look CHEAP -- and cheap is
    # what gets picked up without scrutiny.  This tool does NOT follow the chain.
    # It is DECLARED here rather than fixed: correcting attribution would move
    # numbers other lanes are quoting, and lane GFXBODY's corrected method has
    # only 4 of its 13 extents Ghidra-confirmed.  Wave 22 input.
    pdata = sorted((ib + f.struct.BeginAddress, ib + f.struct.EndAddress)
                   for f in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", []))
    starts = {p[0] for p in pdata}

    for i in range(0, len(args), 2):
        va, n = int(args[i], 0), int(args[i + 1], 0)
        print("\nVTABLE %#x  %d slots" % (va, n))
        data = pe.get_data(va - ib, n * 8)
        npure = 0
        for k in range(n):
            q = int.from_bytes(data[k * 8:k * 8 + 8], "little")
            nm = byva.get(q)
            if nm is None:
                nm = ("FUN_%08x" % q) if q in starts else "(unnamed)"
            if nm == "_purecall":
                npure += 1
            print("   slot %-3d %#018x  %s" % (k, q, nm))
        print("   purecall %d of %d ; concrete %d" % (npure, n, n - npure))
    return 0


if __name__ == "__main__":
    sys.exit(main())
