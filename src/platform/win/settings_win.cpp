// ============================================================================
// src/platform/win/settings_win.cpp — the settings seam, Win32 (Phase D.4 SETTINGS E.2)
// ============================================================================
// src/platform/common/platform_settings.h declares the four functions; this
// file is their Windows body and the only place the seam touches wchar_t.
//
// DocumentsDir is the OS half of retail's FileSystem::GetSaveDirectory
// (0x1800acfd0), moved here unchanged from the deleted
// src/platform/win/filesystem_win.cpp: SHGetFolderPathW(CSIDL_PERSONAL) into
// wchar_t[264], then System::WideString's ctor (0x18000a6e0) and AsUTF8
// (0x1800cacd0). The Format / Filename / Error::SetText half stays in
// src/engine/io/filesystem.cpp.
//
// The rest is NOT retail (plans/SETTINGS.md §B, §C):
//   DocumentsDir(std::string&)       the same SHGetFolderPathW, converted
//                                    without the engine allocator.
//   ReadWholeFile / WriteFileAtomic  over THREADS' file primitives
//                                    (platform_file.h: FileOpenReadShared /
//                                    FileRead / FileClose, DirCreate /
//                                    FileCreateWrite / FileWrite / FileFlush /
//                                    FileReplace / FileDelete; D4_RULES C3,
//                                    Phase D.4 THREADS-2), the path converted
//                                    with MultiByteToWideChar(CP_UTF8).
//   ReadLegacySettings               the one-time, read-only import of
//                                    retail's registry store: KEY_READ, never
//                                    created, REG_SZ values only.
//
// ALLOCATOR-FREE (SETTINGS E.3). Everything but the System::String
// DocumentsDir uses std::string / std::wstring only, because
// src/app/main.cpp's harness guard runs before Engine::Startup constructs
// System::MemoryAllocator. The UTF-16 -> UTF-8 conversion below is
// WideString::AsUTF8's rule (0x1800cacd0: a valid surrogate pair is one
// 4-byte sequence, a lone surrogate encodes to ZERO bytes), so an imported
// key or value is byte-identical to what retail's ReadSettings put in the map.
// ============================================================================

#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "platform_settings.h"
#include "platform_file.h"   // the file primitives ReadWholeFile / WriteFileAtomic stand on
#include "system/String.h"
#include "system/WideString.h"

namespace {

// The store retail's IO::Registry ctor opened (0x1800b9500 builds it with
// Format("Software\\%s\\%s", "Shining Rock Software LLC", "Banished")).
const wchar_t kLegacyKey[] = L"Software\\Shining Rock Software LLC\\Banished";

// `len` UTF-16 units -> UTF-8 by WideString::AsUTF8's rule (the banner):
// a high surrogate followed by a low one is one supplementary code point; a
// lone high or low surrogate is dropped. No engine allocation.
std::string WideToUtf8(const wchar_t* w, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint32_t c = static_cast<uint16_t>(w[i]);
        if (c >= 0xD800 && c < 0xDC00) {
            if (i + 1 >= len) continue;                       // lone high
            const uint32_t c2 = static_cast<uint16_t>(w[i + 1]);
            if (c2 < 0xDC00 || c2 >= 0xE000) continue;        // lone high
            c = 0x10000u + (((c - 0xD800u) << 10) | (c2 - 0xDC00u));
            ++i;
        } else if (c >= 0xDC00 && c < 0xE000) {
            continue;                                         // lone low
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// A path in the file primitives' unit (Platform::PathChar: char16_t, platform_file.h,
// the same 16-bit unit as wchar_t on Windows). std:: only: no engine
// allocator (the banner).
using PathString = std::basic_string<Platform::PathChar>;

// A UTF-8 path -> UTF-16 for the file primitives. Empty on a conversion failure,
// which every caller treats as "cannot open".
PathString Utf8ToPath(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0)
        return PathString();
    PathString out(static_cast<size_t>(n), Platform::PathChar(0));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, reinterpret_cast<wchar_t*>(&out[0]), n);  // CAST-OK: win32
    out.resize(static_cast<size_t>(n - 1));                   // the NUL
    return out;
}

// The directory part of a path (up to the last separator), or empty.
PathString ParentOf(const PathString& path) {
    static const Platform::PathChar kSeparators[] = {'\\', '/', 0};
    const size_t cut = path.find_last_of(kSeparators);
    if (cut == PathString::npos)
        return PathString();
    return path.substr(0, cut);
}

bool WriteAll(Platform::FileHandle h, const char* data, size_t n) {
    while (n > 0) {
        const uint32_t chunk = n > 0x40000000u ? 0x40000000u : static_cast<uint32_t>(n);
        uint32_t written = 0;
        if (!Platform::FileWrite(h, data, chunk, &written) || written == 0)
            return false;
        data += written;
        n -= written;
    }
    return true;
}

}  // namespace

