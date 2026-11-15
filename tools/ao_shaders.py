#!/usr/bin/env python3
"""Compile the ambient-occlusion HLSL to DXBC byte-array headers with fxc (offline).

The exe has no runtime shader compiler (docs/extensions/AO.md §2 "Shaders"), so every entry point
ASSAODX11.cpp asks D3DCompile for is compiled here, once, with exactly the defines and flags
ASSAODX11.cpp passes, and written as `src/render/common/ao/shaders/<entry>.h` (`/Fh`, one
`const uint8_t g_ao_<entry>[]` per header, fxc's BYTE rewritten so shared code needs no Win32 typedef).

    python -I tools/ao_shaders.py [--fxc PATH] [--check]

--check compiles into a temporary directory and reports, writes nothing.
The table below is the ONE place the entry-point / define matrix lives (AO.md §6.4).
"""
import argparse
import glob
import os
import shutil
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AO_DIR = os.path.join(REPO, "src", "render", "common", "ao", "shaders")
AO_SOURCE_DIR = os.path.join(REPO, "src", "render", "dx11", "ao")
def find_fxc():
    """fxc on PATH first, else the newest Windows 10 SDK's x64 fxc.exe; None when neither exists."""
    on_path = shutil.which("fxc")
    if on_path:
        return on_path
    hits = glob.glob(r"C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    if not hits:
        return None
    def key(p):
        ver = os.path.basename(os.path.dirname(os.path.dirname(p)))
        return tuple(int(x) if x.isdigit() else -1 for x in ver.split("."))
    return max(hits, key=key)

# ASSAODX11.cpp InitializeDX: the D3D_SHADER_MACRO list (values from its #defines; one set for
# every entry point -- ASSAO compiles each entry exactly once).
ASSAO_DEFINES = {
    "SSAO_MAX_TAPS": "32",
    "SSAO_MAX_REF_TAPS": "512",
    "SSAO_ADAPTIVE_TAP_BASE_COUNT": "5",
    "SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT": "(32-5)",
    "SSAO_DEPTH_MIP_LEVELS": "4",
    "SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION": "0",
}
# shaderFlags = ENABLE_STRICTNESS | OPTIMIZATION_LEVEL3 | WARNINGS_ARE_ERRORS (release; no DEBUG)
ASSAO_FLAGS = ["/Ges", "/O3", "/WX"]

# (source, entry, profile). AOCORE: INTEL_SSAO_ENABLE_ADAPTIVE_QUALITY is off (ASSAO.h), so the
# five Adaptive-only entries (PSGenerateQ3, PSGenerateQ3Base, PSGenerateImportanceMap,
# PSPostprocessImportanceMapA / B) are not compiled: the tiers are Lowest / Low / Medium / High.
SHADERS = [("assao/ASSAO.hlsl", "VSMain", "vs_5_0")] + [
    ("assao/ASSAO.hlsl", e, "ps_5_0") for e in (
        "PSPrepareDepths", "PSPrepareDepthsAndNormals", "PSPrepareDepthsHalf",
        "PSPrepareDepthsAndNormalsHalf", "PSPrepareDepthMip1", "PSPrepareDepthMip2",
        "PSPrepareDepthMip3", "PSGenerateQ0", "PSGenerateQ1", "PSGenerateQ2",
        "PSSmartBlur", "PSSmartBlurWide", "PSNonSmartBlur", "PSApply", "PSNonSmartApply",
        "PSNonSmartHalfApply",
    )
]
# Our own passes (the depth resolve, ao_depth_resolve.hlsl), no ASSAO defines.
OWN = [
    ("ao_depth_resolve.hlsl", "VSFullscreen", "vs_5_0", {}),
    ("ao_depth_resolve.hlsl", "PSResolveDepthMS", "ps_5_0", {}),
    ("ao_depth_resolve.hlsl", "PSResolveDepth1x", "ps_5_0", {}),
]


def blob_size(header_text):
    # the header holds "const uint8_t g_x[] = { 68, 88, ... };" -- count the numbers in the braces
    body = header_text[header_text.index("{") + 1:header_text.rindex("}")]
    return len(re.findall(r"\b\d+\b", body))


def retype_header(path):
    # fxc /Fh declares `const BYTE g_x[]` (a Win32 typedef); shared code uses <cstdint>.
    with open(path, "r", encoding="ascii", errors="replace") as f:
        text = f.read()
    text = text.replace("const BYTE g_", "const uint8_t g_")
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write("#include <cstdint>\n" + text)


def compile_one(fxc, src, entry, profile, defines, flags, out_h):
    cmd = [fxc, "/nologo", "/T", profile, "/E", entry, "/Vn", "g_ao_" + entry, "/Fh", out_h]
    cmd += flags
    for k, v in defines.items():
        cmd += ["/D", "%s=%s" % (k, v)]
    cmd.append(src)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        retype_header(out_h)
    return r.returncode, (r.stdout + r.stderr).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fxc", default=None, help="fxc.exe path (default: PATH, then the newest Windows 10 SDK)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.fxc is None:
        a.fxc = find_fxc()
        if a.fxc is None:
            sys.exit("fxc.exe not found: put it on PATH, install a Windows 10 SDK, or pass --fxc PATH")
    out_dir = tempfile.mkdtemp() if a.check else AO_DIR
    os.makedirs(out_dir, exist_ok=True)
    jobs = [(s, e, p, ASSAO_DEFINES, ASSAO_FLAGS) for (s, e, p) in SHADERS]
    jobs += [(s, e, p, d, ["/O3", "/WX"]) for (s, e, p, d) in OWN
             if os.path.exists(os.path.join(AO_SOURCE_DIR, s))]
    fails = 0
    total = 0
    for src, entry, profile, defines, flags in jobs:
        out_h = os.path.join(out_dir, entry + ".h")
        rc, log = compile_one(a.fxc, os.path.join(AO_SOURCE_DIR, src), entry, profile, defines, flags, out_h)
        if rc != 0:
            fails += 1
            print("FAIL %-32s %s %s\n%s" % (entry, profile, src, log))
            continue
        with open(out_h, "r", encoding="ascii", errors="replace") as f:
            n = blob_size(f.read())
        total += n
        warn = "" if not log or log.startswith("compilation") else "  [" + log.splitlines()[-1] + "]"
        print("ok   %-32s %s %7d bytes%s" % (entry, profile, n, warn))
    if not a.check:
        # a header no entry above writes is stale (an entry was dropped): remove it
        wanted = set(e + ".h" for (_, e, _, _, _) in jobs)
        for name in sorted(os.listdir(out_dir)):
            if name.endswith(".h") and name not in wanted:
                os.remove(os.path.join(out_dir, name))
                print("rm   %s (no entry writes it)" % name)
    print("%d shaders, %d failed, %d DXBC bytes total -> %s" % (len(jobs), fails, total, out_dir))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
