# tests/fuzz -- libFuzzer targets for the input surface

One `<name>_fuzz.cpp` per parser of outside bytes (packages, saves, resources, audio, shaders, text,
caches). Each defines `LLVMFuzzerTestOneInput` and builds as `fuzz_<name>` in the `fuzz` checking
build (CMakeLists.txt `BANISHED_SANITIZE=fuzz`: clang, ASan + UBSan + libFuzzer, Linux), linked
against the unit suite's tree sources and stubs (`tests/unit/sources.cmake`).

    BANISHED_SANITIZE=fuzz CMAKE_EXTRA="-DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19" \
        tools/build.sh build debug        # configures build/cmake-debug-fuzz
    cmake --build build/cmake-debug-fuzz -t fuzz
    build/cmake-debug-fuzz/fuzz/fuzz_lzf_decompress -max_total_time=600 corpus/lzf_decompress

**Seeds and corpora.** No retail content enters the tree (AGENTS.md §1): a seed is made by our own
writers (the compressor, `PackageFile::Serialize`, `SaveTail::Encode`, the settings writer), by a
`seeds` generator in the target, or kept out of tree. A corpus grown from the user's install stays
in the user's own directory.

**A crash** is reproduced by passing its file to the target (`fuzz_<name> crash-<sha1>`). The fix
PR carries a unit test (`tests/unit/`) with the crashing bytes inline, which fails before the fix.
