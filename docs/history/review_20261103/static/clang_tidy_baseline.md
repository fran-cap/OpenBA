# clang-tidy baseline (P3, 2026-11-03)

Session document for the review campaign (`../README.md`). Copies of the scripts and repros: `repro/` beside this file. Raw output: `~/tidy/out/chunk00..06.txt` on the WSL bed; deduplicated hits: `~/tidy/hits.json`.

## What ran

- Tree: `~/openba-review` (WSL copy, in sync with the Windows tree `src/`), compile DB `~/openba-review/build/cmake-debug-asan/compile_commands.json` (clang-19, `-g -O1`, the ASan+UBSan configure).
- The DB lists most TUs twice (OpenBA and unit targets). It was reduced to one entry per file under `src/` (1,182 TUs) in `~/tidy/db/compile_commands.json`; the first entry seen wins.
- Scope: the P0_MAP section 1 top-30 modules that build on Linux (render/dx11, platform/win, diag/win excluded; ext/ui has no C++ TU), which already contain engine/io, engine/system, engine/save, engine/collection and render/vulkan: **964 TUs**, split into seven chunks of 150 files (`~/tidy/chunk00..06.txt`, anchored regexes).
- Header filter `^/home/uspc/openba-review/src/`: hits in headers count once, under the header's own module, so a few modules outside the scope (engine/math, engine/utility...) carry header hits.
- Hits are deduplicated on (file, line, column, check) across TUs.

```bash
CHECKS='-*,bugprone-*,cert-*,concurrency-*,clang-analyzer-*,performance-move-const-arg,performance-no-int-to-ptr,cppcoreguidelines-init-variables,cppcoreguidelines-narrowing-conversions,-bugprone-easily-swappable-parameters,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp,-bugprone-macro-parentheses,-cert-err58-cpp'
# per chunk (~/tidy/runchunk.sh NN):
mapfile -t R < ~/tidy/chunkNN.txt
timeout 580 run-clang-tidy-19 -p ~/tidy/db -j 6 -quiet -checks="$CHECKS" \
    -header-filter="^$HOME/openba-review/src/" "${R[@]}" > ~/tidy/out/chunkNN.txt 2>&1
python3 ~/tidy/tab.py   # dedup + counts -> ~/tidy/hits.json
```

Wall times (s): chunk00 106, chunk01 100, chunk02 152, chunk03 152, chunk04 180, chunk05 158, chunk06 62. Every chunk finished inside its timeout. `bugprone-implicit-widening-of-multiplication-result` did not flood (54 hits) and stays on.

One TU did not compile: `src/game/Dialog/mainmenudialog.cpp` (`openba_build_info.h` is a generated header missing from the WSL build dir); its hits are absent.

## Totals

**4728 unique hits** across 49 checks and 28 modules.

