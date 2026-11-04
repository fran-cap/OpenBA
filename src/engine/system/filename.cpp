// ============================================================================
// src/engine/system/filename.cpp — System::Filename   (Wave 2, lane A)
// ============================================================================
//
// Byte-faithful port of the decompilation at the retail address named above
// each function (Runtime-x64.dll, image base 0x180000000, read 2026-09-06).
//
// Filename is String plus one thing: every mutating entry point ends in
// AdjustDelimiters, which rewrites '/' to '\\' in place and then collapses
// "\..\" segments. src/engine/system/Filename.h shows the three pins for
// the shared layout; string.h/string.cpp own the refcount idioms this file
// reuses by pointer cast, exactly as retail does.
//
// ABI: same hidden-return-slot rule as String — see the header comment of
// string.cpp. GetPathFilename and GetPathIdentifer both return a Filename BY
// MEMORY, so their signature is (this in RCX, ret in RDX, args from R8).
//
// DIFFABILITY: the ctors, the dtor, the two operator= overloads and
// AdjustDelimiters all mutate a live object and a shared refcount, so running
// the original as an oracle would mutate twice and, at refcount 1, double
// free. They are undiffed. operator<, GetPathFilename and GetPathIdentifer are
// const on `this` and produce independent results, so they are diffed.
// ============================================================================

// ============================================================================
// ENDGAME B.3.1 step 1: the bodies below now live on `System::Filename`
// (src/engine/system/Filename.h), together with GetPathDirectory, whose body
// moved here out of setupdir_helpers.cpp. They MOVED — they were not rewritten.
// The `RtSystem_Filename_*` free functions are one-line shims at the end of the
// file and die in step 2 (B31_RULES.md).
// ============================================================================

#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "system/Filename.h"
#include "system/string_internal.h"

// (B.4 acceptance MOD: the `AsSys(RtSystem::Filename*)` bridge is gone with
// the POD.)

// ############################################################################
// System::Filename — the class (ENDGAME B.3.1 step 1)
// ############################################################################
// `Filename::GetPathExtension`'s body lives in external_path.cpp; see the
// comment on that member below.
namespace RtObject {
System::Filename* RtSystem_Filename_GetPathExtension(const System::Filename* self,
                                                     System::Filename* ret);
}

