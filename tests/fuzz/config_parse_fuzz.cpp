// ============================================================================
// tests/fuzz/config_parse_fuzz.cpp -- System::Configuration::ParseCommand / ParseValue
//   (src/engine/system/configuration.cpp) over an arbitrary command line
// ============================================================================
// P0_MAP input surface rank 11. Build:
//   `~/fz.sh build config_parse src/engine/system/configuration.cpp`
//
// The input becomes a System::String the way String producers build one (SetLength + bytes +
// the NUL at [length], embedded NULs kept), and is walked with SetCommandLine's own loop
// (configuration.cpp banner §1): ParseCommand, stop on an empty key, ParseValue, until
// pos >= length. Each returned String must lie inside the source and `pos` must only advance.
// The loop is the harness's copy rather than SetCommandLine itself so the map insert (and the
// settings store it drags in) stays out of the link.
// ============================================================================
#include "system/Configuration.h"
#include "system/String.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Link stub: error.cpp (pulled in by the allocator) references the platform message box.
namespace Platform::Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {}
}  // namespace Platform::Window

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0x10000)
        return 0;
    System::String cmd;
    const int len = static_cast<int>(size);
    System::StringPrivateAccess::SetLength(cmd, len);
    if (len > 0)
        std::memcpy(cmd._data, data, size);
    cmd._data[len] = '\0';

    // SetCommandLine's guards: a null or empty line returns before the loop.
    if (len <= 0)
        return 0;
    int pos = 0;
    int guard = 0;
    do {
        const int before = pos;
        System::String key(System::Configuration::ParseCommand(pos, cmd));
        if (key._data == nullptr || key.GetLength() == 0)
            break;
        if (key.GetLength() > len)
            __builtin_trap();
        System::String val(System::Configuration::ParseValue(pos, cmd));
        if (val.GetLength() > len)
            __builtin_trap();
        if (pos <= before || pos > len + 1)
            __builtin_trap();
        if (++guard > len + 2)
            __builtin_trap();   // no progress: SetCommandLine would spin
    } while (pos < len);
    return 0;
}
