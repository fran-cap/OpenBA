// ============================================================================
// src/engine/object/external_forceunload.cpp — BATCH 5 (2026-09-12)
//   `?ForceUnload@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z`
//   retail 0x1800beec0, ordinal 2369, 539 bytes / 162 instructions.
// ============================================================================
//
//     void ExternalFactory::ForceUnload(const System::Filename& path)
//
// Clears the usage mask of ONE cache entry so the next `ExternalFactory::Clean`
// evicts it. It does not free anything itself — the name is about the effect,
// not the mechanism.
//
// ITS ONLY CALLER IS `IO::Modification::SetupFileSystem` (0x1800af1a9), ported
// in this same commit; that is why this row became worth doing now.
//
// ---------------------------------------------------------------------------
// IT IS `Find`'s TWIN, AND THE PREAMBLE IS TRANSCRIBED FROM ITS OWN LISTING
// ---------------------------------------------------------------------------
// The first two thirds of this body is instruction for instruction the key
// normalisation `?Find@ExternalFactory@Object@@` (0x1800be0e0,
// src/engine/object/external_find.cpp) performs, and the CALL sequence was
// read out of THIS function rather than inherited from that one:
//
//   1800bef20 / 1800bef29   AdjustDelimiters(key)   x2   (0x1800c6970)
//   1800bef36               GetPathIdentifer(PATH, &ident)  (0x1800c6610)
//   1800bef5e               String::operator+(&tmp, key, ":resource")
//                                                        (0x1800c92d0,
//                                                         R8 = 0x180155ae0)
//   1800befa7               AdjustDelimiters(key)   x1
//   1800bf028 / 1800bf031   AdjustDelimiters(probe) x2
//   1800bf03f               Set<ResourceEntry>::Find(this+0x10, &probe)
//                                                        (0x18002dd70)
//
// The three quirks external_find.cpp documents are all present here and all
// kept:
//   * the "four AdjustDelimiters" are really TWO ASSIGNMENTS each followed by
//     one redundant re-normalisation — `Filename::operator=` already ends in
//     one, and `AdjustDelimiters` is idempotent, so three of the four are
//     no-ops. Dropping them would stop this body matching the listing for no
//     benefit.
//   * the identifier is taken from the ARGUMENT (`MOV RCX,RBX` at 0x1800bef33),
//     not from the normalised copy. It cannot matter — AdjustDelimiters does
//     not touch ':' — and it is transcribed as written anyway, because "it
//     cannot matter" is how a port acquires a difference nobody can find later.
//   * `probe` is a redundant second copy of `key`. Retail builds it; so does
//     this.
//
// The suffix is the ":resource" at 0x180155ae0 — the SAME literal `Find` uses,
// read out of .rdata (`3a 72 65 73 6f 75 72 63 65 00`) and not confused with
// the bare "resource" four bytes later at 0x180155af0, which
// `ExpandRelativePath` uses for the same decision. The wrong one produces a key
// that never matches anything the factory inserted.
//
// ---------------------------------------------------------------------------
// THE ONE THING IT DOES THAT `Find` DOES NOT — AND THE ORDER IS RETAIL'S
// ---------------------------------------------------------------------------
//   1800bf078  TEST RBX,RBX / JZ            the node may be missing
//   1800bf07d  MOV dword ptr [RBX+0x10],ESI ESI is 0 here
//
// `+0x10` is `ResourceEntry::usageMask` (src/engine/object/External.h, which
// pins it from `ExternalFactory::Add` ORing `1 << _cleanBitIndex` into the same
// dword and from `Clean` evicting entries whose mask reaches 0). Zeroing it is
// the whole point of the function.
//
// *** `probe` IS RELEASED BEFORE THAT STORE, NOT AFTER. *** The release at
// 0x1800bf04f..0x1800bf078 sits between the `Find` and the store, and the
// teardown order at the tail is probe, then ident (0x1800bf080), then key
// (0x1800bf0a9). Kept as written: `Find` returns a pointer INTO the tree, not
// into `probe`, so the order is unobservable today — and an unobservable order
// that is transcribed costs nothing, while one that is "tidied" is a
// difference waiting for a future element type that shares storage.
//
// ---------------------------------------------------------------------------
// NO NULL GUARD IS ADDED AND NONE IS REMOVED
// ---------------------------------------------------------------------------
// Retail's own `TEST RBX,RBX` covers the missing-node case, so the port carries
// it; nothing else in the body is guarded and nothing else is guarded here.
//
// ---------------------------------------------------------------------------
// ORACLE — NONE, and the reason is that there is nothing to compare
// ---------------------------------------------------------------------------
// The function returns void and its single effect is `usageMask = 0`, which is
// IDEMPOTENT — so an ours-then-theirs replay at a live call site would run
// cleanly and prove nothing: theirs would find the same node and store the same
// zero, and every comparison would be green whether or not our descent landed
// on the right entry. A site like that is worse than none, because a green
// `calls=` reads as coverage (one level up from a port with zero calls, which is
// unverified, not clean). What DOES check the descent is the same machinery `Find` is
// checked by: `rtresset::Find` is lane RES's ported body with its own lockstep
// probe, and this function contributes only the key normalisation, which is
// byte-identical to the one `Object::ExternalFactory::Find`'s full diff oracle
// has run clean on since Wave 9.
//
// PORTABILITY: no <windows.h> (PLAN.md §4.1 decision 1).
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "external_set.h"

