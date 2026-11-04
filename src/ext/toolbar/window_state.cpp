// ============================================================================
// src/ext/toolbar/window_state.cpp -- the extension window's host record (window_state.h)
// ============================================================================
// NOT RETAIL. DESIGN.md "Host records".
// ============================================================================

#include "ext/toolbar/window_state.h"

namespace Ext {
namespace Toolbar {
namespace {

WindowState g_window;

bool ValidPageId(const std::string& id) {
    if (id.size() > kMaxPageIdLength) {
        return false;
    }
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Wire format v1: u8 open, i32 x, i32 y, String page.
void Encode(std::vector<uint8_t>& out) {
    SaveTail::Writer w(out);
    const uint8_t open = g_window.open ? 1 : 0;
    w.Bytes(&open, 1);
    w.I32(g_window.x);
    w.I32(g_window.y);
    w.String(g_window.page);
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) {
        return false;
    }
    SaveTail::Reader r(bytes, size);
    const uint8_t* open = nullptr;
    WindowState s;
    if (!r.Bytes(1, open) || *open > 1 || !r.I32(s.x) || !r.I32(s.y) || !r.String(s.page)) {
        return false;
    }
    if (!r.AtEnd() || !ValidPageId(s.page)) {
        return false;
    }
    // Hostile-save hardening (review F-259). The window writes only drag positions (int16 mouse
    // coordinates times the UI scale, never below 0); a position far outside that range would overflow
    // `w + x` / `h + y` in DialogInstance::SetUpperLeft's faithful clamp when the window reopens.
    if (s.x < 0 || s.y < 0 || s.x > (1 << 20) || s.y > (1 << 20)) {
        return false;
    }
    s.open = *open == 1;
    g_window = s;   // nothing applied before the whole payload read
    return true;
}

void Reset() { g_window = WindowState(); }

}  // namespace

WindowState& SavedWindow() { return g_window; }

const SaveTail::HostCodec& WindowCodec() {
    static const SaveTail::HostCodec c = { "toolbar", 1, Encode, Decode, Reset };
    return c;
}

}  // namespace Toolbar
}  // namespace Ext
