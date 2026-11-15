// ============================================================================
// src/engine/object/external_compiled.cpp — Wave 11 lane OBJEXT
//   `Object::ExternalFactory::GetCompiledPath` and `Object::ExternalFactory::
//   FindOrLoad`.
// ============================================================================
//
//   ?GetCompiledPath@ExternalFactory@Object@@SA?AVFilename@System@@AEBV34@@Z
//                                                                  0x1800bec10
//   ?FindOrLoad@ExternalFactory@Object@@QEAAPEBVExternal@2@AEBVFilename@System@@@Z
//                                                                  0x1800bf320
//
// These two are in one file because they are one algorithm twice: the six-term
// concatenation that turns a resource path into the path of its COMPILED form
// under the data directory appears verbatim in both, and MSVC emitted it twice
// rather than making `FindOrLoad` call `GetCompiledPath`. Instruction for
// instruction, 0x1800bf398-0x1800bf44e and 0x1800bec37-0x1800becf1 are the same
// sequence with the same two literals in the same order. Splitting them across
// two files would put one lane's fix one file away from the other's copy.
//
// THE CONCATENATION (both bodies)
//
//     result = dataDir + dir + fname + "_" + ident + ".crs"
//
// where dir/fname/ident are `GetPathDirectory()`, `GetPathFilename(strip=true)`
// and `GetPathIdentifer()` of the input, `dataDir` is
// `IO::FileSystem::_singleton->GetDataDirectory()`, and the two literals live at
// 0x180155b04 ("_") and 0x180155afc (".crs"). `AdjustDelimiters` runs once over
// the finished string.
//
// A DEAD CALL THAT IS FAITHFULLY KEPT. Both bodies open with
// `path.GetPathExtension()` into a stack slot that is then never read — only
// released at the end (0x1800bec41 writes [RBP-0x18], 0x1800bee6f releases it,
// nothing in between touches it). It is dead in the sense that its VALUE is
// unused; it is not dead in the sense that it allocates and frees a String. It
// is transcribed rather than elided, because "this call has no effect" is
// exactly the kind of judgement this port has been wrong about before, and
// because eliding it would make the two bodies' allocation traffic differ from
// retail's for no gain.
//
// WHAT THIS PORT CLOSES
//
// `src/engine/engine/mainloop_video.cpp`'s banner records that porting
// `OnVideoRestart` deliberately OPENED three unsatisfied names, two of which
// are `+Load@External` and `+GetCompiledPath@ExternalFactory`. This file closes
// the second; `External::Load` (external_load.cpp) is called by name below.
//
// EVIDENCE. Transcribed from `disassemble_function(address=)` on Runtime-x64.dll
// (image base 0x180000000, read 2026-09-09). Ghidra's C for both drops the
// right-hand operand of all five `System::operator+` calls and, for
// `FindOrLoad`, drops `ExpandRelativePath`'s third and fourth arguments
// entirely — the listing at 0x1800bf34f-0x1800bf359 shows `R8 = path` and
// `R9B = 1`, neither of which appears in the decompiled call
// (reference_ghidra_c_output_vs_disassembly.md).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>          // placement new into the retail hidden return slots

#include "external_path.h"
#include "external_compiled.h"
#include "external_path_stack.h"
#include "external_set.h"
#include "object/resource_ext.h"   // NOT RETAIL: docs/extensions/handins/VISITREPLAY.md

#include "system/String.h"
#include "io/FileSystem.h"   // ENDGAME B.3.9 SS1: FileSystem::_singleton
#include "system/string_internal.h"
#include "../system/setupdir_helpers.h"