#include "object/External.h"
#include "system/String.h"
#include "system/string_internal.h"

namespace RtObject {


namespace {

// 0x180155ae0 — see the banner for why this is NOT the "resource" at
// 0x180155af0.
const char* const kResourceSuffix = ":resource";

}  // namespace

// ============================================================================
// ?ForceUnload@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z
//                                                          retail 0x1800beec0
// ============================================================================
void RtObject_ExternalFactory_ForceUnload(ExternalFactory* self, const System::Filename* path) {
    // ---- 0x1800beedc-0x1800bef29: `Filename key = path`, then adjust again --
    // ENDGAME B.3.1 step 2. Null-init + operator= + the redundant second
    // AdjustDelimiters IS the copy constructor (0x1800067d0), whose ported body
    // normalises exactly twice for that reason: 0x1800bef20 and 0x1800bef25 are
    // both still spent.
    System::Filename key(*path);                         // [RBP+0x28]

    // ---- 0x1800bef2f: the identifier comes from `path`, NOT from `key` ------
    System::Filename ident = path->GetPathIdentifer();   // [RBP+0x38]

    // ---- 0x1800bef3c-0x1800befd2: no identifier -> append ":resource" -------
    // `GetLength` (0x1800063c0) returns 0 for a null buffer, so it is retail's
    // own null-or-empty test, not a widened one.
    if (ident.GetLength() == 0) {
        // 0x1800bef66 + 0x1800befa7 is `Filename::operator=(const String&)`
        // (0x180026d40) — ONE normalise. `tmp`'s 0x1800befad release is its dtor.
        System::String tmp = key.AsString() + kResourceSuffix;   // [RBP+0x30]
        key = tmp;
    }

    // ---- 0x1800befd6-0x1800bf031: `Filename probe = key`, adjusted twice ----
    // The scope is deliberate and is the banner's "*** probe IS RELEASED BEFORE
    // THAT STORE ***": retail's 0x1800bf04f release sits between the `Find` and
    // the `usageMask = 0`, and closing the block here is how RAII keeps that
    // order rather than sliding the release to the end of the function.
    rtresset::Node* node = nullptr;
    {
        System::Filename probe(key);                     // [RBP+0x30], reused
                                                         // 0x1800bf028 / 0x1800bf031
        // ---- 0x1800bf037: descend `this->_resources` (this + 0x10) ---------
        node = rtresset::Find(
            reinterpret_cast<const rtresset::Set*>(&self->_resources),  // CAST-OK: elem_stride
            &probe);
    }   // 0x1800bf04f — probe's release, still ahead of the store

    // ---- 0x1800bf078: the whole point of the function ----------------------
    if (node != nullptr) rtresset::Elem(node)->usageMask = 0;

    // ident (0x1800bf080) and key (0x1800bf0a9) release in their destructors,
    // in that order — reverse declaration order is retail's teardown order.
}

}  // namespace RtObject
