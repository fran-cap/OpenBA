// ============================================================================
// src/engine/io/filesystem.cpp — Wave 4 lane D: IO::FileSystem + Modification
// ============================================================================
// Byte-faithful ports of the 6 FileSystem/Modification exports our tree calls
// (docs/history/runtime_port/call_surface_20260906.csv) that do not need Win32.
// GetSaveDirectory's OS half (SHGetFolderPathW) is Platform::DocumentsDir in
// src/platform/win/settings_win.cpp (Phase D.4 SETTINGS E.2) — <windows.h>
// must never appear here. src/engine/io/FileSystem.h and Modification.h carry the layouts;
// read them first.
//
//   ?Exists@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z          retail 0x1800ab310
//   ?GetDataDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ   retail 0x180007bd0
//   ?GetSourceDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ retail 0x180007b90
//   ?GetPackfileId@FileSystem@IO@@QEBAIAEBVFilename@System@@@Z    retail 0x1800ab050
//   ?Find@Modification@IO@@QEBAHAEBVFilename@System@@@Z           retail 0x1800aea10
//
//   ?GetSaveDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ   retail 0x1800acfd0
//   (moved here from src/platform/win/filesystem_win.cpp, Phase D.4 SETTINGS E.2)
//
// All six names were checked against the shipped DLL's export directory with
// pefile BEFORE any code was written here (WAVE4_LANE_D.md §2) — none is a
// mangled name inferred from the signature rather than read from that table.
//
// HIDDEN RETURN SLOTS. GetDataDirectory, GetSourceDirectory and
// GetSaveDirectory all return `System::Filename` BY VALUE (`?AV` in the
// mangling), so each has an RDX hidden-return-slot argument. No wild-write
// defect exists in this family. See docs/history/runtime_port/WAVE4_LANE_D.md §2.
//
// STATE. Exists, GetDataDirectory, GetSourceDirectory, GetPackfileId and Find
// are read-only walks of the FileSystem / Modification objects; none writes a
// field.
//
// NO ADDED NULL GUARDS. Exists, GetPackfileId and Find all deref fields
// unconditionally exactly as retail's disassembly does; see each function's
// comment for the specific instruction.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <new>

#include "io/FileSystem.h"
#include "io/Modification.h"
#include "io/PackageFile.h"   // the packfile array's element
#include "io/packfile_fold_index.h"   // block PATHINDEX: the hash index beside _packfileIndex
#include "system/Error.h"
#include "system/String.h"
#include "system/string_internal.h"
#include "platform_settings.h"   // Platform::DocumentsDir (src/platform/common)

namespace RtIO {

using RtSystem::StringLength;

// Case-insensitive ASCII compare, sign-preserving (needed by GetPackfileId's
// tree descent, not just equality). Duplicated rather than shared from
// filename.cpp's file-local StriCmpAscii for the same reason that one gives:
// src/engine stays free of a CRT `_stricmp` whose locale behaviour differs
// between toolchains (PLAN.md §4.1 portability seam).
static int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0) return 0;
    }
}

// ============================================================================
// ?Exists@FileSystem@IO@@QEBA_NAEBVFilename@System@@@Z   retail 0x1800ab310
//
//     bool FileSystem::Exists(const Filename& path) const
//     {
//         if (this->existsInPackfileGate && ExistsInPackfile(path)) return true;
//         if (!this->platformExistsGate) return false;
//         return PlatformExists(path);
//     }
//
// ExistsInPackfile (0x1800ab180) and PlatformExists (0x1800adc70) are exported
// members whose bodies are in filesystem_find.cpp; Exists calls them as members.
// The two gate reads and the short-circuit order are Exists's own.
// ============================================================================
// The body is the member `IO::FileSystem::Exists`, at the bottom of this file.