namespace RtObject {


CompiledCounters g_cc = {};

// EXT SEAM (NOT RETAIL, docs/extensions/KITFREE.md §2): the resource providers' entry (external_compiled.h), null until
// the first registers.
::Object::External* (*g_extProvideSeam)(const char* expandedPath) = nullptr;

namespace {

// 0x180155b04 and 0x180155afc.
const char* const kUnderscore = "_";
const char* const kCrs        = ".crs";

// `?_singleton@FileSystem@IO@@0PEAV12@EA`, retail 0x1801fc6a8 — the qword the
// `MOV RCX,qword ptr [0x1801fc6a8]` at 0x1800bec75 loads. The class static now
// (ENDGAME B.3.9 SS1, B39_RULES D7); read fresh, no lazy cache.
inline const ::IO::FileSystem* FileSystemSingleton() {
    return ::IO::FileSystem::_singleton;
}


// ---------------------------------------------------------------------------
// THE SHARED CONCATENATION.
//
// Written once and called from both bodies BECAUSE THEY ARE THE SAME BYTES, and
// that claim is the comparison, not an assumption: the two instruction runs
// were diffed (0x1800bec37..0x1800becf1 against 0x1800bf398..0x1800bf44e) and
// differ only in which stack slots they name and in `FindOrLoad` writing its
// result to a slot rather than to the hidden return parameter. Where two
// retail bodies genuinely share bytes this port records the comparison rather
// than assuming it (external_set.h sets that precedent, and gives the reason:
// this family has produced four near-misses that differed only in an alignment
// immediate or a shift).
//
// `out` is raw on entry in retail (a stack slot) and is written before it is
// read, so callers pass an uninitialised `Filename`.
// ---------------------------------------------------------------------------
void BuildCompiledPath(const System::Filename* path, System::Filename* out) {
    ++g_cc.buildCalls;

    const System::Filename& in = *path;

    System::Filename ext   = in.GetPathExtension();              // [RBP-0x18]
    (void)ext;                                     // value UNUSED, see banner
    System::Filename ident = in.GetPathIdentifer();              // [RBP-0x50]
    System::Filename fname = in.GetPathFilename(true);           // [RBP+0x38]
    System::Filename dir   = in.GetPathDirectory();              // [RBP+0x30]

    // B.4 R-IO: the member, returning into the local (one AddRef, as retail).
    System::Filename dataDir =
        FileSystemSingleton()->GetDataDirectory();               // [RBP-0x28]

    System::String t1 = dataDir.AsString() + dir.AsString();
    System::String t2 = t1 + fname.AsString();
    System::String t3 = t2 + kUnderscore;
    System::String t4 = t3 + ident.AsString();
    System::String t5 = t4 + kCrs;

    // 0x1800becdd-0x1800becf1: null the slot, adopt t5's buffer (a refcount
    // bump, NOT a character copy), normalise. That is exactly
    // `Filename::Filename(const String&)`, 0x18000c780. `out` is raw on entry,
    // hence the placement.
    new (out) System::Filename(t5);

    // Retail's release order — t5..t1, dataDir, dir, fname, ident, ext — is
    // reverse construction order, which is precisely the order these ten
    // destructors run in. Nothing is released by hand any more.
}

}  // namespace

// ============================================================================
// ?GetCompiledPath@ExternalFactory@Object@@SA?AVFilename@System@@AEBV34@@Z
//                                                          retail 0x1800bec10
// ============================================================================
// `SA` — a STATIC. There is no `this`. It returns a `System::Filename` BY
// VALUE, so the hidden return slot is the FIRST argument and the `const
// Filename&` is the second: `RCX = ret, RDX = path`, confirmed by the prologue
// (`MOV RDI,RCX ; MOV RBX,RDX` at 0x1800bec2f/0x1800bec32, `MOV RAX,RDI` at
// 0x1800bee98). Getting this wrong is the shipped defect
// reference_hidden_retslot_wild_write.md describes: an fn_* typedef that omits
// the slot is a wild write that still returns the right answer.
System::Filename* RtObject_ExternalFactory_GetCompiledPath(System::Filename* ret,
                                                           const System::Filename* path) {
    ++g_cc.getCompiledCalls;

    BuildCompiledPath(path, ret);

    return ret;
}

// ============================================================================
// ?FindOrLoad@ExternalFactory@Object@@QEAAPEBVExternal@2@AEBVFilename@System@@@Z
//                                                          retail 0x1800bf320
// ============================================================================
// RCX = this, RDX = path, returns `const External*` in RAX (RBX in the body).
//
// Retail, in listing order:
//
//   0x1800bf339  if (!path->data) return null
//   0x1800bf345  if (path->data length == 0) return null        [data-8, NOT -4]
//   0x1800bf359  expanded = ExpandRelativePath(this, path, /*appendIdent=*/true)
//   0x1800bf366  found = Find(expanded)                          *** SEE BELOW
//   0x1800bf376  if (found) { if (this->_loadEnabled) Add(found); return found; }
//   0x1800bf398  compiled = <the six-term concatenation>
//   0x1800bf550  loaded = External::Load(compiled, nullptr)
//   0x1800bf562  if (loaded) { loaded->_path = expanded; AdjustDelimiters;
//                              Add(loaded); return loaded; }
//                return null
//
// *** THE `Find` IT CALLS IS THE PRIVATE ONE. 0x1800be0e0 is
// `?Find@ExternalFactory@Object@@AEAAPEAVExternal@2@AEBVFilename@System@@@Z` —
// note `AEAA`, non-const, returning a MUTABLE `External*`. It is NOT
// 0x1800bdeb0, the public `QEBA...PEBVExternal` that `external.cpp` already
// ports. Both are EXPORTED: `AEAA` means PRIVATE, not unexported, and this DLL
// exports its private members (reference_aeaa_is_private_not_unexported.md).
// Calling the ported public one here would be a different function.
//
// B3R RT-Core: `Find`(private, 0x1800be0e0), `Add` (0x1800be570) and
// `External::Load` (0x1800bc620) are ported and called by name.

// `useFast` (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): whether ext.find_fastpath may answer
// or remember this request. The visit's own walk passes false: it asks once per row per run (the
// VISITPASS shortcut answers every later visit), so remembering its requests only costs.
static const ::Object::External* FindOrLoadBody(ExternalFactory* self, const System::Filename* path, bool useFast) {
    ++g_cc.findOrLoadCalls;

    // 0x1800bf339 / 0x1800bf345 — `GetLength` (0x1800063c0) is retail's own
    // pair of tests: it returns 0 for a null buffer and otherwise reads the
    // dword at data-8, which is the field the `CMP [RAX-0x8],0x0` reads.
    if (path->GetLength() == 0) {
        ++g_cc.emptyPath;
        return nullptr;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.find_fastpath answers a
    // request that already named a row, under the empty path stack that makes the answer a function
    // of the request's characters. Null by default.
    const ::Object::ExtFindFast* const fast = ::Object::ExternalFactory::_extFindFast;
    const bool fastUsable = useFast && fast != nullptr && self->_pathStack._size == 0;
    if (fastUsable) {
        void* const row = fast->find(*self, path->_data, path->GetLength());
        if (row != nullptr)
            return RtObject_ExternalFactory_FindOrLoadHitRow(self, row);
    }

    // ExpandRelativePath writes a hidden return slot; the null local it is
    // handed is constructed into, not assigned to.
    System::Filename expanded;                                   // [RBP+0x38]
    RtObject_ExternalFactory_ExpandRelativePath(self, &expanded, path, true);

    ::Object::External* found = RtObject_ExternalFactory_Find(self, &expanded);    // private Find

    const ::Object::External* result = nullptr;

    if (found != nullptr) {
        // ---- 0x1800bf376: the CACHE HIT -----------------------------------
        ++g_cc.cacheHit;
        // EXT SEAM (NOT RETAIL, VISITREPLAY.md): the request named this row.
        if (fastUsable)
            fast->store(*self, path->_data, path->GetLength(), found);
        if (self->_loadEnabled) {
            ++g_cc.hitReAdded;
            RtObject_ExternalFactory_Add(self, found);
        } else {
            ++g_cc.hitNotReAdded;
        }
        result = found;
    } else {
        // ---- 0x1800bf390: the MISS ----------------------------------------
        ++g_cc.cacheMiss;
        // EXT SEAM (NOT RETAIL, VISITREPLAY.md): a miss (a load, a provider, or nothing).
        ::Object::NotifyResourceEvent(::Object::ResourceEvent::Miss, nullptr, nullptr, nullptr);
        System::Filename compiled;                               // [RBP+0x40]
        BuildCompiledPath(&expanded, &compiled);

        ::Object::External* loaded = RtObject_External_Load(&compiled, nullptr);

        // EXT SEAM (NOT RETAIL, docs/extensions/KITFREE.md §2): no loose
        // .crs and no package entry -- a registered provider may build the object
        // from the exe's own data. Null under the faithful profile (no provider
        // registers), so retail's miss path is unchanged there.
        if (loaded == nullptr && g_extProvideSeam != nullptr)
            loaded = g_extProvideSeam(expanded._data);

        if (loaded != nullptr) {
            ++g_cc.loaded;
            // 0x1800bf562-0x1800bf5a6: retail's release-then-adopt with its own
            // `!=` guard, then AdjustDelimiters over the object's OWN field, is
            // `Filename::operator=(const Filename&)` (0x180006770) instruction
            // for instruction.
            // EXT SEAM (NOT RETAIL, VISITREPLAY.md): a `_path` write.
            ::Object::NotifyResourceEvent(::Object::ResourceEvent::PathWrite, nullptr, loaded, expanded._data);
            loaded->_path = expanded;
            RtObject_ExternalFactory_Add(self, loaded);           // 0x1800bf5b1
            result = loaded;
        } else {
            ++g_cc.loadFailed;
        }
        // `compiled` releases here in its destructor, at the end of the else
        // branch, exactly where retail releases it — and `expanded` after it.
    }


    return result;
}

const ::Object::External* RtObject_ExternalFactory_FindOrLoad(ExternalFactory* self,
                                                    const System::Filename* path) {
    return FindOrLoadBody(self, path, true);
}

// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): the visit walk's FindOrLoad, the same body
// without ext.find_fastpath (external_visit.cpp).
const ::Object::External* RtObject_ExternalFactory_FindOrLoadVisit(ExternalFactory* self,
                                                         const System::Filename* path) {
    return FindOrLoadBody(self, path, false);
}

// ============================================================================
// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md): FindOrLoad's hit arm on a
// row ext.find_fastpath named (the row the private Find returns for the
// request; src/ext/find_fastpath/find_fastpath.cpp has the argument). The same
// counters, the same `if (_loadEnabled) Add(found)` (AddFound: the hit arm on
// the row in hand when its key is found's path), the same result.
// ============================================================================
const ::Object::External* RtObject_ExternalFactory_FindOrLoadHitRow(ExternalFactory* self, void* row) {
    rtresset::Node* const node = static_cast<rtresset::Node*>(row);
    ::Object::External* const found = rtresset::Elem(node)->external;
    ++g_cc.cacheHit;
    if (self->_loadEnabled) {
        ++g_cc.hitReAdded;
        RtObject_ExternalFactory_AddFound(self, found, node);
    } else {
        ++g_cc.hitNotReAdded;
    }
    return found;
}

}  // namespace RtObject

// ??1CompiledExternal@Object@@UEAA@XZ (0x1800bb980): ENDGAME B.3.9 OBJ -- the
// door went with the minted ??_7CompiledExternal@Object@@6B@ row and its
// RT_DELDTOR row 0x18000b7e0, its only callers; the body is the real
// Object::CompiledExternal::~CompiledExternal (external_class.cpp).
