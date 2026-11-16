// ============================================================================
// src/ext/custom_maps/mod_check.cpp -- ext.custom_maps: is a mod the game has enabled (mod_check.h)
// ============================================================================
// The runtime type headers first (saveloaddialog.cpp's note: Volume.h's Rectangle against <wingdi.h>).
#include "io/Modification.h"
#include "system/String.h"

#include "ext/custom_maps/mod_check.h"

#include <cctype>
#include <cstring>
#include <string>

namespace Ext {
namespace CustomMaps {

namespace {

bool SameName(const char* a, const std::string& b) {
    if (a == nullptr) return false;
    const size_t n = std::strlen(a);
    if (n != b.size()) return false;
    for (size_t i = 0; i < n; ++i) {
        const int left = std::tolower(static_cast<unsigned char>(a[i]));
        const int right = std::tolower(static_cast<unsigned char>(b[i]));
        if (left != right) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool ModIsEnabled(const ModRef& mod) {
    const ::IO::Modification* mods = ::IO::Modification::_singleton;
    if (mods == nullptr) return true;
    for (int i = 0, n = mods->GetCount(); i < n; ++i) {
        const RtIO::Mod& installed = mods->GetModification(i);
        if (installed.enabled != 0 && SameName(installed.name._data, mod.name)) return true;
    }
    return false;
}

}  // namespace CustomMaps
}  // namespace Ext
