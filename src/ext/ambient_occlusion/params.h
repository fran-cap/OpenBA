// src/ext/ambient_occlusion/params.h -- the contract between the menu (AOMENU) and the pass (AOCORE).
//
// `ext.ambient_occlusion` (class A, docs/extensions/AO.md). AOMENU owns the values: the settings keys
// `ext.ao_quality` / `ext.ao_opacity`, their parse / clamp / record, the Options tab that edits
// them (settings.cpp, enhanced_entry.cpp). AOCORE owns everything that consumes them (ambient_occlusion.cpp, src/render/dx11/ao/).
// Neither block edits the other's files; this header is the only shared line.
#pragma once

namespace Ext {
namespace AmbientOcclusion {

struct Params {
    // 0 = off (the row's bool mirrors `quality > 0`), 1 Low, 2 Medium, 3 High, 4 Lowest (AO.md §3 "Tiers").
    int quality = 2;
    // 0..30 percent (default 20, the user's look 2026-10-14), folded into ASSAO's
    // ShadowMultiplier (strength x opacity / 100, AoMath::FrameFor).
    int opacity = 20;
};

// The live values, read once per frame by the pass (never inside a loop). AOMENU's settings.cpp
// rebuilds them on every call from the registry's row and the ext.ao_quality / ext.ao_opacity keys.
const Params& Current();

// AOMENU's setter: clamps, stores, records the keys (`SetAndRecord`-style), and keeps the row's
// bool in step with `quality > 0`. AOCORE never calls it.
void Set(const Params& p);

}  // namespace AmbientOcclusion
}  // namespace Ext
