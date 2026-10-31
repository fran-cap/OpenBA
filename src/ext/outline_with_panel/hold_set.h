#pragma once
// ============================================================================
// src/ext/outline_with_panel/hold_set.h -- fix.outline_with_panel's per-frame record (pure)
// ============================================================================
// NOT RETAIL (docs/retail_bugs.md §16, docs/extensions/handins/OUTLINEWITHPANEL.md). Standard C++ only
// (tests/unit/ext_outline_with_panel_test.cpp); the locking is the caller's.
//
// Which outlines a frame gathered, and whether this frame holds back the ones the last frame did not
// gather. BeginFrame starts a frame (the last frame's record becomes `prev`); Gathered answers, for one
// outline the frame's gather reaches, whether to skip it, and records it when it is drawn. An outline
// gathered several times in one frame (several scene passes) is recorded once and answered the same
// way each time. A full record is no longer trusted: the next frame holds nothing back.
// ============================================================================

#include <cstdint>

namespace Ext {
namespace OutlineWithPanel {

template <int32_t N>
class HoldSet {
public:
    // A new frame. `hold`: this frame holds back the outlines the last frame did not draw.
    void BeginFrame(bool hold) {
        for (int32_t i = 0; i < curCount_; ++i)
            prev_[i] = cur_[i];
        prevCount_ = curCount_;
        prevFull_ = curFull_;
        curCount_ = 0;
        curFull_ = false;
        hold_ = hold;
    }

    // One outline reached by this frame's gather. True: skip it this frame (the frame holds and the
    // last frame did not draw it). False: draw it; it is recorded as drawn.
    bool Gathered(const void* outline) {
        if (hold_ && !prevFull_ && !Contains(prev_, prevCount_, outline)) return true;
        if (Contains(cur_, curCount_, outline)) return false;
        if (curCount_ < N) {
            cur_[curCount_++] = outline;
        } else {
            curFull_ = true;
        }
        return false;
    }

    bool Holding() const { return hold_; }

private:
    static bool Contains(const void* const* set, int32_t count, const void* p) {
        for (int32_t i = 0; i < count; ++i) {
            if (set[i] == p) return true;
        }
        return false;
    }

    const void* prev_[N] = {};
    const void* cur_[N] = {};
    int32_t prevCount_ = 0;
    int32_t curCount_ = 0;
    bool prevFull_ = false;
    bool curFull_ = false;
    bool hold_ = false;
};

}  // namespace OutlineWithPanel
}  // namespace Ext
