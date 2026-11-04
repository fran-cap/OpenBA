// ============================================================================
// tests/fuzz/string_serialize2_fuzz.cpp -- string_serialize_fuzz.cpp with its masks OFF
// ============================================================================
// Used to reproduce the masked findings (M2: uncapped length word, M3: unterminated String)
// with hand-built inputs; not a long-running target (every large length word is an OOM).
// Build: `~/fz.sh build string_serialize2 src/engine/io/text_codec.cpp`.
// ============================================================================
#define FUZZ_NO_MASK 1
#include "string_serialize_fuzz.cpp"