| Check | Hits |
|---|---|
| `cert-err33-c` | 2356 |
| `cppcoreguidelines-init-variables` | 782 |
| `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` | 357 |
| `clang-analyzer-core.NullDereference` | 254 |
| `bugprone-multi-level-implicit-pointer-conversion` | 183 |
| `clang-analyzer-core.CallAndMessage` | 157 |
| `cert-err34-c` | 121 |
| `bugprone-integer-division` | 83 |
| `bugprone-implicit-widening-of-multiplication-result` | 54 |
| `clang-analyzer-deadcode.DeadStores` | 53 |
| `performance-no-int-to-ptr` | 45 |
| `bugprone-incorrect-roundings` | 37 |
| `bugprone-assignment-in-if-condition` | 35 |
| `bugprone-parent-virtual-call` | 30 |
| `bugprone-signed-char-misuse,cert-str34-c` | 21 |
| `bugprone-branch-clone` | 20 |
| `clang-analyzer-optin.cplusplus.UninitializedObject` | 15 |
| `bugprone-misplaced-widening-cast` | 13 |
| `bugprone-forward-declaration-namespace` | 11 |
| `cert-dcl54-cpp` | 10 |
| `clang-analyzer-core.NonNullParamChecker` | 9 |
| `bugprone-argument-comment` | 9 |
| `bugprone-sizeof-expression` | 8 |
| `clang-analyzer-optin.performance.Padding` | 8 |
| `bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c` | 6 |
| `bugprone-switch-missing-default-case` | 6 |
| `cert-dcl50-cpp` | 5 |
| `bugprone-unhandled-self-assignment,cert-oop54-cpp` | 4 |
| `concurrency-mt-unsafe` | 4 |
| `clang-analyzer-core.uninitialized.Assign` | 4 |
| `cert-oop54-cpp` | 3 |
| `clang-analyzer-cplusplus.PlacementNew` | 3 |
| `bugprone-suspicious-realloc-usage` | 3 |
| `clang-analyzer-core.DivideZero` | 2 |
| `bugprone-bool-pointer-implicit-conversion` | 2 |
| `bugprone-return-const-ref-from-parameter` | 2 |
| `bugprone-pointer-arithmetic-on-polymorphic-object,cert-ctr56-cpp` | 1 |
| `bugprone-infinite-loop` | 1 |
| `clang-analyzer-unix.Stream` | 1 |
| `bugprone-inc-dec-in-conditions` | 1 |
| `cert-int09-c` | 1 |
| `bugprone-copy-constructor-init` | 1 |
| `clang-analyzer-core.UndefinedBinaryOperatorResult` | 1 |
| `clang-analyzer-core.uninitialized.UndefReturn` | 1 |
| `clang-diagnostic-error` | 1 |
| `clang-analyzer-optin.core.EnumCastOutOfRange` | 1 |
| `clang-analyzer-unix.Malloc` | 1 |
| `clang-analyzer-optin.portability.UnixAPI` | 1 |
| `bugprone-not-null-terminated-result` | 1 |

## Per module

Columns: the eight largest checks, then the rest summed. Module = `src/<area>/<module>` of the file the hit is in.

| Module | Total | `cert-err33-c` | `cppcg-init-variables` | `bugprone-narrowing-conversions` | `csa-core.NullDereference` | `bugprone-multi-level-implicit-pointer-conversion` | `csa-core.CallAndMessage` | `cert-err34-c` | `bugprone-integer-division` | other |
|---|---|---|---|---|---|---|---|---|---|---|
| diag/(root) | 1180 | 1015 | 37 | 25 | 1 | 3 | 3 | 76 | 2 | 18 |
| game/Component | 664 | 222 | 104 | 97 | 67 | 60 | 12 | 1 | 19 | 82 |
| game/Game | 637 | 478 | 52 | 24 | 9 | 10 | 7 | 24 | 11 | 22 |
| game/Dialog | 384 | 122 | 49 | 48 | 46 | 7 | 59 | 1 | 6 | 46 |
| game/Action | 300 | 9 | 179 | 40 | 33 | 2 | 5 | 0 | 9 | 23 |
| game/Work | 278 | 20 | 71 | 51 | 47 | 38 | 12 | 0 | 20 | 19 |
| game/Player | 213 | 76 | 76 | 6 | 5 | 6 | 3 | 0 | 15 | 26 |
| render/vulkan | 201 | 151 | 1 | 25 | 0 | 4 | 0 | 1 | 1 | 18 |
| engine/ui | 152 | 9 | 51 | 3 | 28 | 27 | 18 | 0 | 0 | 16 |
| engine/io | 92 | 12 | 32 | 6 | 4 | 13 | 2 | 0 | 0 | 23 |
| render/common | 80 | 16 | 29 | 6 | 0 | 0 | 0 | 0 | 0 | 29 |
| engine/graphics | 75 | 3 | 37 | 6 | 4 | 4 | 6 | 0 | 0 | 15 |
| app/(root) | 59 | 37 | 2 | 0 | 0 | 0 | 0 | 15 | 0 | 5 |
| engine/object | 52 | 14 | 10 | 4 | 5 | 6 | 2 | 0 | 0 | 11 |
| game/Tutorial | 47 | 34 | 4 | 1 | 0 | 0 | 3 | 0 | 0 | 5 |
| ext/resources | 43 | 36 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 4 |
| game/Entity | 40 | 23 | 4 | 6 | 0 | 1 | 1 | 0 | 0 | 5 |
| engine/system | 36 | 5 | 17 | 0 | 3 | 1 | 0 | 0 | 0 | 10 |
| platform/posix | 31 | 16 | 6 | 0 | 0 | 0 | 0 | 3 | 0 | 6 |
| ext/phone_menu | 29 | 16 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 13 |
| platform/common | 25 | 15 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 7 |
| engine/collection | 24 | 4 | 1 | 6 | 0 | 0 | 0 | 0 | 0 | 13 |
| engine/engine | 21 | 1 | 1 | 0 | 0 | 1 | 15 | 0 | 0 | 3 |
| engine/audio | 21 | 9 | 10 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| engine/save | 15 | 0 | 3 | 0 | 2 | 0 | 9 | 0 | 0 | 1 |
| platform/sdl | 13 | 7 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 5 |
| engine/(root) | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8 |
| ext/(root) | 8 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2 |

