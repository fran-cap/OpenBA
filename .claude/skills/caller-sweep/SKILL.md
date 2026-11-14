---
name: caller-sweep
description: Prove who calls, overrides or binds a function before deleting it, renaming it, changing its signature or writing "no other caller" - the binding forms to grep, then the nm proof over the build's objects (one cached pass, about 7 s, then 0.5 s per symbol) and the retail CALL census with objdump when Ghidra is down. Use before removing a function or a PORT GAP arm, for "who calls X", "is this still used", "all overrides of slot N", "every seam of ext.<id>", and when a PR or a hand-in claims "only caller" or "dead".
---

# Caller sweep: grep finds, nm proves

Grep finds the call sites (recall was 1.0 on four measured questions) but proves nothing about what the
compiler bound; `nm -u` on the objects is the proof. Run it in the worktree whose `build/cmake-debug`
is current (`tools/build.sh build`, or `unit` for the test objects; a stale object answers for old code).

## 1. Grep every binding form (Grep tool, path `src` then `tests`, never the repo root)
1. Qualified calls `Ns::Fn(` and unqualified calls inside `namespace Ns`.
2. Policy-struct and typed-template wrappers that forward to the erased core (`set_avl_pathid.cpp`-style).
3. Address taken: `&Ns::Fn`, a function-pointer field or `_ext<What>` static armed by the game.
4. Indirect: a caller of a caller (`Erase` -> `Retrace`), a message-callback or vtable slot.
5. Overrides: `::Fn(` with the slot's exact parameter list, in `src` AND `tests/unit` (test doubles).
Comments, docs, `// 0x14...` provenance and strings are not callers.

## 2. nm proof (debug build; WinLibs nm, not the MSYS2 one on PATH)
```bash
B=build/cmake-debug; C=build/nm-cache; NM=/c/winlibs-gcc16/mingw64/bin/nm.exe; mkdir -p $C
O="$B/CMakeFiles/OpenBA.dir $B/CMakeFiles/unit.dir"
if [ ! -s $C/undef.txt ] || [ -n "$(find $O -name '*.obj' -newer $C/undef.txt | head -1)" ]; then
  find $O -name '*.obj' -print0 | xargs -0 -n 200 $NM -A -C -u > $C/undef.txt
  find $O -name '*.obj' -print0 | xargs -0 -n 200 $NM -A -C --defined-only > $C/def.txt
fi
grep -F ' U rtsetavl::Retrace(' $C/undef.txt | cut -d: -f1 | sort -u      # objects that bind it
grep -F '::Read(int, void*)' $C/def.txt | grep -E ' [TtW] ' | sed -E 's#^(.*)\.obj:[0-9a-f]+ [TtW] #\1  #' | sort -u
```
- Expected: one `CMakeFiles/<target>.dir/<source>.obj` per binding TU (6 lines for `rtsetavl::Retrace(`);
  the second form lists `<object>  <Class>::Read(int, void*)` per definition (37 for `IO::Stream` slot 3).
  The index pass takes about 7 s for 1,836 objects (124 MB, gitignored); each query about 0.5 s.
  Never loop `nm` per object: 1,836 processes took 58 s for one symbol.
- End the prefix with `(` so `Retrace` does not match `RetraceAll`; quote the demangled signature.
- Every grep TU must appear in the nm list and every nm TU in the grep list. A TU only in nm is a
  binding the grep missed (a macro, a wrapper); a TU only in grep is dead text or an inline copy.
- Blind spots: inline / template bodies emitted in the caller's own TU (defined, never `U`), calls
  through a pointer or vtable slot, and code in neither target (`render-backend`, Android, macOS).

The block is Windows-only (WinLibs paths, `.obj` objects).

**clangd first pass (optional).** It is a first pass only, never a proof of absence: it misses
template-dependent calls (27-36 of 51 for `Retrace`), so grep + nm above still decide.
`python -I .claude/skills/caller-sweep/scripts/lsp_query.py --index refs <relpath> <line> <name>`
(also `callers --depth N`, `impl`, `subtypes`, `def`, `wsym`; the script's header lists them).
- Needs `build/cmake-debug/compile_commands.json` (a debug `tools/build.sh build`; a file added since the
  last configure gets a guessed command) and a `.clangd` at the checkout root: on Windows copy
  `.claude/skills/caller-sweep/clangd.windows.yaml` there as `.clangd` (gitignored; its `--target` line is
  Windows-only, drop it on Linux / macOS).
- `--root` defaults to the git checkout of the current directory; clangd is `--clangd <exe>` or the one on
  PATH (LLVM 23.1.5 checked); `-j 2` by default so it does not compete with builds.
- About 2 s on a warm index, 33-56 s and up to 2.6 GB on a cold one (`build/cmake-debug/.cache/clangd/`).
  Without `--index` it answers for the open file only and says so on its header line.

## 3. Retail side (when the claim is "retail calls it N times")
Ghidra up: skill `ghidra` (`get_function_xrefs` by address, `program=` on every call). Ghidra down:
```bash
OBJDUMP=/c/winlibs-gcc16/mingw64/bin/objdump.exe   # the PATH objdump is MSYS2's ucrt64 one
$OBJDUMP -d --no-show-raw-insn "<game dir>/Application-x64.exe" > <scratchpad>/app.dis   # ~1 s; never Read it
grep -c -E 'call +1400c29a0\b' <scratchpad>/app.dis       # direct CALL sites; Runtime-x64.dll for 0x180...
grep -E '1400c29a0' <scratchpad>/app.dis | grep -v -E 'call ' | head   # LEA / pointer uses
```
A linear sweep misses calls hidden in mis-disassembled data; say which tool answered.

## 4. Report
`<symbol>: grep N sites in M files; nm M' objects (list); retail K CALLs (Ghidra|objdump)`, then every
mismatch with its reason. "No other caller" is written only when grep and nm agree.