// ============================================================================
// ?GetDataDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ  retail 0x180007bd0
//
//     Filename FileSystem::GetDataDirectory() const
//     { return Filename(this->dataDirectory); }    // copy ctor, AddRef only
//
// Disassembly (0x180007bd0..0x180007c0b) is a single tail-call:
//   LEA RDX,[RCX+0x8]; MOV RCX,RBX(=hidden retval); CALL 0x1800067d0
// 0x1800067d0 is `??0Filename@System@@QEAA@AEBV01@@Z`, Wave 2's
// RtSystem_Filename_CtorCopy — confirmed by disassembling both this function
// and GetSourceDirectory (same CALL target) rather than assumed from the
// pseudocode's `Filename::Filename(...)` name.
//
// ============================================================================
// The body is the member `IO::FileSystem::GetDataDirectory`, at the bottom of
// this file.

// ============================================================================
// ?GetSourceDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ retail 0x180007b90
//
//     Filename FileSystem::GetSourceDirectory() const
//     { return Filename(*this); }   // reinterprets `this` itself as the Filename
//
// Disassembly (0x180007b90..0x180007bca): `MOV RDX,RCX(=this); MOV RCX,RBX
// (=hidden retval); CALL 0x1800067d0` — the SAME copy ctor GetDataDirectory
// uses, called on `this` rather than `this+8`. That is only possible because
// FileSystem+0x00 IS a String/Filename object (see FileSystem.h); "source
// directory" is the class's own first field, not a nested lookup.
//
// ============================================================================
// The body is the member `IO::FileSystem::GetSourceDirectory`, at the bottom of
// this file.

// ============================================================================
// ?GetPackfileId@FileSystem@IO@@QEBAIAEBVFilename@System@@@Z    retail 0x1800ab050
//
//     unsigned int FileSystem::GetPackfileId(const Filename& path) const
//
// Only proceeds when the path is longer than dataDirectory (the length check
// substitutes for an actual prefix compare — retail trusts the caller passes
// a full path under dataDirectory, and strips the prefix BY LENGTH alone).
// Builds the relative substring, normalises it, then walks the
// PackfileIndexNode tree at `this+0x40` with a case-insensitive compare,
// descending via the pattern documented at the node struct: effectively "if
// relPath >= key, check relPath == key or go right; else go left". On a hit,
// returns `packfiles[node.packIndex]->packfileId`; on a miss, or if the
// length check fails, returns 0.
//
// NO ADDED NULL GUARDS: retail's own length check (`iVar4 < iVar3`) is the
// only guard here, transcribed exactly; a match failure returns 0, which is
// retail's own "in no package" sentinel — not an added defensive path.
//
// ============================================================================
// The body is the member `IO::FileSystem::GetPackfileId`, at the bottom of this
// file.

// ============================================================================
// ?Find@Modification@IO@@QEBAHAEBVFilename@System@@@Z   retail 0x1800aea10
//
//     int Modification::Find(const Filename& path) const
//     { for each Mod, case-insensitively compare its file to path; return
//       the index of the first match or -1. }
//
// Length-prefixed compare identical in shape to GetPackfileId's: compare
// stored lengths first, then StriCmp only if they match.
//
// ============================================================================
// The body is the member `IO::Modification::Find(const System::Filename&)`,
// below.

}  // namespace RtIO

