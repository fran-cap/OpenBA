// ============================================================================
// src/engine/io/external_path.cpp — Wave 9 lane RRES
//   `Object::ExternalFactory::ExpandRelativePath` and the three readers it
//   needs, byte-faithful from the disassembly.
// ============================================================================
//
//   ?GetPathExtension@Filename@System@@QEBA?AV12@XZ                0x1800c64f0
//   ?GetPathStackFilename@ExternalFactory@Object@@QEBA?AV...XZ     0x1800bcf60
//   ?GetPathStackDirectory@ExternalFactory@Object@@QEBA?AV...XZ    0x1800bcfd0
//   ?ExpandRelativePath@ExternalFactory@Object@@QEBA?AV...AEBV34@_N@Z
//                                                                  0x1800bd360
//
// Every body below was transcribed from `disassemble_function(address=)` on
// Runtime-x64.dll (image base 0x180000000, read 2026-09-08), NOT from the
// decompiled C. Ghidra's C for 0x1800bd360 is actively misleading here: it
// mistakes the hidden return slot for `param_1`, so the `const Filename&`
// argument arrives as `param_2` reconstructed out of `in_register_00000081`,
// the `bool` shows up as a phantom `in_R9B`, and the destination slot of every
// one of the four `GetPath*` calls at the top of the function is DROPPED
// entirely.
//
// `ExpandRelativePath` calls `GetFileAttributesW`, exactly as
// `src/engine/io/filesystem_setupdirs.cpp` does for the same inlined
// `FileSystem::Exists`; since Phase D.4 THREADS E.6 the call is
// Platform::PathAttributes (platform_file.h) and this file left
// src/platform/win (it was `external_path_win.cpp`).
//
// Layout evidence for the path stack: the `PathStackEntry` block in
// src/engine/object/External.h. Wave write-up, including HONEST LIMITS and
// what this lane DECLINED: docs/history/runtime_port/WAVE9_LANE_RRES.md.
//
// ---------------------------------------------------------------------------
// THE RESOURCE-LOAD LOCK
// ---------------------------------------------------------------------------
// `ExpandRelativePath` takes `ResLoadGuard` (src/app/res_load_lock.h). It is
// on the racy path lane FNHOOK measured — `FindOrLoad` (external_compiled.cpp)
// and ext.find_fastpath call it, it does refcounted `String`/`Filename` traffic
// while building six concatenations, and that traffic on two threads at once is
// one of the two faces of the crash FNHOOK's lock closes.
//
// **THIS IS NOT THE GUARD THE BRIEF ASKED FOR, AND IT DOES NOT REPLACE IT.**
// The constraint was that the ported `FindOrLoad` body must take the lock;
// `FindOrLoad` (src/engine/object/external_compiled.cpp) takes no ResLoadGuard
// of its own. The lock is recursive, so a `FindOrLoad` that takes it later
// nests over this one for free.
// ---------------------------------------------------------------------------

#include "platform_file.h"   // Platform::PathAttributes

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>          // placement new into the retail hidden return slots
#include <string>

#include "object/external_path.h"
#include "object/expand_memo.h"      // block SAVELOAD: the memo in front of the body

#include "io/FileSystem.h"
#include "system/String.h"
#include "system/string_internal.h"
#include "system/setupdir_helpers.h"
#include "system/widestring_internal.h"
#include "res_load_lock.h"


// ENDGAME B.3.1 step 2: `AdjustDelimiters` (0x1800c6970), `GetPathFilename`
// (0x1800c6740), `GetPathIdentifer` (0x1800c6610) and the copy ctor
// (0x1800067d0) used to be forward-declared here as C entry points. They are
// `System::Filename` members now, so the block is gone — with one fact from it
// that is still worth having:
//
// `GetPathFilename`'s parameter is NOT "strip the extension". The backward scan
// for '.' strips it unconditionally; the flag is read in exactly one place, and
// a TRUE flag makes a path with NO '.' at index >= 1 yield the EMPTY filename
// rather than the whole leaf. So `GetPathFilename(true)` on an extension-less
// path returns nothing at all, which is why the `filenameFromStack` fallback
// below exists. Retail's own parameter reads as `stripExt`; that name is what
// sent Wave 11 lane OBJEXT to re-derive this from the disassembly.

