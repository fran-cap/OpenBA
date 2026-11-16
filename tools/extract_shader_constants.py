#!/usr/bin/env python3
"""
tools/extract_shader_constants.py -- LIFT the shader-constant table out of
Runtime-x64.dll's .rdata. Do not retype it.

WHY THIS TOOL EXISTS
--------------------
`?AssignConstants@Context@Video@@AEAAXXZ` (0x1800ef3d0) builds the engine's
shader constant-buffer layout from ~45 string literals in the DLL's .rdata. A
mistyped constant NAME is a shader parameter that silently never binds: it does
not crash, does not diverge, and has no oracle short of a rendering diff.

This project already paid for the general form of that mistake once -- a CRT
port where `sin` and `cos` used DIFFERENT bit patterns for the same term, and
lifting the coefficients as raw bits was the only thing that caught it. The
standing rule is that coefficients and literals are lifted as bytes, never
re-typed from a listing. Constant names are literals; this tool is that rule
applied to them.

WHAT IT DOES
------------
1. Disassembles nothing. It reads the SHIPPED PE directly with pefile, walks the
   byte range of `AssignConstants`, and finds every `LEA reg,[rip+disp32]` whose
   target lies inside one of the PE's sections.
2. Reads each target as a NUL-terminated ASCII string straight out of the file
   image.
3. Emits a generated C++ include with one row per literal: the RVA, the length,
   and the string as a C literal with every byte escaped explicitly.

The emitted rows carry the RVA each literal was lifted from, so `--verify` can
re-extract from the DLL and diff the committed .inc row by row: a typo fails the
diff instead of silently unbinding a parameter.

USAGE
    tools/extract_shader_constants.py                 # print the table
    tools/extract_shader_constants.py --emit <path>   # write the .inc
    tools/extract_shader_constants.py --verify <path> # re-extract and diff
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
IMAGE_BASE = 0x180000000

# The byte range of ?AssignConstants@Context@Video@@AEAAXXZ.
#
# The END IS DERIVED, NOT GUESSED, and that matters: the first version of this
# tool guessed 0x1800f0000 and found 16 of the 45 literals while reporting
# nothing wrong. 1975 instructions do not fit in 0xc30 bytes, and a tool that
# silently under-reads is exactly the failure mode this whole file exists to
# prevent. The end is now the next EXPORTED address above the start, taken from
# the export directory at run time -- see next_export_after().
FN_START = 0x1800EF3D0


def load():
    pe = pefile.PE(DLL)
    return pe


def next_export_after(pe, va):
    """The lowest exported address strictly above `va`.

    `AssignConstants` is followed in address order by other exported members of
    the same class, so this is a tight and self-checking bound: if a later build
    ever moved a function in between, the walk would shrink rather than run off
    into unrelated code.
    """
    addrs = sorted({IMAGE_BASE + e.address
                    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols
                    if e.name and IMAGE_BASE + e.address > va})
    if not addrs:
        sys.exit("no export above 0x%x -- cannot bound the function" % va)
    return addrs[0]


def section_for(pe, va):
    rva = va - IMAGE_BASE
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize,
                                                            s.SizeOfRawData):
            return s
    return None


def read_cstr(pe, va, limit=128):
    s = section_for(pe, va)
    if s is None:
        return None
    off = (va - IMAGE_BASE) - s.VirtualAddress
    data = s.get_data()
    if off < 0 or off >= len(data):
        return None
    end = data.find(b"\x00", off)
    if end < 0 or end - off > limit:
        return None
    raw = data[off:end]
    try:
        txt = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    if not txt or not all(32 <= b < 127 for b in raw):
        return None
    return txt


def find_lea_targets(pe):
    """Every `LEA r64,[rip+disp32]` in the function body, in address order.

    Encoding: REX.W (0x48 | 0x4C for r8-r15) 8D /r with mod=00 rm=101.
    That is bytes  48 8D <modrm> <disp32>  where (modrm & 0xC7) == 0x05.
    Matched on the RAW BYTES rather than on a disassembler's text, so the tool
    has no dependency on Ghidra being up and cannot inherit a listing's typo.
    """
    text = None
    for s in pe.sections:
        if s.Name.rstrip(b"\x00") == b".text":
            text = s
            break
    if text is None:
        sys.exit(".text not found")

    data = text.get_data()
    base = IMAGE_BASE + text.VirtualAddress
    fn_end = next_export_after(pe, FN_START)
    out = []
    i = FN_START - base
    end = min(fn_end - base, len(data) - 7)
    while i < end:
        if data[i] in (0x48, 0x4C) and data[i + 1] == 0x8D and (data[i + 2] & 0xC7) == 0x05:
            disp = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
            target = base + i + 7 + disp
            txt = read_cstr(pe, target)
            if txt is not None:
                out.append((base + i, target, txt))
            i += 7
            continue
        i += 1
    return out


def emit(rows, path):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("// GENERATED by tools/extract_shader_constants.py -- DO NOT EDIT.\n")
        f.write("//\n")
        f.write("// The shader-constant string literals of\n")
        f.write("// ?AssignConstants@Context@Video@@AEAAXXZ (0x1800ef3d0), LIFTED byte\n")
        f.write("// for byte out of Runtime-x64.dll's read-only data. Never retyped.\n")
        f.write("//\n")
        f.write("// Each row is { rva, length, literal }. The RVA is what lets\n")
        f.write("// RtVideoShaderConstantsVerify() compare our compiled-in copy against\n")
        f.write("// the loaded DLL at startup, so a typo introduced by a later edit fails\n")
        f.write("// loudly instead of silently unbinding a shader parameter.\n")
        f.write("//\n")
        f.write("// Regenerate:  tools/extract_shader_constants.py --emit "
                "src/engine/video/shader_constants.inc\n")
        f.write("\n")
        f.write("// clang-format off\n")
        f.write("static const RtShaderConstantLiteral kShaderConstantLiterals[] = {\n")
        seen = {}
        for _site, target, txt in rows:
            if target in seen:
                continue
            seen[target] = txt
            esc = "".join("\\x%02x" % ord(c) if not (32 <= ord(c) < 127) or c in '"\\'
                          else c for c in txt)
            f.write('    { 0x%08xu, %2d, "%s" },\n'
                    % (target - IMAGE_BASE, len(txt), esc))
        f.write("};\n")
        f.write("// clang-format on\n")
    return len(seen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit")
    ap.add_argument("--verify")
    a = ap.parse_args()

    pe = load()
    rows = find_lea_targets(pe)
    distinct = {}
    for _s, t, x in rows:
        distinct[t] = x

    if a.emit:
        n = emit(rows, a.emit)
        print("wrote %s: %d distinct literals (%d references)" % (a.emit, n, len(rows)))
        return

    if a.verify:
        import re
        text = open(a.verify, encoding="ascii").read()
        have = dict((int(m.group(1), 16), m.group(3))
                    for m in re.finditer(r'\{\s*0x([0-9a-f]+)u,\s*(\d+),\s*"([^"]*)"\s*\}',
                                         text))
        bad = 0
        for rva, txt in sorted((t - IMAGE_BASE, x) for t, x in distinct.items()):
            if have.get(rva) != txt:
                print("MISMATCH rva=0x%08x dll=%r file=%r" % (rva, txt, have.get(rva)))
                bad += 1
        for rva in have:
            if (rva + IMAGE_BASE) not in distinct:
                print("EXTRA in file: rva=0x%08x %r" % (rva, have[rva]))
                bad += 1
        print("verify: %d distinct in DLL, %d in file, %d mismatches"
              % (len(distinct), len(have), bad))
        sys.exit(1 if bad else 0)

    for site, target, txt in rows:
        print("0x%x  ->  0x%x  %r" % (site, target, txt))
    print("--- %d references, %d distinct ---" % (len(rows), len(distinct)))


if __name__ == "__main__":
    main()
