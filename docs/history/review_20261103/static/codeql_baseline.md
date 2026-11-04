# P3 CodeQL baseline (2026-11-03)

Session record for the review campaign (`../README.md`). The tree is branch `review/sanitizer-builds`
at `bfe14e5fe`. CodeQL CLI 2.27.4, `codeql/cpp-queries` 1.10.0 (`codeql/cpp-all` 12.4.0).

## Databases

| Database | How | Use |
|---|---|---|
| `C:/codeql-dbs/openba-cpp-traced` | traced build: a fresh ccache-off `debug` configure (`-DBANISHED_USE_CCACHE=OFF`, the pinned WinLibs GCC 16.2), `cmake --build <dir> -j 4 -t OpenBA` under `codeql database create --command=...`; 1456 build steps, about 20 minutes | **the baseline and the taint query** |
| `C:/codeql-dbs/openba-cpp` | `--build-mode=none --source-root=src` | superseded; kept for comparison |

Two traps, both worth knowing before the next run:

- **The first build-mode-none attempt used `--source-root=.`.** It indexed 18,992 sources, of which
  15,273 were copies under `.claude/worktrees/`, plus `tools/renderdoc_build` and the `build/` dirs.
  It was stopped and recreated over `src/` only.
- **Build-mode none cannot resolve this tree's includes.** The tree includes through `-Isrc/engine`
  etc., and the none extractor does not see those paths, so most calls lose their target. On the
  none database a query finds 22 `Read` calls and 5 `MemAlloc` calls. The traced database finds 67 and
  787. Its function count is 18,882, against 82,748 traced. The none SARIF
  (`codeql_security_quality_buildmode_none.sarif`) is mostly parse artefacts: `cpp/missing-return` on
  destructors, and `cpp/short-global-name` and `cpp/unused-static-variable` on unresolved macros.
  Do not triage from it.

## security-and-quality suite (traced database)

`codeql_security_quality.sarif`: **1838 results, 49 rules**. Of these, **450 are under `src/`**. The
other 1388 are in `third_party/` (SDL, FreeType, imgui, wmadec, dxbc-spirv) and are not triaged here.
2187 of 2577 C/C++ files were scanned.

### Under `src/`, per rule