namespace System {

using RtSystem::StringLength;

// ============================================================
// Constructors / destructor
// ============================================================

// B.4 Z MIRROR2: each ctor builds the String BASE first, as retail's do (the
// inlined String ctor under its own EH state, then the body); the dtor's
// release is ~String on the base (0x180006980 = the inlined 0x180006c80).

// (B7 D4: Filename() 0x180006220, Filename(const String&) 0x18000c780, the
// move ctor and ~Filename() 0x180006980 are header-inline in Filename.h. The
// String(const String&) form adopts the buffer and AdjustDelimiters writes
// through it: a Filename built from a String mutates the String's characters
// too, retail's behaviour, which several call sites depend on.)

// ??0Filename@System@@QEAA@PEBD@Z  retail 0x180026cf0: String() (the null
// store at 0x180026d06), String::Set 0x1800c95d0, AdjustDelimiters.
Filename::Filename(const char* src) : String() {
    Set(src);
    AdjustDelimiters();
}

// ??0Filename@System@@QEAA@AEBV01@@Z  retail 0x1800067d0
// Not in this lane's assigned list, but GetPathIdentifer calls it, so it is
// ported (and registered — it is a Filename ctor, and leaving one of the four
// on the DLL is exactly the half-ported class PLAN.md §4.1.5 warns about).
// Retail zeroes, then inlines operator=(const Filename&) — which already ends
// in AdjustDelimiters — and then calls AdjustDelimiters a SECOND time. The
// second call is idempotent, but it is transcribed rather than folded away.
Filename::Filename(const Filename& other) : String() {   // 0x1800067ee/f0: the null store
    if (_data != other._data) {
        StringPrivateAccess::Release(AsString());
        _data = other._data;
        if (_data != nullptr)
            ++RtSystem::StringRefCount(_data);
    }
    AdjustDelimiters();
    AdjustDelimiters();
}



// ============================================================
// Assignment
// ============================================================

// ??4Filename@System@@QEAAAEAV01@AEBV01@@Z  retail 0x180006770
Filename& Filename::operator=(const Filename& rhs) {
    if (_data != rhs._data) {
        StringPrivateAccess::Release(AsString());
        _data = rhs._data;
        if (_data != nullptr)
            ++RtSystem::StringRefCount(_data);
    }
    AdjustDelimiters();
    return *this;
}

// ??4Filename@System@@QEAAAEAV01@AEBVString@1@@Z  retail 0x180026d40
// Byte-identical to the Filename& overload — the two are separate exports of
// the same code shape, not a forwarder.
Filename& Filename::operator=(const String& rhs) {
    if (_data != rhs._data) {
        StringPrivateAccess::Release(AsString());
        _data = rhs._data;
        if (_data != nullptr)
            ++RtSystem::StringRefCount(_data);
    }
    AdjustDelimiters();
    return *this;
}

// Not retail's: the pointer steal, self-move short-circuited as above.
Filename& Filename::operator=(Filename&& rhs) noexcept {
    if (this != &rhs) {
        StringPrivateAccess::Release(AsString());
        _data = rhs._data;
        rhs._data = nullptr;
    }
    return *this;
}

// ============================================================
// operator< — diffable
// ============================================================

// ??MFilename@System@@QEBA_NAEBV01@@Z  retail 0x180026cb0
// Case-INSENSITIVE, unlike String::operator< which is case-sensitive; that is
// the whole reason Filename has its own overload, and it is what makes
// Collection::Set<Filename> treat "Terrain\Grass.rsc" and "terrain\grass.rsc"
// as one key.
//
// Retail calls the CRT's _stricmp. That is locale-sensitive in MSVC; the DLL
// never calls setlocale, so it runs in the "C" locale where _stricmp is plain
// ASCII case folding. Spelled out here rather than calling _stricmp so that
// runtime/src stays free of a CRT whose locale behaviour differs between
// toolchains (PLAN.md §4.1: the portability seam). Only the SIGN is used.
static int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb)
            return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0)
            return 0;
    }
}

bool Filename::operator<(const Filename& other) const {
    bool ours;
    if (other._data == nullptr)
        ours = false;
    else if (_data == nullptr)
        ours = true;
    else
        ours = StriCmpAscii(_data, other._data) < 0;

    return ours;
}

// ============================================================
// AdjustDelimiters — NOT DIFFED (rewrites the shared buffer in place)
// ============================================================

