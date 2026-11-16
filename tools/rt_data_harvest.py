#!/usr/bin/env python3
"""Harvest the exported DATA symbols Wave 9 lane C is responsible for.

WHY THIS EXISTS

Waves 6 and 7 both avoided ever hand-writing a vtable: Runtime-x64.dll EXPORTS
its `??_7` symbols, so a ported constructor just copies the DLL's bytes
(`??_7Geometry@Graphics@@6B@` and all 68 UI vtables are exported). At
cutover (Wave 9) there is no DLL left to copy from, so every one of these 59
exported DATA symbols — 27 `??_7` vtables, 16 `?_singleton@…` and 14
`?_typeInfo@…` (`BaseTypeInfo`) found in the "called but not ported" list —
has to be MINTED by us instead.

This script captures, while the shipped DLL is still on disk, exactly what
each symbol's bytes are TODAY. That is the harvest: it does not port anything,
it records ground truth that is gone forever once the DLL is removed
(WAVE9.md §2, "every verification mechanism dies at cutover").

SOURCES OF TRUTH (in this order)
1. `C:\\GOG Games\\Banished\\Runtime-x64.dll` on disk, via `pefile`. The export
   directory gives every symbol's RVA; a `??_7` vtable's slots are qword
   pointers we can resolve BACK to an export name via the same directory, or
   to a Ghidra-known internal (`FUN_<addr>`) if it isn't exported. Note that a
   `??_7` vtable's bytes ARE meaningful in the file (they are link-time
   constants — string/function addresses — with no runtime computation), but a
   `?_singleton@…` slot is NOT: it lives in an uninitialised (BSS-style)
   region with no file backing (pefile.get_data returns b'' there), and is
   only ever non-null after the process runs. Do not mistake an empty/zero
   read for "the singleton is null" — it means "ask at runtime, not on disk".
2. `docs/history/runtime_port/wave9_unported_20260907.txt`, the source list this
   script filters (Wave 9 prep's "called but not ported" census dump) — it
   also names, per symbol, which file in OUR tree asks for it.
3. `docs/history/runtime_port/census_20260907_prewave9.csv` — cross-reference so a
   vtable slot's target can be marked "already ported in our tree" or not.

USAGE
    tools/ghidra_mcp/.venv/Scripts/python.exe tools/rt_data_harvest.py
    ... --csv docs/history/runtime_port/wave9_data_harvest.csv   (default)
    ... --dll "C:/GOG Games/Banished/Runtime-x64.dll"    (default)
"""
import argparse
import bisect
import csv
import os
import re
import sys

import pefile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools"))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
DEFAULT_DLL = retail("Runtime-x64.dll")
DEFAULT_UNPORTED = os.path.join(REPO, "docs", "runtime_port", "wave9_unported_20260907.txt")
DEFAULT_CENSUS = os.path.join(REPO, "docs", "runtime_port", "census_20260907_prewave9.csv")
DEFAULT_CSV = os.path.join(REPO, "docs", "runtime_port", "wave9_data_harvest.csv")

ROW_RE = re.compile(r'^\s*(export|addr|ordinal)\s+(\S+)\s+(\S+)\s*$')
MAX_VTABLE_SLOTS = 64  # generous upper bound; real vtables here run 4-30 slots

# Known CRT thunks that show up as vtable slots with no export name. Confirmed
# via Ghidra (get_function_by_address names 0x18007ec28 "_purecall"; its
# decompile is the stock MSVC pure-virtual-call trap: DecodePointer + abort).
# A slot pointing here is a pure virtual method this concrete class never
# overrides -- retail relies on it never being called -- NOT an unnamed
# function for lane D to chase. 30 of the 28 vtables' slots point here across
# 10 classes; see WAVE9_LANE_C.md.
KNOWN_INTERNAL_THUNKS = {
    0x18007ec28: "_purecall",
}