| Count | Rule | Severity | Triage |
|---:|---|---|---|
| 109 | cpp/unused-static-function | rec | noise (retail-address helpers kept for documentation) |
| 67 | cpp/equality-on-floats | rec | faithful float compares; noise |
| 44 | cpp/wrong-type-format-argument | error (7.5) | **UB, finding**: `String::Format("%d", (uint64_t)x)` in about 40 UI sites (`game/Dialog/*`, `game/Player/tool_base_createbar.cpp`, `ext/family_tree/toolbar_page.cpp:174`), plus `render/dx11/devicedata.cpp:1303` `"%AA"` with an `int` |
| 33 | cpp/unused-static-variable | rec | noise |
| 26 | cpp/commented-out-code | rec | noise |
| 25 | cpp/inconsistent-null-check | error | mostly ext/diag `Find` / `MemAlloc` results; `action_pf_pools.cpp:56,103,153` MemAlloc unchecked (retail shape); no proof, not filed |
| 23 | cpp/stack-address-escape | warning | engine UI/video sites store a parameter's address by design (retail); ext sites to recheck in P8 |
| 17 | cpp/constant-comparison | warning | transcribed retail guards |
| 12 | cpp/loop-variable-changed | rec | noise |
| 9 | cpp/world-writable-file-creation | warning (7.8) | `fopen` in diag / harness writers; not reachable from input |
| 9 | cpp/function-in-block | rec | noise |
| 7 | cpp/signed-overflow-check | warning (8.1) | `citizencomponent.cpp:323,333,474,486,508`, `nomadcomponent_states.cpp:91`, `populationcomponent.cpp:180`: `x + 1 < 1` transcribes retail's `INC/JLE`; UB only at INT_MAX (an action-stack index); P8 |
| 7 | cpp/long-switch | rec | noise |
| 6 | cpp/integer-multiplication-cast-to-long | warning (8.1) | `bc4.cpp:123-127` (block indexes, bounded by the caller's level sizes); `herdcomponent.cpp:144` |
| 6 | cpp/declaration-hides-variable, cpp/array-in-interface, cpp/useless-expression | | `citizenai_work_scheduler.cpp:301,632` "no effect" Insert calls: recheck in P8 |
| 5 | cpp/guarded-free, cpp/unused-local-variable | rec | noise |
| 4 | cpp/uninitialized-local | warning (7.8) | `engine/video/device.cpp:486-493`: false positive (the pointer-to-member reference is bound before use) |
| 4 | cpp/declaration-hides-parameter | rec | noise |
| 3 | cpp/use-of-goto | warning | Ghidra-shaped bodies |
| 2 | cpp/overflowing-snprintf | warning (8.1) | `diag/scene_watch.cpp:719,735`: diag only |
| 2 | cpp/assign-where-compare-meant | error | `citizen_updatehealth.cpp:455,471` `(once = true)` one-shot idiom: false positive |
| 2 | cpp/bitwise-sign-check | warning | `dx11/imagebufferdata.cpp:711`, `compress_ao/bc4_create.cpp:40` |
| 2 | cpp/empty-block, cpp/empty-if, cpp/rule-of-two | | noise |
| 1 | cpp/uncontrolled-allocation-size | error (8.1) | `package_file_read.cpp:465` `MemAlloc(blobLen)`: **true** (also the taint query's top hit) |
| 1 | cpp/redundant-null-check-simple | error | `keystone_a_removalcalcpath.cpp:130` |
| 1 | cpp/offset-use-before-range-check | warning | `tutorial_base.cpp:99`: false positive (the source is NUL-terminated; `k` bounds the destination) |
| 1 | others (complex-block, poorly-documented-function) | | noise |

### Whole SARIF, per rule (all paths)

416 fixme-comment, 316 commented-out-code, 305 unused-static-variable, 141 equality-on-floats,
109 unused-static-function, 64 wrong-type-format-argument, 54 poorly-documented-function,
46 include-non-header, 41 integer-multiplication-cast-to-long, 41 stack-address-escape,
36 loop-variable-changed, 35 constant-comparison, 31 long-switch, 27 inconsistent-null-check,
18 missing-header-guard, 12 declaration-hides-variable, 11 local-variable-hides-global-variable,
10 non-constant-format, 9 suspicious-pointer-scaling, 9 world-writable-file-creation,
9 function-in-block, 9 ambiguously-signed-bit-field, 8 trivial-switch, 7 signed-overflow-check,
6 each: unused-local-variable, array-in-interface, irregular-enum-init, useless-expression, empty-if,
5 each: guarded-free, declaration-hides-parameter, 4 each: uninitialized-local, empty-block,
3 each: use-of-goto, lossy-pointer-cast, 2 each: overflowing-snprintf, complex-block, rule-of-two,
bitwise-sign-check, unsigned-comparison-zero, assign-where-compare-meant, 1 each:
redundant-null-check-simple, offset-use-before-range-check, uncontrolled-process-operation,
overflow-buffer, uncontrolled-allocation-size, complex-condition, large-parameter,
leap-year/unchecked-after-arithmetic-year-modification.

## Custom query: `queries/size_taint.ql`

A path-problem taint query (`openba/size-taint`).

- **Sources:** the out-parameter of
  - `Stream::Read(n, dst)` and every override (matched by shape)
  - `Read(pos, n, dst)`
  - `FileSystem::ReadFile(fi, n, dst)` and Win32 `ReadFile`
  - `fread`
  - every `Serialize*` scalar (`T&`, arithmetic or enum) and array / bytes form
- **Sinks**, in seven kinds:
  - `alloc-size`: `MemAlloc`, `RtMemAlloc`, `malloc` and its siblings, `new[]`
  - `count`: `SetSize`, `SetSizeFill`, `reserve`, `resize`, `SetLength`, and the free
    `RtColl_ArraySetSize*_<addr>` / `Rt_SetSize_<addr>` / `*ArraySetSize` forms (first integral
    argument)
  - `copy-len`: `memcpy`, `memmove`, `memset`
  - `read-len`: the length handed to a `Read` / `Serialize*Array` / `SerializeBytes`
  - `index`, `ptr-offset`, `loop-bound`
- **Barrier:** a guard that bounds the same value number from above (`ensuresLt`). A `> 0` check is
  not a barrier.

To run it:

    codeql database analyze C:/codeql-dbs/openba-cpp-traced queries/size_taint.ql \
        --format=sarif-latest --output=codeql_size_taint.sarif

The run takes about 50 s. The pack is `queries/qlpack.yml`; run `codeql pack install` in `queries/`
first.

**Result: 558 paths, 550 unique sinks** (`codeql_size_taint.sarif`; one row per sink with the first
path steps in `codeql_size_taint_triage.tsv`).

### Paths per reachability and sink kind

The reachability column is a path heuristic: `game/*description.cpp` are package resources, not save
data, although the heuristic files them under `.sav`.

| Reachability | alloc-size | count | copy-len | read-len | index | ptr-offset | loop-bound |
|---|---:|---:|---:|---:|---:|---:|---:|
| A: mod `.pkm` / packages (engine io, graphics, audio, video, ui resources) | 5 | 87 | 5 | 20 | 19 | 14 | 57 |
| B: shared `.sav` (save, game components, map) | 26 | 93 | 3 | 28 | 22 | 1 | 129 |
| C: local caches | 0 | 0 | 0 | 0 | 1 | 1 | 2 |
| D: local / harness | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| E: other / third_party | 0 | 11 | 2 | 0 | 3 | 0 | 11 |

### Triage

All 550 sinks were classified by kind. The 177 non-count, non-loop rows of A and B were read
individually, and a sample of the count and loop rows.

- **count and loop-bound (about 400): mostly the serializer idiom.**
  `rw->SerializeInt(n); if (reading) SetSize(a, n); for (i < n) ...`. The array is sized by the same
  `n` the loop walks, so a hostile `n` is an allocation-size DoS (MemAlloc of up to 32 x 2^31 bytes),
  not a write past the end. Reported once, as a class (finding 8, proven on `LoadMetaData`). The
  exceptions are the rows where a *different* stored value bounds the loop; those are findings 9
  and 10.
- **False-positive classes:**
  - `ext/savetail.cpp:134` and `ext/package_cache`: the `Reader` checks every length. The query does
    not model `Reader::Bytes`.
  - `persistence_saveinfo_job.cpp:281` and `crc.cpp:70`: a `uint8_t` index into a 256-entry table.
  - `allocator.cpp` indexes: size classes from the request, bounded by the class table.
  - `filename.cpp`, `string.cpp`: the length is the string's own.
- **False negative:** `CompressedStream::DecompressFromSource`
  (`compressed_stream_read.cpp:276/285`). The u16 block size travels through `_headerData[blk]`,
  an array filled by `Read` and read back through a pointer, and the query loses that field flow.
  It is found by hand and proven (findings 1-2).

### Proven and static findings

Proofs are under `poc/`:

- `cs_block_poc.cpp`, `pkm_readpackfile_poc.cpp`, `sav_meta_poc.cpp`, with their sanitizer
  output `*.txt`
- the link stubs `stubs.cpp` / `fs_stubs.cpp`

They are built in WSL against the `fuzz` checking build's `libfuzz_tree.a` (an rsync copy of this
branch at `~/openba-review`; the four io files are byte-identical to the tree). The compile line is
the fuzz target's (clang-19, `-fsanitize=address,...,signed-integer-overflow,...`), plus:

- the real `compressed_stream_open.cpp`, `encoding.cpp`, `memory_stream.cpp`,
  `compressed_stream_write.cpp`, `compressed_stream_release.cpp`, `compressedstream.cpp` and the
  body under test (`package_file_read.cpp`, `saveload_loadmeta.cpp`)
- `-Wl,--allow-multiple-definition`, so those win over `io_stream_abi_stubs.cpp`

Copies of the binaries and logs are in `~/tidy/p3/` on WSL.

| # | Where | What | Label | Proof |
|---|---|---|---|---|
| 1 | `compressed_stream_read.cpp:276` DecompressFromSource | u16 `compressedSize` > 0x8000 → `Read` into `uint8_t scratch[0x8000]`: stack overflow | retail (`_alloca(0x8020)` 0x1800a96b7) | ASan stack-buffer-overflow WRITE 36864 (`poc/compressed.asan.txt`) |
| 2 | `compressed_stream_read.cpp:285` stored arm | u16 `uncompressedSize` > 0x8000 → `Read` into the 0x8000 window: heap overflow | retail | ASan heap-buffer-overflow WRITE 36864 (`poc/stored.asan.txt`) |
| 3 | `compressed_stream_open.cpp:117` (and :128) | `blockCount * 8` int32 overflow | retail (comment: `LEA EAX,[RDX*8]`) | UBSan signed-integer-overflow (`poc/count.ubsan.txt`) |
| 4 | `package_file_read.cpp:596` entry name | `nameLen` (only `> 0`) → `Read` into the 0x1840 frame buffer | retail | ASan stack-buffer-overflow WRITE 8192 via real `ReadPackfile` (`poc/pkm_name.san.txt`) |
| 5 | `package_file_read.cpp:385` ReadStringRaw (and :347 ReadStringUtf16) | metadata string length → same frame buffer | retail | ASan stack-buffer-overflow WRITE 8192 (`poc/pkm_meta.san.txt`) |
| 6 | `package_file_read.cpp:515` | `_iconHeight * _iconWidth` int32 overflow | retail | UBSan (`poc/pkm_icon.san.txt`) |
| 7 | `saveload_loadmeta.cpp:317` | thumbnail `w * h * 4` int32 overflow | retail (comment: `IMUL/SHL` transcribed) | UBSan (`poc/sav_thumb.san.txt`) |
| 8 | `saveload_loadmeta.cpp:339` (the count class) | mod count → `SetSize(count)`: 0xFFFFFFFE0-byte request | retail | ASan out-of-memory (`poc/sav_mods.san.txt`) |
| 9 | `gamegrid_serialize.cpp:218-250` Map::Serialize | `size` and the cell `count` are read independently; `width*width` loops (version < 2 fix-up, FillZones, Pathfinder_Seed, Region_Seed, ext.fast_paths) index `cells` of `count` entries | retail | static (taint path `gamegrid_serialize.cpp:218 → :237 → :250/:312/:320`) |
| 10 | `spritecapture_class.cpp:193` | `_sheets._data[sheetIndex]`, sheetIndex from the resource, only `< 0` checked | retail | static |
| 11 | `internal_class.cpp:682/690/730` PersistObject | `index` read by `InternalPtr` serialize (`internal.cpp:246`) indexes `_persistIds` with no upper bound | retail | static |
| 12 | `text_codec.cpp:522` RtIo_SerializeStringArray | `static_cast<int32_t>(len) * 2 - 2` overflows for `len >= 0x40000000` | retail | static (the PoC `poc/strarray_poc.cpp` did not finish in 200 s: a 2 GB read) |
| 13 | `modelcomponent.cpp:94-96` | `meshGroup` / `meshIndex` (int16 from the save) index the description arrays with no upper bound | unknown (port-written helper) | static |
| 14 | 44 `String::Format` sites | `%d`/`%i` with `uint64_t`, `%A` with `int`: wrong vararg type | unknown / retail (`devicedata.cpp:1303`) | static (CodeQL `cpp/wrong-type-format-argument`) |
