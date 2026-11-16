// ============================================================================
// src/engine/io/settings_store.h — settings.cfg on disk (Phase D.4 SETTINGS E.3)
// ============================================================================
// NOT RETAIL. Retail's IO::Persistence kept its flat settings map in
// HKCU\Software\Shining Rock Software LLC\Banished (IO::Registry, deleted).
// This is the file that replaces that key: the codec (settings_file.h) over
// the platform seam (platform_settings.h), and nothing else. It knows no
// engine type, so the unit tests drive it over a temp directory
// (tests/unit/persistence_settings_test.cpp), and it allocates nothing from
// System::MemoryAllocator, so src/app/main.cpp's harness guard can use it
// before Engine::Startup.
//
// Callers: IO::Persistence::ReadSettings / WriteSettings
// (persistence_settings.cpp), src/app/main.cpp (DumpVideoSettings,
// HarnessGuardUnattendedBoot), src/ext/render_backend/render_backend.cpp and
// src/platform/common/cpu/topology.cpp; DefaultPath alone from
// src/ext/derived_cache/derived_cache_shared.cpp and
// src/ext/building_finder/finder_core.cpp. docs/runtime_port/phase_d/plans/SETTINGS.md §B.
// ============================================================================
#pragma once

#include <string>

#include "io/settings_file.h"

namespace IO {
namespace SettingsStore {

// `<Documents>\Banished\settings.cfg`: FileSystem::GetSaveDirectory()'s
// folder ("%s\\%s\\" over the documents folder and "Banished"; its
// AdjustDelimiters is a no-op on an SHGetFolderPathW path) plus the file
// name, next to `Save\`. false when the documents folder is unknown — retail's
// "the key could not be opened" (`IO::Registry::_key == 0`).
bool DefaultPath(std::string& out);

// The legacy-store reader Load falls back on: Platform::ReadLegacySettings's
// shape. Injected so a unit test never reads the real registry.
using LegacyReader = bool (*)(void (*emit)(void* ctx, const char* key, const char* value),
                              void* ctx);

enum class Source {
    File,      // `path` was read
    Legacy,    // `path` is absent; the legacy store was imported READ-ONLY
    None,      // neither exists: `out` is empty (every config keeps its ctor default)
};

// `out` = the entries of `path`, in file order. When `path` cannot be read,
// the legacy store's string values instead (nothing is written: the first
// Store materialises the file). `out` is replaced in every case.
Source Load(const std::string& path, SettingsFile::Entries& out, LegacyReader legacy);
Source Load(const std::string& path, SettingsFile::Entries& out);   // legacy = the platform's

// Re-read `path`, overlay `entries` on it (SettingsFile::Merge: `entries` win,
// disk-only keys survive — retail's delete pass never deleted anything) and
// replace the file atomically. Writers are serialised by one mutex. false when
// the write failed (the old file is then intact).
bool Store(const std::string& path, const SettingsFile::Entries& entries);

}  // namespace SettingsStore
}  // namespace IO