def load_data_rows(path):
    """Filter the Wave 9 'called but not ported' dump to the three exported
    DATA kinds this lane owns: ??_7 vtables, ?_singleton@ and ?_typeInfo@."""
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    rows = []
    i = 0
    while i < len(lines):
        m = ROW_RE.match(lines[i])
        if m:
            kind, ns, sym = m.groups()
            consumers = lines[i + 1].strip() if i + 1 < len(lines) else ""
            if sym.startswith("??_7") or sym.startswith("?_singleton@") or sym.startswith("?_typeInfo@"):
                rows.append({
                    "kind": kind, "namespace": ns, "symbol": sym,
                    "consumers": [c.strip() for c in consumers.split(",") if c.strip()],
                })
            i += 2
        else:
            i += 1
    return rows


def load_census_ported(path):
    """key -> ported bool, from runtime_census.py's --csv output. Keys are the
    mangled export name for 'export' rows, or the hex address for 'addr' rows."""
    ported = {}
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            ported[row["key"]] = row["ported"] == "1"
    return ported


def cls(sym):
    if sym.startswith("??_7"):
        return "vtable"
    if sym.startswith("?_singleton@"):
        return "singleton"
    if sym.startswith("?_typeInfo@"):
        return "typeinfo"
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default=DEFAULT_DLL)
    ap.add_argument("--unported", default=DEFAULT_UNPORTED)
    ap.add_argument("--census", default=DEFAULT_CENSUS)
    ap.add_argument("--csv", default=DEFAULT_CSV)
    args = ap.parse_args()

    pe = pefile.PE(args.dll)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    image_size = pe.OPTIONAL_HEADER.SizeOfImage

    by_name = {}   # export name -> rva
    by_rva = {}    # rva -> export name
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.name:
            name = e.name.decode()
            by_name[name] = e.address
            by_rva[e.address] = name
    sorted_rvas = sorted(by_rva)

    def next_export_rva(rva):
        i = bisect.bisect_right(sorted_rvas, rva)
        return sorted_rvas[i] if i < len(sorted_rvas) else None

    def read(rva, length):
        try:
            data = pe.get_data(rva, length)
        except Exception:
            return b""
        return data

    def cstr(rva):
        if rva is None or rva < 0 or rva >= image_size:
            return None
        data = read(rva, 256)
        if not data:
            return None
        end = data.find(b"\0")
        if end < 0:
            end = len(data)
        try:
            return data[:end].decode("ascii")
        except UnicodeDecodeError:
            return data[:end].decode("latin1")

    data_rows = load_data_rows(args.unported)
    ported = load_census_ported(args.census)

    out_rows = []           # for the CSV
    report = {"vtable": [], "singleton": [], "typeinfo": [], "phantom": []}

    for row in data_rows:
        sym, ns, kind = row["symbol"], row["namespace"], cls(row["symbol"])
        rva = by_name.get(sym)
        if rva is None:
            report["phantom"].append(row)
            out_rows.append({"kind": kind, "symbol": sym, "namespace": ns,
                              "rva": "", "note": "PHANTOM: not in export directory",
                              "slot_index": "", "slot_va": "", "slot_name": "",
                              "slot_ported": "", "consumers": ";".join(row["consumers"])})
            continue

        va = image_base + rva

        if kind == "vtable":
            bound_rva = next_export_rva(rva)
            max_slots = min(MAX_VTABLE_SLOTS, ((bound_rva - rva) // 8) if bound_rva else MAX_VTABLE_SLOTS)
            raw = read(rva, max_slots * 8)
            slots = []
            for i in range(len(raw) // 8):
                qw = int.from_bytes(raw[i * 8:i * 8 + 8], "little")
                if qw == 0:
                    break
                target_rva = qw - image_base
                if target_rva < 0 or target_rva >= image_size:
                    break
                name = by_rva.get(target_rva)
                is_ported = ported.get(name) if name else ported.get(hex(qw))
                slots.append({"index": i, "va": qw, "name": name, "ported": is_ported})
            report["vtable"].append({"symbol": sym, "namespace": ns, "va": va,
                                      "slots": slots, "consumers": row["consumers"],
                                      "bound_hit": bound_rva is not None and len(raw) // 8 == max_slots})
            # fold_group: sibling slot indices (in THIS vtable) that share the
            # same target address -- a real hazard either way: a known CRT
            # thunk (_purecall) sharing one address across many slots is
            # expected, but two ORDINARY functions sharing an address means
            # the linker folded identical bodies (ICF) and minting them as
            # two separate reimplementations is still correct, just not
            # provably the same function retail's folded copy is.
            by_va = {}
            for s in slots:
                by_va.setdefault(s["va"], []).append(s["index"])
            for s in slots:
                siblings = [i for i in by_va[s["va"]] if i != s["index"]]
                s["fold_group"] = siblings
            for s in slots:
                thunk = KNOWN_INTERNAL_THUNKS.get(s["va"])
                fold_note = (f"shares target with slot(s) {s['fold_group']}" if s["fold_group"] else "")
                note = "; ".join(x for x in [
                    ("pure virtual, never overridden by this class -- not lane D work" if thunk else ""),
                    fold_note,
                ] if x)
                out_rows.append({
                    "kind": "vtable", "symbol": sym, "namespace": ns, "rva": hex(rva),
                    "note": note,
                    "slot_index": s["index"], "slot_va": hex(s["va"]),
                    "slot_name": thunk or s["name"] or f"FUN_{s['va']:x}",
                    "slot_ported": ("" if s["ported"] is None else ("1" if s["ported"] else "0")),
                    "fold_group": ";".join(str(i) for i in s["fold_group"]),
                    "consumers": ";".join(row["consumers"]),
                })
            if not slots:
                out_rows.append({"kind": "vtable", "symbol": sym, "namespace": ns, "rva": hex(rva),
                                  "note": "0 slots decoded (unexpected)", "slot_index": "", "slot_va": "",
                                  "slot_name": "", "slot_ported": "", "consumers": ";".join(row["consumers"])})

        elif kind == "singleton":
            raw = read(rva, 8)
            live = bool(raw) and any(raw)
            note = ("has non-zero bytes in the FILE -- unexpected, check by hand"
                    if live else
                    "NOT backed by file data (BSS-style) or zero -- only ever set at runtime; do not read this as null")
            report["singleton"].append({"symbol": sym, "namespace": ns, "va": va,
                                         "raw": raw.hex() if raw else "", "consumers": row["consumers"]})
            out_rows.append({"kind": "singleton", "symbol": sym, "namespace": ns, "rva": hex(rva),
                              "note": note, "slot_index": "", "slot_va": "",
                              "slot_name": raw.hex() if raw else "(empty read)",
                              "slot_ported": "", "consumers": ";".join(row["consumers"])})

        else:  # typeinfo -- Object::BaseTypeInfo, 0x38 bytes (src/engine/object/TypeInfo.h)
            raw = read(rva, 0x38)
            fields = {}
            if len(raw) == 0x38:
                type_id = int.from_bytes(raw[0:4], "little")
                name_p = int.from_bytes(raw[8:16], "little")
                parent_p = int.from_bytes(raw[16:24], "little")
                ext_p = int.from_bytes(raw[24:32], "little")
                factory_p = int.from_bytes(raw[32:40], "little")
                inplace_p = int.from_bytes(raw[40:48], "little")
                next_p = int.from_bytes(raw[48:56], "little")
                fields = {
                    "typeId": hex(type_id),
                    "name": cstr(name_p - image_base) if name_p else None,
                    "parentName": cstr(parent_p - image_base) if parent_p else None,
                    "fileExtension": cstr(ext_p - image_base) if ext_p else None,
                    "factoryFn": by_rva.get(factory_p - image_base, hex(factory_p)) if factory_p else None,
                    "inPlaceFactoryFn": by_rva.get(inplace_p - image_base, hex(inplace_p)) if inplace_p else None,
                    "next_raw": hex(next_p),
                }
            report["typeinfo"].append({"symbol": sym, "namespace": ns, "va": va,
                                        "fields": fields, "consumers": row["consumers"]})
            note = ("typeId sentinel 0xFFFFFFFF + name/parentName/fileExtension/factoryFn/"
                    "inPlaceFactoryFn are LINK-TIME CONSTANTS baked into the file; "
                    "'next' is runtime-only (head-inserted by the ctor) and reads 0 here")
            out_rows.append({"kind": "typeinfo", "symbol": sym, "namespace": ns, "rva": hex(rva),
                              "note": note, "slot_index": "", "slot_va": "",
                              "slot_name": repr(fields), "slot_ported": "",
                              "consumers": ";".join(row["consumers"])})

    with open(args.csv, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["kind", "symbol", "namespace", "rva", "note",
                                          "slot_index", "slot_va", "slot_name", "slot_ported",
                                          "fold_group", "consumers"])
        w.writeheader()
        w.writerows(out_rows)

    # console summary
    print(f"DLL: {args.dll}")
    print(f"vtables: {len(report['vtable'])}  singletons: {len(report['singleton'])}  "
          f"typeinfo: {len(report['typeinfo'])}  phantoms: {len(report['phantom'])}")
    print(f"CSV written: {args.csv}  ({len(out_rows)} rows)")
    if report["phantom"]:
        print("\nPHANTOM (in the source list but NOT in the export directory):")
        for r in report["phantom"]:
            print(f"  {r['symbol']}  ({r['namespace']})  asked by: {', '.join(r['consumers'])}")

    print("\nVtables ranked by NOT-YET-PORTED slot count (cheapest first);"
          " 'thunk' = known CRT trap (e.g. _purecall), not lane D work:")
    ranked = []
    for v in report["vtable"]:
        total = len(v["slots"])
        thunk = sum(1 for s in v["slots"] if s["va"] in KNOWN_INTERNAL_THUNKS)
        unported_named = sum(1 for s in v["slots"] if s["name"] and s["ported"] is False)
        unnamed = sum(1 for s in v["slots"] if not s["name"] and s["va"] not in KNOWN_INTERNAL_THUNKS)
        already = sum(1 for s in v["slots"] if s["ported"])
        untracked = sum(1 for s in v["slots"] if s["name"] and s["ported"] is None)
        not_ready = unported_named + unnamed + untracked
        ranked.append((not_ready, v["symbol"], total, already, unported_named, untracked, unnamed, thunk))
    ranked.sort()
    print(f"{'symbol':<45} {'slots':>5} {'ported':>7} {'unp-named':>10} {'untracked':>10} {'unnamed':>8} {'thunk':>6}")
    for not_ready, sym, total, already, unp, untracked, unnamed, thunk in ranked:
        print(f"{sym:<45} {total:>5} {already:>7} {unp:>10} {untracked:>10} {unnamed:>8} {thunk:>6}")

    # Shared-address (folded) slot groups, measured across all vtables -- the
    # check that caught this report's own first-pass ICF guess being wrong.
    print("\nShared-address slot groups (same target VA more than once in one vtable):")
    any_folds = False
    for v in report["vtable"]:
        counts = {}
        for s in v["slots"]:
            counts.setdefault(s["va"], []).append(s["index"])
        dupes = {va: idxs for va, idxs in counts.items() if len(idxs) > 1}
        if dupes:
            any_folds = True
            for va, idxs in dupes.items():
                label = KNOWN_INTERNAL_THUNKS.get(va, hex(va))
                print(f"  {v['symbol']:<45} slots {idxs} -> {label}")
    if not any_folds:
        print("  none")

    return 1 if report["phantom"] else 0


if __name__ == "__main__":
    sys.exit(main())
