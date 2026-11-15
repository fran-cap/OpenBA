#!/usr/bin/env python3
"""Runtime-x64.dll port census: called vs ported vs total, per namespace.

The exe port's progress question was "how many of the addresses our tree asks
for are registered". The runtime port's is the same shape, with three numbers
instead of two:

    total   — functions the DLL actually contains, per namespace
              (from docs/history/runtime_port/survey_ghidra_census.md, a live Ghidra
              count; this script does not talk to Ghidra)
    called  — the distinct functions OUR tree calls today, i.e. the dependency
              surface we must satisfy before cutover
              (scanned live from code/, engine/, runtime/, standalone/, src/render/dx11/)
    ported  — of those, the ones a RegisterRuntimeExport / RegisterRuntimeAddr /
              RegisterRuntimeOrdinal call has taken over

"called" is the honest denominator for the port: it is what has to work for
`OpenBA.exe` to boot without the DLL. "total" is the denominator for a
complete DLL reimplementation, which is not the stated goal (PLAN.md §4.1
decision 1: static link, no drop-in DLL).

Usage:
    tools/ghidra_mcp/.venv/Scripts/python.exe perf/mintsel/runtime_census_at_MINTSEL.py
    ... --csv docs/runtime_port/census_<stamp>.csv    write the per-function rows
    ... --unported                                    list what is called but not ported
    ... --namespace UI                                restrict to one namespace
"""

import argparse
import collections
import csv
import os
import re
import sys

# Set from --allow-degraded in main(); see load_export_addrs().
ALLOW_DEGRADED = False

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ["code", "engine", "runtime", "standalone", "dx11"]
SKIP_DIR_NAMES = {"build", "build-og", "build-perf", "obj", ".git", "__pycache__", ".venv"}
SOURCE_EXT = (".cpp", ".h", ".hpp", ".c")

# --- call sites into the DLL -------------------------------------------------
# These match only up to the OPENING quote; the mangled name itself is read by
# concat_literal() below, which absorbs any further string literals that
# follow separated only by whitespace. A mangled name split across two source
# lines is one C++ string literal after preprocessing, not two -- a plain
# `"([^"]+)"` capture reads only the first fragment and silently truncates it.
# (Confirmed live: gameupdateloop_load.cpp:71 SetAchievement, and
# pickingcomponent.cpp:171 AddMesh, both reported as phantom exports by
# export_name_audit.py before this fix.)
STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"')


def concat_literal(text, pos):
    """Read the C++ string literal starting at `pos` and absorb any further
    literals that immediately follow (separated only by whitespace) into one
    joined name, exactly as the C++ preprocessor concatenates adjacent string
    literals. Returns the joined content with quotes stripped."""
    parts = []
    i = pos
    while True:
        m = STRING_LITERAL.match(text, i)
        if not m:
            break
        parts.append(m.group(0)[1:-1])
        i = m.end()
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j < len(text) and text[j] == '"':
            i = j
            continue
        break
    return "".join(parts)


RE_EXPORT = re.compile(r'ResolveExport(?:Raw)?\s*\(\s*(?=")')

# A ResolveExport whose argument is a NAMED CONSTANT rather than a literal --
# the SIXTH blind spot, and the first one this script found by being used
# rather than by an incident.
#
# Wave 10 hit it while porting the static GetAlignedPosition overload. The port
# spelled the mangled name once as `const char* const kGetAlignedStatic = ...`
# and used it at BOTH the ResolveExport site and the RegisterRuntimeExport site
# -- which is the practice the RE_NAME_CONST comment explicitly ENCOURAGES,
# because it stops the door and the registration drifting apart. The
# registration side resolved it. The CALL side did not, so the function fell out
# of `called` entirely, and since `ported` is a subset of `called` the freshly
# landed port read as a REGRESSION: UI called 161->160, ported flat.
#
# So the same `name_consts` table both sides already needed is now applied to
# both sides, and an unresolvable identifier is reported instead of dropped.
RE_EXPORT_NONLITERAL = re.compile(
    r'ResolveExport(?:Raw)?\s*\(\s*(?!")([A-Za-z_][A-Za-z0-9_:]*)\s*\)')

# THE MACRO DOORS -- the fifth blind spot, and the most expensive one this
# script has had. `called` matched a LITERAL `ResolveExport(` only, so the two
# macros in src/engine/rt_port.h were invisible:
#
#   RT_EXPORT_ONCE(FnType, "?Mangled@...")   -- name is the SECOND argument
#   RT_DATA_SLOT(T,        "?Mangled@...")   -- name is the SECOND argument
#   RtResolveCached("?Mangled@...", &cache)  -- name is the FIRST argument
#
# All three end in RtResolveCached and are therefore live cutover dependencies
# in exactly the sense this column exists to count. 113 of them were unseen --
# including `?SetupDirectories@FileSystem@IO@@` and `?LoadPackages@FileSystem@IO@@`,
# the two symbols the DLL-less boot spent attempts 09 through 12 dying on. The
# census could not see the wall it was being used to look for.
#
# The two macros take a TYPE as their first argument, which can itself contain
# commas inside angle brackets or a function-pointer parameter list, so the name
# is found by scanning for the first string literal rather than by splitting on
# commas.
RE_MACRO_DOOR = re.compile(r'\b(?:RT_EXPORT_ONCE|RT_DATA_SLOT)\s*\(')
RE_RESOLVE_CACHED = re.compile(r'\bRtResolveCached\s*\(\s*(?=")')


def first_literal_after(text, pos, limit=400):
    """Position of the first `"` within `limit` chars of `pos`, or None.

    Used for the two macro doors, whose first argument is a TYPE that may
    contain commas (`Collection::Array<A, B>`, a function-pointer signature).
    Splitting on commas would pick the wrong argument; the name is simply the
    first string literal in the call, so find that.
    """
    stop = min(len(text), pos + limit)
    i = text.find('"', pos)
    if i == -1 or i >= stop:
        return None
    # A `)` before the literal means the call had no string argument at all.
    if ')' in text[pos:i]:
        return None
    return i
