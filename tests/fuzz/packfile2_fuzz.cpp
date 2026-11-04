// ============================================================================
// tests/fuzz/packfile2_fuzz.cpp -- ReadPackfile over a fuzzed index PLAINTEXT
// ============================================================================
// The same harness as packfile_fuzz.cpp in its structured mode: byte 0 = flags (bit 0 sets the
// header's metadata bit), the rest is the decompressed index, wrapped in a valid STORED
// container so every input reaches the index parser. See packfile_fuzz.cpp for the shim.
// ============================================================================
#define PACKFILE_STRUCTURED 1
#include "packfile_fuzz.cpp"