namespace RtObject {


PathCounters g_pc = {};

namespace {

// ENDGAME B.3.1 step 2 removed this file's `AsStr`, `BufLength` and
// `ReleaseThenAdopt` helpers.
//
// `ReleaseThenAdopt` was retail's inlined `Filename x = y` — and it is
// `String::operator=` (0x1800c9810) exactly, `!=` self-assign guard included,
// which is why the sites that adopt WITHOUT a following AdjustDelimiters now
// say `x.AsString() = y` and the ones that adopt WITH one say `x = y` (the
// Filename overload, 0x180026d40, which adds the normalise). Which of the two
// each site is was read off the listing, not assumed; the addresses are on the
// lines. `BufLength` was `String::GetLength` (0x1800063c0), whose null arm
// returns 0 and is therefore the same two branches retail tests.

// `?_singleton@FileSystem@IO@@0PEAV12@EA`, retail 0x1801fc6a8 — the qword
// `MOV R14,qword ptr [0x1801fc6a8]` at 0x1800bd5e3 loads. The class static now
// (ENDGAME B.3.9 SS1, B39_RULES D7): read fresh, exactly as
// src/engine/io/filesystem_packages.cpp does.
inline const ::IO::FileSystem* FileSystemSingleton() {
    return ::IO::FileSystem::_singleton;
}


const char* kUnderscore = "_";
const char* kDot        = ".";
const char* kColon      = ":";
const char* kCrs        = ".crs";
const char* kResource   = "resource";

}  // namespace

// ============================================================================
// ?GetPathExtension@Filename@System@@QEBA?AV12@XZ            retail 0x1800c64f0
// ============================================================================
// Lives in src/engine/io/filename_extension.cpp (its own translation unit, so the
// host unit test can link it; moved verbatim 2026-10-17). It still counts into
// `g_pc` above.

// ============================================================================
// The two path-stack readers                     retail 0x1800bcf60/0x1800bcfd0
// ============================================================================
//
// Byte-identical bodies apart from the `ADD RDX,0x8` that selects the second
// half of the entry. Both return the TOP of the stack (`size - 1`) and both
// return a NULL Filename — not an empty one — when the stack is empty, which
// is the case `ExpandRelativePath` relies on to decide whether to substitute
// the stack's filename for a missing one.
//
// NO DIFF ORACLE, and the reason is specific rather than general: retail's body
// ends in the `Filename` COPY CONSTRUCTOR (0x1800067d0) on a live entry, so a
// replay bumps that entry's refcount twice and releases it twice. The
// arithmetic is balanced, but running a second copy of a refcount edit on live
// shared state to check a two-line accessor is the wrong trade — the evidence
// here is transcription plus `stackEmpty`/`stackNonEmpty`, which prove the body
// ran and which branch it took.
// ============================================================================
System::Filename* RtObject_ExternalFactory_GetPathStackFilename(const ExternalFactory* self,
                                                                System::Filename* ret) {
    // 0x1800bcf7c: `CMP dword ptr [RCX + 0x38],EAX` with EAX = 0
    if (self->_pathStack._size == 0) {
        new (ret) System::Filename();           // 0x1800bcf81
        ++g_pc.stackEmpty;
        return ret;
    }
    ++g_pc.stackNonEmpty;
    const PathStackEntry& top = self->_pathStack._data[self->_pathStack._size - 1];
    // Retail's copy ctor (0x1800067d0) into the hidden return slot.
    new (ret) System::Filename(top.filename);
    return ret;
}

System::Filename* RtObject_ExternalFactory_GetPathStackDirectory(const ExternalFactory* self,
                                                                 System::Filename* ret) {
    if (self->_pathStack._size == 0) {
        new (ret) System::Filename();
        ++g_pc.stackEmpty;
        return ret;
    }
    ++g_pc.stackNonEmpty;
    const PathStackEntry& top = self->_pathStack._data[self->_pathStack._size - 1];
    new (ret) System::Filename(top.directory);
    return ret;
}

// ============================================================================
// ?ExpandRelativePath@ExternalFactory@Object@@                retail 0x1800bd360
// ============================================================================
//
//     Filename ExternalFactory::ExpandRelativePath(const Filename& path,
//                                                  bool appendIdentifier) const
//
// WHAT IT COMPUTES, in one paragraph, because the 834 instructions are ~85%
// refcount boilerplate around six string concatenations:
//
//   Split `path` into extension / identifier / filename / directory. An empty
//   identifier defaults to "resource"; an empty filename is taken from the TOP
//   OF THE PATH STACK (this is what makes a bare ":identifier" resolve against
//   the file currently being parsed). Build the absolute candidate
//   `<dataDir><stackDir><dir><file>_<ident>.crs` and ask the filesystem whether
//   it exists. If it does, the answer is the STACK-QUALIFIED relative path
//   `<stackDir><dir><file>.<ext>`; if it does not, the answer is the bare
//   `<dir><file>.<ext>`. Append `:<ident>` when asked. Normalise, return.
//
// TWO THINGS THAT LOOK LIKE TRANSCRIPTION ERRORS AND ARE NOT
//
//  1. THE SECOND CANDIDATE IS THE SAME STRING AS THE FIRST. The `else` arm at
//     0x1800bd825 rebuilds `<dataDir><stackDir><dir><file>_<ident>.crs` operand
//     for operand — same six sources in the same order, same two literals — and
//     probes it again. It is not a different path. What differs is the PROBE:
//     the first `ExistsInPackfile` at 0x1800bd7c1 is behind
//     `fs->existsInPackfileGate` (it is MSVC's inlining of `FileSystem::Exists`,
//     gate and all), and the second at 0x1800bd8fb is UNGATED. So the retry is
//     reachable, and on a build with the gate clear it is the only packfile
//     probe that runs. Kept literally, allocations and all.
//
//  2. `this` IS DEAD AFTER 0x1800bd5f1. Retail reuses R15 for the
//     GetFileAttributesW result. `GetPathStackDirectory` is the last read of
//     the factory, which is why nothing below touches `self`.
//
// ============================================================================
// BLOCK SAVELOAD: the transcribed body is `ExpandRelativePathBody`, unchanged.
// The exported entry point below takes the lock, asks the memo, and runs the
// body on a miss.
namespace {

void ExpandRelativePathBody(const ExternalFactory* self, System::Filename* ret,
                            const System::Filename* path, bool appendIdentifier) {
    ++g_pc.expandCalls;

    // `ret` is the hidden return slot, so every write to it below CONSTRUCTS
    // rather than assigns.
    const System::Filename& in = *path;

    // ---- 0x1800bd3a3: `if (path->data == null || length == 0) return null` --
    // `GetLength` (0x1800063c0) returns 0 for a null buffer, so it is retail's
    // own pair of tests and not a widened one.
    if (in.GetLength() == 0) {
        new (ret) System::Filename();                 // 0x1800bde7b
        ++g_pc.expandEmptyPath;
        return;
    }

    // ---- 0x1800bd3b9-0x1800bd3ea: the four splits ---------------------------
    // Retail hands these four uninitialised stack slots; every callee writes
    // `*ret` before it reads it. As class values each is simply the return of
    // the member that produced it, and the four destructors at the tail are
    // retail's four releases at 0x1800bde1c..0x1800bde76 in the same order.
    System::Filename ext   = in.GetPathExtension();   // [RBP-0x29]
    System::Filename ident = in.GetPathIdentifer();   // [RBP-0x41]
    System::Filename fname = in.GetPathFilename(true);// [RBP+0x77]
    System::Filename dir   = in.GetPathDirectory();   // [RBP+0x07]

    // ---- 0x1800bd3f0-0x1800bd497: an empty identifier defaults to "resource"
    if (ident.GetLength() == 0) {
        ++g_pc.identDefaulted;
        // Set + AdjustDelimiters (0x1800bd418) on the same object is the
        // `const char*` ctor, 0x180026cf0.
        System::Filename tmp(kResource);                         // [RBP-0x61]
        // Release-then-adopt (0x1800bd422) followed by AdjustDelimiters
        // (0x1800bd468) is `Filename::operator=`, 0x180006770 — ONE normalise.
        ident = tmp;
        // tmp's 0x1800bd46e release is its destructor.
    } else {
        ++g_pc.identPresent;
    }

    // ---- 0x1800bd497-0x1800bd5e3: an empty filename comes off the stack -----
    // BOTH halves are replaced: the filename AND the extension. A resource
    // reference that is only an identifier therefore inherits the extension of
    // the file being parsed, not the one it was written next to.
    if (fname.GetLength() == 0) {
        ++g_pc.filenameFromStack;
        // The reader's hidden return slot again: constructed into, not assigned.
        System::Filename stackFile;                              // [RBP-0x59]
        RtObject_ExternalFactory_GetPathStackFilename(
            self, &stackFile);

        System::Filename tmp = stackFile.GetPathFilename(true);  // [RBP-0x61]
        // 0x1800bd4ca + 0x1800bd507 is `Filename::operator=`, one normalise.
        fname = tmp;

        // 0x1800bd511 releases `tmp` and 0x1800bd53a then hands GetPathExtension
        // THE SAME SLOT. Move-assignment is that pair: it releases the old
        // buffer and takes the fresh one whole, with no refcount edit and no
        // extra normalise (the callee already normalised).
        tmp = stackFile.GetPathExtension();
        ext = tmp;                                               // 0x1800bd54a
                                                                 // + 0x1800bd587
        // tmp (0x1800bd591) then stackFile (0x1800bd5ba) release at the end of
        // this block, in retail's order.
    } else {
        ++g_pc.filenamePresent;
    }

    // ---- 0x1800bd5e3: R14 = IO::FileSystem::_singleton ---------------------
    const ::IO::FileSystem* const fs = FileSystemSingleton();

    System::Filename stackDir;                                   // [RBP+0x0f]
    RtObject_ExternalFactory_GetPathStackDirectory(
        self, &stackDir);

    // ---- 0x1800bd5f7-0x1800bd68d: candidate =
    //      <dataDir><stackDir><dir><fname>_<ident>.crs
    System::Filename candidate;                                  // [RBP-0x01]
    {
        System::Filename dataDir = fs->GetDataDirectory();       // [RBP-0x39]

        System::String t1 = dataDir.AsString() + stackDir.AsString();  // -0x19
        System::String t2 = t1 + dir.AsString();                       // -0x11
        System::String t3 = t2 + fname.AsString();                     // -0x09
        System::String t4 = t3 + kUnderscore;                          // -0x21
        System::String t5 = t4 + ident.AsString();                     // -0x61
        System::String t6 = t5 + kCrs;                                 // -0x59

        // 0x1800bd670 adopt + 0x1800bd683 normalise, onto an object that is
        // already null: `Filename::operator=(const String&)`, 0x180026d40.
        candidate = t6;

        // 0x1800bd68d..0x1800bd783 releases t6..t1 then dataDir. Declaration
        // order here is dataDir, t1..t6, so the destructors run in exactly that
        // sequence.
    }

    // ---- 0x1800bd7ac-0x1800bdab5: does it exist? ---------------------------
    bool found = false;
    bool decided = false;

    // MSVC's inlined FileSystem::Exists(candidate), gate and all.
    if (fs->_existsInPackfileGate != 0 &&
        fs->ExistsInPackfile(candidate)) {                       // 0x1800ab180
        found = true; decided = true;                            // 0x1800bd7c8
        ++g_pc.hitPackfile;
    }
    if (!decided && fs->_platformExistsGate != 0) {
        ++g_pc.attrProbeEntered;
        // ??0WideString(const String&) (0x1800cab50) over the Filename read AS
        // a String; the block ends where retail's 0x1800bd7f3 release stands,
        // so ~WideString (0x18000a730) runs there.
        bool ok;
        {
            const System::WideString ws(candidate.AsString());   // [RBP-0x39]
            // `CMP EAX,-1 ; SETNZ` — INVALID_FILE_ATTRIBUTES, not a HRESULT.
            ok = (Platform::PathAttributes(ws._data) != Platform::kInvalidPathAttributes);  // GetFileAttributesW
        }
        if (ok) {
            found = true; decided = true;                        // 0x1800bd81f
            ++g_pc.hitAttributes;
        }
    }
    if (!decided) {
        // 0x1800bd825: the SAME string, built again, probed UNGATED. See note 1.
        System::Filename dataDir2 = fs->GetDataDirectory();     // [RBP-0x49]

        System::String u1 = dataDir2.AsString() + stackDir.AsString();  // -0x51
        System::String u2 = u1 + dir.AsString();                        // -0x31
        System::String u3 = u2 + fname.AsString();                      // -0x21
        System::String u4 = u3 + kUnderscore;                           // -0x09
        System::String u5 = u4 + ident.AsString();                      // -0x11
        System::String u6 = u5 + kCrs;                                  // -0x19

        // DECLARED LAST ON PURPOSE, even though retail's slot is [RBP-0x59] and
        // sits before the others: 0x1800bd90c releases candidate2 FIRST and
        // then u6..u1 and dataDir2, so declaring it here is what makes reverse
        // declaration order reproduce retail's unwind order exactly. (Retail
        // writes that unwind as an EDI bitmask of which locals had been
        // constructed when the probe ran; scoping is the same set, same order.)
        System::Filename candidate2;                             // [RBP-0x59]
        // 0x1800bd8cf adopt + 0x1800bd8e2 normalise, onto a null object.
        candidate2 = u6;

        found = fs->ExistsInPackfile(candidate2);                // 0x1800bd8fb
        if (found) ++g_pc.hitPackfileRetry; else ++g_pc.notFound;
    }

    // ---- 0x1800bdab5-0x1800bdc8c: the answer -------------------------------
    // THE THREE ADOPTS BELOW HAVE NO FOLLOWING AdjustDelimiters, so none of
    // them is `Filename::operator=` (0x180006770 / 0x180026d40) — those add a
    // normalise retail does not make here, and the one normalise this value
    // gets is the ctor at the very bottom. They are `String::operator=`
    // (0x1800c9810), which is release-then-adopt with retail's own `!=` guard
    // and nothing else, reached through `AsString()`.
    System::Filename result;    // RBX / [RBP-0x61]
    if (found) {
        // 0x1800bdabe: <stackDir><dir><fname>.<ext>
        System::String a = stackDir.AsString() + dir.AsString();
        System::String b = a + fname.AsString();
        System::String c = b + kDot;
        System::String d = c + ext.AsString();
        result.AsString() = d;                                   // 0x1800bdb06
    } else {
        // 0x1800bdbc8: <dir><fname>.<ext> — the stack directory is DROPPED.
        System::String a = dir.AsString() + fname.AsString();
        System::String b = a + kDot;
        System::String c = b + ext.AsString();
        result.AsString() = c;                                   // 0x1800bdbff
    }

    // ---- 0x1800bdc8c-0x1800bdd43: `if (appendIdentifier) result += ":" + ident`
    if (appendIdentifier) {
        ++g_pc.appendedIdentifier;
        System::String x = result.AsString() + kColon;            // -0x51
        System::String y = x + ident.AsString();                  // -0x49
        result.AsString() = y;                                    // 0x1800bdcbd
    } else {
        ++g_pc.noIdentifierTail;
    }

    // ---- 0x1800bdd43-0x1800bdd59: publish, normalise ------------------------
    // Null-init (0x1800bdd43), adopt (0x1800bdd4b), normalise (0x1800bdd51),
    // into the raw hidden return slot: `Filename::Filename(const String&)`,
    // 0x18000c780. This is the ONE normalise the returned value gets.
    new (ret) System::Filename(result.AsString());

    // ---- 0x1800bdd60-0x1800bde76: the tail releases, retail's order ---------
    // result, candidate, stackDir, dir, fname, ident, ext — which is reverse
    // declaration order, so the seven destructors run in exactly that sequence
    // and nothing is released by hand.
    return;
}

// BLOCK PATHINDEX (NOT RETAIL, exact): the body's answer for a non-empty request
// while the path stack is EMPTY, without the candidate and its three existence
// probes. With no stack, `stackDir` and the stack's filename are null Filenames,
// and the "found" arm's `<stackDir><dir><fname>.<ext>` is the "not found" arm's
// `<dir><fname>.<ext>` character for character (expand_memo.h, point 4), so the
// probes cannot change the answer: they are reads and a stat, and nothing else.
// What remains is the body's own sequence for everything the answer reads -- the
// four splits, the two defaults, the "not found" concatenation, the identifier
// tail, the one normalise -- so the characters are the body's.
void ExpandEmptyStack(System::Filename* ret, const System::Filename* path, bool appendIdentifier) {
    const System::Filename& in = *path;

    System::Filename ext   = in.GetPathExtension();
    System::Filename ident = in.GetPathIdentifer();
    System::Filename fname = in.GetPathFilename(true);
    System::Filename dir   = in.GetPathDirectory();

    if (ident.GetLength() == 0) {
        System::Filename tmp(kResource);
        ident = tmp;
    }

    if (fname.GetLength() == 0) {
        // What GetPathStackFilename returns for an empty stack: a null Filename.
        const System::Filename stackFile;
        System::Filename tmp = stackFile.GetPathFilename(true);
        fname = tmp;
        tmp = stackFile.GetPathExtension();
        ext = tmp;
    }

    System::Filename result;
    {
        System::String a = dir.AsString() + fname.AsString();
        System::String b = a + kDot;
        System::String c = b + ext.AsString();
        result.AsString() = c;
    }

    if (appendIdentifier) {
        System::String x = result.AsString() + kColon;
        System::String y = x + ident.AsString();
        result.AsString() = y;
    }

    new (ret) System::Filename(result.AsString());
}

}  // namespace

// ============================================================================
// The exported entry point: lock, memo, body (block SAVELOAD)
// ============================================================================
// The memo is consulted only for a non-empty request made while the path stack
// is EMPTY (expand_memo.h, point 3: the stack's top feeds the answer), and under
// an empty stack every answer is a function of the request alone (point 4), so
// every answer is kept. A hit returns a fresh Filename holding the cached
// characters: `Filename(const char*)` is Set + AdjustDelimiters, and the
// characters are the body's already-normalised output, on which AdjustDelimiters
// changes nothing.
//
// THE DEBUG EXE CHECKS EVERY HIT. Under _DEBUG the body runs anyway on a hit and
// the two answers are compared; a difference prints one `[ExpandMemo] MISMATCH`
// line on stderr and the body's answer is returned. The coordinator's debug
// gate runs (new game, resave, tutorial) therefore prove the memo on the game's
// whole request stream: zero such lines = every hit equalled the body. The perf
// and release exes trust it.
System::Filename* RtObject_ExternalFactory_ExpandRelativePath(const ExternalFactory* self,
                                                              System::Filename* ret,
                                                              const System::Filename* path,
                                                              bool appendIdentifier) {
    ResLoadGuard resGuard;   // see THE RESOURCE-LOAD LOCK at the top

    const int len = path->GetLength();
    const bool memoable = (len != 0 && self->_pathStack._size == 0);

    if (memoable) {
        const char* const hit = ExpandMemoFind(path->_data, len, appendIdentifier);
        if (hit != nullptr) {
#ifdef _DEBUG
            System::Filename check;   // null; the body constructs into it, as into `ret`
            ExpandRelativePathBody(self, &check, path, appendIdentifier);
            if (check._data == nullptr || std::strcmp(check._data, hit) != 0) {
                fprintf(stderr, "[ExpandMemo] MISMATCH path=\"%s\" memo=\"%s\" body=\"%s\"\n",
                        path->_data, hit, check._data ? check._data : "(null)");
                fflush(stderr);
                new (ret) System::Filename(static_cast<System::Filename&&>(check));
                return ret;
            }
#endif
            new (ret) System::Filename(hit);
            return ret;
        }
    }

    if (!memoable) {
        ExpandRelativePathBody(self, ret, path, appendIdentifier);
        return ret;
    }

    // Block PATHINDEX: a first request under an empty stack builds the answer
    // without the existence probes (expand_memo.h, point 4). The debug exe runs
    // the body too and compares, as for a memo hit.
    ExpandEmptyStack(ret, path, appendIdentifier);
#ifdef _DEBUG
    {
        System::Filename check;
        ExpandRelativePathBody(self, &check, path, appendIdentifier);
        if (check._data == nullptr || ret->_data == nullptr || std::strcmp(check._data, ret->_data) != 0) {
            fprintf(stderr, "[ExpandMemo] MISMATCH path=\"%s\" emptyStack=\"%s\" body=\"%s\"\n", path->_data,
                    ret->_data ? ret->_data : "(null)", check._data ? check._data : "(null)");
            fflush(stderr);
            ret->~Filename();
            new (ret) System::Filename(static_cast<System::Filename&&>(check));
            return ret;
        }
    }
#endif

    if (ret->_data != nullptr)
        ExpandMemoStore(path->_data, len, appendIdentifier, ret->_data, ret->GetLength());
    return ret;
}

}  // namespace RtObject
