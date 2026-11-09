#!/usr/bin/env python3
"""Compile ext.better_water's HLSL to a DXBC byte-array header with fxc (offline).

The exe has no runtime shader compiler, so the shore overlay's pixel program is compiled here, once,
twice (river and, with WATER_LAKE, lake: their uv sets scroll differently), and written as
`src/render/common/better_water/shore_overlay_{river,lake}_ps.h` (`/Fh`, `const uint8_t
g_bw_PSShoreRiver[]` / `g_bw_PSShoreLake[]`); Fancy's shore strip program likewise as `shore_strip_ps.h`
(`g_bw_PSShoreStrip`), and its contact foam as `shore_contact_{river,lake}_ps.h`; ext.winter_ice's lake ice as `src/render/common/winter_ice/ice_overlay_ps.h`
(`g_wi_PSIceOverlay`, docs/extensions/handins/WINTERICE.md); ext.better_clouds' sky as
`src/render/common/better_clouds/cloud_sky{,_cheap}_ps.h` (`g_bc_PSCloudSky`, `g_bc_PSCloudSkyCheap`,
docs/extensions/handins/BETTERCLOUDS.md). fxc's BYTE rewritten so shared code needs no Win32 typedef). Every backend
takes the DXBC (docs/extensions/handins/BETTERWATER.md). The tools/ao_shaders.py pattern.

    python -I tools/water_shaders.py [--fxc PATH] [--check]

--check compiles into a temporary directory and reports, writes nothing.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WATER_DIR = os.path.join(REPO, "src", "render", "common", "better_water")
ICE_DIR = os.path.join(REPO, "src", "render", "common", "winter_ice")
CLOUDS_DIR = os.path.join(REPO, "src", "render", "common", "better_clouds")
TERRAIN_DIR = os.path.join(REPO, "src", "render", "common", "fancy_terrain")
DOF_DIR = os.path.join(REPO, "src", "render", "common", "dof")

# (source, entry, profile, variable, header, defines). ps_4_0: the profile retail's own water programs
# use, so the overlay runs wherever the water does.
SHADERS = [
    ("shore_overlay.hlsl", "PSShoreOverlay", "ps_4_0", "g_bw_PSShoreRiver", "shore_overlay_river_ps.h", []),
    ("shore_overlay.hlsl", "PSShoreOverlay", "ps_4_0", "g_bw_PSShoreLake", "shore_overlay_lake_ps.h", ["WATER_LAKE=1"]),
    ("shore_strip.hlsl", "PSShoreStrip", "ps_4_0", "g_bw_PSShoreStrip", "shore_strip_ps.h", []),
    ("shore_contact.hlsl", "PSShoreContact", "ps_4_0", "g_bw_PSContactRiver", "shore_contact_river_ps.h", []),
    ("shore_contact.hlsl", "PSShoreContact", "ps_4_0", "g_bw_PSContactLake", "shore_contact_lake_ps.h", ["WATER_LAKE=1"]),
    # ext.winter_ice's lake ice (docs/extensions/handins/WINTERICE.md): lakes only, so one variant.
    ("winter_ice/ice_overlay.hlsl", "PSIceOverlay", "ps_4_0", "g_wi_PSIceOverlay", "winter_ice/ice_overlay_ps.h", []),
    # ext.better_clouds' sky (docs/extensions/handins/BETTERCLOUDS.md): the full program for the sky's own pass,
    # the cheap one (no detail octave, no sun probe) for its reflection in the water.
    ("better_clouds/cloud_sky.hlsl", "PSCloudSky", "ps_4_0", "g_bc_PSCloudSky", "better_clouds/cloud_sky_ps.h", []),
    ("better_clouds/cloud_shadow.hlsl", "PSCloudShadow", "ps_4_0", "g_bc_PSCloudShadow", "better_clouds/cloud_shadow_ps.h", []),
    ("better_clouds/cloud_sky.hlsl", "PSCloudSky", "ps_4_0", "g_bc_PSCloudSkyCheap", "better_clouds/cloud_sky_cheap_ps.h",
     ["CLOUDS_CHEAP=1"]),
    # ext.fancy_terrain's terrain light (src/ext/fancy_terrain/horizon.h): the full-screen multiply.
    ("fancy_terrain/terrain_light.hlsl", "PSTerrainLight", "ps_4_0", "g_ft_PSTerrainLight",
     "fancy_terrain/terrain_light_ps.h", []),
    # ext.depth_of_field's depth blur (docs/extensions/DOF.md section 3.1): the four full-screen passes.
    ("dof/dof.hlsl", "PSDofPrefilter", "ps_4_0", "g_dof_PSPrefilter", "dof/dof_prefilter_ps.h", []),
    ("dof/dof.hlsl", "PSDofGather", "ps_4_0", "g_dof_PSGather", "dof/dof_gather_ps.h", []),
    ("dof/dof.hlsl", "PSDofTent", "ps_4_0", "g_dof_PSTent", "dof/dof_tent_ps.h", []),
    ("dof/dof.hlsl", "PSDofComposite", "ps_4_0", "g_dof_PSComposite", "dof/dof_composite_ps.h", []),
]
FLAGS = ["/O3", "/WX", "/Ges"]


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


def blob_size(header_text):
    body = header_text[header_text.index("{") + 1:header_text.rindex("}")]
    return len(re.findall(r"\b\d+\b", body))


def source_path(name):
    """A source or header name relative to better_water/, or under a `winter_ice/` or `better_clouds/` prefix to
    that directory."""
    if name.startswith("winter_ice/"):
        return os.path.join(ICE_DIR, name[len("winter_ice/"):])
    if name.startswith("better_clouds/"):
        return os.path.join(CLOUDS_DIR, name[len("better_clouds/"):])
    if name.startswith("fancy_terrain/"):
        return os.path.join(TERRAIN_DIR, name[len("fancy_terrain/"):])
    if name.startswith("dof/"):
        return os.path.join(DOF_DIR, name[len("dof/"):])
    return os.path.join(WATER_DIR, name)


def retype_header(path, source):
    with open(path, "r", encoding="ascii", errors="replace") as f:
        text = f.read()
    text = text.replace("const BYTE g_", "const uint8_t g_")
    banner = ("// GENERATED by tools/water_shaders.py from %s -- do not edit; rerun the script.\n"
              "// NOT RETAIL: our own shader (docs/extensions/handins/BETTERWATER.md).\n"
              "#pragma once\n#include <cstdint>\n") % source
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(banner + text.replace("\r\n", "\n"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fxc", default=None, help="fxc.exe path (default: PATH, then the newest Windows 10 SDK)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.fxc is None:
        a.fxc = find_fxc()
        if a.fxc is None:
            sys.exit("fxc.exe not found: put it on PATH, install a Windows 10 SDK, or pass --fxc PATH")
    tmp_dir = tempfile.mkdtemp() if a.check else None
    fails = 0
    for src, entry, profile, var, header, defines in SHADERS:
        out_h = os.path.join(tmp_dir, os.path.basename(header)) if tmp_dir else source_path(header)
        cmd = [a.fxc, "/nologo", "/T", profile, "/E", entry, "/Vn", var, "/Fh", out_h]
        cmd += FLAGS
        for d in defines:
            cmd += ["/D", d]
        cmd.append(source_path(src))
        r = subprocess.run(cmd, capture_output=True, text=True)
        log = (r.stdout + r.stderr).strip()
        if r.returncode != 0:
            fails += 1
            print("FAIL %-24s %s %s\n%s" % (entry, profile, src, log))
            continue
        rel = os.path.relpath(source_path(src), REPO).replace(os.sep, "/")
        retype_header(out_h, rel)
        with open(out_h, "r", encoding="ascii", errors="replace") as f:
            n = blob_size(f.read())
        print("ok   %-24s %s %6d bytes -> %s" % (var, profile, n, out_h))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
