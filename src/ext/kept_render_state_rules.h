#pragma once
// ============================================================================
// src/ext/kept_render_state_rules.h -- what a KeptRenderState does at a request (pure)
// ============================================================================
// NOT RETAIL. Standard C++ only (tests/unit/ext_kept_render_state_test.cpp); the object itself is
// kept_render_state.h.
//
// The slot holds at most one derived state. A request names the source state it is derived from:
//   - no state yet: a new object is made;
//   - the state's device half was dropped by a video restart: the state is made again in its own
//     storage, whatever the source (the restart rebuilds the source in place, at the same address, so
//     the address says nothing about what the kept copy still holds);
//   - a live state of that source: it is handed out again;
//   - a live state of another source (the material was loaded anew): a new object is made, and the old
//     one stays alive, since a context may still have it bound.
// ============================================================================

namespace Ext {

enum class KeptStateAction {
    Reuse,         // hand out the kept state
    MakeNew,       // allocate and initialize a new state
    MakeInPlace,   // construct and initialize a state in the kept storage
};

inline KeptStateAction KeptStateActionFor(bool haveStorage, bool lost, bool sameSource) {
    if (!haveStorage) return KeptStateAction::MakeNew;
    if (lost) return KeptStateAction::MakeInPlace;
    return sameSource ? KeptStateAction::Reuse : KeptStateAction::MakeNew;
}

}  // namespace Ext
