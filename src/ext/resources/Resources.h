#pragma once
// ============================================================================
// src/ext/resources/Resources.h -- resource providers (docs/extensions/KITFREE.md §2)
// ============================================================================
// NOT RETAIL. The extension resources (the Extensions dialog, the Enhanced
// tab, the string table, the sprite sheet, ext.sharp_ui's font variants) are
// built inside the exe instead of being compiled by the mod kit. When the
// ExternalFactory's lookup for a path misses on disk (no loose file, no
// package entry), the load path asks Provide(); the provider that owns the
// path builds the Object::External in memory, gives it that path, and the
// factory registers it exactly as it registers a loaded object
// (ExternalFactory::Add). A provider may be asked again for the same path
// after ExternalFactory::Clean dropped the object: build it again.
//
// Shared by three blocks (KITFREE.md §3): RSCTEXT owns resources.cpp and the
// engine seam; RSCSPRITE and RSCFONT register providers through Register().
// This header is the contract; change it only with the coordinator.
// ============================================================================

#include <cstddef>

namespace Object { class External; }

namespace Ext {
namespace Resources {

// Builds the object for `path` ("Dialog/Extensions.rsc:buttonExtensions",
// "Font/SharpFont_32.rsc:resource", the "File.rsc:name" form the factory
// uses; a top-level object is named "resource"). Returns null when this
// provider does not own the path or could not build it (log why, as
// `[ExtRes]` on stderr). The object's path must already be set
// (SetPath(path)); the caller registers it.
using ProviderFn = Object::External* (*)(const char* path, void* user);

// Registers a provider for every path that starts with `prefix`
// (e.g. "Dialog/Extensions", "Font/SharpFont"). Called from the ext boot
// (registry_boot.cpp's Ext::InitFromSettings order) by each block. Returns
// false for a null prefix or function; the table has no size limit, and the same (prefix, fn, user) twice is
// registered once (and returns true).
bool Register(const char* prefix, ProviderFn fn, void* user);

// True when a registered provider owns `path` (prefix match, nothing built):
// for code that would otherwise test the disk before asking for a resource
// (ext.sharp_ui's variant pick).
bool HasProvider(const char* path);

// The load-miss seam. Walks the table in registration order and returns the
// first provider's object, or null. Called by the engine's load path (one
// `// EXT SEAM` line) only after the disk lookup failed; never under the
// command-line faithful profile (`/profile:faithful`, Ext::GateProfileForced:
// no provider registers at boot) until a save carrying an `ext:` pseudo-mod
// is opened (SaveLoadDialog::DoLoad then calls InitText).
Object::External* Provide(const char* path);

// Sets the object's own path (External::_path, what GetPath returns and what
// ExternalFactory::Add keys the resource tree by). Implemented in
// resources.cpp with the engine's System::Filename.
void SetPath(Object::External* obj, const char* path);

// Hands a built object to the factory (ExternalFactory::Add) and returns it;
// null in, null out. Providers normally do NOT call this: the seam does.
Object::External* Publish(Object::External* obj);

}  // namespace Resources
}  // namespace Ext