## Per module, every check (non-zero cells)

- **diag/(root)** (1180): `cert-err33-c` 1015, `cert-err34-c` 76, `cppcoreguidelines-init-variables` 37, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 25, `bugprone-signed-char-misuse,cert-str34-c` 5, `performance-no-int-to-ptr` 5, `clang-analyzer-core.CallAndMessage` 3, `bugprone-multi-level-implicit-pointer-conversion` 3, `bugprone-implicit-widening-of-multiplication-result` 2, `bugprone-integer-division` 2, `concurrency-mt-unsafe` 1, `clang-analyzer-unix.Stream` 1, `cert-dcl50-cpp` 1, `bugprone-inc-dec-in-conditions` 1, `clang-analyzer-core.NullDereference` 1, `cert-int09-c` 1, `clang-analyzer-optin.performance.Padding` 1
- **game/Component** (664): `cert-err33-c` 222, `cppcoreguidelines-init-variables` 104, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 97, `clang-analyzer-core.NullDereference` 67, `bugprone-multi-level-implicit-pointer-conversion` 60, `bugprone-parent-virtual-call` 22, `bugprone-integer-division` 19, `clang-analyzer-deadcode.DeadStores` 15, `bugprone-assignment-in-if-condition` 14, `clang-analyzer-core.CallAndMessage` 12, `bugprone-signed-char-misuse,cert-str34-c` 9, `bugprone-implicit-widening-of-multiplication-result` 7, `bugprone-sizeof-expression` 5, `bugprone-misplaced-widening-cast` 3, `clang-analyzer-cplusplus.PlacementNew` 2, `clang-analyzer-core.NonNullParamChecker` 2, `bugprone-bool-pointer-implicit-conversion` 2, `cert-err34-c` 1, `performance-no-int-to-ptr` 1
- **game/Game** (637): `cert-err33-c` 478, `cppcoreguidelines-init-variables` 52, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 24, `cert-err34-c` 24, `bugprone-integer-division` 11, `bugprone-multi-level-implicit-pointer-conversion` 10, `clang-analyzer-core.NullDereference` 9, `clang-analyzer-core.CallAndMessage` 7, `bugprone-argument-comment` 5, `clang-analyzer-deadcode.DeadStores` 3, `bugprone-suspicious-realloc-usage` 3, `clang-analyzer-optin.performance.Padding` 2, `clang-analyzer-core.NonNullParamChecker` 1, `clang-analyzer-unix.Malloc` 1, `clang-analyzer-cplusplus.PlacementNew` 1, `performance-no-int-to-ptr` 1, `clang-analyzer-core.uninitialized.Assign` 1, `clang-analyzer-optin.portability.UnixAPI` 1, `bugprone-misplaced-widening-cast` 1, `bugprone-branch-clone` 1, `bugprone-implicit-widening-of-multiplication-result` 1
- **game/Dialog** (384): `cert-err33-c` 122, `clang-analyzer-core.CallAndMessage` 59, `cppcoreguidelines-init-variables` 49, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 48, `clang-analyzer-core.NullDereference` 46, `performance-no-int-to-ptr` 16, `bugprone-incorrect-roundings` 13, `bugprone-multi-level-implicit-pointer-conversion` 7, `bugprone-integer-division` 6, `bugprone-switch-missing-default-case` 5, `clang-analyzer-deadcode.DeadStores` 3, `bugprone-argument-comment` 2, `bugprone-signed-char-misuse,cert-str34-c` 1, `clang-diagnostic-error` 1, `clang-analyzer-core.NonNullParamChecker` 1, `bugprone-sizeof-expression` 1, `bugprone-branch-clone` 1, `cert-err34-c` 1, `bugprone-return-const-ref-from-parameter` 1, `clang-analyzer-core.DivideZero` 1
- **game/Action** (300): `cppcoreguidelines-init-variables` 179, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 40, `clang-analyzer-core.NullDereference` 33, `bugprone-assignment-in-if-condition` 12, `bugprone-integer-division` 9, `cert-err33-c` 9, `bugprone-misplaced-widening-cast` 5, `clang-analyzer-core.CallAndMessage` 5, `bugprone-multi-level-implicit-pointer-conversion` 2, `clang-analyzer-core.NonNullParamChecker` 2, `bugprone-implicit-widening-of-multiplication-result` 1, `bugprone-switch-missing-default-case` 1, `clang-analyzer-deadcode.DeadStores` 1, `clang-analyzer-core.DivideZero` 1
- **game/Work** (278): `cppcoreguidelines-init-variables` 71, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 51, `clang-analyzer-core.NullDereference` 47, `bugprone-multi-level-implicit-pointer-conversion` 38, `bugprone-integer-division` 20, `cert-err33-c` 20, `clang-analyzer-core.CallAndMessage` 12, `clang-analyzer-deadcode.DeadStores` 10, `bugprone-parent-virtual-call` 6, `bugprone-signed-char-misuse,cert-str34-c` 1, `clang-analyzer-core.NonNullParamChecker` 1, `bugprone-misplaced-widening-cast` 1
- **game/Player** (213): `cert-err33-c` 76, `cppcoreguidelines-init-variables` 76, `bugprone-integer-division` 15, `bugprone-assignment-in-if-condition` 8, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `bugprone-multi-level-implicit-pointer-conversion` 6, `performance-no-int-to-ptr` 6, `clang-analyzer-deadcode.DeadStores` 5, `clang-analyzer-core.NullDereference` 5, `bugprone-signed-char-misuse,cert-str34-c` 3, `clang-analyzer-core.CallAndMessage` 3, `bugprone-misplaced-widening-cast` 2, `bugprone-implicit-widening-of-multiplication-result` 2
- **render/vulkan** (201): `cert-err33-c` 151, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 25, `performance-no-int-to-ptr` 6, `bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c` 4, `bugprone-multi-level-implicit-pointer-conversion` 4, `bugprone-implicit-widening-of-multiplication-result` 4, `bugprone-branch-clone` 2, `clang-analyzer-optin.performance.Padding` 1, `cert-err34-c` 1, `clang-analyzer-deadcode.DeadStores` 1, `bugprone-integer-division` 1, `cppcoreguidelines-init-variables` 1
- **engine/ui** (152): `cppcoreguidelines-init-variables` 51, `clang-analyzer-core.NullDereference` 28, `bugprone-multi-level-implicit-pointer-conversion` 27, `clang-analyzer-core.CallAndMessage` 18, `cert-err33-c` 9, `clang-analyzer-optin.cplusplus.UninitializedObject` 4, `bugprone-branch-clone` 3, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 3, `bugprone-forward-declaration-namespace` 2, `bugprone-incorrect-roundings` 2, `bugprone-implicit-widening-of-multiplication-result` 2, `bugprone-unhandled-self-assignment,cert-oop54-cpp` 1, `clang-analyzer-core.NonNullParamChecker` 1, `clang-analyzer-deadcode.DeadStores` 1
- **engine/io** (92): `cppcoreguidelines-init-variables` 32, `bugprone-multi-level-implicit-pointer-conversion` 13, `cert-err33-c` 12, `clang-analyzer-optin.cplusplus.UninitializedObject` 7, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `clang-analyzer-core.NullDereference` 4, `clang-analyzer-deadcode.DeadStores` 4, `bugprone-branch-clone` 4, `clang-analyzer-core.uninitialized.Assign` 3, `cert-dcl54-cpp` 2, `clang-analyzer-core.CallAndMessage` 2, `clang-analyzer-core.UndefinedBinaryOperatorResult` 1, `bugprone-unhandled-self-assignment,cert-oop54-cpp` 1, `clang-analyzer-optin.core.EnumCastOutOfRange` 1
- **render/common** (80): `cppcoreguidelines-init-variables` 29, `bugprone-implicit-widening-of-multiplication-result` 26, `cert-err33-c` 16, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `bugprone-not-null-terminated-result` 1, `bugprone-misplaced-widening-cast` 1, `bugprone-branch-clone` 1
- **engine/graphics** (75): `cppcoreguidelines-init-variables` 37, `bugprone-implicit-widening-of-multiplication-result` 7, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `clang-analyzer-core.CallAndMessage` 6, `clang-analyzer-core.NullDereference` 4, `bugprone-multi-level-implicit-pointer-conversion` 4, `cert-err33-c` 3, `bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c` 2, `bugprone-parent-virtual-call` 2, `cert-dcl54-cpp` 1, `clang-analyzer-optin.cplusplus.UninitializedObject` 1, `bugprone-pointer-arithmetic-on-polymorphic-object,cert-ctr56-cpp` 1, `bugprone-branch-clone` 1
- **app/(root)** (59): `cert-err33-c` 37, `cert-err34-c` 15, `performance-no-int-to-ptr` 3, `cppcoreguidelines-init-variables` 2, `clang-analyzer-optin.performance.Padding` 1, `concurrency-mt-unsafe` 1
- **engine/object** (52): `cert-err33-c` 14, `cppcoreguidelines-init-variables` 10, `cert-dcl54-cpp` 6, `bugprone-multi-level-implicit-pointer-conversion` 6, `clang-analyzer-core.NullDereference` 5, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 4, `clang-analyzer-core.CallAndMessage` 2, `bugprone-signed-char-misuse,cert-str34-c` 1, `bugprone-branch-clone` 1, `bugprone-assignment-in-if-condition` 1, `cert-oop54-cpp` 1, `clang-analyzer-core.NonNullParamChecker` 1
- **game/Tutorial** (47): `cert-err33-c` 34, `cppcoreguidelines-init-variables` 4, `clang-analyzer-core.CallAndMessage` 3, `bugprone-argument-comment` 2, `clang-analyzer-deadcode.DeadStores` 2, `bugprone-return-const-ref-from-parameter` 1, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 1
- **ext/resources** (43): `cert-err33-c` 36, `cppcoreguidelines-init-variables` 3, `cert-dcl50-cpp` 2, `clang-analyzer-optin.cplusplus.UninitializedObject` 1, `clang-analyzer-optin.performance.Padding` 1
- **game/Entity** (40): `cert-err33-c` 23, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `cppcoreguidelines-init-variables` 4, `bugprone-branch-clone` 2, `clang-analyzer-core.CallAndMessage` 1, `bugprone-implicit-widening-of-multiplication-result` 1, `bugprone-multi-level-implicit-pointer-conversion` 1, `performance-no-int-to-ptr` 1, `bugprone-signed-char-misuse,cert-str34-c` 1
- **engine/system** (36): `cppcoreguidelines-init-variables` 17, `cert-err33-c` 5, `clang-analyzer-deadcode.DeadStores` 3, `clang-analyzer-core.NullDereference` 3, `cert-oop54-cpp` 2, `cert-dcl50-cpp` 2, `bugprone-unhandled-self-assignment,cert-oop54-cpp` 1, `bugprone-copy-constructor-init` 1, `clang-analyzer-core.uninitialized.UndefReturn` 1, `bugprone-multi-level-implicit-pointer-conversion` 1
- **platform/posix** (31): `cert-err33-c` 16, `cppcoreguidelines-init-variables` 6, `performance-no-int-to-ptr` 4, `cert-err34-c` 3, `concurrency-mt-unsafe` 2
- **ext/phone_menu** (29): `cert-err33-c` 16, `bugprone-incorrect-roundings` 12, `bugprone-branch-clone` 1
- **platform/common** (25): `cert-err33-c` 15, `bugprone-incorrect-roundings` 5, `performance-no-int-to-ptr` 2, `cppcoreguidelines-init-variables` 2, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 1
- **engine/collection** (24): `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 6, `clang-analyzer-deadcode.DeadStores` 5, `cert-err33-c` 4, `bugprone-branch-clone` 3, `bugprone-sizeof-expression` 2, `bugprone-unhandled-self-assignment,cert-oop54-cpp` 1, `cppcoreguidelines-init-variables` 1, `bugprone-implicit-widening-of-multiplication-result` 1, `clang-analyzer-optin.cplusplus.UninitializedObject` 1
- **engine/engine** (21): `clang-analyzer-core.CallAndMessage` 15, `cert-dcl54-cpp` 1, `bugprone-forward-declaration-namespace` 1, `bugprone-infinite-loop` 1, `cert-err33-c` 1, `cppcoreguidelines-init-variables` 1, `bugprone-multi-level-implicit-pointer-conversion` 1
- **engine/audio** (21): `cppcoreguidelines-init-variables` 10, `cert-err33-c` 9, `bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions` 2
- **engine/save** (15): `clang-analyzer-core.CallAndMessage` 9, `cppcoreguidelines-init-variables` 3, `clang-analyzer-core.NullDereference` 2, `clang-analyzer-optin.cplusplus.UninitializedObject` 1
- **platform/sdl** (13): `cert-err33-c` 7, `bugprone-incorrect-roundings` 5, `cppcoreguidelines-init-variables` 1
- **engine/(root)** (8): `bugprone-forward-declaration-namespace` 8
- **ext/(root)** (8): `cert-err33-c` 6, `clang-analyzer-optin.performance.Padding` 2

## Triage

About 330 hits read at the code: every hit of the rare checks (all `clang-analyzer-*` except the
NullDereference / CallAndMessage bulk, `bugprone-sizeof-expression`, `-misplaced-widening-cast`,
`-signed-char-misuse`, `-infinite-loop`, `-suspicious-*`, `-unhandled-self-assignment`,
`-not-null-terminated-result`, `-bool-pointer-implicit-conversion`, `-return-const-ref-from-parameter`,
`concurrency-mt-unsafe`), the 83 `bugprone-integer-division` and 121 `cert-err34-c` lines, and the 35
NullDereference / CallAndMessage paths in engine/io, engine/system, engine/save, engine/engine.
`cert-err33-c` (2,356), `cppcoreguidelines-init-variables` (782), the narrowing pair (357) and
`bugprone-multi-level-implicit-pointer-conversion` (183) were not triaged: style or volume, no UB claim.

### Real (reported, repro in `~/tidy/repro/`)

| Where | Check | Finding | Label | Proof |
|---|---|---|---|---|
| `src/render/common/better_trees/natural_growth.cpp:152` AppendElement | bugprone-not-null-terminated-result | `strncmp(d.data()+no, "TEXCOORD", 9)` with only `no < d.size()` checked: a signature chunk whose last name is `TEXCOORD` without a NUL reads one byte past the chunk vector | ext | `isgn_strncmp.cpp`, ASan heap-buffer-overflow READ in strncmp, 0 bytes after a 40-byte region |
| `src/engine/io/package_file_read.cpp:302/381/487` ReadStringRaw -> BuildStringFrom | clang-analyzer-core.UndefinedBinaryOperatorResult, core.uninitialized.Assign | formatVersion > 2 and a first string length <= 0: nothing is read into `frameBuf[0x1840]` (no initialiser, as retail's frame) and the NUL scan reads indeterminate stack bytes (and can run off the frame). P0_MAP input surface §2 lists the "stale stack data" lead; this is the never-written case | retail (frame and body carry retail addresses 0x1800b4fd3.., 0x1800b55a2..) | `pkg_rawstring_uninit.cpp`, MSan use-of-uninitialized-value in BuildStringFrom, origin `frameBuf` |
| `src/game/Component/citizenai_executor.cpp:637`, `citizencomponent.cpp:297`, `src/game/Game/eventlog_achievement.cpp:90` | clang-analyzer-cplusplus.PlacementNew | placement new of `Math::Transform` / `UI::String` into `uint8_t[N]` locals with no `alignas`: storage alignment 1, object alignment 4 / 8 | port (stack-frame transcription) | static: x86-64 SysV happens to align local arrays >= 16 bytes to 16; Win64 and AArch64 give no such rule |
| `src/game/Game/gameupdateloop_serialize.cpp:1009` GameUpdateLoop_SaveRoundtripCheck | clang-analyzer-unix.Malloc | the error arm returns without freeing `a` / `b` from RoundtripDecompress (and leaks `fresh` on purpose) | harness (`BANISHED_SAVE_TEST=roundtrip`) | static |
| `src/app/main.cpp:262` (settings.cfg getInt), `src/app/options.cpp` (11 flags), diag harnesses | cert-err34-c | `atoi` on file / environment text: out-of-range input is undefined behaviour in C and C++ (glibc and MSVCRT happen to truncate) | ext / harness | static |

### Judged false positive or not UB

- `bugprone-integer-division` (83): every one is `cell / mapWidth` (row of a cell index) converted to float after the divide, as intended.
- `bugprone-misplaced-widening-cast` (13): 32-bit cell index math widened after the fact; map sizes keep the products far below 2^31.
- `bugprone-signed-char-misuse` (23): every build passes `-fsigned-char` (`CMakeLists.txt:100`), so `char c; while (c < 0)` in `statemachine_tick.cpp` holds on AArch64 too.
- `bugprone-sizeof-expression` (8): templates instantiated with a pointer `T`; `sizeof(T)` is the element size.
- `bugprone-unhandled-self-assignment` / `cert-oop54-cpp` (7): String, Array (`ArrayAssignCore` `a == s`), Mod, Link all short-circuit on the pointer.
- `bugprone-infinite-loop` `mainloop_frame.cpp:352`: retail's documented clamp hang, kept with an `asm volatile` barrier, unreachable with the ctor's constants.
- `clang-analyzer-optin.core.EnumCastOutOfRange` `Stream.h:434`: `Direction` has a fixed underlying type (`int32_t`): not UB.
- `clang-analyzer-core.DivideZero` `action_pf_region.cpp:421` (labels 1..N always have a cell) and `townhallui.cpp:787` (combo index outside 0..6): infeasible paths.
- `clang-analyzer-core.uninitialized.UndefReturn` `allocator.cpp:141`: the path continues after `FatalOutOfMemory`.
- `water_contour_mesh.cpp:175/213` (garbage hash key, `realloc` of 0): keys are four floats with no padding; `chainCap` is never 0.
- `bugprone-suspicious-memory-comparison` (6): `PipelineKey` and `SamplerDesc` are static-asserted padding-free; `Transform` / `Matrix` are packed floats (the compare treats -0 / NaN bytewise, by design).
- `concurrency-mt-unsafe` (4): `readdir` on a per-call `DIR*`, `getenv` in the boot-time Options reader.
- NullDereference / CallAndMessage sample (35): every path assumes `MemAlloc` / a singleton / a factory lookup returns null; the factory miss in `modification_setupfilesystem.cpp:644` is retail's documented unguarded deref. The bulk (376 in game/*) is the faithful unguarded-deref pattern and was not read one by one.
