// ============================================================================
// src/platform/win/pick_file_win.cpp -- Platform::PickOpenFile over GetOpenFileNameW
// ============================================================================
// NOT RETAIL. The rules are in platform_pick_file.h.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <string>

#include "platform_pick_file.h"

namespace Platform {

namespace {

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
    }
    return out;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], n, nullptr,
                            nullptr);
    }
    return out;
}

}  // namespace

bool CanPickFile() {
    return true;
}

bool PickOpenFile(const char* title, const char* filterName, const char* filterPattern,
                  const std::string& startDir, std::string& out) {
    wchar_t buffer[MAX_PATH * 4] = {0};
    const std::wstring titleW = Widen(title != nullptr ? title : "");
    const std::wstring startW = Widen(startDir);
    // "name\0pattern\0All files\0*.*\0\0": the dialog's filter list is double-NUL terminated.
    std::wstring filter = Widen(filterName != nullptr ? filterName : "");
    filter.push_back(L'\0');
    filter += Widen(filterPattern != nullptr ? filterPattern : "*.*");
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    ofn.lpstrTitle = titleW.c_str();
    if (!startW.empty()) {
        ofn.lpstrInitialDir = startW.c_str();
    }
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }
    out = Narrow(std::wstring(buffer));
    return true;
}

}  // namespace Platform
