// ============================================================================
// src/ext/phone_menu/dialog_records.h -- the dialog descriptions ext.phone_menu recognises
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_phone_dialog_records_test.cpp).
//
// The row knows the menus, the pause menus, the version line and the loading screens by their
// descriptions (UI::Dialog resources). ExternalFactory::Clean evicts those between a menu and a game,
// and they come back at a new address on every round trip; a freed address can then hold another
// resource (review F-257). So a record is the address AND a hash of the resource path: a description
// matches only when both do. The table is a ring: a new record replaces the oldest, so the newest
// menu is always recorded (a table that only grew stopped recognising the fourth menu).
//
// Written on the main thread, read by the draw as well: each slot's address is published after its
// hash (release / acquire). A reader that meets a slot being rewritten answers "not recorded" for
// that one call.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>

namespace Ext {
namespace PhoneMenu {

// FNV-1a over the path with ASCII letters folded to lower case (resource paths compare with
// _stricmp). Null hashes as the empty path.
inline uint64_t PathHash(const char* path) {
    uint64_t h = 1469598103934665603ull;
    if (path == nullptr) return h;
    for (const char* p = path; *p != '\0'; ++p) {
        uint8_t c = static_cast<uint8_t>(*p);
        if (c >= 'A' && c <= 'Z') c = static_cast<uint8_t>(c - 'A' + 'a');
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

template <int N>
class DialogRecords {
public:
    // True when `dialog` was recorded with `path`. The path is hashed only on an address match.
    bool Has(const void* dialog, const char* path) const {
        if (dialog == nullptr) return false;
        for (int i = 0; i < N; ++i) {
            if (_dialog[i].load(std::memory_order_acquire) != dialog) continue;
            if (_path[i].load(std::memory_order_relaxed) == PathHash(path)) return true;
        }
        return false;
    }

    // Records `dialog` with `path`: a recorded address takes the new path, otherwise the oldest
    // slot is replaced. Main thread only.
    void Record(const void* dialog, const char* pathChars) {
        if (dialog == nullptr) return;
        const uint64_t path = PathHash(pathChars);
        int slot = -1;
        for (int i = 0; i < N && slot < 0; ++i) {
            if (_dialog[i].load(std::memory_order_relaxed) == dialog) slot = i;
        }
        if (slot < 0) {
            slot = _next;
            _next = (_next + 1) % N;
        } else if (_path[slot].load(std::memory_order_relaxed) == path) {
            return;
        }
        _dialog[slot].store(nullptr, std::memory_order_relaxed);
        _path[slot].store(path, std::memory_order_relaxed);
        _dialog[slot].store(dialog, std::memory_order_release);
    }

private:
    std::atomic<const void*> _dialog[N] = {};
    std::atomic<uint64_t> _path[N] = {};
    int _next = 0;
};

}  // namespace PhoneMenu
}  // namespace Ext