// ?AdjustDelimiters@Filename@System@@AEAAXXZ  retail 0x1800c6970
//
// PRIVATE (AEAA), not QEAA — the `...@QEAAXXZ` spelling in circulation is not
// an export and resolves to null forever (the failure mode of WAVE1 defect 3).
// It is exported anyway, as everything in this DLL is, so it is registered.
//
// Two phases:
//   1. Every '/' becomes '\\', IN PLACE, through the shared buffer.
//   2. Repeatedly find a "\.." whose ".." is not the tail, and splice out the
//      preceding segment: head = [0 .. lastDelim-1], tail = [i+3 .. len-1].
//
// Two details that read as bugs and are not:
//   * `lastDelim` is initialised ONCE, at 0x1800c69b5, ABOVE the outer loop
//     label at 0x1800c69c0. Every re-scan after a splice therefore starts with
//     the PREVIOUS scan's delimiter index, not -1. Transcribed as written.
//   * Each splice both recurses into AdjustDelimiters and then continues the
//     outer loop. The recursion normally finishes the job and the outer
//     re-scan finds nothing, but the loop is what retail wrote.
// GetLength is re-read from the object on every OUTER iteration because the
// splice replaces the buffer.
//
// BLOCK SAVELOAD (perf:, exact). Retail re-reads the length once per character
// in phase 1 and twice per character in phase 2. Nothing inside either scan can
// change it (phase 1 writes characters in [0, len), never the header at
// data-8; phase 2's scan writes nothing), so it is read once per scan here.
// And phase 2 can only splice at a "\.." triple, whatever `lastDelim` holds, so
// a scan whose window [0, len-3] holds no such triple ends in retail's own
// `return` -- the case for every already-normalised path, i.e. nearly every
// call (~20 per resource reference on a save load). The transcribed scan runs
// only when a triple exists. tests/unit/filename_adjust_test.cpp compares this
// body with the pre-SAVELOAD one on every string of length <= 8 over
// { a / \ . : } and on the game's resource names.
//
// BLOCK PATHINDEX (perf:, exact): both scans find their byte with memchr. Phase
// 1 writes exactly the '/' it finds, as the byte loop did; phase 2's window test
// visits every '\' starting in [0, len-3] and tests the two bytes after it.
void Filename::AdjustDelimiters() {
    // Phase 1: '/' -> '\\'
    {
        const int len = AsString().GetLength();
        char* const d = _data;
        if (len > 0) {
            char* const end = d + len;
            for (char* p = static_cast<char*>(std::memchr(d, '/', static_cast<size_t>(len))); p != nullptr;
                 p = static_cast<char*>(std::memchr(p + 1, '/', static_cast<size_t>(end - (p + 1))))) {
                *p = '\\';
            }
        }
    }

    int lastDelim = -1;   // see the note above: NOT reset by the outer loop

    for (;;) {
        const int len = AsString().GetLength();
        if (len - 2 <= 0)
            return;

        const char* base = _data;   // cached once, as retail caches RBP

        // The fast exit: no "\.." starting in [0, len-3] means this scan cannot
        // splice, and retail's scan then returns.
        {
            bool triple = false;
            const char* const windowEnd = base + (len - 2);   // a triple starts before here
            const char* p = base;
            while ((p = static_cast<const char*>(std::memchr(p, '\\', static_cast<size_t>(windowEnd - p)))) !=
                   nullptr) {
                if (p[1] == '.' && p[2] == '.') {
                    triple = true;
                    break;
                }
                ++p;
            }
            if (!triple)
                return;
        }

        int i = 0;
        int64_t k = 0;
        bool collapse = false;

        for (;;) {
            bool isDelim = false;
            if (base[k] == '\\') {
                if (base[k + 1] == '.' && base[k + 2] == '.' && lastDelim >= 1) {
                    if (i + 3 < len - 1) {
                        collapse = true;
                        break;
                    }
                    // ".." runs to the end of the path: nothing to splice, so
                    // this backslash is treated as an ordinary delimiter.
                }
                isDelim = true;
            }
            if (isDelim)
                lastDelim = i;
            ++i;
            ++k;
            if (i >= len - 2)
                return;
        }
        if (!collapse)
            return;

        // NOT RETAIL (hardening, review F-078): retail 0x1800c6970 has no such
        // check. A splice from the current scan has lastDelim < i and shortens
        // the path. Only the STALE lastDelim of an earlier scan (see the note
        // above) can sit at or past i + 3, and then the splice is no shorter:
        // at i + 3 it rebuilds the same string and the outer loop repeats it
        // forever ("\..\\..\x08.", "\..\a\..\b"); past it, it copies a segment
        // twice, usually growing without end ("\..a\\..aa"), and with
        // lastDelim > len the head SubString reads past the buffer. Such a
        // splice is refused and the path is left as the scan found it. Splices
        // that shorten, i.e. every ".." of a real path, are unchanged. Over all
        // 2.39M strings of length <= 13 on { a \ . }, a model of this loop
        // matches retail on every input retail finishes, except 36 that hold a
        // doubled '\' and a "..." segment (retail duplicates a segment, then
        // collapses it); the 16,047 that hang in retail now return.
        // tests/unit/filename_adjust_hang_test.cpp.
        if (lastDelim >= i + 3)
            return;

        String tail    = AsString().SubString(i + 3, len - 1);
        String head    = AsString().SubString(0, lastDelim - 1);
        String spliced = head + tail;
        AsString() = spliced;
        AdjustDelimiters();
        // The three explicit releases retail wrote out are the three locals'
        // destructors, and they run in retail's order (spliced, head, tail)
        // because the declarations are in the reverse of it.
    }
}

