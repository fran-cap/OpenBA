#pragma once
// ============================================================================
// src/engine/collection/array_internal.h — shared between array.cpp, the files
// that drive its `kSpec_*` rows and the unit tests (Wave 9 lane Q).
// ============================================================================
//
// It exists so every includer drives the SAME `SetSizeCore` and `ArrayDtorImpl`
// the entry points drive, against the SAME `ArraySpec` rows — a re-derived row
// would be a copy of the port rather than the port.
//
// The full commentary — what each spec was read out of, why the arity varies,
// which instantiations leave bytes unwritten — is the banner of array.cpp.
// ============================================================================

#include <cstdint>

#include "collection/Collection.h"

namespace rtcoll {

// ENDGAME B.3.2 step 1 — `ArrayHdr`, `ArraySpec`, the Elem* typedefs and the
// four core declarations MOVED to src/engine/collection/Collection.h, where
// `Collection::Array<T>`'s inline bodies can name them. Nothing about them
// changed; this header still owns the `kSpec_*` rows below and the commentary
// on what each spec was read out of, and the per-field commentary that used to
// sit on `ArraySpec` stays HERE, next to those rows:
//
//   * a written-extent constant belongs to a WRITER, not to a type — `assign` and `init` are two
//     code paths with two different written extents, so there are two windows
//     and the oracle has to know which arm produced the element it is looking
//     at. Each pair is a half-open byte range WITHIN one element that the arm
//     does NOT write; equal (both 0) means the arm covers the whole stride.
//     Comparing an unwritten byte fails 100% of cases and reads like a
//     catastrophic port defect.
//   * `seedZeroLo/Hi` is the range a seeded test buffer must leave ZERO because
//     the element operations DEREFERENCE it (a String pointer whose refcount at
//     `str-4` gets bumped); everything outside it can take a varying non-zero
//     pattern — all-zero elements are the one seed under which an
//     unwritten-bytes bug is invisible.

// ---------------------------------------------------------------------------
// ENDGAME B.3.2 step 1 — the instantiation a retail address maps onto
// ---------------------------------------------------------------------------
// A hand-derived `kSpec_*` row is not expressible from an element type's traits
// (Collection.h's Array banner lists the twelve kinds that are not), so every
// address-named `SetSize`/`~Array` shim names its own row and gets the
// instantiation that carries it:
//
//     Collection::Array<uint8_t, DefaultAllocator, ExplicitSpec<&kSpec_X>>
//
// The element type is `uint8_t` for the same reason `ArrayHdr`'s is: the shim
// is handed a `void*` and the stride lives in the spec, not in the C++ type.
// A converted caller in step 2 uses `Collection::Array<RealElement>` instead
// and gets `rtcoll::SpecFor<RealElement>()`.
template <const ArraySpec* SPEC>
using ArrayWithSpec = Collection::Array<uint8_t, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<SPEC>>;

template <const ArraySpec* SPEC>
inline ArrayWithSpec<SPEC>* AsArray(void* a) {
    return reinterpret_cast<ArrayWithSpec<SPEC>*>(a);  // CAST-OK: elem_stride
}

// The per-address specs, in the order of the parameterization table in
// docs/history/runtime_port/WAVE9_LANE_Q.md.
extern const ArraySpec kSpec_Bool;        // 0x180008b90   tail UNINITIALISED
extern const ArraySpec kSpec_I32;         // 0x18002fea0   tail UNINITIALISED
extern const ArraySpec kSpec_U64Zero;     // 0x180008ff0
extern const ArraySpec kSpec_PtrZero;     // 0x180008d60, 0x1800764c0
extern const ArraySpec kSpec_Transform;   // 0x180059140
extern const ArraySpec kSpec_I32Neg1;     // 0x18007b960
extern const ArraySpec kSpec_Fill8;       // 0x18001f990
extern const ArraySpec kSpec_Fill8Z;      // 0x180076900, 0x18007a0d0
extern const ArraySpec kSpec_Fill4;       // 0x180023830
extern const ArraySpec kSpec_Fill16;      // 0x18004f790, 0x18004f8e0
extern const ArraySpec kSpec_Node18;      // 0x18005fb00   (Wave 9 lane GFX3)
extern const ArraySpec kSpec_Sprite38;    // 0x180004e30   NON-TRIVIAL
extern const ArraySpec kSpec_EffectSlot;  // 0x180051390   NON-TRIVIAL
extern const ArraySpec kSpec_StringExt;   // 0x180061480   NON-TRIVIAL
// Wave 9 lane UIW. Both NON-TRIVIAL (element assign and a shrink-path destroy),
// but unlike the three above, every element operation they use is PORTED rather
// than called by address into the DLL — see array.cpp's registration note.
extern const ArraySpec kSpec_StringRef;   // 0x180034860   NON-TRIVIAL
extern const ArraySpec kSpec_StringRefZ;  // 0x180029a80  tail ZEROED
extern const ArraySpec kSpec_GraphSeries; // 0x1800417d0   NON-TRIVIAL

// Wave 9 lane DESTROY. tests/unit/array_test.cpp drives SetSizeCore's
// SHRINK-TAIL arm for this spec: a live boot only ever GROWS the packfile entry
// array, so that arm has zero live coverage and the unit test is the only place
// it gets driven at all.
extern const ArraySpec kSpec_PkgEntry;    // 0x1800298f0   NON-TRIVIAL

// Wave 9 lane VIDL. DEFINED IN src/engine/engine/mainloop_video.cpp, not in
// array.cpp — the only spec in this table that lives outside the Collection
// port. Its element assign calls System::Filename's copy ctor and its destroy
// is the 0x180034440 body, RtEngine_VideoResourceDestroyRange_180034440
// (rtcoll::ReleaseStringsCore over kD_Str_18), so the spec sits next to them
// rather than next to this declaration.
extern const ArraySpec kSpec_VideoResource;  // 0x18004f620   NON-TRIVIAL

// Wave 27 lane FRONTIER. NON-TRIVIAL and TWO-ARGUMENT: the element is
// {System::Filename, int32, int32}, the assign is a CALL to Filename's copy
// ctor and the destroy runs on BOTH the realloc and the shrink arm (the
// disassembly: docs/history/runtime_port/WAVE27_LANE_FRONTIER.md §2.3).
extern const ArraySpec kSpec_ExtArrayElem;   // 0x18002c6a0   NON-TRIVIAL
extern const ArraySpec kSpec_StringPair10;    // 0x180078c90   NON-TRIVIAL

// ENDGAME B.3.2 step 2 (callers) — declared here (defined in array.cpp) so a
// converted caller can bridge through it with `rtcoll::AsArray<&SPEC>(...)`,
// same as the rows above. A `const` at namespace scope has INTERNAL linkage in
// C++ unless it is declared `extern` somewhere, so these declarations are what
// make the five definitions in array.cpp reachable from another translation
// unit at all — they change nothing about the rows themselves.
extern const ArraySpec kSpec_Tri18A4;        // 0x1800136f0  Array<EdgeMesh::Triangle>, tail UNINITIALISED
extern const ArraySpec kSpec_MetaEntry20;    // 0x180034010, 0x180033080  NON-TRIVIAL
extern const ArraySpec kSpec_MetaData58;     // 0x1800332d0  NON-TRIVIAL
extern const ArraySpec kSpec_EmitterInstance;// 0x180052330  zeroed 10-byte prefix init
extern const ArraySpec kSpec_InnerArrayU8;   // 0x180053f00  Array<Array<uint8_t>>  NON-TRIVIAL

// ENDGAME B.3.2 step 2 (callers, src/engine/ui/*) — same reasoning: these
// rows were file-local to array.cpp until a converted UI call site needed to
// bridge through them.
extern const ArraySpec kSpec_Event30;      // 0x180033690   UI::UserInterface::Event, PARTIAL assign
extern const ArraySpec kSpec_SpriteIndexZ; // 0x1800370c0   UI::Sprite, tail ZEROED
extern const ArraySpec kSpec_CaptureEntry;  // 0x18004bde0  UI::SpriteCapture::Entry (3-arg, with fill) NON-TRIVIAL
extern const ArraySpec kSpec_CaptureEntryZ; // 0x18004ba90  UI::SpriteCapture::Entry (2-arg, default tail) NON-TRIVIAL
extern const ArraySpec kSpec_ExtStr10at08; // 0x180075d20   UI::DialogNamedTemplate NON-TRIVIAL
extern const ArraySpec kSpec_CfgEntry20;   // 0x180075b20   UI::DialogControllerConfigEntry NON-TRIVIAL
extern const ArraySpec kSpec_Sprite38Z;    // 0x180078950   UI::SpriteSheet::SpriteInfo (2-arg) NON-TRIVIAL
extern const ArraySpec kSpec_SpriteDraw;   // 0x180078520, 0x1800783d0   UI::SpriteDraw NON-TRIVIAL (partial assign)

// ENDGAME B.3.2 step 2 (callers, src/engine/io/persistence_savelist_job.cpp)
// — same reasoning: NOT trait-expressible (byteCopy=false, a String-adopt
// assign, and NO init arm — a trait derivation over {uint64, System::String}
// would synthesize a non-null init from String's user-provided default ctor,
// which retail's 3-arg-only SetSize never has), so the hand-derived row stays
// and is bridged through with `AsArray<&kSpec_SaveGameInfo>`.
extern const ArraySpec kSpec_SaveGameInfo; // 0x18002ae20, 0x18002aae0  IO::Persistence::SaveGameInfo NON-TRIVIAL

// ENDGAME B.3.2 step 2 (callers, src/engine/graphics/wave28_lane_gfxbody.cpp)
// — NOT trait-expressible even though `Renderer::drawGroups` is now a real
// `Collection::Array<RendererDrawGroup>` member: retail's element assign
// (0x18001be60) deep-copies the three nested `Array<RendererDrawItem>` via
// their own hand-derived copy path, but RendererDrawGroup has no user-defined
// copy constructor, so the trait-derived placement-copy would fall back to
// each inner Array's `=default` SHALLOW copy assign instead — a real behaviour
// difference. Bridged through with `AsArray<&kSpec_DrawGroup1f0>`.
extern const ArraySpec kSpec_DrawGroup1f0; // 0x1800638e0  Graphics::Renderer::drawGroups NON-TRIVIAL

}  // namespace rtcoll
