# P3 clang-tidy baseline: Windows-only code

Session document (review campaign 2026-11-03, phase P3). Scope: `src/render/dx11/**`,
`src/platform/win/**`, `src/app/**`, `src/diag/win/**`, plus the two Windows-side ext files the P0 map
names (`src/ext/better_water/*`, `src/ext/async_gpu_create/*`).

## How it ran

- Compile DB: `ninja -C build/cmake-debug-check -t compdb` (the tree had no `compile_commands.json`),
  rewritten by `scratchpad/tidy_win/mkdb.py`: `ccache g++` swapped for
  `C:/winlibs-gcc16-llvm/mingw64/bin/clang++.exe --target=x86_64-w64-mingw32`, GCC-only flags
  (`-fno-lifetime-dse`, `-ffp-contract=off`, `-MD/-MT/-MF`) dropped, `-Wno-unknown-warning-option`
  added. 70 translation units: dx11 30, platform/win 20, app 10, diag/win 6, ext 4. 0 compile errors.
- Tool: clang-tidy (LLVM 23.1.3), `--checks=-*,bugprone-*,cert-*,concurrency-*,clang-analyzer-*`,
  `--header-filter=src/(render/dx11|platform/win|app|diag/win|ext)/`. Wall time 47 s at `-P 6`.
- Raw per-TU logs and the de-duplicated hit list stay in the session scratchpad
  (`tidy_win/out/*.log`, `tidy_win/all_hits.txt`), not in the tree.

Line numbers are those of HEAD bfe14e5fe (a concurrent uncommitted edit to `devicedata.cpp` shifts lines after 1227 by +11).

## Totals (unique file:line:col:check)

**356 hits.** By directory: `src/render/dx11` 154, `src/diag/win` 82, `src/app` 75,
`src/platform/win` 38, `src/ext/better_water` 7.

| Count | Check | Triage |
|---:|---|---|
| 130 | bugprone-signed-bitwise | noise: `_flags & 1` style tests on int fields; the 15 left shifts read, all bounded (UTF-16 surrogate math after a range check, `1 << 16`, uint64 casts) |
| 99 | cert-err33-c | 68 `fflush`, 18 `snprintf` into fixed buffers (no length reuse), `freopen`/`setvbuf` of the log redirect, `atexit`, `fclose`, `fputs`: no defect |
| 34 | bugprone-easily-swappable-parameters | style |
| 17 | bugprone-unchecked-string-to-number-conversion / cert-err34-c | `atoi`/`atof` on `BANISHED_*` harness flags in `options.cpp` / `frame_limiter.cpp`; 0 on bad input is the intended default |
| 14 | bugprone-branch-clone | 12 in third-party ASSAO switch tables, 1 tweak overlay, 1 ASSAO ternary: intentional |
| 13 | bugprone-narrowing-conversions | int -> float of small sizes (ASSAO, shore strip): no defect |
| 10 | bugprone-invalid-enum-default-initialization | `D3D11_*_DESC d = {}` then fields set; 0 is inside each enum's value range (not UB) |
| 6 | cert-dcl50-cpp | C variadic log helpers (`one_write_stdio.h`, diag): format-attributed, no defect |
| 5 | bugprone-multi-level-implicit-pointer-conversion | `T**` to `void*` for memcpy/memset of pointer arrays: no defect |
| 4 | clang-analyzer-deadcode.DeadStores | ASSAO (third party) |
| 3 | clang-analyzer-core.NullPointerArithm | **finding W-3** (`devicedata.cpp:1436, 1479`) |
| 3 | bugprone-std-namespace-modification / cert-dcl58-cpp | **finding W-5** (`one_write_stdio.h:70-72`) |
| 3 | bugprone-macro-parentheses | ASSAO macro (third party), args are plain identifiers |
| 3 | bugprone-implicit-widening-of-multiplication-result | diag dumps, `i * 8` with `i < 13`: no defect |
| 2 | clang-analyzer-optin.performance.Padding | `Options`, better_water `StripSlot`: no defect |
| 2 | clang-analyzer-optin.core.EnumCastOutOfRange | `D3D_FEATURE_LEVEL(0)`: 0 lies inside the enum's value range; not UB |
| 2 | bugprone-throwing-static-initialization / cert-err58-cpp | `g_queue` (async_create) led to **finding W-6**; `g_copy` (scene_depth) no defect |
| 1 | concurrency-mt-unsafe | `options.cpp:26` env read once at boot before threads: no defect |
| 1 | clang-analyzer-optin.cplusplus.VirtualCall | ASSAO dtor calls its own (non-pure) virtual: defined |
| 1 | clang-analyzer-core.NullDereference | **finding W-4** (`devicedata.cpp:1698`) |
| 1 | bugprone-switch-missing-default-case | `main.cpp:116` exit-code switch: no defect |
| 1 | bugprone-reserved-identifier | `__cxa_pure_virtual` override in the crash handler: deliberate |
| 1 | bugprone-integer-division | ASSAO (third party), intended |

