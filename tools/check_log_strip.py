#!/usr/bin/env python3
"""Fail the log-stripped builds BEFORE they break on a qualified `std::printf`.

WHY THIS EXISTS
---------------
The perf and release builds (CMakeLists.txt) both compile with
`-DBANISHED_STRIP_LOGS -include src/app/log_strip.h`, which does:

    #define printf(...) ((void)0)

The preprocessor cannot replace a *qualified* name, so `std::printf(...)` does
not get stripped -- it expands into `std:: ((void)0)(...)` and the translation
unit dies with "expected unqualified-id before '(' token", pointing at
log_strip.h rather than at the offending file. That misdirection is why it takes
a while to diagnose every time.

`log_strip.h`'s own header comment has said "all call sites must use unqualified
`printf`" since it was written. It was still violated twice in two days:

  * Wave 11 follow-up 1 (lane ARM) found `make standalone-perf` had been broken
    for some unknown period -- so `sa-stress` was unrunnable and every "no perf
    regression" claim in that window was never measured on the perf exe.
  * Wave 11 follow-up 2 (lane IOSTREAM) hit it AGAIN one lane later, and four of
    the six offending files were its own.

A rule that lives only in a comment gets broken. This makes it a build gate.

THE COMMENT STRIPPER IS THE POINT
---------------------------------
A naive `grep -rn "std::printf"` reports THREE hits on a clean tree today, and
all three are PROSE -- two in log_strip.h stating the rule, one in
ui_dialog_stub_factories.cpp explaining why the file obeys it. A checker without
a stripper would fail a clean build and send someone to "fix" documentation.
That failure mode is on the record here: a checker with no comment stripper
manufactures work, and its false positives cluster on exactly the comments that
describe the thing it looks for.

So: comments and string literals are removed before matching, and the stripper
carries its own controls (`--self-test`).

USAGE
    python tools/check_log_strip.py            # gate; exit 1 on a violation
    python tools/check_log_strip.py --self-test  # prove the stripper works
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOTS = ["runtime", "code", "engine", "standalone", "dx11"]
EXTS = (".cpp", ".h", ".hpp", ".cc", ".c")

# Only `printf` is macro'd by log_strip.h. fprintf/sprintf/snprintf are NOT, so
# `std::fprintf` is legal and must not be flagged -- flagging it would be the
# mirror error: a gate that refuses correct code.
PATTERN = re.compile(r"\bstd\s*::\s*printf\b")


def strip(src: str) -> str:
    """Blank out // comments, /* */ comments and string/char literals.

    Newlines are preserved so reported line numbers stay true.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                i += 1
        elif c == "/" and nxt == "*":
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                if src[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        elif c in ('"', "'"):
            quote = c
            i += 1
            while i < n and src[i] != quote:
                if src[i] == "\\":
                    i += 1
                if i < n and src[i] == "\n":
                    out.append("\n")
                i += 1
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan():
    bad = []
    for root in ROOTS:
        d = os.path.join(REPO, root)
        if not os.path.isdir(d):
            continue
        for dirpath, _dirs, files in os.walk(d):
            if "build" in dirpath.replace("\\", "/").split("/"):
                continue
            for f in files:
                if not f.endswith(EXTS):
                    continue
                p = os.path.join(dirpath, f)
                try:
                    src = open(p, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                if "std" not in src:
                    continue
                for ln, line in enumerate(strip(src).split("\n"), 1):
                    if PATTERN.search(line):
                        rel = os.path.relpath(p, REPO).replace(os.sep, "/")
                        bad.append((rel, ln, line.strip()))
    return bad


def self_test() -> int:
    """Both controls. A gate nobody has seen fail is not known to work."""
    neg = '// std::printf("in a line comment")\n/* std::printf("block") */\nconst char* s = "std::printf in a literal";\n'
    pos = 'void f() { std::printf("real call"); }\n'
    ok = True

    hits = [l for l in strip(neg).split("\n") if PATTERN.search(l)]
    print(f"  negative control (prose only, must find 0): found {len(hits)}"
          f" -> {'PASS' if not hits else 'FAIL'}")
    ok &= not hits

    hits = [l for l in strip(pos).split("\n") if PATTERN.search(l)]
    print(f"  positive control (one real call, must find 1): found {len(hits)}"
          f" -> {'PASS' if len(hits) == 1 else 'FAIL'}")
    ok &= len(hits) == 1

    # The gate must also be quiet on the real tree as it stands today, and the
    # tree today contains all three prose mentions. If this goes red, either the
    # stripper regressed or someone really did add a qualified call.
    live = scan()
    print(f"  live tree (must find 0): found {len(live)}"
          f" -> {'PASS' if not live else 'FAIL'}")
    for rel, ln, txt in live:
        print(f"      {rel}:{ln}: {txt}")
    ok &= not live

    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    bad = scan()
    if not bad:
        return 0
    print("ERROR: qualified `std::printf` cannot survive the log-stripped build.", file=sys.stderr)
    print("       src/app/log_strip.h does `#define printf(...) ((void)0)`, and the", file=sys.stderr)
    print("       preprocessor cannot replace a qualified name. Use unqualified `printf`.", file=sys.stderr)
    print("       (The compiler would have blamed log_strip.h, not these lines.)", file=sys.stderr)
    for rel, ln, txt in bad:
        print(f"  {rel}:{ln}: {txt}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