namespace Platform {

bool DocumentsDir(System::String& out) {
    wchar_t buf[264];
    const HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf);
    if (hr != S_OK)
        return false;

    // 0x1800ad034: the WideString slot is zeroed, then Set (0x1800caf40) --
    // the `const wchar_t*` ctor is exactly that pair. AsUTF8 returns the
    // String by value.
    System::WideString ws(reinterpret_cast<const char16_t*>(buf));  // CAST-OK: win32
    out = ws.AsUTF8();
    return true;
}

bool DocumentsDir(std::string& out) {
    wchar_t buf[264];
    const HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf);
    if (hr != S_OK)
        return false;
    out = WideToUtf8(buf, wcslen(buf));
    return true;
}

bool ReadWholeFile(const char* pathUtf8, std::string& out) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const PathString wpath = Utf8ToPath(pathUtf8);
    if (wpath.empty())
        return false;

    const Platform::FileHandle h = Platform::FileOpenReadShared(wpath.c_str());
    if (h == Platform::kInvalidFileHandle)
        return false;

    std::string data;
    char chunk[16384];
    bool ok = true;
    for (;;) {
        uint32_t got = 0;
        if (!Platform::FileRead(h, chunk, sizeof(chunk), &got)) {
            ok = false;
            break;
        }
        if (got == 0)
            break;
        data.append(chunk, got);
    }
    Platform::FileClose(h);
    if (!ok)
        return false;
    out.swap(data);
    return true;
}

bool WriteFileAtomic(const char* pathUtf8, const char* data, size_t n) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const PathString wpath = Utf8ToPath(pathUtf8);
    if (wpath.empty())
        return false;
    static const Platform::PathChar kTmpSuffix[] = {'.', 't', 'm', 'p', 0};
    const PathString wtmp = wpath + kTmpSuffix;

    const PathString parent = ParentOf(wpath);
    // Platform::EnsureDirChain (platform_file.h); a real failure surfaces at the FileCreateWrite
    // that follows.
    if (!parent.empty())
        Platform::EnsureDirChain(parent.c_str());

    const Platform::FileHandle h = Platform::FileCreateWrite(wtmp.c_str());
    if (h == Platform::kInvalidFileHandle)
        return false;

    bool ok = WriteAll(h, data, n);
    if (ok && !Platform::FileFlush(h))
        ok = false;
    Platform::FileClose(h);

    if (ok && !Platform::FileReplace(wtmp.c_str(), wpath.c_str()))
        ok = false;
    if (!ok)
        Platform::FileDelete(wtmp.c_str());
    return ok;
}

bool ReadLegacySettings(void (*emit)(void* ctx, const char* key, const char* value),
                        void* ctx) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    DWORD valueCount = 0, maxNameLen = 0, maxDataBytes = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &valueCount, &maxNameLen, &maxDataBytes, nullptr,
                         nullptr) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> name(static_cast<size_t>(maxNameLen) + 1);
    // +1 unit so a REG_SZ stored without its terminator still ends in one.
    std::vector<wchar_t> value(static_cast<size_t>(maxDataBytes) / sizeof(wchar_t) + 1);

    for (DWORD i = 0; i < valueCount; ++i) {
        DWORD nameLen = static_cast<DWORD>(name.size());
        DWORD type = 0;
        DWORD bytes = static_cast<DWORD>((value.size() - 1) * sizeof(wchar_t));
        const LONG rc = RegEnumValueW(key, i, name.data(), &nameLen, nullptr, &type,
                                      reinterpret_cast<BYTE*>(value.data()),  // CAST-OK: win32
                                      &bytes);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc != ERROR_SUCCESS)
            continue;

        const std::string keyUtf8 = WideToUtf8(name.data(), nameLen);
        if (type != REG_SZ) {
            std::fprintf(stderr,
                         "[Settings] legacy import: skipped non-REG_SZ value '%s' (type %lu)\n",
                         keyUtf8.c_str(), static_cast<unsigned long>(type));
            continue;
        }

        size_t units = bytes / sizeof(wchar_t);
        while (units > 0 && value[units - 1] == L'\0')
            --units;                        // the stored terminator(s)
        const std::string valueUtf8 = WideToUtf8(value.data(), units);
        emit(ctx, keyUtf8.c_str(), valueUtf8.c_str());
    }

    RegCloseKey(key);
    return true;
}

}  // namespace Platform