RE_ORDINAL = re.compile(r'ResolveOrdinal(?:Raw)?\s*\(\s*(\d+)')
RE_ADDR = re.compile(r'ResolveAddr\s*\(\s*Module::Runtime\s*,\s*(0x[0-9a-fA-F]+)')
RE_ADDR_RAW = re.compile(r'ResolveRuntimeRaw\s*\(\s*(0x[0-9a-fA-F]+)')

# src/render/dx11/ reaches Runtime-x64.dll through its OWN resolver, not ResolveExport:
#   Dx11ResolveRuntimeImport(mangled, iatGhidraAddress)
# and it almost never passes the name at the call site -- the names live in
# `{ &g_im.Field, "?Mangled@...", 0x1800134xx }` import tables that are then
# walked in a loop. RE_EXPORT sees none of it, so every one of dx11's Runtime
# imports was missing from `called` until the BANISHED_RT_STRICT residue census
# caught 86 of them arriving at the DLL during a plain autostart run.
#
# That gap matters more than its size: src/render/dx11/ IS our VideoDX11 replacement, the
# one subsystem that already survives its DLL being absent, and its Runtime
# imports are part of the cutover surface exactly like the exe's are.
#
# Restricted to src/render/dx11/ and to names that look like MSVC C++ manglings (leading
# '?', an '@@' somewhere) so an ordinary string literal cannot become a phantom
# call row -- the failure strip_comments() exists to prevent.
RE_DX11_MANGLED = re.compile(r'"(\?[A-Za-z0-9_?$@]*@@[A-Za-z0-9_?$@]*)"')

# --- ports -------------------------------------------------------------------
RE_REG_EXPORT = re.compile(r'RegisterRuntimeExport\s*\(\s*(?=")')

# A RegisterRuntimeExport whose first argument is NOT a string literal. This
# regex exists because of a real, silent under-count: Wave 4 lane D registered
# its six FileSystem/Modification ports through `kName*` constants, which
# RE_REG_EXPORT cannot see through, and the census reported IO 30/41 while 36
# were in fact registered. Nothing failed and nothing warned -- the ports were
# live in the process and invisible to the tool the project treats as its
# authority over any agent's "all N ported" claim.
#
# This is the mirror image of reference_uncalled_port_is_not_verified.md: there
# a port is counted but never runs; here a port runs but is never counted. Both
# end in a census that does not describe reality, so both must be loud.
RE_REG_EXPORT_NONLITERAL = re.compile(
    r'RegisterRuntimeExport\s*\(\s*(?!")([A-Za-z_][A-Za-z0-9_:]*)')

