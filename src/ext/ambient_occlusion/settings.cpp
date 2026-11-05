// ============================================================================
// src/ext/ambient_occlusion/settings.cpp -- Ext::AmbientOcclusion::Current / Set (block AOMENU)
// ============================================================================
// NOT RETAIL. params.h is the contract with AOCORE. The values live in the
// registry (src/ext/registry.h, "VALUED KEYS"): the bool row
// `ext.ambient_occlusion` and the two valued keys `ext.ao_quality` (1..4) and
// `ext.ao_opacity` (0..30). This file only maps them to Params and back:
//
//   Current().quality = row on ? ext.ao_quality : 0      (0 = off, params.h)
//   Current().opacity = ext.ao_opacity
//
// Set(p) is the menu's write (the Options dialog's "Enhanced" tab):
//   p.quality <= 0   the row goes OFF; ext.ao_quality keeps its stored tier, so
//                    turning the row back on (here or in the Extensions dialog)
//                    restores it
//   p.quality >= 1   ext.ao_quality = clamp(p.quality, 1, 4), the row goes ON (4 = Lowest)
//   p.opacity        ext.ao_opacity = clamp(p.opacity, 0, 30)
// The row is written through the registry's own SetAndRecord (so its Sync
// hooks fire: AOCORE's Ext::AmbientOcclusion::Sync rebuilds the readable depth on a
// flip) and only when it actually changes. The caller flushes settings.cfg
// (Ext::PersistSettings, or retail's own WriteSettings in OnApply).
// ============================================================================

#include "ext/ambient_occlusion/params.h"

#include "ext/registry.h"

namespace Ext {
namespace AmbientOcclusion {

namespace {

const ValueKey& Key(const char* id) {
    // Both keys are in the registry's table (registry.cpp kValueKeys); the unit
    // tests check the names, so a miss is a build-time typo, not a run-time state.
    return *FindValueKey(id);
}

const Entry& Row() { return *FindEntry("ambient_occlusion"); }

}  // namespace

const Params& Current() {
    // Rebuilt on every call (the pass reads it once per frame): the row's bool can
    // change in the Extensions dialog without this file hearing of it. Per thread,
    // so a reader on the render thread never sees a half-written copy.
    thread_local Params p;
    p.quality = Ext::Ext().ambient_occlusion ? Val().ao_quality : 0;
    p.opacity = Val().ao_opacity;
    return p;
}

void Set(const Params& p) {
    const bool on = p.quality > 0;
    if (on) SetValueAndRecord(Key("ao_quality"), p.quality);
    SetValueAndRecord(Key("ao_opacity"), p.opacity);
    const Entry& row = Row();
    if (Requested(row) != on) SetAndRecord(row, on);
}

}  // namespace AmbientOcclusion
}  // namespace Ext
