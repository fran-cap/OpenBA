#pragma once
// ============================================================================
// src/app/log_redirect.h -- the standalone_*.log redirect of stdout / stderr
// ============================================================================
// NOT RETAIL: retail opens no log files. WinMain, the POSIX main and GameMain's
// re-open after Engine::Startup bind stdout / stderr to the two logs here.
// ============================================================================

#include <cstdio>

// Rebinds `stream` to `path` (fopen mode `mode`). True when the stream now writes the file.
// Review F-135: freopen closes the stream before it opens the file, so on a path that cannot be
// opened (an unwritable run directory) stdout / stderr were left closed and every later write
// used a closed FILE (undefined) and was lost. The file is opened once first; when that fails the
// stream is left as it was (the console, or nothing in a GUI process) and false is returned.
inline bool RedirectLogStream(const char* path, const char* mode, std::FILE* stream) {
    std::FILE* probe = std::fopen(path, mode);
    if (probe == nullptr)
        return false;
    std::fclose(probe);
    return std::freopen(path, mode, stream) != nullptr;
}
