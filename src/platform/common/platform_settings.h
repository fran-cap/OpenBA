// ============================================================================
// src/platform/common/platform_settings.h — the settings seam (Phase D.4 SETTINGS E.2)
// ============================================================================
// NOT RETAIL. The four OS services the settings file and the save directory
// need (docs/runtime_port/phase_d/plans/SETTINGS.md §C, header spelling per
// D4_RULES C1). Every path and string is UTF-8; no OS header is included here.
//
// Implementations:
//   Windows  src/platform/win/settings_win.cpp
//   POSIX    src/platform/posix/settings_posix.cpp — DocumentsDir is
//            the per-user data dir WITHOUT getenv (<home>/.local/share, <home>
//            from getpwuid_r); ReadLegacySettings returns false.
//
// Callers: IO::FileSystem::GetSaveDirectory (DocumentsDir, System::String);
// IO::SettingsStore (src/engine/io/settings_store.cpp, SETTINGS E.3) for the
// rest. Per D4_RULES C3 settings_win.cpp's two file functions stand on THREADS'
// file primitives (platform_file.h).
//
// ALLOCATOR-FREE. Every function except the System::String DocumentsDir
// works on std::string and the CRT heap only: src/app/main.cpp's harness
// guard calls them BEFORE Engine::Startup has constructed
// System::MemoryAllocator, when any System::String allocation would
// dereference a null allocator singleton.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>

namespace System { class String; }

namespace Platform {

// The user's documents folder (Windows: CSIDL_PERSONAL), UTF-8, no trailing
// separator. false when the OS cannot say; `out` is then untouched.
bool DocumentsDir(System::String& out);

// The same folder as UTF-8 in a std::string (allocator-free; see the banner).
// The UTF-16 -> UTF-8 rule is WideString::AsUTF8's (a lone surrogate is
// dropped), so both overloads spell one folder byte-identically.
bool DocumentsDir(std::string& out);

// NOT RETAIL, harness only (src/platform/common/user_dir_override.cpp). The
// BANISHED_USER_DIR override of the game's per-user folder "<Documents>\Banished"
// AS A WHOLE: the directory itself holds settings.cfg, Save\ and Cache\. Set once
// by src/app/main.cpp after OptionsInit (the platform layer never reads Options);
// null / empty clears it. UserDirOverride() is null when unset, so the two
// consumers (IO::SettingsStore::DefaultPath, FileSystem::GetSaveDirectory) fall
// through to their retail DocumentsDir arm untouched. UTF-8, no trailing
// separator.
void SetUserDirOverride(const char* dirUtf8);
const char* UserDirOverride();

// The whole file as bytes. false when it cannot be opened (absent included);
// `out` is then untouched.
bool ReadWholeFile(const char* pathUtf8, std::string& out);

// Write `n` bytes to `<path>.tmp`, flush, then replace `path` with it. The
// parent directory chain is created first if absent. false on any failure
// (the tmp file is removed; `path` keeps its old content).
bool WriteFileAtomic(const char* pathUtf8, const char* data, size_t n);

// The read-only import of a legacy settings store (Windows: retail's
// HKCU\Software\Shining Rock Software LLC\Banished, opened KEY_READ, never
// created). Calls `emit` once per string value with UTF-8 key and value.
// false when there is no legacy store (POSIX: always).
bool ReadLegacySettings(void (*emit)(void* ctx, const char* key, const char* value),
                        void* ctx);

}  // namespace Platform
