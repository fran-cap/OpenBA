// ============================================================================
// src/ext/render_pipe/replay_params.h -- SMOOTHREPLAY: the replayed frames' tunables
// ============================================================================
// NOT RETAIL. ext.render_pipe, value key render_pipe_replay
// (docs/extensions/handins/SMOOTHREPLAY.md section 2). The defaults are what ships; the
// debug tweak overlay moves a copy (Ext::Tweak::ReplayValues, docs/reference/tweak_overlay.md).
// ============================================================================
#pragma once

#include <cstdint>

#include "ext/render_pipe/display_clock.h"   // kDisplayDelayUs

namespace Ext {
namespace RenderPipe {

struct ReplayLook {
    // How far the displayed pose runs behind retail's, microseconds (display_clock.h). 0 = no delay:
    // a replayed frame then mostly repeats the packet's last picture.
    int32_t delayUs = static_cast<int32_t>(kDisplayDelayUs);
};

}  // namespace RenderPipe
}  // namespace Ext