// ============================================================
// Path decomposition — diffable
// ============================================================

// Both of these produce a fresh Filename with its own buffer and touch nothing
// shared.

// ?GetPathFilename@Filename@System@@QEBA?AV12@_N@Z  retail 0x1800c6740
// Returns the leaf, with the extension stripped when `stripExt` is true.
//
// The `stripExt` flag is misleadingly named by its own logic: when NO '.' is
// found at index >= 1, a true flag returns the EMPTY filename rather than the
// whole leaf. So `GetPathFilename(true)` on an extension-less path yields
// nothing at all.
Filename Filename::GetPathFilename(bool stripExt) const {
    Filename ret;

    const char* d = _data;
    if (d != nullptr) {
        int end = StringLength(d) - 1;

        // Scan back for the extension dot; i == -1 means none was found.
        int i = end;
        int64_t k = end;
        if (end >= 0) {
            do {
                if (d[k] == '.')
                    break;
                --i;
                --k;
            } while (k >= 0);
        }
        bool done = false;
        if (i < 1) {
            if (stripExt)
                done = true;         // the NULL Filename; see the comment above
        } else {
            end = i - 1;
        }

        if (!done) {
            // Scan back for the last path delimiter; either separator counts,
            // even though phase 1 of AdjustDelimiters should have removed '/'.
            i = end;
            k = end;
            if (end >= 0) {
                do {
                    if (d[k] == '/' || d[k] == '\\')
                        break;
                    --i;
                    --k;
                } while (k >= 0);
            }
            int start = 0;
            if (i >= 0)
                start = i + 1;

            if (end >= start) {
                String tmp = AsString().SubString(start, end);
                StringPrivateAccess::Adopt(ret.AsString(), tmp._data);
                ret.AdjustDelimiters();
            }
        }
    }

    return ret;
}

// ?GetPathIdentifer@Filename@System@@QEBA?AV12@XZ  retail 0x1800c6610
// (the misspelling is retail's own). NOTE the return type: V12 = FILENAME.
// The `...QEBA?AVString@2@XZ` spelling in circulation is not an export.
//
// Returns everything after the last ':' — i.e. the path with its drive or
// package identifier removed. If there is no ':' at all the whole path comes
// back UNLESS a '.', '/' or '\\' was seen on the way, in which case the result
// is empty: an identifier is a bare name, and anything that looks like a path
// or a file with an extension has no identifier.
Filename Filename::GetPathIdentifer() const {
    Filename ret;

    const char* d = _data;
    if (d != nullptr) {
        bool sawPathChar = false;
        int i = StringLength(d) - 1;
        int64_t k = i;
        if (i >= 0) {
            do {
                const char c = d[k];
                if (c == ':')
                    break;
                bool isPathChar;
                if (static_cast<unsigned char>(c - 0x2e) < 2)   // '.' (0x2e) or '/' (0x2f)
                    isPathChar = true;
                else
                    isPathChar = (c == '\\');
                sawPathChar = sawPathChar || isPathChar;
                --i;
                --k;
            } while (k >= 0);
        }

        if (i < 0) {
            if (!sawPathChar)
                ret = Filename(*this);   // retail's copy ctor, twin AdjustDelimiters and all
        } else {
            const int len = d ? StringLength(d) : 0;
            String tmp = AsString().SubString(i + 1, len - 1);
            StringPrivateAccess::Adopt(ret.AsString(), tmp._data);
            ret.AdjustDelimiters();
        }
    }

    return ret;
}

