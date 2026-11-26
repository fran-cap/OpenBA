// ============================================================================
// src/ext/kept_render_state.cpp -- NOT RETAIL. kept_render_state.h has the contract.
// ============================================================================

#include "video/RenderState.h"

#include "ext/kept_render_state.h"
#include "ext/kept_render_state_rules.h"

#include <mutex>

namespace Ext {

namespace {

// The list of every KeptRenderState: linked by the constructors (function statics, built at their first
// use on whichever thread draws), walked by OnVideoLost.
std::mutex       g_listLock;
KeptRenderState* g_first = nullptr;

}  // namespace

KeptRenderState::KeptRenderState() {
    const std::lock_guard<std::mutex> lock(g_listLock);
    _next   = g_first;
    g_first = this;
}

const Video::RenderState* KeptRenderState::For(const Video::RenderState* source, InitFn init) {
    const KeptStateAction action = KeptStateActionFor(_ours != nullptr, _lost, source == _source);
    if (action == KeptStateAction::Reuse) return _ours;

    Video::RenderState* state = nullptr;
    if (action == KeptStateAction::MakeInPlace) {
        // As OnVideoRestart makes the factory's states again: the in-place factory on the same storage.
        state = static_cast<Video::RenderState*>(Video::RenderState::CreateInPlaceObject(_ours));
    } else {
        // A live state of another source stays alive (a context may still have it bound).
        state = static_cast<Video::RenderState*>(Video::RenderState::CreateNewObject());
    }
    if (state == nullptr) return nullptr;
    init(*state, *source);
    _ours   = state;
    _source = source;
    _lost   = false;
    return _ours;
}

void KeptRenderState::Drop() {
    _source = nullptr;
    if (_ours == nullptr || _lost) return;
    _ours->Destruct();   // External slot 7: ~RenderState, which releases the device half
    _lost = true;
}

void KeptRenderState::OnVideoLost() {
    const std::lock_guard<std::mutex> lock(g_listLock);
    for (KeptRenderState* k = g_first; k != nullptr; k = k->_next) {
        k->Drop();
    }
}

}  // namespace Ext
