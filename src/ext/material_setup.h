// ============================================================================
// src/ext/material_setup.h -- the extensions that share Graphics::MaterialInstance::_extAfterSetup
// ============================================================================
// NOT RETAIL. The engine seam is one null-by-default pointer (DESIGN.md hook rule 5), called after every
// MaterialInstance::Setup on the drawing thread. Two rows use it: ext.better_trees' natural tree growth
// (the draw's blossom code, docs/extensions/BLOSSOM.md) and ext.better_clouds (the sky draw's constants
// and noise texture, docs/extensions/handins/BETTERCLOUDS.md). Each sets its own function here; the seam
// holds that function itself while one is set, a two-call dispatcher while both are, and null while
// neither is, so the faithful path and either row alone pay what they paid before.
//
// Main thread only (each row's Sync / BeginFrame). The drawing thread reads the engine pointer once per
// Setup; it holds a valid function (or null) at every moment.
// ============================================================================
#pragma once

namespace Graphics { class MaterialInstance; }
namespace Video { class Context; }

namespace Ext {
namespace MaterialSetup {

using Fn = void (*)(const Graphics::MaterialInstance& instance, Video::Context& context);

void SetBlossom(Fn fn);   // ext.better_trees (natural tree growth); null = off
void SetClouds(Fn fn);    // ext.better_clouds; null = off

}  // namespace MaterialSetup
}  // namespace Ext