All 356 were triaged (every non-noise hit read at its line; the 130 signed-bitwise and 99 err33 hits
were scanned by line text). Analyzer / bugprone hits that are defects: 4 (W-3, W-4, W-5, W-6).
Manual review added W-1 and W-2.

## Focused review

### HRESULT handling (P0_MAP gpu_platform (c), 21 discarded)

The retail-path sites carry the "original ignores this HRESULT, reproduced" note and were not
re-litigated. The non-retail ones:

- `render/dx11/better_water/scene_depth.cpp:121` and `:254` `QueryInterface`: COM sets `*ppv` to
  null on failure; both targets are null beforehand (`Rel()` nulls `data->texture`; `texture` is a
  null-initialised local) and both consumers test for null. Cannot crash.
- `render/dx11/async_gpu_create/async_create.cpp:112` `CreateSamplerState`: writes the same
  `ImageBufferData` sampler field the synchronous retail create writes, which `ReleaseInterfaces`
  nulls first; a failure leaves null and the bind path binds a null sampler (D3D default). Cannot
  crash. Same behaviour as the retail-ignored HRESULT it replaces.

### Format / sampler tables indexed by serialized data

`ImageBuffer::Serialize` (`engine/video/imagebuffer_vtable.cpp:384-389`) reads `_format`,
`_addressU/V/W` and `_filter` as raw uint32 with no range check, so a texture resource (a mod `.pkm`
or a damaged `.rsc`) can carry any value. These are W-1 and W-2.

## Findings

| ID | Where | Class | Label | Proof |
|---|---|---|---|---|
| W-1 | `render/dx11/imagebufferdata.cpp:212,215` used at `:555,610,634,682,737,770` | OOB read (UB) | retail (Ghidra VideoDX11 0x180002a18: `MOVSXD RAX,[R8+0x30]; MOV ECX,[RBP+RAX*4-0x79]`, no bound) | UBSan (WSL clang-19) on a standalone copy: `index 9 out of bounds for type 'const int[8]'`; Windows clang `-fsanitize-trap`: illegal instruction for 9, clean for 5 |
| W-2 | `imagebufferdata.cpp:343-378` (0x180002720 sampler create) and `:825-830` (ext point sampler) | stack OOB read (UB) | retail (comment address) + ext variant | ASan+UBSan on a standalone copy: `index 3 out of bounds for type 'int[3]'`, `stack-buffer-overflow READ of size 4` |
| W-3 | `render/dx11/devicedata.cpp:1434-1437, 1477-1480` grower `base + i` before `if (!dst)` | UB null pointer arithmetic | retail (0x180005b00 comment) | clang-analyzer path (MemAlloc null); UBSan on a standalone copy: `applying non-zero offset 56 to null pointer`. Reachable only if MemAlloc returns null |
| W-4 | `devicedata.cpp:1689-1698` mode list: `modes` null when the first count is 0, second (count-query) call returns n > 0 | null deref | retail (comment: inlined `Array<DXGI_MODE_DESC>::SetLength`, null base) | clang-analyzer path; UBSan on a standalone copy: `reference binding to null pointer`. Needs the output's mode list to grow between the two calls |
| W-5 | `app/one_write_stdio.h:69-73` `namespace std { using ::BanishedOneWrite...; }` (force-included in every TU) | UB ([namespace.std]: adding declarations to std) | ext | static_only (clang-tidy) |
| W-6 | `render/dx11/async_gpu_create/async_create.cpp:70-73,121-128,154` detached workers blocked in `g_workCv.wait` while static destructors run after `main` returns | UB ([thread.condition.condvar]: destroying a cv with blocked waiters) | ext (row off by default) | static_only; a GCC 16 `_GLIBCXX_ASSERTIONS` repro exits 0 (winpthreads does not report EBUSY) |

Repro sources: `scratchpad/tidy_win/repro/*.cpp` (copies in WSL `~/tidy/`), output in
`scratchpad/tidy_win/repro/wsl_ubsan.txt`.

## Not covered

- The Vulkan / Metal backends' copies of the format table (out of this task's scope; worth the same
  check in the Linux / macOS pass).
- `clang-analyzer-*` with cross-TU analysis (CTU) and CodeChecker: not run here.
