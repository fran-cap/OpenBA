// ============================================================================
// src/engine/io/filename_extension.cpp -- System::Filename::GetPathExtension's body
//   ?GetPathExtension@Filename@System@@QEBA?AV12@XZ            retail 0x1800c64f0
// ============================================================================
// Moved verbatim out of external_path.cpp (2026-10-17) so the host unit test can link
// it (tests/unit/filename_test.cpp) without ExpandRelativePath, the path stack, the
// file system and the resource-load lock that file stands on. The body is unchanged
// and still counts into that lane's `RtObject::g_pc` (external_path.h), which
// external_path.cpp defines; the member Filename::GetPathExtension (filename.cpp)
// forwards here as before.
// ============================================================================
#include <cstdint>
#include <new>          // placement new into the retail hidden return slot

#include "object/external_path.h"
#include "system/Filename.h"
#include "system/String.h"

namespace RtObject {

// ============================================================================
// ?GetPathExtension@Filename@System@@QEBA?AV12@XZ            retail 0x1800c64f0
// ============================================================================
//
//     Filename Filename::GetPathExtension() const
//
// Two backward scans over the buffer. The FIRST looks for ':' and, when it
// finds one past index 1, shortens the window to end just before it — that is
// what makes `"data/x.rsc:identifier"` yield `rsc` and not `identifier`. The
// SECOND looks for '.' inside the shortened window and the extension is
// everything after it.
//
// THREE EDGE CASES ARE RETAIL'S AND ARE KEPT:
//   * `CMP ECX,1 / JLE` — a ':' at index 0 or 1 does NOT shorten the window.
//   * the '.' scan runs over [0, end], so a '.' in a DIRECTORY name is found
//     when the filename itself has no extension.
//   * `LEA R8D,[RCX+1] / CMP R8D,R9D / JLE` — a trailing '.' (start > end)
//     returns the NULL Filename, not an empty one. Those two are different
//     values to every caller: `ExpandRelativePath` tests `data == null`.
// ============================================================================
System::Filename* RtSystem_Filename_GetPathExtension(const System::Filename* self,
                                                     System::Filename* ret) {
    ++g_pc.extCalls;

    // `ret` is the hidden return slot, i.e. RAW BYTES, and every arm below
    // therefore CONSTRUCTS into it rather than assigning.
    const System::Filename& s = *self;
    const char* const d = s._data;

    // 0x1800c6514: `MOV RDX,[RCX] ; TEST RDX,RDX ; JNZ`
    if (d == nullptr) {
        new (ret) System::Filename();
        ++g_pc.extNullNoData;
        return ret;
    }

    // 0x1800c652c: `MOV R9D,[RDX-8] ; DEC R9D`
    int32_t end = s.GetLength() - 1;

    // 0x1800c6536-0x1800c654b: scan back for ':' from `end`. `c` and the index
    // move together, so `c` is the index of the ':' or -1 if there is none.
    int32_t c = end;
    for (int32_t i = end; i >= 0; --i) {
        if (d[i] == ':') break;
        --c;
    }

    // 0x1800c654d: `CMP ECX,1 ; JLE ; LEA R9D,[RCX-1]`
    if (c > 1) {
        end = c - 1;
        ++g_pc.extColonTrim;
    }

    // 0x1800c6556-0x1800c656c: the same scan for '.' over [0, end].
    c = end;
    for (int32_t i = end; i >= 0; --i) {
        if (d[i] == '.') break;
        --c;
    }

    // 0x1800c656e: `TEST ECX,ECX ; JNS`
    if (c < 0) {
        new (ret) System::Filename();
        ++g_pc.extNoDot;
        return ret;
    }

    // 0x1800c657f: `LEA R8D,[RCX+1] ; CMP R8D,R9D ; JLE`
    const int32_t start = c + 1;
    if (start > end) {
        new (ret) System::Filename();
        ++g_pc.extEmptyAfterDot;
        return ret;
    }

    // 0x1800c6595: SubString(self, &tmp, start, end) — inclusive at both ends,
    // which is what src/engine/system/string.cpp:423 implements.
    System::String tmp = s.AsString().SubString(start, end);

    // 0x1800c65a3 null-init, 0x1800c65a6 adopt, 0x1800c65b4 normalise: that
    // three-step IS `Filename::Filename(const String&)`, 0x18000c780. `tmp`'s
    // 0x1800c65c5 release is its destructor.
    new (ret) System::Filename(tmp);
    ++g_pc.extOk;

    return ret;
}

}  // namespace RtObject
