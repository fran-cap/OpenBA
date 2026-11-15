#!/bin/sh
# Block EROSION bench (docs/extensions/handins/EROSION.md): the tree's wg_f390_erosion, compiled
# with the perf flavour's flags, against the pre-EROSION body (copied into bench.cpp) on the same
# heightfields and seeds; prints ms, ns/step and IDENTICAL / DIFFERENT (heightmap + Random state).
# Block EROSIONPAR (docs/extensions/handins/EROSIONPAR.md): the tree's body is timed with each
# worker count listed (System::Parallel::SetWorkers; 0 = the sequential body), with its counters.
# usage, from the repo root:  sh tools/erosion_bench/bench.sh [reps] [workers...]  (default 3; 0 3 7)
#                             sh tools/erosion_bench/bench.sh 1 census            (conflict census)
# Reads C:/GOG Games/Banished/heightmap_dump.raw (a 513^2 float dump) when present.
D=build/erosion_bench
B=tools/erosion_bench
mkdir -p "$D"
FL="-std=gnu++17 -fexceptions -fno-strict-aliasing -fno-lifetime-dse -O2 -DNDEBUG -w -Isrc/game -Isrc/engine -Isrc/render/common -Isrc/render/dx11 -Isrc/app -Isrc/diag -Isrc/platform/common -Isrc"
g++ $FL -c -o "$D/erosion.o" src/game/Game/worldgen_f390_erosion.cpp || exit 1
g++ $FL -c -o "$D/bench.o" "$B/bench.cpp" || exit 1
g++ $FL -c -o "$D/pf.o" src/engine/system/parallel_for.cpp || exit 1
g++ $FL -c -o "$D/thr.o" src/platform/win/thread_win.cpp || exit 1
# every IO::, rtcoll:: or kSpec* symbol the two objects leave undefined that the bench does not define: an aborting stub
{ nm "$D/erosion.o"; nm "$D/bench.o"; } | awk '$1=="U"{print $2}' | sort -u > "$D/undef.txt"
nm "$D/bench.o" | awk '$2=="T"{print $3}' | sort -u > "$D/def.txt"
{ echo '#include <stdlib.h>'; comm -23 "$D/undef.txt" "$D/def.txt" | grep '^_ZN2IO\|^_ZN6rtcoll\|^kSpec' | sed 's/.*/void &(void){abort();}/'; } > "$D/stubs.c"
gcc -O2 -w -c -o "$D/stubs.o" "$D/stubs.c" || exit 1
g++ -static -o "$D/bench.exe" "$D/bench.o" "$D/erosion.o" "$D/pf.o" "$D/thr.o" "$D/stubs.o" || exit 1
if [ $# -eq 0 ]; then set -- 3; fi
"$D/bench.exe" "$@"