# A file-scope `const char* const kName = "?Mangled@...";` definition. Spelling
# the name once and using it at BOTH the RT_ORIGINAL_ONCE site and the
# RegisterRuntimeExport site is good practice -- the diff oracle and the
# registration cannot then drift apart -- so the census resolves these instead
# of punishing them. Only same-file, single-literal definitions are resolved;
# anything else still raises the NON-LITERAL warning above, because a name this
# script cannot follow is a name it must not silently drop.
#
# BOTH SPELLINGS. `const char* const kX` and `constexpr const char* kX` are the
# same idiom and the tree uses both -- src/engine/ui/ElementDescription.h and
# src/game/Dialog/mod_dialog_vtables.cpp use the constexpr form. Matching only
# the first meant a file could name a symbol once, correctly, and still be
# invisible to this script.
RE_NAME_CONST = re.compile(
    r'\b(?:(?:static\s+)?const\s+char\s*\*\s*const|constexpr\s+const\s+char\s*\*)\s+'
    r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?=")')
RE_REG_ORDINAL = re.compile(r'RegisterRuntimeOrdinal\s*\(\s*(\d+)')
RE_REG_ADDR = re.compile(r'RegisterRuntimeAddr\s*\(\s*(0x[0-9a-fA-F]+)')

# --- detours (a port that also took over the DLL's own internal callers) -----
RE_DETOUR_ADDR = re.compile(r'InstallRuntimeDetour(?:Checked)?\s*\(\s*"([^"]*)"\s*,\s*(0x[0-9a-fA-F]+)')

# The SAME blind spot as RE_REG_EXPORT_NONLITERAL, one column over, and it bit
# immediately: Wave 10's first UI detour passes its target as
# `constexpr uintptr_t kUpdateContainersAddr = 0x1800d9710;` and RE_DETOUR_ADDR
# -- which demands a literal -- could not see it. The detour was installed, the
# audit tool confirmed it, the process logged it, and this census still printed
# `detour 0` for UI.
#
# A tool that already documents a blind spot for one column and then repeats it
# in the next is the shape of reference_literal_grep_blind_to_variables.md, so
# both halves of the cure are here: same-file `constexpr uintptr_t kX = 0x...;`
# definitions are RESOLVED, and anything still unresolved is reported LOUDLY
# rather than dropped.
RE_DETOUR_NONLITERAL = re.compile(
    r'InstallRuntimeDetour(?:Checked)?\s*\(\s*"([^"]*)"\s*,\s*(?!0x)([A-Za-z_][A-Za-z0-9_:]*)')

# A file-scope `constexpr uintptr_t kX = 0x...;` (or const/static uintptr_t).
# Mirrors RE_NAME_CONST, which resolves the same shape for mangled names.
RE_ADDR_CONST = re.compile(
    r'\b(?:static\s+)?(?:constexpr|const)\s+(?:uintptr_t|unsigned\s+long\s+long|uint64_t)\s+'
    r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+)')
RE_DETOUR_EXPORT = re.compile(r'InstallRuntimeDetourExport\s*\(\s*"([^"]*)"\s*,\s*"([^"]+)"')

# Function counts per namespace, from the live Ghidra census recorded in
# docs/runtime_port/PLAN.md §3.1. Update both together.
TOTAL_FUNCTIONS = {
    "UI": 1625, "Graphics": 599, "Video": 573, "IO": 407, "System": 262,
    "Math": 228, "Object": 191, "Audio": 137, "Achievement": 118, "Engine": 108,
    "Collision2D": 76, "Volume": 64, "Input": 63, "Picking": 49,
    "Pathfinding": 45, "Save": 37, "Application": 26, "Utility": 16, "Debug": 8,
}


# Absolute path of the shipped DLL, used ONLY to turn a raw address back into
# the exported name at that address. Everything else in this script is a source
# scan.
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from banished_dir import retail  # noqa: E402  -- env BANISHED_DIR, default C:/GOG Games/Banished
RUNTIME_DLL = retail("Runtime-x64.dll")


def load_export_addrs():
    """{ image address -> mangled name } for every export in the shipped DLL.

    Added 2026-09-06 (Wave 5), because the census had a third blind spot that
    both lane B and lane D hit independently:

        A function our tree reaches only by RAW ADDRESS was bucketed under the
        pseudo-namespace "internal", never into the namespace it belongs to.

    So a namespace row UNDER-COUNTED any class our tree calls by address --
    "Picking 8/8 100%" while a ninth Picking function (RemoveMesh, reached only
    at 0x180125290) sat in the internal bucket -- and, worse, the same function
    could be counted TWICE when one call site used the name and another used
    the address. Both directions corrupt the number this project treats as its
    authority over any agent's "all N ported" claim.

    FAILS LOUDLY if pefile or the DLL is unavailable.

    It used to `return {}` and let the census "degrade to its old address-blind
    behaviour rather than failing". That was wrong, and Wave 9 lane 0 measured
    exactly how wrong: run under the project venv (which has pefile) the census
    reports **830 called / 512 ported**; run under system Python 3.12 (which does
    not) the SAME tree reports **1016 / 616**. Same command, same source, two
    different authorities, no warning either time. It produced a wrong
    intermediate finding inside lane 0's own work (57 phantom "census blind
    spots") before the cause was found.

    That is this project's most-repeated failure mode, and this function was an
    instance of it: `reference_truncated_tool_output_manufactures_calls_zero.md`
    (a `tail -80` faked sixteen sites disappearing) and WAVE9.md's three census
    defects are the same shape -- a tool that answers when it cannot answer. The
    census is the number this project treats as authority over any agent's "all N
    ported" claim, so it must refuse to produce one it cannot stand behind.

    `--allow-degraded` restores the old behaviour for the one case that deserves
    it (a machine with no pefile that only wants the name-keyed rows), and says so
    on stderr every time rather than silently.
    """
    def _bail(why):
        if ALLOW_DEGRADED:
            sys.stderr.write(
                "WARNING: census running DEGRADED (%s).\n"
                "         Address-keyed rows are invisible; 'called' and 'ported'\n"
                "         are BOTH understated and the namespace split is wrong.\n"
                "         Do not publish this number.\n" % why)
            return {}
        sys.stderr.write(
            "FATAL: %s\n"
            "       The census cannot see functions our tree reaches by ADDRESS\n"
            "       without it, which changes the answer materially -- measured\n"
            "       830/512 (correct) vs 1016/616 (degraded) on the same tree.\n"
            "       Run it with the project venv:\n"
            "         tools/ghidra_mcp/.venv/Scripts/python.exe tools/runtime_census.py\n"
            "       or pass --allow-degraded if you really want the partial view.\n"
            % why)
        sys.exit(2)

    try:
        import pefile
    except ImportError:
        return _bail("pefile is not importable in this interpreter")
    if not os.path.exists(RUNTIME_DLL):
        return _bail("the shipped DLL is missing: %s" % RUNTIME_DLL)
    try:
        pe = pefile.PE(RUNTIME_DLL, fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        base = pe.OPTIONAL_HEADER.ImageBase
        out = {}
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if e.name:
                out[base + e.address] = e.name.decode("utf-8", "replace")
        if not out:
            return _bail("the DLL's export directory parsed EMPTY")
        return out
    except SystemExit:
        raise
    except Exception as exc:
        return _bail("reading the DLL's export directory failed: %r" % exc)


def namespace_of(mangled):
    """Outermost namespace of an MSVC mangled name: ?Method@Class@Namespace@@...

    The qualified name is a list of components, inner-to-outer, and the LAST
    one is the namespace. Parsing it needs the actual MSVC grammar, not a
    split on "@@", because of two rules that interact:

      * A NAME FRAGMENT is terminated by '@'.
      * A BACK-REFERENCE is a single digit indexing the fragments already seen
        in this name, and it is NOT terminated by '@'.
      * The component list itself is terminated by an empty component, i.e. a
        bare '@'.

    So the familiar "@@" is fragment-terminator plus list-terminator, and a
    name whose innermost component is a back-reference ends its scope with a
    SINGLE '@'. `?GetDescription@Achievement@1@QEBA?AVString@System@@XZ` is
    Achievement::Achievement::GetDescription returning a System::String — and
    splitting it on "@@" hands back "System", filing three Achievement methods
    under System. This walks the grammar instead.
    """
    if not mangled.startswith("?"):
        return "?"
    i, n = 1, len(mangled)
    if i < n and mangled[i] == "?":
        # Operator name: "?X" (??0 = ctor, ??1 = dtor, ??Y = operator+ ...) or
        # the extended "?_X" form. It is not a name fragment, so it must not
        # enter the back-reference table — skip it and start the scope list at
        # the next component.
        i += 1
        if i < n and mangled[i] == "_":
            i += 1
        i += 1
        fragments, resolved = [], [""]   # placeholder for the "method name"
    else:
        fragments, resolved = [], []
    while i < n:
        c = mangled[i]
        if c == "@":            # empty component: end of the qualified name
            break
        if c.isdigit():         # back-reference, one character, no terminator
            idx = int(c)
            resolved.append(fragments[idx] if idx < len(fragments) else c)
            i += 1
            continue
        j = mangled.find("@", i)
        if j < 0:
            return "?"          # unterminated fragment: not a name we can read
        frag = mangled[i:j]
        fragments.append(frag)
        resolved.append(frag)
        i = j + 1

    # resolved[0] is the method name; the rest are scopes, outermost last.
    scopes = resolved[1:]
    if not scopes:
        return "?"
    if scopes[-1] in TOTAL_FUNCTIONS:
        return scopes[-1]
    for comp in reversed(scopes):
        if comp in TOTAL_FUNCTIONS:
            return comp
    return "?"


def iter_sources():
    for top in SCAN_DIRS:
        root_dir = os.path.join(REPO, top)
        if not os.path.isdir(root_dir):
            continue
        for dirpath, dirnames, filenames in os.walk(root_dir):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES]
            for fn in filenames:
                if fn.endswith(SOURCE_EXT):
                    yield os.path.join(dirpath, fn)


# Files whose RegisterRuntime* calls are OPT-IN: their Register function only runs
# when an env var arms it and returns 0 otherwise, so the registrations are
# present in the source but NOT live. A census that counted them would say
# "System 95% ported" for a wave in which the allocator has never executed a
# single instruction -- the same overstatement as a diff site reporting
# divergences=0 on calls=0 (WAVE1.md defect 4). They are reported separately.
GATED_OFF_FILES = {
    "src/engine/system/allocator.cpp",       # BANISHED_RT_ALLOC, verdict NO-GO
    "src/platform/win/allocator_win.cpp",
}

# The same overstatement one level finer: a registration inside an `if (<flag>)`
# block that is OFF by default. GATED_OFF_FILES cannot see these because the
# file around them registers plenty of live ports too.
#
# Found 2026-09-07 while preparing wave 9, and found by the PROCESS, not by
# reading: the BANISHED_RT_STRICT residue census recorded four names arriving
# at Runtime-x64.dll that this script was calling ported --
# `??0Internal@Object@@QEAA@XZ`, `??1Internal@Object@@UEAA@XZ`,
# `?CheckPtr@InternalBasePtr@Object@@IEBAXXZ` and
# `?Serialize@InternalBasePtr@Object@@QEAAXAEAVSerializer@IO@@@Z`. All four are
# in WAVE3.md's NO-GO handle-table tier, which is exactly why they are off.
#
# Keyed by file -> the gate variables whose `if (<var>) { ... }` body is opt-in.
GATED_OFF_BLOCKS = {
    "src/engine/object/internal.cpp": ("table", "shadow", "checkPtr"),
}

# The MINT is a third gating shape and the first two could not describe it.
#
# standalone/rt_data_mint.cpp registers the exported DATA symbols -- ??_7
# vtables, ?_typeInfo@ BaseTypeInfo, and the .data statics -- by LOOPING over
# the generated tables in rt_data_mint_tables.inc, so the names reach
# RegisterRuntimeExport as `v.symbol` / `t.symbol` / `s.symbol`.
#
# BEFORE THIS, THE CENSUS REPORTED THEM UNDER THE NON-LITERAL WARNING, whose
# text is "These ports are LIVE in the process but INVISIBLE to this census, so
# every number above UNDERSTATES what is actually registered." **Both halves of
# that were wrong here.** They are not live -- RegisterMintedData() runs only
# under BANISHED_RT_MINT=1, and this script's own rule is that "a gated
# registration is not a port". And they are not an understated PORT count at
# all: a vtable and a BaseTypeInfo have no body to reimplement, which is the
# whole premise of rt_data_mint.h. The warning was pointing a reader at a
# shortfall that does not exist while saying nothing about the 197 real data
# symbols that are genuinely not live yet.
#
# So the mint gets its own line, counted from the generated table where every
# symbol IS a literal, and it is kept OUT of the non-literal warning.
MINT_FILE   = "standalone/rt_data_mint.cpp"
MINT_TABLES = "standalone/rt_data_mint_tables.inc"

# Wave 10 lane MINTSEL made registration PER-ROW, and this file is the list of
# rows registered unconditionally on every run. Those rows are ordinary ports
# and are already counted as such by the main scan above: the file spells each
# symbol as a literal at its RegisterRuntimeExport call site precisely so that
# no tool change is needed to see them. It is read here only to keep the mint's
# own summary line honest about how many rows are still unregistered.
MINT_PROMOTED_FILE = "standalone/reg_mint_promoted.cpp"


def mint_promoted_count(repo):
    """Minted rows promoted to unconditional registration."""
    path = os.path.join(repo, MINT_PROMOTED_FILE.replace("/", os.sep))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return 0
    return len(set(re.findall(r'RtMintPromotionStorage\(\s*"([^"]+)"', text)))


# The three tables whose rows ARE the minted symbols. Everything else in the
# .inc that looks like a mangled name is a vtable SLOT TARGET -- the function
# each slot points at -- and those are ordinary members, not minted data. A
# first cut at this counted every mangled string in the file and reported 637
# where the truth is 197; the generated header says "55 vtables, 104
# BaseTypeInfo, 38 storage symbols" and disagreeing with it by 3x is exactly the
# kind of number that gets quoted once and then believed.
MINT_TABLE_ARRAYS = ("kMintVtables", "kMintTypeInfos", "kMintStorage")


def mint_symbol_count(repo):
    """Minted exported-data symbols, counted from the generated tables.

    Counted from the .inc rather than the .cpp because that is where the names
    are; the .cpp only walks them. Only rows INSIDE the three symbol tables are
    counted -- see MINT_TABLE_ARRAYS."""
    path = os.path.join(repo, MINT_TABLES.replace("/", os.sep))
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return 0

    total, named = 0, 0
    for arr in MINT_TABLE_ARRAYS:
        m = re.search(r'\b' + arr + r'\s*\[\s*\]\s*=\s*\{', text)
        if not m:
            continue
        # Brace-match the initialiser so a row count cannot run off the end of
        # the table into the next one.
        i, depth = m.end() - 1, 0
        while i < len(text):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = text[m.end():i]
        # Rows are `{ ... }` at the table's own nesting level. A row whose first
        # field is a STRING carries an exported name; a row whose first field is
        # `nullptr` is an UNEXPORTED slot the mint knows only by address.
        #
        # Both are real and they are not interchangeable, so both are returned.
        # Counting only the named ones gave 193 against the generated header's
        # 197 -- right direction, wrong amount, which is the shape that reads as
        # success and hides four rows. The four are the nullptr-symbol slots.
        total += len(re.findall(r'\{\s*(?:"|nullptr)', body))
        named += len(re.findall(r'\{\s*"', body))
    return total, named


def _add_detour(ports, instruments, label, key, rel):
    """File a detour under `ports` or `instruments` according to its label.

    A label is trusted only when it carries the `[port]` marker the tree uses
    for a real takeover. Anything else -- including an UNLABELLED one -- is an
    instrument, because the safe default here is to under-credit: a probe
    counted as a port silently inflates the headline number, while a port
    counted as a probe shows up as a missing row somebody chases.
    """
    bucket = ports if label.endswith("[port]") else instruments
    bucket.setdefault(key, set()).add(rel)


def gated_block_spans(text, gate_vars):
    """Byte spans of every `if (<gate>) { ... }` body, brace-matched.

    Brace matching rather than a regex because the bodies contain both braces
    and strings; strip_comments() has already run, so only string literals can
    hide a brace, and those are skipped here."""
    spans = []
    for var in gate_vars:
        for m in re.finditer(r'\bif\s*\(\s*%s\s*\)\s*\{' % re.escape(var), text):
            i = m.end() - 1
            depth = 0
            while i < len(text):
                ch = text[i]
                if ch == '"':
                    lit = STRING_LITERAL.match(text, i)
                    i = lit.end() if lit else i + 1
                    continue
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        spans.append((m.start(), i))
                        break
                i += 1
    return spans


def strip_comments(text):
    """Blank out C/C++ comments, preserving length so byte offsets still line up.

    Added 2026-09-06 (Wave 5). The census reported a port row literally named
    `<literal>` -- a function that does not exist -- because four source files
    document this tool's own match pattern inside a comment. The regex matched
    the comment text. One fake row in the tool this project treats as
    authoritative over any agent's "all N ported" claim is exactly the kind of
    small wrongness that makes the true rows less believable, so it is fixed
    here rather than by rewording the four comments: a reworded comment fixes
    the four that exist, this fixes the fifth nobody has written yet.

    Comment bodies are replaced with SPACES rather than deleted so that every
    match offset stays valid for concat_literal(), which reads forward from
    m.end() to join adjacent string literals.

    String and character literals are tracked, so a "//" or "/*" inside one (a
    URL, a Windows path) is not mistaken for the start of a comment.
    """
    BACKSLASH = chr(92)
    out = list(text)
    i, n = 0, len(text)
    state = None          # None | '"' | "'" | '//' | '/*'
    while i < n:
        c = text[i]
        if state is None:
            if c == '"' or c == "'":
                state = c
            elif c == '/' and i + 1 < n and text[i + 1] == '/':
                state = '//'
                out[i] = out[i + 1] = ' '
                i += 2
                continue
            elif c == '/' and i + 1 < n and text[i + 1] == '*':
                state = '/*'
                out[i] = out[i + 1] = ' '
                i += 2
                continue
        elif state == '"' or state == "'":
            if c == BACKSLASH:
                i += 2
                continue
            if c == state:
                state = None
        elif state == '//':
            if c == '\n':
                state = None
            else:
                out[i] = ' '
        elif state == '/*':
            if c == '*' and i + 1 < n and text[i + 1] == '/':
                out[i] = out[i + 1] = ' '
                state = None
                i += 2
                continue
            if c != '\n':
                out[i] = ' '
        i += 1
    return ''.join(out)


def scan():
    called_exports, called_addrs, called_ordinals = {}, {}, {}
    ported_exports, ported_addrs, ported_ordinals = {}, {}, {}
    gated_exports, gated_addrs = {}, {}
    detoured = {}
    nonliteral_regs = {}   # file -> [identifier, ...] the census cannot resolve
    nonliteral_calls = {}  # same, for ResolveExport CALL sites
    nonliteral_detours = {}  # same, for InstallRuntimeDetour* targets
    instrument_detours = {}  # probe:/selftest:/[allocshadow] -- NOT ports

    for path in iter_sources():
        rel = os.path.relpath(path, REPO).replace(os.sep, "/")
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue

        # Match against CODE only. A comment that quotes this tool's own match
        # pattern used to produce a phantom port row -- see strip_comments().
        text = strip_comments(text)

        # Same-file `const char* const kX = "?Mangled@..."` table. Built HERE,
        # before the call scans, because BOTH the call side and the
        # registration side need it -- see RE_EXPORT_NONLITERAL.
        name_consts = {}
        for m in RE_NAME_CONST.finditer(text):
            lit = concat_literal(text, m.end())
            if lit:
                name_consts[m.group(1)] = lit

        for m in RE_EXPORT.finditer(text):
            name = concat_literal(text, m.end())
            if name:
                called_exports.setdefault(name, set()).add(rel)
        for m in RE_EXPORT_NONLITERAL.finditer(text):
            ident = m.group(1)
            if ident in name_consts:
                called_exports.setdefault(name_consts[ident], set()).add(rel)
            elif ident not in ("mangled", "name", "sym", "symbol", "mangledName"):
                # Those are PARAMETER names in the declarations and definitions
                # of ResolveExport and of InstallRuntimeDetourExport, forwarding
                # a caller's string -- not call sites naming a symbol.
                nonliteral_calls.setdefault(rel, []).append(ident)
        # The macro doors (RT_EXPORT_ONCE / RT_DATA_SLOT / RtResolveCached).
        # These are live cutover dependencies exactly as ResolveExport is; see
        # RE_MACRO_DOOR above for what their invisibility cost.
        for m in RE_MACRO_DOOR.finditer(text):
            lit = first_literal_after(text, m.end())
            if lit is None:
                continue
            name = concat_literal(text, lit)
            if name:
                called_exports.setdefault(name, set()).add(rel)
        for m in RE_RESOLVE_CACHED.finditer(text):
            name = concat_literal(text, m.end())
            if name:
                called_exports.setdefault(name, set()).add(rel)
        if rel.startswith("src/render/dx11/"):
            for m in RE_DX11_MANGLED.finditer(text):
                called_exports.setdefault(m.group(1), set()).add(rel)
        for m in RE_ORDINAL.finditer(text):
            called_ordinals.setdefault(int(m.group(1)), set()).add(rel)
        for m in RE_ADDR.finditer(text):
            called_addrs.setdefault(int(m.group(1), 16), set()).add(rel)
        for m in RE_ADDR_RAW.finditer(text):
            called_addrs.setdefault(int(m.group(1), 16), set()).add(rel)

        # Registrations inside an opt-in `if (<flag>)` block are gated the same
        # way an opt-in FILE is -- see GATED_OFF_BLOCKS.
        block_spans = gated_block_spans(text, GATED_OFF_BLOCKS.get(rel, ()))
        def in_gated_block(pos):
            return any(a <= pos <= b for a, b in block_spans)
        # A registration inside an OPT-IN file is NOT a live port: its
        # Register function is only called when an env var arms it, and it
        # returns 0 otherwise. Counting it would repeat the exact overstatement
        # this census exists to prevent -- see GATED_OFF_FILES.
        gated = rel in GATED_OFF_FILES


        for m in RE_REG_EXPORT.finditer(text):
            name = concat_literal(text, m.end())
            if name:
                g = gated or in_gated_block(m.start())
                (gated_exports if g else ported_exports).setdefault(name, set()).add(rel)
        for m in RE_REG_EXPORT_NONLITERAL.finditer(text):
            # `const` is the parameter type in the DECLARATIONS of
            # RegisterRuntimeExport itself (rt_port.h, resolve.h,
            # resolve_standalone.cpp), not a call passing a constant.
            if m.group(1) == "const":
                continue
            # The MINT is gated DATA, not an unseen port -- see MINT_FILE. It
            # gets its own reported line; leaving it here made the warning claim
            # the port numbers were understated, which was false in both
            # directions (not live, and not functions).
            if rel == MINT_FILE:
                continue
            # rt_selftest.cpp deliberately registers a FAKE name and one real
            # one to prove the seam works end to end. It is a self-test, not a
            # port, and counting it would be wrong -- so it is excluded rather
            # than left to emit a permanent warning that trains readers to
            # ignore this block.
            if rel == "standalone/rt_selftest.cpp":
                continue
            resolved = name_consts.get(m.group(1))
            if resolved:
                g = gated or in_gated_block(m.start())
                (gated_exports if g else ported_exports).setdefault(
                    resolved, set()).add(rel)
                continue
            nonliteral_regs.setdefault(rel, []).append(m.group(1))
        for m in RE_REG_ORDINAL.finditer(text):
            ported_ordinals.setdefault(int(m.group(1)), set()).add(rel)
        for m in RE_REG_ADDR.finditer(text):
            g = gated or in_gated_block(m.start())
            (gated_addrs if g else ported_addrs).setdefault(int(m.group(1), 16), set()).add(rel)

        # NOT EVERY DETOUR IS A PORT DETOUR, and counting them together
        # credited two instruments as ports.
        #
        # The tree already distinguishes them by label, and consistently:
        #   "...[port]"          a reimplementation taking over retail's callers
        #   "probe:..."          a recorder installed to observe a call
        #   "selftest:..."       a deliberate fake proving the seam works
        #   "...[allocshadow]"   an allocator-shadow instrument
        #
        # Only the first is what the `detour` column means ("of those PORTED,
        # ALSO patched so the DLL's own callers come to us"). Before this,
        # `probe:Audio::Device::Stop` -- a detour to a RECORDER, which is a test
        # fake by construction -- counted as a detour, and once detour targets
        # started conferring `ported` it would have counted as a port too. An
        # instrument is not a port, and a census that says otherwise is the
        # thing this file exists to prevent.
        for m in RE_DETOUR_ADDR.finditer(text):
            _add_detour(detoured, instrument_detours, m.group(1),
                        int(m.group(2), 16), rel)
        for m in RE_DETOUR_EXPORT.finditer(text):
            _add_detour(detoured, instrument_detours, m.group(1),
                        m.group(2), rel)

        # Detour targets spelled as a same-file constant. Resolved rather than
        # punished, exactly as RE_NAME_CONST does for mangled names -- naming
        # the address once and using it at both the install site and the byte
        # guard is good practice, and a census that could not follow it printed
        # `detour 0` for a detour that was demonstrably installed.
        addr_consts = {mm.group(1): int(mm.group(2), 16)
                       for mm in RE_ADDR_CONST.finditer(text)}
        for m in RE_DETOUR_NONLITERAL.finditer(text):
            ident = m.group(2)
            if ident in addr_consts:
                _add_detour(detoured, instrument_detours, m.group(1),
                            addr_consts[ident], rel)
            else:
                nonliteral_detours.setdefault(rel, []).append(ident)

    return dict(
        nonliteral_regs=nonliteral_regs,
        nonliteral_calls=nonliteral_calls,
        nonliteral_detours=nonliteral_detours,
        instrument_detours=instrument_detours,
        gated_exports=gated_exports, gated_addrs=gated_addrs,
        called_exports=called_exports, called_addrs=called_addrs,
        called_ordinals=called_ordinals, ported_exports=ported_exports,
        ported_addrs=ported_addrs, ported_ordinals=ported_ordinals,
        detoured=detoured,
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", help="write per-function rows to this CSV")
    ap.add_argument("--unported", action="store_true",
                    help="list every called-but-not-ported function")
    ap.add_argument("--namespace", help="restrict the listings to one namespace")
    ap.add_argument("--allow-degraded", action="store_true",
                    help="proceed without pefile/the DLL. The address-keyed "
                         "rows become invisible and BOTH numbers are "
                         "understated -- the census says so on stderr. Do not "
                         "publish a number produced this way.")
    args = ap.parse_args()

    global ALLOW_DEGRADED
    ALLOW_DEGRADED = args.allow_degraded

    d = scan()

    # An address that names an EXPORT is the same function as that export.
    # Resolving it lets the row land in its real namespace, and lets a function
    # our tree reaches BOTH ways be counted once instead of twice. See
    # load_export_addrs() for the two failure modes this fixes.
    addr_to_name = load_export_addrs()

    merged = {}   # canonical key -> [kind, key, ns, ported, detoured, set(files)]

    def add_entity(kind, key, ns, ported, detoured, files):
        e = merged.get(key)
        if e is None:
            merged[key] = [kind, key, ns, ported, detoured, set(files)]
        else:
            e[3] = e[3] or ported
            e[4] = e[4] or detoured
            e[5].update(files)

    for name, files in d["called_exports"].items():
        add_entity("export", name, namespace_of(name),
                   name in d["ported_exports"], name in d["detoured"], files)
    for addr, files in d["called_addrs"].items():
        name = addr_to_name.get(addr)
        ported = addr in d["ported_addrs"]
        detoured = addr in d["detoured"]
        if name:
            # Named export reached by address. Its port may be registered under
            # either door, so consult both.
            add_entity("export", name, namespace_of(name),
                       ported or (name in d["ported_exports"]),
                       detoured or (name in d["detoured"]), files)
        else:
            add_entity("addr", "0x%x" % addr, "internal", ported, detoured, files)

    # A DETOURED PORT IS NEVER CALLER-LESS, and before this it was invisible
    # twice over: absent from the namespace table and listed as a suspected
    # vestigial resolve.
    #
    # `called` counts functions our tree resolves through a door
    # (ResolveExport / ResolveAddr / an ordinal). A detour creates callers a
    # different way -- it patches the DLL, so RETAIL'S OWN internal callers
    # arrive at our body -- and those callers are the entire point of
    # installing one. Wave 10's UI::TabElement::UpdateContainers is the case
    # that exposed it: ported, registered under both doors, detoured, verified
    # against the DLL, exercised in a live run... and printed as `detour 0`
    # with the function itself sitting in the "ports with no caller" list,
    # because our tree calls it as a direct C++ call rather than through a
    # resolve door.
    #
    # So a detour target is folded in here as called + ported + detoured. This
    # is not crediting unrun code: the detour is what makes it run.
    for dkey, dfiles in d["detoured"].items():
        if isinstance(dkey, int):
            name = addr_to_name.get(dkey)
            if name:
                add_entity("export", name, namespace_of(name), True, True, dfiles)
            else:
                add_entity("addr", "0x%x" % dkey, "internal", True, True, dfiles)
        else:
            add_entity("export", dkey, namespace_of(dkey), True, True, dfiles)

    rows = [(k, key, ns, p, det, sorted(f)) for k, key, ns, p, det, f in merged.values()]
    for ordn, files in d["called_ordinals"].items():
        # An ordinal carries no name, so no namespace can be inferred from it —
        # its own bucket, not lumped in with names that failed to parse.
        rows.append(("ordinal", str(ordn), "ordinal",
                     ordn in d["ported_ordinals"], False, sorted(files)))

    # Ports with no caller in our tree are worth surfacing: for the exe port a
    # registration nobody asks for was usually a vestigial resolve, and the same
    # trap exists here.
    # A port is an orphan only if NEITHER door has a caller. Registering both
    # the name and the address for one function is deliberate insurance in this
    # project, and before this was fixed it made every such function report as
    # caller-less forever -- which trained the reader to ignore the list that
    # exists to catch reference_uncalled_port_is_not_verified.md.
    name_of_called_addr = {addr_to_name[a] for a in d["called_addrs"] if a in addr_to_name}
    called_export_names = set(d["called_exports"]) | name_of_called_addr
    # Detoured names/addresses have retail's callers, so they are not orphans.
    detoured_names = {k for k in d["detoured"] if not isinstance(k, int)}
    detoured_names |= {addr_to_name[k] for k in d["detoured"]
                       if isinstance(k, int) and k in addr_to_name}
    detoured_addrs = {k for k in d["detoured"] if isinstance(k, int)}
    detoured_addrs |= {a for a, nm in addr_to_name.items() if nm in detoured_names}
    called_export_names |= detoured_names
    orphan_exports = sorted(set(d["ported_exports"]) - called_export_names)

    addr_of_called_name = {a for a, nm in addr_to_name.items() if nm in d["called_exports"]}
    called_addr_set = set(d["called_addrs"]) | addr_of_called_name
    called_addr_set |= detoured_addrs
    orphan_addrs = sorted(set(d["ported_addrs"]) - called_addr_set)

    per_ns = collections.defaultdict(lambda: {"called": 0, "ported": 0, "detoured": 0})
    for kind, key, ns, ported, detoured, files in rows:
        b = per_ns[ns]
        b["called"] += 1
        b["ported"] += 1 if ported else 0
        b["detoured"] += 1 if detoured else 0

    print("Runtime-x64.dll port census")
    print("=" * 78)
    print("%-14s %8s %8s %8s %8s  %s" %
          ("namespace", "total", "called", "ported", "detour", "called covered"))
    print("-" * 78)

    order = sorted(per_ns, key=lambda n: (-per_ns[n]["called"], n))
    tot_called = tot_ported = tot_detour = 0
    for ns in order:
        b = per_ns[ns]
        total = TOTAL_FUNCTIONS.get(ns, "")
        pct = (100.0 * b["ported"] / b["called"]) if b["called"] else 0.0
        print("%-14s %8s %8d %8d %8d  %5.1f%%" %
              (ns, total, b["called"], b["ported"], b["detoured"], pct))
        tot_called += b["called"]
        tot_ported += b["ported"]
        tot_detour += b["detoured"]

    print("-" * 78)
    pct = (100.0 * tot_ported / tot_called) if tot_called else 0.0
    print("%-14s %8d %8d %8d %8d  %5.1f%%" %
          ("TOTAL", sum(TOTAL_FUNCTIONS.values()), tot_called, tot_ported, tot_detour, pct))
    print()
    print("total  = functions in the DLL (PLAN.md §3.1 Ghidra census; blank for "
          "non-namespaced internals)")
    print("called = distinct functions our tree resolves today")
    ng = len(d["gated_exports"]) + len(d["gated_addrs"])
    if ng:
        print()
        print("%d registration(s) are present in the source but NOT counted above:" % ng)
        for f in sorted(GATED_OFF_FILES):
            n = sum(1 for v in list(d["gated_exports"].values()) + list(d["gated_addrs"].values())
                    if f in v)
            if n:
                print("  %-40s %d  (opt-in; its Register function returns 0 unless armed)" % (f, n))
        for f, gates in sorted(GATED_OFF_BLOCKS.items()):
            n = sum(1 for v in list(d["gated_exports"].values()) + list(d["gated_addrs"].values())
                    if f in v)
            if n:
                print("  %-40s %d  (opt-in; inside if(%s))"
                      % (f, n, ") / if(".join(gates)))
        print("  A gated registration is not a port. Arm it, measure it, then move it.")

    # The mint is a THIRD gating shape and it used to be reported as a port
    # shortfall, which was wrong twice over -- see MINT_FILE.
    nmint, nmint_named = mint_symbol_count(REPO)
    npromoted = mint_promoted_count(REPO)
    if nmint:
        print()
        print("%d DATA slot(s) are MINTED (%d carry an exported name; the other %d "
              "are UNEXPORTED and known only by address):"
              % (nmint, nmint_named, nmint - nmint_named))
        print("  %-40s (vtables + BaseTypeInfo + .data statics, from %s)"
              % (MINT_FILE, MINT_TABLES))
        print("  %d PROMOTED, i.e. registered on every run with no env var (%s)."
              % (npromoted, MINT_PROMOTED_FILE))
        print("  %d still registered only under BANISHED_RT_MINT=1, so by this "
              "script's own" % (nmint - npromoted))
        print("  rule those are NOT ports.")
        print()
        print("  ACCOUNTING, corrected 2026-09-09 (Wave 10 lane MINTSEL). This note")
        print("  used to end 'they therefore cannot move the columns above at all,")
        print("  in either direction'. That was wrong in both directions:")
        print("    * A minted row ALWAYS moved the *called* side. MINT_FILE is")
        print("      excluded from the ported scan but not the called scan, so an")
        print("      unregistered minted row that our tree resolves has always")
        print("      appeared under --unported. That is the correct accounting --")
        print("      the call really is unsatisfied by our code -- but the note")
        print("      denied it, and lane UIDATA spent a lane discovering that the")
        print("      13 UI rows it was sent after were of exactly that shape.")
        print("    * A PROMOTED row now moves the *ported* side too, and is counted")
        print("      by the ordinary scan above rather than here: reg_mint_promoted.cpp")
        print("      spells each symbol as a literal at its RegisterRuntimeExport")
        print("      call site so that no special case in this script is needed.")
        print("  What remains true is that a minted row has no BODY to reimplement,")
        print("  so it is not a shortfall in the function port.")
    nl = d.get("nonliteral_regs") or {}
    if nl:
        total = sum(len(v) for v in nl.values())
        print()
        print("*** WARNING: %d RegisterRuntimeExport call(s) pass a NON-LITERAL name."
              % total)
        print("*** These ports are LIVE in the process but INVISIBLE to this census,")
        print("*** so every number above UNDERSTATES what is actually registered.")
        for f in sorted(nl):
            print("  %-44s %s" % (f, ", ".join(sorted(set(nl[f])))))
        print("  A same-file `const char* const kX = \"?Mangled...\";` IS resolved now;")
        print("  these are something else -- a name built at runtime, or from a header.")
        print("  Fix by spelling the mangled name as a literal at the call site.")
        print("  (Wave 4 lane D hit this: 6 ports registered through kName* constants")
        print("   reported as IO 30/41 when 36 were registered. Nothing warned.)")
        print()
    nlc = d.get("nonliteral_calls") or {}
    if nlc:
        total = sum(len(v) for v in nlc.values())
        print()
        print("*** WARNING: %d ResolveExport call(s) pass an identifier this script"
              % total)
        print("*** cannot follow, so `called` -- and therefore `ported` -- UNDERSTATE.")
        for f in sorted(nlc):
            print("  %-44s %s" % (f, ", ".join(sorted(set(nlc[f])))))

    inst = d.get("instrument_detours") or {}
    if inst:
        print()
        print("%d detour(s) are INSTRUMENTS, not ports, and are excluded above:" % len(inst))
        for k in sorted(inst, key=lambda x: (isinstance(x, str), str(x))):
            shown = ("0x%x" % k) if isinstance(k, int) else k
            print("  %-44s %s" % (shown, ", ".join(sorted(inst[k]))))
        print("  A `probe:` recorder, a `selftest:` fake and an `[allocshadow]` shadow")
        print("  all patch the DLL, and none of them is a reimplementation. Only a")
        print("  label ending in `[port]` counts toward the detour column.")

    nld = d.get("nonliteral_detours") or {}
    if nld:
        total = sum(len(v) for v in nld.values())
        print()
        print("*** WARNING: %d detour target(s) are neither a literal address nor a"
              % total)
        print("*** same-file `constexpr uintptr_t kX = 0x...;` this script can follow.")
        print("*** The detour column UNDERSTATES by at least that much.")
        for f in sorted(nld):
            print("  %-44s %s" % (f, ", ".join(sorted(set(nld[f])))))

    print("ported = of those, taken over by a RegisterRuntime* call")
    print("detour = of those, ALSO patched in the DLL so its own internal callers "
          "come to us")

    if orphan_exports or orphan_addrs:
        print()
        print("Ports with no caller in our tree (%d exports, %d addresses) — check "
              "each is not\na vestigial resolve:" % (len(orphan_exports), len(orphan_addrs)))
        for n in orphan_exports:
            print("  export  %s" % n)
        for a in orphan_addrs:
            print("  addr    0x%x" % a)
        print("  All 10 of these have a WRITTEN VERDICT as of 2026-09-07 -- see the")
        print("  'ports with no caller' table in docs/history/runtime_port/WAVE8_5.md section 2. FIVE are")
        print("  exercised by a direct C++ call from a sibling port (the two Quaternion")
        print("  members and Transform::FromDirection; Audio::Scene::PauseEffect and")
        print("  ::StopEffect, called by Scene::Pause and Scene::StopEffect3D exactly as")
        print("  retail calls them, and swept by ProbeAudioScene). Three are deliberately")
        print("  dead members of the IO::Serializer forwarder family (verified structurally")
        print("  at registration), and two are verified by standalone/rt_orphan_probe.cpp")
        print("  and by disassembly. A NEW name appearing here has NOT been triaged.")

    if args.unported:
        print()
        sel = [r for r in rows if not r[3]]
        if args.namespace:
            sel = [r for r in sel if r[2] == args.namespace]
        print("Called but not ported (%d):" % len(sel))
        for kind, key, ns, _ported, _detoured, files in sorted(sel, key=lambda r: (r[2], r[1])):
            print("  %-8s %-12s %s" % (kind, ns, key))
            print("           %s" % ", ".join(files[:4]) + (" …" if len(files) > 4 else ""))

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh)
            w.writerow(["kind", "key", "namespace", "ported", "detoured", "callers"])
            for kind, key, ns, ported, detoured, files in sorted(rows, key=lambda r: (r[0], r[2], r[1])):
                w.writerow([kind, key, ns, int(ported), int(detoured), ";".join(files)])
        print()
        print("wrote %s (%d rows)" % (args.csv, len(rows)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
