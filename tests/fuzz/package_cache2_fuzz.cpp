// ============================================================================
// tests/fuzz/package_cache2_fuzz.cpp -- System::Filename(const char*) over package entry names
// ============================================================================
// Every package entry name (a mod .pkm's index: outside bytes) becomes a System::Filename, whose
// constructor runs AdjustDelimiters (retail 0x1800c6970: '/' -> '\', then "\.." splices). The
// package_cache target hung inside it (libFuzzer timeout); this target isolates the name.
// Input: the name bytes (NULs end it, as a C string).
// ============================================================================
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
    System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 512)
        return 0;
    const std::string s(reinterpret_cast<const char*>(data), size);
    System::Filename f(s.c_str());
    return 0;
}

// error.cpp (in the archive) reaches the platform's message box (package_cache_fuzz.cpp has the note).
namespace Platform {
namespace Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {
}
}  // namespace Window
}  // namespace Platform
