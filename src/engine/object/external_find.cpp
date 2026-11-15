// ============================================================================
// src/engine/object/external_find.cpp — Wave 9 lane RRES, follow-on rung
//   `?Find@ExternalFactory@Object@@AEAAPEAVExternal@2@AEBVFilename@System@@@Z`
//   retail 0x1800be0e0, 165 instructions.
// ============================================================================
//
// The private cache lookup. `FindOrLoad` (0x1800bf320) calls it once, at
// 0x1800bf366, and it is the ONLY caller in the DLL.
//
// WHY THIS ROW IS CHEAP NOW AND WAS NOT BEFORE
//
// Its closure is complete and was completed by two other lanes, not this one:
// `MemFree`, `AdjustDelimiters`, `GetPathIdentifer` and
// `String::operator+(const char*)` were already ours, and `0x18002dd70` — the
// 0x18-element `Collection::Set::Find` this body descends — is lane RES's
// (`src/engine/object/external_set.cpp`). **Nothing new is opened by this
// file.** That is the whole reason it was picked: WAVE9_LANE_RRES.md §6 named
// it "the cheapest next rung by a wide margin" and this is that rung.
//
// PORTABLE: no <windows.h> (PLAN.md §4.1 decision 1).
//
// ---------------------------------------------------------------------------
// THE TRAP THIS BODY EXISTS TO EXPLAIN — lane RES's finding, now with its cause
// ---------------------------------------------------------------------------
// `WAVE9_LANE_RES.md` §2 recorded that `Find` at `0x18002dd70` **requires a
// delimiter-adjusted key**: a lookup with a raw `/` path silently misses, and
// misses identically in the shipped body, because `Filename`'s copy
// constructor adjusts delimiters on the way INTO the node and nothing adjusts
// the search key. Lane RES paid one run to discover it and wrote that "retail
// guarantees the adjustment at every call site — `ExternalFactory`'s own lookup
// at 0x1800be0e0 calls `AdjustDelimiters` FOUR times before it descends."
//
// This is that function, and the four calls are now accounted for individually.
// They are TWO PAIRS, and each pair is one `Filename x = y` assignment:
// `Filename::operator=` (0x180006770) already ends in `AdjustDelimiters`, and
// retail then calls it AGAIN, explicitly, on the same object:
//
//     0x1800be140 / 0x1800be145   on `key`   after `key = path`
//     0x1800be244 / 0x1800be24d   on `probe` after `probe = key`
//
// So the "four calls" are not four normalisations of four different strings.
// They are two assignments each followed by one redundant re-normalisation, and
// `AdjustDelimiters` is idempotent, so three of the four are no-ops. All four
// are transcribed anyway: the cost is a function call on an already-normalised
// buffer, and dropping them would make this body stop matching the disassembly
// for no benefit.
//
// ---------------------------------------------------------------------------
// THE SECOND COPY IS ALSO REDUNDANT, AND IS ALSO KEPT
// ---------------------------------------------------------------------------
// `probe` ([RBP+0x30], 0x1800be1fe onward) is a fresh `Filename` that is
// assigned from `key` and then handed to `Set::Find`. `key` was already
// normalised and would have served. Retail builds the copy; so does this port.
// A tidied version would be observably identical and would lose the property
// that makes this file checkable against the listing.
//
// ---------------------------------------------------------------------------
// RETAIL USES ITS CALLER'S SHADOW SPACE AS SCRATCH
// ---------------------------------------------------------------------------
// `MOV RBP,RSP` sits after four pushes, so `[RBP+0x20]` is the return address
// and `[RBP+0x28]` / `[RBP+0x30]` / `[RBP+0x38]` — the three `Filename` locals
// — are the CALLER's 32-byte home space. That is legal in the MS x64 ABI. Noted
// because a reader checking offsets against the listing will otherwise read
// `[RBP+0x28]` as an argument and it is not one.
// ============================================================================

#include <cstdio>
#include <cstdlib>

#include "external_find.h"
#include "external_set.h"

#include "system/String.h"
#include "system/string_internal.h"