// ============================================================================
// `IO::Modification::Find` — the member body.
// ============================================================================
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS §A.2), and reaching this file's
// `RtIO`-scoped helpers as `RtIO::X` is what lets it stay where it is rather
// than moving to modification.cpp.
namespace IO {

// ============================================================================
// ENDGAME B.3.4 PACKAGE+FS — `IO::FileSystem`'s member bodies
// ============================================================================
// The five members this file holds: Exists, GetDataDirectory, GetSourceDirectory,
// GetPackfileId and GetSaveDirectory. The others are members too, in the seven
// sibling `filesystem_*.cpp` files; src/engine/io/FileSystem.h lists which and where.
//
// Defined outside `namespace RtIO` because a member of `IO::X` must appear in a
// namespace ENCLOSING the class (B34_BRIEFS section A.2); this block reaches
// this file's `RtIO`-scoped helpers as `RtIO::X`, which works for its `static`
// members too in this same translation unit. Every POD is spelled `RtIO::` or
// `RtSystem::` and every real class `System::` (B34_RULES section 7.2).
//
// NO ADDED NULL GUARDS ANYWHERE BELOW. This file's own banner already records
// that Exists, GetPackfileId and Find deref fields unguarded because retail
// does; the move changes nothing about that.

bool FileSystem::Exists(const System::Filename& pathRef) const {
    const System::Filename* const path = &pathRef;

    // B3R RT-IO: the two arms call ExistsInPackfile / PlatformExists as
    // members (filesystem_find.cpp); they were RT_EXPORT_ONCE doors.
    bool ours;
    if (_existsInPackfileGate != 0 &&
        ExistsInPackfile(*path)) {
        ours = true;
    } else if (_platformExistsGate == 0) {
        ours = false;
    } else {
        ours = PlatformExists(*path);
    }

    return ours;
}

System::Filename FileSystem::GetDataDirectory() const {

    // ENDGAME B.3.1 step 2: retail's copy ctor, spelled as itself. The result is
    // constructed in the hidden return slot, which is what the CALL at 0x180007c06
    // does. `_dataDirectory` is a `System::Filename` member (B4 X-R3).
    return System::Filename(_dataDirectory);

}

System::Filename FileSystem::GetSourceDirectory() const {

    // ENDGAME B.3.1 step 2, as GetDataDirectory above. `sourceDirectory` is
    // FileSystem+0x00, which is why retail's `MOV RDX,RCX` passes `this` itself.
    return System::Filename(_sourceDirectory);

}

uint32_t FileSystem::GetPackfileId(const System::Filename& path) const {
    uint32_t ours = 0;

    const char* pathData = path._data;
    const int pathLen = pathData ? RtIO::StringLength(pathData) : 0;
    const char* dataDirData = _dataDirectory._data;
    const int dataDirLen = dataDirData ? RtIO::StringLength(dataDirData) : 0;

    if (dataDirLen < pathLen) {
        // Relative path = pathData + dataDirLen (a suffix of the SAME buffer,
        // not a copy yet — the Filename ctor below is what allocates the copy).
        // ENDGAME B.3.1 step 2: `Filename relPath(pathData + dataDirLen)` is
        // exactly retail's Set-then-AdjustDelimiters pair (the Filename
        // const char* ctor, 0x180026cf0). RAII replaces the manual release at
        // the bottom of the block.
        System::Filename relPath(pathData + dataDirLen);

        const char* str1 = relPath._data;
        // NOT RETAIL (block PATHINDEX): the hash index answers when it can.
        RtIO::PackfileIndexNode* indexed = nullptr;
        const bool answered = ::IO::PackIndexFind(&_packfileIndex, str1, &indexed);
        RtIO::PackfileIndexNode* node = _packfileIndex.root;
        RtIO::PackfileIndexNode* found = nullptr;
        if (answered && !OPENBA_PATHINDEX_CHECK) {
            found = indexed;
            node = nullptr;
        }
        while (node) {
            const char* key = node->element.key._data;
            bool condA;
            if (!key) condA = true;
            else if (!str1) condA = false;
            else condA = RtIO::StriCmpAscii(str1, key) >= 0;

            if (condA) {
                bool condB;
                if (!str1) condB = true;
                else if (!key) condB = false;
                else condB = RtIO::StriCmpAscii(key, str1) >= 0;

                if (condB) { found = node; break; }
                node = node->right;
            } else {
                node = node->left;
            }
        }

        ::IO::PackIndexCheck(answered, str1, indexed, found);
        if (found)
            ours = _packfiles[found->element.value.packIndex]->_crc;          // +0xA8, the packfile id

        // (relPath's release is ~Filename now — retail's own 0x1800ab0f2.)
    }

    return ours;
}


// ?Find@Modification@IO@@QEBAHAEBVFilename@System@@@Z       retail 0x1800aea10
//
//     int Modification::Find(const Filename& path) const
//     { for each Mod, case-insensitively compare its file to path; return
//       the index of the first match or -1. }
//
// Length-prefixed compare identical in shape to GetPackfileId's: compare
// stored lengths first, then StriCmp only if they match. NO ADDED NULL GUARD
// on `_mods` — this file's own banner records that Exists, GetPackfileId and
// Find all deref fields unguarded because retail does.
int Modification::Find(const System::Filename& path) const {
    int ours = -1;
    const char* pathData = path._data;
    const int pathLen = pathData ? RtIO::StringLength(pathData) : 0;

    for (int i = 0; i < _mods._size; ++i) {
        const char* modData = _mods._data[i].file._data;
        const int modLen = modData ? RtIO::StringLength(modData) : 0;
        if (modLen == pathLen && RtIO::StriCmpAscii(modData, pathData) == 0) {
            ours = i;
            break;
        }
    }

    return ours;
}

// ?GetSaveDirectory@FileSystem@IO@@QEBA?AVFilename@System@@XZ   retail 0x1800acfd0
//
//     Filename FileSystem::GetSaveDirectory() const
//     {
//         wchar_t buf[264];
//         if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, buf) != S_OK) {
//             Error::SetText("FileSystem: Unable to retrive the Documents "
//                             "folder for saving!", true);
//             return Filename();               // null
//         }
//         WideString ws;  ws.Set(buf);
//         String utf8 = ws.AsUTF8();
//         String formatted = String::Format("%s\\%s\\", utf8.data, "Banished");
//         Filename ret;  ret.data = formatted.data;  if (ret.data) ++refcount;
//         ret.AdjustDelimiters();
//         return ret;
//     }
//
// Phase D.4 SETTINGS E.2: the body moved here from the deleted
// src/platform/win/filesystem_win.cpp. Its OS half (SHGetFolderPathW, the
// WideString and AsUTF8) is Platform::DocumentsDir; the Format, the Filename
// and the Error::SetText arm below are retail's, unchanged.
//
// Retail's `this` (RCX) is loaded into RBX and never dereferenced — confirmed
// in the disassembly, not just the decompile — so the member touches no field;
// that is a fact about retail, not a gap. Every call allocates a FRESH buffer
// (unlike GetDataDirectory / GetSourceDirectory, which AddRef a shared one).
System::Filename FileSystem::GetSaveDirectory() const {
    // NOT RETAIL: BANISHED_USER_DIR (harness, tools/game_dirs.sh) replaces the
    // "<Documents>\Banished" folder as a whole, so the override takes the place of
    // the Format below -- the folder plus retail's trailing separator. Unset, the
    // retail arm runs unchanged.
    if (const char* user = Platform::UserDirOverride()) {
        System::String formatted = System::String::Format("%s\\", user);
        return System::Filename(formatted);
    }

    System::String docs;
    if (!Platform::DocumentsDir(docs)) {
        System::Error::SetText(
            "FileSystem: Unable to retrive the Documents folder for saving!", true);
        // Retail's `*(longlong*)in_RDX = 0` is the default ctor (0x180006220),
        // not a store through a live object.
        return System::Filename();
    }

    System::String formatted = System::String::Format(
        "%s\\%s\\", docs._data ? docs._data : "", "Banished");

    // AddRef-then-AdjustDelimiters IS `??0Filename@System@@QEAA@AEBVString@1@@Z`
    // (0x18000c780): retail stores the formatted buffer's own pointer into the
    // return slot, bumps its refcount and normalises
    // (0x1800acfd0: `*(longlong*)in_RDX = lVar2; *(int*)(lVar2-4) += 1;`).
    System::Filename result(formatted);

    // The releases are the locals' destructors in reverse declaration order:
    // `formatted` ([RSP+0x288], 0x1800ad0a3), then the UTF-8 docs string
    // ([RSP+0x38], 0x1800ad0dc). Retail's third, the WideString ([RSP+0x280],
    // 0x1800ad109), now happens inside Platform::DocumentsDir.
    return result;
}

// ?_singleton@FileSystem@IO@@0PEAV12@EA (0x1801fc6a8) -- the class static
// (ENDGAME B.3.9 IO, D7).
FileSystem* FileSystem::_singleton = nullptr;

}  // namespace IO