// ============================================================================
// ?GetPathDirectory@Filename@System@@QEBA?AV12@XZ          retail 0x1800c6880
// ============================================================================
// MOVED HERE from src/engine/system/setupdir_helpers.cpp (ENDGAME B.3.1 step 1
// — that file kept it out of filename.cpp only because Wave 2 lane A owned this
// file and two lanes must not edit one source; that reason is gone). The
// `RtSystem_Filename_GetPathDirectory` name and its registry rows stay there,
// now as a shim.
//
// "Everything up to and INCLUDING the last delimiter", or the null Filename if
// there is no delimiter (and for a null or empty `this`).
//
// The trailing delimiter is kept, and that is load-bearing: SetupDirectories
// immediately does `root + "cmd.txt"` and `Format("%s%s%s\\", root, ...)` with
// no separator of its own, so a GetPathDirectory that stripped the '\' would
// silently produce "C:\GOG Games\Banishedcmd.txt". It is kept because SubString
// takes an INCLUSIVE END, not a count, and retail passes `end = <index of the
// delimiter>`.
//
// The scan itself is a REVERSE walk from `length - 1` testing 0x2F and 0x5C,
// read from the disassembly (0x1800c68d0-0x1800c68e4); the decompiled C for
// this loop prints the MOVSXD of the index as a zero extension
// (reference_ghidra_c_output_vs_disassembly.md), which would have made an
// empty string walk off the front instead of taking the `JS` at 0x1800c68c6.
Filename Filename::GetPathDirectory() const {
    Filename ret;

    const char* d = _data;
    if (d != nullptr) {
        int i = StringLength(d) - 1;          // `MOV R9D,[RDX-8] ; DEC R9D`
        while (i >= 0) {                      // `MOVSXD RAX,R9D ; JS`
            const unsigned char c = static_cast<unsigned char>(d[i]);
            if (c == '/' || c == '\\') break;
            --i;
        }
        if (i >= 0) {
            String tmp = AsString().SubString(0, i);
            StringPrivateAccess::Adopt(ret.AsString(), tmp._data);
            ret.AdjustDelimiters();
        }
    }

    return ret;
}

// ?GetPathExtension@Filename@System@@QEBA?AV12@XZ          retail 0x1800c64f0
//
// THE BODY IS STILL IN src/engine/io/external_path.cpp and this is a
// forward to it, not a second transcription (a duplicate would be a silently
// dead body — reference_grep_for_the_port_before_writing_it.md). That file
// wraps the body in the `g_pc` path-counter instrumentation its own lane
// reports, and it is not this batch's file; the body comes here when
// `Object::ExternalFactory` is converted.
Filename Filename::GetPathExtension() const {
    // `out` is born null and the C body CONSTRUCTS into it (its hidden return
    // slot), so nothing is leaked and no refcount is touched.
    Filename out;
    RtObject::RtSystem_Filename_GetPathExtension(this, &out);
    return out;
}

}  // namespace System

// ############################################################################
// ENDGAME B.3.1 step 1 — THE SHIMS. THEY DIE IN STEP 2.
// ############################################################################
// Same contract as string.cpp's shim block: the pre-conversion signature is
// kept exactly (hidden return slot included), the registry rows below register
// THESE, and a shim that produces a value constructs into the caller's raw
// `ret` slot with placement new rather than assigning to it.
// ############################################################################
namespace RtSystem {

// (B.4 Z LAST IO: `RtSystem_Filename_CtorCopy` and
// `RtSystem_Filename_AdjustDelimiters`, the POD spellings of the copy ctor
// 0x1800067d0 and of AdjustDelimiters 0x1800c6970, are gone with their last
// callers: the package entry's name is a real Filename.)

}  // namespace RtSystem