namespace RtObject {


FindCounters g_fc = {};

namespace {

// 0x180155ae0. Read out of .rdata, not guessed:
//   180155ae0: 3a 72 65 73 6f 75 72 63 65 00   ":resource"
// The neighbouring literal at 0x180155af0 is the bare "resource" that
// `ExpandRelativePath` uses for the same defaulting decision — two different
// strings for the same concept, four bytes apart, and using the wrong one here
// would produce a key that never matches anything the factory inserted.
const char* const kResourceSuffix = ":resource";


}  // namespace

// ============================================================================
// ?Find@ExternalFactory@Object@@AEAAPEAVExternal@2@AEBVFilename@System@@@Z
//                                                          retail 0x1800be0e0
//
//     External* ExternalFactory::Find(const Filename& path)
//
// AEAA — PRIVATE, and private is not the same as unexported: this DLL exports
// its private members and the name above is ordinal 2336 in the retail export
// directory (reference_aeaa_is_private_not_unexported.md). Verified with
// pefile before this body was written, not inferred from the mangling.
// ============================================================================
::Object::External* RtObject_ExternalFactory_Find(ExternalFactory* self, const System::Filename* path) {
    ++g_fc.calls;

    // ---- 0x1800be0fc-0x1800be149: `Filename key = path`, then adjust again --
    // ENDGAME B.3.1 step 2. Retail's null-init + operator= + the redundant
    // second AdjustDelimiters IS the copy constructor (??0Filename@System@@
    // QEAA@AEBV01@@Z, 0x1800067d0), whose ported body normalises exactly twice
    // for that reason. So this one line still spends 0x1800be140 AND
    // 0x1800be145, and all four of the header's AdjustDelimiters calls are
    // accounted for inside the two copies.
    System::Filename key(*path);                         // [RBP+0x28]

    // ---- 0x1800be14f: the identifier comes from `path`, NOT from `key` ------
    // Retail passes RBX (the argument) here, not the normalised copy. It cannot
    // matter — AdjustDelimiters does not touch ':' — but it is transcribed the
    // way it is written, because "it cannot matter" is how a port acquires a
    // difference nobody can find later.
    System::Filename ident = path->GetPathIdentifer();   // [RBP+0x38]

    // ---- 0x1800be15c-0x1800be1f6: no identifier -> append ":resource" -------
    // `GetLength` is retail's own null-or-empty test (0x1800063c0 returns 0 for
    // a null buffer), so it is the same two branches as the raw header read.
    if (ident.GetLength() == 0) {
        ++g_fc.identDefaulted;
        // 0x1800be186 (release-then-adopt) + 0x1800be1c7 (AdjustDelimiters) is
        // exactly `Filename::operator=(const String&)`, 0x180026d40 — ONE
        // normalise, not two. `tmp`'s 0x1800be1cd release is its destructor.
        System::String tmp = key.AsString() + kResourceSuffix;   // [RBP+0x30]
        key = tmp;
    } else {
        ++g_fc.identPresent;
    }

    // ---- 0x1800be1f6-0x1800be251: `Filename probe = key`, adjusted twice ----
    // The redundant second copy; see the file header. Same copy constructor as
    // `key` above, so 0x1800be244 and 0x1800be24d are both still spent.
    System::Filename probe(key);                         // [RBP+0x30], reused

    // ---- 0x1800be257: descend `this->_resources` (this + 0x10) -------------
    rtsetavl24::Set* const tree =
        reinterpret_cast<rtsetavl24::Set*>(&self->_resources);  // CAST-OK: elem_stride
    rtsetavl24::Node* const node = rtresset::Find(tree, &probe);

    // (probe's 0x1800be26f release is its destructor now. It runs at the end of
    // the function instead of here — nothing between the two reads its buffer,
    // and the DESTRUCTION ORDER is still retail's: probe, then ident
    // (0x1800be2a6), then key (0x1800be2cf), which is reverse declaration order.)

    // ---- 0x1800be298: `return node ? node->element.external : nullptr` ------
    // `MOV RBX,qword ptr [RBX + 0x8]` — ResourceEntry::external at +0x08, which
    // is what External.h asserts. THE DEREF IS UNGUARDED IN RETAIL BEYOND THE
    // null test, and it stays that way here: a node in this tree always carries
    // an External, and a guard would hide the desync rather than survive it.
    ::Object::External* result = nullptr;
    if (node != nullptr) {
        ++g_fc.hits;
        result = rtresset::Elem(node)->external;
    } else {
        ++g_fc.misses;
    }

    // ident (0x1800be2a6) and key (0x1800be2cf) release in their destructors.
    return result;
}

}  // namespace RtObject
