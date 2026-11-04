#pragma once
// ============================================================================
// src/engine/object/External.h — Object::External / ExternalFactory /
//                                   ExternalList / ExternalPtr layouts
// ============================================================================
//
// Wave 3, lane C of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2).
// Every offset below was read out of a decompiled Runtime-x64.dll function
// (image base 0x180000000, read 2026-09-06) and the naming function is quoted
// next to it. The full write-up, with the per-field evidence table and the
// confidence levels, is docs/history/runtime_port/WAVE3_LANE_C.md.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
//
// WHAT THIS FAMILY IS
// -------------------
// An `Object::External` is one loaded resource file: a path plus whatever the
// concrete subclass parsed out of it. `Object::ExternalFactory` is the global
// resource table — a singleton at 0x1801fc6c8 that owns two AVL trees, one
// keyed by path and one keyed by a serialisation "reference id".
// `Object::ExternalList` is itself an External whose payload is a flat array
// of other Externals (the mod list, the help-topic list, the scenario list,
// the map-generator list, ...).
//
// `Object::ExternalPtr<T>` is ONE RAW POINTER, eight bytes, with no refcount:
// ExternalList::Find (0x1800bf700) returns one by value and its whole payload
// write is a single `*retval = _data[i]` qword store, and the not-found path is
// `*retval = 0`. Ownership lives in the factory, never in the pointer.
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"
#include "../system/Filename.h"
// ENDGAME B.3.3 step 5: hoisted from the `#include "TypeInfo.h"` that sits at
// FILE SCOPE INSIDE `namespace RtObject` further down (the Wave 3 integration
// block). That placement only ever worked because every real TU reached
// TypeInfo.h first; a TU that reached External.h first would have landed
// `Object::Base` in `RtObject::Object::Base` and this file's own `namespace
// Object` block would then have failed to find it. The lower include is now a
// `#pragma once` no-op and is left in place with its explanatory banner.
#include "TypeInfo.h"
#include "resource_ext.h"   // NOT RETAIL: the ext.visit_replay / ext.find_fastpath seams (VISITREPLAY.md)

namespace Object { class External; }   // ResourceEntry::external

namespace RtObject {


// (B.4 P2 wave X: the `RtObject::ExternalPtr<T>` POD that stood here had no
// user left; the one ExternalPtr is `Object::ExternalPtr<T>` below, pinned by
// ?Find@ExternalList@Object@@ (0x1800bf700), which returns one BY VALUE whose
// whole payload is a single qword store.)

// ============================================================
// Object::External — vtable + path + a 0x10-stride owning array.
//
// PINS
//   ??0External@Object@@QEAA@XZ  (0x1800bc9c0) stamps ??_7Base@Object@@6B@ and
//     then ??_7External@Object@@6B@ over +0x00 and zeroes +0x08, +0x10, +0x18.
//     Base is therefore 8 bytes — a vtable pointer and nothing else — and
//     External adds exactly 0x18.
//   ?GetPath@External@Object@@QEBA?AVFilename@System@@XZ (0x180006250) is one
//     line: `Filename::Filename(retval, (Filename*)(this + 8))`. So +0x08 is a
//     System::Filename, i.e. the same refcounted char* String.h describes.
//   ??1External@Object@@UEAA@XZ  (0x1800bc950) hands `this + 0x10` to
//     FUN_180006b80, which is the Array<T> teardown: destroy [0, _size) via
//     FUN_180006ac0, Free the buffer under the allocator's +0x640 lock, then
//     zero +0x00 and the size/capacity qword together. FUN_180006ac0 walks with
//     a stride of 0x10 (`param_2 = param_2 + 2` on a longlong*) and releases
//     only the FIRST qword of each element with the String release idiom.
//
// So +0x10 is a Collection::Array<T> whose T is 0x10 bytes and whose leading
// member is a String/Filename. WHAT that element is has NOT been pinned: no
// decompiled body in this lane's scope writes it, only the destructor reads it.
// It is declared here as an opaque 0x10-byte pair so the sizes are right and
// the destructor can be transcribed faithfully; naming it is future work.
// (`Object\External.cpp` exists in the profile build's source-tree strings, so
// a name is recoverable — see docs/history/runtime_port/original_source_tree.txt.)
//
// VIRTUAL SLOTS used by this lane (byte offsets into the vtable):
//   +0x10  GetTypeId() -> uint32           (ExternalFactory::FindNext, SaveText)
//   +0x28  Serialize(IO::Stream&)          (ExternalFactory::Add, SaveText)
// Slot +0x28 is named by two independent uses: SaveText (0x1800bb9d0) drives it
// with an IO::TextFileStream to write the object out as text, and
// ExternalFactory::Add (0x1800be570) drives it with an IO::VisitorStream to
// walk the object's resource references. See the FindOrLoad caching note in
// src/engine/object/external.cpp.
// ============================================================
// WAVE 27 LANE FRONTIER: the element is now pinned by THREE independent
// writers, and they do not contradict each other - they split +0x08 differently
// because they are doing different jobs.
//   * FUN_180006ac0 (the range destroy, now ported as kD_Str_10) touches ONLY
//     +0x00 and steps 0x10 (`ADD RBX,0x10` at 0x180006b5f).
//   * FUN_18002c6a0 (`Array<ExternalArrayElement>::SetSize`, now ported as
//     kSpec_ExtArrayElem) copies +0x00 through System::Filename's copy ctor
//     0x1800067d0 and then +0x08..+0x0f as ONE qword (0x18002c779).
//   * Object::External::Load's dependency replay serializes +0x00 as a String
//     and then TWO int32s, at +0x08 and +0x0c (0x1800bc7fc / 0x1800bc811).
// So the tail IS two 32-bit words, both through SerializeUInt (+0x98), so
// uint32_t. SetSize's one-qword copy of them is written as the two named
// stores, in address order (H7).
struct ExternalArrayElement {
    System::Filename owned;  // +0x00 — released with the String idiom by
                          //         FUN_180006ac0, copied (and delimiter-
                          //         normalised) by 0x1800067d0; the array's row
                          //         kSpec_ExtArrayElem owns the lifecycle
    uint32_t field_0x08;  // +0x08 — SerializeUInt at 0x1800bc7fc; copied by SetSize
    uint32_t field_0x0c;  // +0x0c — SerializeUInt at 0x1800bc811; copied by SetSize
};
static_assert(sizeof(ExternalArrayElement) == 0x10,
              "the destructor's element stride is 0x10 (FUN_180006ac0), and "
              "SetSize's SHL RCX,0x4 at 0x18002c70a agrees");
static_assert(offsetof(ExternalArrayElement, owned)  == 0x00,
              "ExternalArrayElement::owned @ +0x00 (MOV RAX,[RBX] at 0x180006b0a)");
static_assert(offsetof(ExternalArrayElement, field_0x08) == 0x08,
              "ExternalArrayElement +0x08 (MOV [R14+0x8],RAX at 0x18002c77d; 0x1800bc7fc)");
static_assert(offsetof(ExternalArrayElement, field_0x0c) == 0x0c,
              "ExternalArrayElement +0x0c (0x1800bc811)");

}  // namespace RtObject
namespace rtcoll {
// Array<ExternalArrayElement>'s row (src/engine/collection/array.cpp, SetSize
// 0x18002c6a0: assign = the Filename copy ctor + one qword, init = zero,
// destroy = 0x180006ac0, the per-element ~Filename).
extern const ArraySpec kSpec_ExtArrayElem;
}
namespace RtObject {
using ExternalRefArray = Collection::Array<ExternalArrayElement, Collection::DefaultAllocator,
                                           Collection::ExplicitSpec<&rtcoll::kSpec_ExtArrayElem>>;

// (B.4 Z MIRROR: the `RtObject::External` POD mirror that stood here had no
// user left: the one External is `Object::External` below.)

// ============================================================
// Object::ExternalFactory — the global resource table.
//
// Singleton: ?_singleton@ExternalFactory@Object@@0PEAV12@EA at 0x1801fc6c8 (note
// the `0PEAV12@EA` spelling -- the obvious `1PEAV12@A` guess is not an export),
// assigned by the constructor ??0ExternalFactory@Object@@QEAA@XZ (0x1800be7e0),
// which is also the field-by-field pin for everything below: it writes a qword
// of zero at +0x00, an int32 at +0x08, a qword at +0x10, an int32 at +0x18, a
// qword at +0x20, an int32 at +0x28, qwords at +0x30 and +0x38, an int16 at
// +0x40 (so +0x40 and +0x41 are two adjacent bools) and a byte at +0x42.
//
//  +0x00 int32  _cleanBitIndex
//        Object::ExternalFactory::Clean (0x1800be310) reads it as a SIGNED int
//        and uses it two ways: when >= 0 it clears bit `1 << (idx & 31)` from
//        each entry's usage mask and evicts the entries whose mask reaches 0;
//        when < 0 it evicts EVERYTHING unconditionally. Add (0x1800be570) sets
//        the same bit on the entry it touches.
//  +0x04 uint32 _cleanBitMask
//        *** CORRECTED BY WAVE 8 LANE A. This field was declared `_pad04` and
//        described as "zeroed with +0x00 as one qword". The zeroing claim is
//        true — the ctor does write one qword — but the word is not padding:
//        it is the ALLOCATION MASK for the 32 clean-bit slots, and the pair of
//        functions that own it is in Engine, which is why Wave 3 saw no writer.
//          Engine::UpdateLoop::UpdateLoop (0x1800ddd30) scans it with BT for
//          the lowest CLEAR bit, sets that bit, and stores the bit's INDEX in
//          UpdateLoop::_loadBit (+0x20), or stores -1 when all 32 are taken.
//          Engine::UpdateLoop::~UpdateLoop (0x1800dddb0) writes that index into
//          _cleanBitIndex above, calls Clean(), and then clears the bit here.
//        So the engine supports at most 32 simultaneously live UpdateLoops, and
//        an UpdateLoop's "load bit" is a slot rented out of this word.
//  +0x08 int32  _generation
//        Clean increments it once per sweep. This is the counter the exe's
//        ResolveExternalByName cache compares against — see the ExternalRef
//        layout comment in engine/funcs/engine_funcs.h, which describes "+0x08:
//        cached generation counter (compared against ExternalFactory+0x08)".
//        That comment is CONFIRMED by this port.
//  +0x10 Set<ResourceEntry> _resources    root +0x10, count +0x18
//  +0x20 Set<ReferenceEntry> _references  root +0x20, count +0x28
//  +0x30, +0x38  NOT PINNED by this lane. Zeroed by the ctor; no function in
//        this lane's scope reads or writes either. Declared as two opaque
//        qwords so the later offsets are right.
//  +0x40 bool _referenceModeEnabled  — EnableReferenceMode 0x1800bcf40 writes,
//        IsReferenceModeEnabled 0x1800bf0f0 reads.
//  +0x41 bool _loadEnabled           — ?EnableLoad@…@QEAAX_N@Z (0x1800bcaf0).
//        FindOrLoad's CACHE-HIT branch calls Add() again only when this is set.
//  +0x42 bool _simpleLoadEnabled     — ?EnableSimpleLoad@…@QEAAX_N@Z (0x1800be9c0).
// ============================================================

// One row of the path-keyed tree. sizeof == 0x18, so the Collection::Set node
// tail lands at +0x18 parent / +0x20 left / +0x28 right — exactly the offsets
// FindNext (0x1800be9d0), Clean (0x1800be310) and SerializeReferenceTable
// (0x1800bcb60) all walk.
//
// B.4 Z MIRROR: `path` is the real System::Filename and the element has the
// constructor retail's `Set<ResourceEntry>::Insert` (0x18002ca00) builds its
// stack pair with: the Filename copy ctor (0x1800067d0) on `path`, then the
// value's fields in order; the pair's implicit destructor is the inlined
// ~Filename (0x180006980) at 0x18002ca70, under the same EH state (-2) retail
// opens. Nodes are raw allocations: PairCtor (0x18002e6e0) placement-constructs
// `path` in the node, and the node frees (0x18002ee40, 0x18002eea0, the remove
// at 0x18002cfdc) run ~Filename on it by hand. Nothing copies, memsets or
// default-constructs an entry; ~Set's Clear runs after ~ExternalFactory has
// zeroed the root, so it destroys nothing.
struct ResourceEntry {
    ResourceEntry(const System::Filename& key, ::Object::External* ext,
                  uint32_t mask, uint32_t refId)
        : path(key), external(ext), usageMask(mask), referenceId(refId) {}

    System::Filename    path;         // +0x00 — the Set key; compared with _stricmp
    ::Object::External* external;     // +0x08 — Find/FindNext return this
    uint32_t            usageMask;    // +0x10 — Add ORs in 1 << factory->_cleanBitIndex
    uint32_t            referenceId;  // +0x14 — GetReferenceId returns this; Add seeds -1
};
static_assert(sizeof(ResourceEntry) == 0x18, "ResourceEntry is 0x18 bytes");
static_assert(offsetof(ResourceEntry, path) == 0x0, "ResourceEntry::path");
static_assert(offsetof(ResourceEntry, external)    == 0x08, "ResourceEntry::external @ +0x08");
static_assert(offsetof(ResourceEntry, usageMask)   == 0x10, "ResourceEntry::usageMask @ +0x10");
static_assert(offsetof(ResourceEntry, referenceId) == 0x14, "ResourceEntry::referenceId @ +0x14");

// One row of the id-keyed tree, rebuilt from the stream by
// SerializeReferenceTable. sizeof == 0x10, so the node tail lands at +0x10
// parent / +0x18 left / +0x20 right — the offsets GetPathForReferenceId
// (0x1800bf1d0) descends through (`piVar5[6]` and `piVar5[8]` on an int*).
//
// B.4 Z MIRROR: `path` is the real System::Filename; the constructor is the
// stack pair of retail's `Set<ReferenceEntry>::Insert` (0x18002cb90): a DWORD
// id store (`_pad` never written) then the Filename copy ctor (0x1800067d0);
// the implicit destructor is the inlined release at 0x18002cbda. Nodes are raw
// allocations with hand-written lifecycles, as ResourceEntry's above.
struct ReferenceEntry {
    ReferenceEntry(uint32_t key, const System::Filename& value) : id(key), path(value) {}

    uint32_t         id;    // +0x00 — the Set key, compared with plain integer <
    uint32_t         _pad;  // +0x04 — never written (see above)
    System::Filename path;  // +0x08 — GetPathForReferenceId copies this out
};
static_assert(sizeof(ReferenceEntry) == 0x10, "ReferenceEntry is 0x10 bytes");
static_assert(offsetof(ReferenceEntry, id) == 0x0, "ReferenceEntry::id");
static_assert(offsetof(ReferenceEntry, _pad) == 0x4, "ReferenceEntry::_pad");
static_assert(offsetof(ReferenceEntry, path) == 0x08, "ReferenceEntry::path @ +0x08");

// The two Set instantiations. NOTE THE WAVE 2 TRAP (WAVE2.md "Notes for later
// waves", reference_one_bst_find_two_node_layouts.md): the node's pointer tail
// offsets are a function of sizeof(T), so these two trees have DIFFERENT node
// layouts and MSVC emits a separate find/insert/rebalance for each. One ported
// Set body cannot serve both.
// (ENDGAME B.3.2 step 1: `Collection::Set` is now a class and its parameter
// order is <T, Compare, Allocator, Key> — Compare second, because that is the
// one a caller varies. Retail's mangled name spells it <T, Allocator, Key,
// Compare>; the four parameters and their meanings are unchanged.)
using ResourceSet  = Collection::Set<ResourceEntry,  Collection::AVLCompare,
                                     Collection::DefaultAllocator, System::Filename>;
using ReferenceSet = Collection::Set<ReferenceEntry, Collection::AVLCompare,
                                     Collection::DefaultAllocator, uint32_t>;
static_assert(sizeof(ResourceSet::Node)  == 0x38, "ResourceEntry node is 0x38");
static_assert(offsetof(ResourceSet::Node, parent) == 0x18, "resource node parent @ +0x18");
static_assert(offsetof(ResourceSet::Node, left)   == 0x20, "resource node left @ +0x20");
static_assert(offsetof(ResourceSet::Node, right)  == 0x28, "resource node right @ +0x28");
static_assert(sizeof(ReferenceSet::Node) == 0x30, "ReferenceEntry node is 0x30");
static_assert(offsetof(ReferenceSet::Node, parent) == 0x10, "reference node parent @ +0x10");
static_assert(offsetof(ReferenceSet::Node, left)   == 0x18, "reference node left @ +0x18");
static_assert(offsetof(ReferenceSet::Node, right)  == 0x20, "reference node right @ +0x20");

// ============================================================
// Object::ExternalFactory's PATH STACK — +0x30/+0x38, PINNED BY WAVE 9 LANE
// RRES. This block used to read `_opaque30 / _opaque38 — NOT PINNED`, and that
// gap was the stated blocker for `ExpandRelativePath` in both WAVE9_LANE_RES.md
// §5 and WAVE9_LANE_FNHOOK.md §4.3.
//
// It is a plain `Collection::Array<PathStackEntry>` — _data +0x30, _size +0x38,
// _capacity +0x3c — and three bodies pin it independently:
//
//   ?GetPathStackFilename@ExternalFactory@Object@@QEBA?AVFilename@System@@XZ
//   (0x1800bcf60):
//       CMP dword ptr [RCX + 0x38],0      ; the SIZE, read as an int32
//       JNZ ...                            ; empty stack -> return a null Filename
//       MOV EAX,dword ptr [RCX + 0x38]
//       DEC EAX ; MOVSXD RDX,EAX ; SHL RDX,0x4   ; index (size-1) * 0x10
//       ADD RDX,0x8                              ; ... + 8  <-- the SECOND field
//       ADD RDX,qword ptr [RCX + 0x30]           ; ... + _data
//       CALL 0x1800067d0                         ; Filename copy ctor
//
//   ?GetPathStackDirectory@...@ (0x1800bcfd0) is the SAME body without the
//   `ADD RDX,0x8`, so the FIRST field is the directory.
//
//   ?PushPath@ExternalFactory@Object@@QEAAXAEBVFilename@System@@@Z (0x1800bd0e0)
//   builds a 0x10-byte local out of two ADJACENT stack slots — [RBP-0x18] takes
//   a `Filename` copy of GetPathDirectory(path) and [RBP-0x10] takes
//   `GetPathFilename(path,true) + "." + GetPathExtension(path)` — and hands
//   `&[RBP-0x18]` to 0x18002e220 with RCX = `this + 0x30` and EDX = size + 1.
//   That is `Array<PathStackEntry>::Resize(size+1, entry)`, and it fixes the
//   stride at 0x10 and the field order from the WRITE side.
//
// So the entry is two Filenames, directory first. The two halves are read by
// separate accessors, so it is spelled as a struct rather than Filename[2].
// ============================================================
//
// B.4 Z MIRROR: the two halves are the real System::Filename. The constructor
// is PushPath's stack pair (0x1800bd177: the Filename copy ctor 0x1800067d0 on
// the directory, then on the filename) and RemoveLast's copy-out (0x18002ccb0,
// the same two copy ctors in the same order); the implicit destructor releases
// +0x08 before +0x00, retail's order in PushPath, PopPath (0x1800bd066) and the
// destroy-range 0x18002e8d0. The array's lifecycle is retail's row
// `rtcoll::kSpec_PathStack` (SetSize 0x18002e220), named by its type below;
// ~ExternalFactory's ~Array (0x18002e9e0) zeroes the header, so the member's
// implicit ~Array that may follow releases nothing.
struct PathStackEntry {
    PathStackEntry(const System::Filename& dir, const System::Filename& file)
        : directory(dir), filename(file) {}

    System::Filename directory;  // +0x00 — GetPathStackDirectory copies this out
    System::Filename filename;   // +0x08 — GetPathStackFilename copies this out
};
static_assert(sizeof(PathStackEntry) == 0x10,
              "the path-stack stride is 0x10 (SHL RDX,0x4 in both accessors)");
static_assert(offsetof(PathStackEntry, directory) == 0x0, "PathStackEntry::directory");
static_assert(offsetof(PathStackEntry, filename) == 0x08,
              "GetPathStackFilename's extra `ADD RDX,0x8`");

}  // namespace RtObject
namespace rtcoll {
// Array<PathStackEntry>'s row (src/engine/object/external_path_stack.cpp,
// SetSize 0x18002e220: assign = the two copy ctors, destroy = 0x18002e8d0).
extern const ArraySpec kSpec_PathStack;
}
namespace RtObject {
using PathStackArray = Collection::Array<PathStackEntry, Collection::DefaultAllocator,
                                         Collection::ExplicitSpec<&rtcoll::kSpec_PathStack>>;

// B.4 P2 wave X (X-R1): the POD that stood here is RETIRED. It was the same
// 0x43 bytes as `Object::ExternalFactory` below, field for field and asserted
// so, with no constructor, destructor or virtual of its own, and every body
// held it by pointer only. The name stays as an ALIAS of the real class so the
// ported C bodies in `namespace RtObject` keep their spelling and the
// `reinterpret_cast` bridges between the two spellings go. The layout pins are
// the real class's (`static_assert`s after `class ExternalFactory`).
}  // namespace RtObject
namespace Object { class ExternalFactory; }
namespace RtObject {
using ExternalFactory = ::Object::ExternalFactory;

// ============================================================
// Object::ExternalList — an External whose payload is an array of External*.
//
// The two pinned offsets are EXACTLY where Collection::Array<External*> would
// put them (_data +0x20, _size +0x28, _capacity +0x2c), so this is almost
// certainly that container rather than a hand-rolled pair. It is declared as
// the plain fields below because only two of the three are pinned: nothing in
// this lane's scope reads or writes +0x2c, and asserting a capacity field that
// no decompiled body touches would be a guess dressed as a layout.
//
// PINS, both from ?Find@ExternalList@Object@@…(0x1800bf700):
//   the loop bound is `*(int*)(this + 0x28)`
//   the element read is `*(qword*)(*(qword*)(this + 0x20) + i * 8)`
// and ?GetResourceCount@ExternalList@Object@@QEBAHXZ (0x180007650) is the
// one-line `return *(int*)(this + 0x28)`.
//
// The base sub-object is External, so +0x00..+0x1f is the External above and
// the derived part starts exactly at +0x20. The vtable is exported as
// ??_7ExternalList@Object@@6B@ (0x180154800).
//
// +0x2c is NOT PINNED. `_capacity` is the plausible reading, given both the
// offsets above and every other array in this engine, but no decompiled body in
// scope touches it.
// ============================================================
// (B.4 P2 wave X: the `RtObject::ExternalList` POD that stood here had no user
// left but its own cross-assertions; the class is `Object::ExternalList` below,
// with the same pins.)

// (B.4 P2 R-OBJ: the `FactoryTypeNodeOffsets` byte offsets that external.cpp's
// inlined IsDerivedFrom walk used are gone with their last reader: that walk
// reads `Object::Factory::TypeSet::Node` / `TypeRecord` by name, TypeInfo.h.)

}  // namespace RtObject

// ============================================================================
// ENDGAME B.3.1 step 1 — the REAL class, under retail's name.
// ============================================================================
//
// `Object::ExternalPtr<T>` is one raw pointer with retail's members on it (the
// `RtObject::ExternalPtr<T>` POD it replaced is gone, B.4 P2 wave X).
//
// TRIVIALLY COPYABLE AND TRIVIALLY DESTRUCTIBLE, ON PURPOSE AND BY ASSERTION.
// `Collection::Array<{String, ExternalPtr}>` bulk-moves these eight bytes with
// a plain qword copy and an explicit "NO refcount touch"
// (src/engine/collection/array.cpp:983). A user-provided copy constructor or
// destructor here would be SILENTLY BYPASSED by every array resize in the
// engine. There is nothing to run on copy in any case: the pointer owns
// nothing -- `Object::ExternalFactory`'s two AVL trees own every `External*`
// (docs/history/endgame_b/SMARTPTR_INVENTORY.md sections 1, 5.2, 5.4).
// This is a non-owning view, not a `shared_ptr`.
//
// RETAIL ADDRESSES. The only non-trivial member is `Serialize`, and it is ONE
// 748-byte body emitted 22 times plus ONE 818-byte array body emitted 8 times;
// the per-T difference is the two `MOV EDX,[rip+?_typeInfo@T@...]` loads and
// nothing else. Re-verified 2026-09-13 across every registered address with
// tools/extptr_clone_check.py (bounded to the real function size, because the
// tool's own "stop at the first RET" cut over-runs a body that ends in a tail
// JMP and then reports a bogus instruction-count mismatch):
//   22/22 single-pointer  identical=193  position-only=20  REAL=2
//    8/8  array           identical=212  position-only=20  REAL=2
// every REAL pair being the type-info load. ONE BODY, confirmed for the whole
// family, not just the four the pairwise tool had covered.
//
//   single-pointer form, `void Serialize(IO::Serializer&)`, per T:
//     0x1800042a0 UI::StringTable          0x180038f90 UI::ElementDescription
//     0x180076610 UI::Dialog               0x180005dc0 Achievement::Goal
//     0x180004590 UI::SpriteSheet          0x180014520 Graphics::MaterialInstance
//     0x18005d0f0 Video::ImageBuffer       0x180061100 Graphics::Material
//     0x180014230 Graphics::GraphicsMesh   0x1800756d0 UI::DialogControllerConfig
//     0x180051790 Audio::SoundEffect       0x18005b840 Graphics::AnimationGroup
//     0x180006d10 Achievement::Achievement 0x180055090 Video::VertexLayout
//     0x1800542a0 Video::VertexProgram     0x180055380 Video::PixelProgram
//     0x180055670 Video::RenderState       0x18003f1e0 UI::Font
//     0x1800594a0 Graphics::AnimationEvent 0x180076e90 UI::CharacterSet
//     0x180052a50 Audio::SoundBuffer       0x18003dc90 Object::ExternalList
//   array form, `static void SerializeArray(IO::Serializer&, int, ExternalPtr*)`:
//     0x18002f170 Object::External         0x180005220 Achievement::Goal
//     0x1800493d0 UI::ElementDescription   0x18005d4a0 Graphics::MaterialInstance
//     0x1800605e0 Video::VideoProgram      0x18003e710 UI::Font
//     0x18003eb60 UI::SpriteSheet          0x1800667f0 Collision2D::Surface
//
// There is NO `CheckPtr`, NO `AssignId` and NO `Clone` for this family: neither
// the export table nor any decompiled body defines one, because there is no
// refcount to check or clone (SMARTPTR_INVENTORY.md section 1).
// ============================================================================

#include <type_traits>

namespace rt { namespace io { struct Serializer; } }

// B.3.4 STREAMS (5/6) — `ExternalFactory::SerializeReferenceTable` takes
// retail's own `IO::Stream&`. A FORWARD DECLARATION and not an include of
// src/engine/io/Stream.h, deliberately: the parameter is a reference, so an
// incomplete type is all the declaration needs, and this header is included by
// most of src/engine/object/. Pulling the whole IO stream family in here would
// put `IO::Stream`, `IO::MemoryStream` and the Compressed branch into every one
// of those translation units to satisfy one parameter — and, worse for section
// 7.2, it would arm the bare-name grep hazard in files that have no other
// reason to see those classes. The one file that forwards the call
// (external_class_doors.cpp) includes Stream.h itself. `IO::Serializer` is
// forward-declared for the same reason: the serialize entry points below take
// it by reference only.
namespace IO { class Stream; class Serializer; class CompressedFileStream; }

namespace Object {

class External;

// ============================================================================
// THE ONE ERASED CORE of `ExternalPtr<T>::Serialize` (B.4 P2 wave X, X-R1).
//
// Retail emits the same 748-byte body once per T (the table above); the only
// per-T instructions are the two `MOV EDX,[rip+?_typeInfo@T@...]` loads. The
// port is ONE body, defined in src/engine/object/external_ptr_serialize.cpp,
// with the type record as a parameter; the typed template wrappers below are
// what every caller names (they replace the 30 `RtObject_ExternalPtr*Serialize_<T>`
// thunks, which took the field as `RtObject::External**` and the pair as the
// `rt::io::Serializer` POD).
//
// `cur` is the field's value on entry: retail reads `*self` and writes nothing
// through it until the load tail. The body returns TRUE exactly where retail
// stores `*self = result` (0x180004555) and hands that value back in `loaded`;
// on every other exit it returns false and the caller's field is not written.
// `typeInfo.typeId` is read at call time, at each of the two sites, as retail
// does (it is 0xFFFFFFFF until Factory::Startup).
// ============================================================================
bool ExternalPtrSerializeBody(External* cur, IO::Serializer& ser,
                              const BaseTypeInfo& typeInfo, External*& loaded);

// ?Serialize@?$ExternalPtr@VT@...@Object@@QEAAXAEAVSerializer@IO@@@Z for a
// field held as a bare `T*` (most description headers declare it so: the eight
// bytes ARE the ExternalPtr). T is an `Object::External` subclass with its own
// DEFINE_TYPE record; the downcast is the one retail's template body makes
// after its IsDerivedFrom(T) test.
template <class T>
inline void SerializeExternal(IO::Serializer& ser, T*& field) {
    External* loaded = nullptr;
    if (ExternalPtrSerializeBody(field, ser, T::GetTypeInfo(), loaded))
        field = static_cast<T*>(loaded);
}

// The array form (818 bytes, eight instantiations in retail): its loop body is
// the single-pointer body with `elems[i]` in place of `*self` (0x18005d4a0,
// instruction for instruction, indexed by R14); `count <= 0` runs nothing
// (`TEST EDX,EDX / JLE <epilogue>`).
template <class T>
inline void SerializeExternalArray(IO::Serializer& ser, int count, T** elems) {
    for (int i = 0; i < count; ++i)
        SerializeExternal(ser, elems[i]);
}

template <class T>
class ExternalPtr {
public:
    // Retail emits no out-of-line constructor for this template -- a bare
    // pointer member is initialised inline at every construction site, and
    // `ExternalList::Find` (0x1800bf700) writes the not-found case as a single
    // `*retval = 0` qword store. Zeroing is therefore retail's default.
    ExternalPtr() : _ptr(nullptr) {}
    ExternalPtr(T* p) : _ptr(p) {}

    // Copy, assign and destroy are the compiler's, and MUST stay trivial -- see
    // the banner. Retail had no move (C++03) and a pointer steal would be
    // observationally identical to a copy here, so none is added.
    ExternalPtr(const ExternalPtr&) = default;
    ExternalPtr& operator=(const ExternalPtr&) = default;
    ~ExternalPtr() = default;

    ExternalPtr& operator=(T* p) { _ptr = p; return *this; }

    T*   Get() const        { return _ptr; }
    T*   operator->() const { return _ptr; }
    T&   operator*()  const { return *_ptr; }
    explicit operator bool() const { return _ptr != nullptr; }

    bool operator==(const ExternalPtr& o) const { return _ptr == o._ptr; }
    bool operator!=(const ExternalPtr& o) const { return _ptr != o._ptr; }

    // ?Serialize@?$ExternalPtr@...@Object@@QEAAXAEAVSerializer@IO@@@Z -- the one
    // template body over the erased core (SerializeExternal above); T's own
    // `_typeInfo` record is the per-T load.
    void Serialize(IO::Serializer& ser) { SerializeExternal(ser, _ptr); }

    // The array form, over a run of ExternalPtr<T> (see SerializeExternalArray).
    static void SerializeArray(IO::Serializer& ser, int count, ExternalPtr* elems) {
        for (int i = 0; i < count; ++i)
            elems[i].Serialize(ser);
    }

    T* _ptr;   // +0x00 -- public: the array bulk-copy paths reach these bytes
               //         directly and retail's own layout is one bare pointer.
};

static_assert(sizeof(ExternalPtr<External>) == 8,
              "Object::ExternalPtr<T> is one pointer (ExternalList::Find 0x1800bf700)");
// ...and for every other T, including one that is only forward-declared, which
// is what most of the 26 instantiations are in this tree today.
struct ExternalPtrLayoutProbe;
static_assert(sizeof(ExternalPtr<ExternalPtrLayoutProbe>) == 8, "...for every T");
static_assert(std::is_trivially_copyable<ExternalPtr<External> >::value,
              "Array<T>::operator= memcpys these; a real copy ctor would be skipped "
              "(src/engine/collection/array.cpp:983)");
static_assert(std::is_trivially_destructible<ExternalPtr<External> >::value,
              "the array range-destroy never calls one; a real dtor would be skipped");

// ############################################################################
// ENDGAME B.3.3 step 5 (EXTERNAL) — the real classes, under retail's names.
//
// Everything in `namespace RtObject` above STAYS and stays TRIVIALLY
// DESTRUCTIBLE where it already was: B31_RULES step 1 keeps the old PODs
// declared until their last caller converts, and `RtObject::External` in
// particular is held by ported bodies as `RtObject::External tmp;` with
// `RtObject_External_Dtor(&tmp)` called BY HAND — giving that POD a destructor
// would make the compiler run a second one (the `RtSystem::String` double-free
// lesson, B31_RULES, restated in B33_RULES §7).
//
// What follows is the same bytes as real C++, cross-asserted field for field
// against the PODs so the two descriptions cannot drift.
//
// -------------------------------------------------------------------------
// THE SLOT LISTS ARE MEASURED. `??_7External@Object@@6B@` @ 0x1801547a0, read
// out of Runtime-x64.dll on 2026-09-18 (8 qwords, then a zero qword before the
// next object — B33_RULES §1.4 independently bounds it at eight):
//
//   [0] +0x00  0x180006640  vector deleting dtor (elem 0x20, ~External
//                           0x1800bc950) — deleting_dtor.cpp's own row agrees
//   [1] +0x08  0x1800066c0  `MOV RAX,qword[0x1801f9b40]; RET`
//                           = ?TypeName@External@Object@@UEBAPEBDXZ
//   [2] +0x10  0x1800066d0  `MOV EAX,dword[0x1801f9b38]; RET`
//                           = ?Type@External@Object@@UEBAIXZ
//   [3] +0x18  0x18007ec28  _purecall -> GetFileVersion
//   [4] +0x20  0x1800066e0  `MOV EAX,0x1; RET`
//   [5] +0x28  0x18007ec28  _purecall -> Serialize
//   [6] +0x30  0x18007ec28  _purecall -> Reconstruct
//   [7] +0x38  0x18007ec28  _purecall -> Destruct
//
// *** SLOT 4 HAS A RETAIL NAME, AND B33_RULES §1.4 IS OUT OF DATE ON IT. ***
// §1.4 says "name UNKNOWN, keep it as Slot4()". It is
// `?Compile@External@Object@@UEAA?AW4CompileResult@12@AEBVFilename@System@@@Z`,
// and that is not a guess: `Get xrefs to 0x1800066e0` shows an `[EXTERNAL]
// Entry Point` reference, i.e. the address IS in the DLL's export table, and
// the DLL-era residue traces resolved that exact mangled string to it
// (docs/history/runtime_port/wave9_lanes.txt:263, wave9_residue_*.txt). The tree has
// carried the name since Wave 9 lane Y — src/engine/object/external.cpp
// registers it by name AND at 0x1800066e0, and src/game/Goal/
// goal_exe_helpers.cpp:218 resolves it by name. Renaming it `Slot4()` would
// throw away a measured export name and contradict decision 5 ("names are the
// original ones"), so the retail name is kept and §1.4 is what needs the
// correction.
//
// SLOTS 6 AND 7 ARE NAMED BY POSITION, NOT BY A BODY. They are `_purecall` in
// External itself, so the names come from the one derived vtable this file also
// owns: `??_7ExternalList@Object@@6B@` @ 0x180154800 puts `Reconstruct`
// (0x180007570) at [6] and `Destruct` (0x180007640) at [7], both exported under
// those names. A pure slot's name can only ever be read off an override, and
// this is the override; src/engine/object/external_list.cpp has carried the
// same reading since VTABLE BATCH 9.
//
// THE DESTRUCTORS ARE VIRTUAL (the normal-virtual cutover, B3X W1). Retail's
// slot 0 is MSVC's deleting destructor, ONE slot; GCC spends TWO on
// `virtual ~T()` (D1 + D0), so retail slot N >= 1 (the `[N]` comments) is
// compiler slot N + 1. Deletion is `delete p` (D0: the chain, then
// Object::Base's MemFree `operator delete`); nothing reads these tables by
// index.
// ############################################################################

class ExternalFactory;

// ============================================================
// Object::External — one loaded resource file. 0x20 bytes.
// ============================================================
class External : public Base {
    // ?_typeInfo@External@Object@@0VBaseTypeInfo@2@A (0x1801f9b38): slot 1
    // TypeName 0x1800066c0 `MOV RAX,[0x1801f9b40]`, slot 2 Type 0x1800066d0
    // `MOV EAX,[0x1801f9b38]`, TypeId 0x180029350 and the factory pair
    // 0x18002c980 / 0x18002c990 (both `XOR EAX,EAX; RET` -- abstract).
    // DEFINE_TYPE is in external_class.cpp (ENDGAME B.3.9 OBJ, D6 W3).
    DECLARE_TYPE(External)

public:
    // `?A W4CompileResult@External@Object@@` — returned BY VALUE, but a `W4`
    // enum is four bytes, so it comes back in EAX with NO hidden return slot
    // (reference_hidden_retslot_wild_write.md checked, not assumed). The
    // ENUMERATOR NAMES ARE NOT RECOVERABLE from the binary — only the value 1
    // that the base's body returns — so this is spelled as the 32-bit integer
    // it is rather than an enum with invented names.
    using CompileResult = uint32_t;

    // ??0External@Object@@QEAA@XZ  retail 0x1800bc9c0. Retail stamps Base's
    // vptr and then External's; a real constructor chain does exactly that for
    // free, which is why the two stamps are not transcribed here.
    External();

    // ---- the eight slots, in measured order ----
    // ??1External@Object@@UEAA@XZ  retail 0x1800bc950. VIRTUAL (B3X W1); reached by
    // the deleting destructor, and by ~ExternalList / ~CompiledExternal as a base dtor.
    ~External() override;                                       // [0] 0x180006640
    // [1] TypeName 0x1800066c0 / [2] Type 0x1800066d0: DECLARE_TYPE.
    virtual int32_t       GetFileVersion() const = 0;           // [3] pure
    virtual CompileResult Compile(const System::Filename& path);// [4] 0x1800066e0
    virtual bool          Serialize(IO::Stream& stream) = 0;          // [5] pure
    virtual External*     Reconstruct() = 0;                    // [6] pure
    virtual void          Destruct() = 0;                       // [7] pure

    // ?GetPath@External@Object@@QEBA?AVFilename@System@@XZ  retail 0x180006250
    System::Filename GetPath() const;

    // ?SaveText@External@Object@@QEAA_NAEBVFilename@System@@0@Z
    //   retail 0x1800bb9d0, ordinal 364. ENDGAME B.3.3 step 6A: added because
    //   the caller batch found a call site for it and the member was missing;
    //   the body is unchanged and stays in external_savetext.cpp, so this
    //   is a one-line spelling over `RtObject_External_SaveText` exactly like
    //   every other member in external_class_doors.cpp.
    //
    //   THE SECOND PARAMETER IS A `Filename`, NOT A `String`: the mangled name
    //   spells `AEBVFilename@System@@` and then `0`, the backreference to the
    //   SAME type, so both arguments are `System::Filename const&`. Returns
    //   `_N` (bool) in AL — no hidden return slot.
    bool SaveText(const System::Filename& path, const System::Filename& name);

public:
    // ---- the 0x18 bytes External adds, spelled as RtObject::External spells
    // them, and the two spellings are NOT interchangeable taste:
    //
    //  * `_path` is a `System::Filename` member (B4 X-R3, H5). Retail's ctor
    //    0x1800bc9c0 stores +0x08 = 0 (0x1800bc9f0, under its own unwind
    //    state) and then +0x10 / +0x18; retail's dtor 0x1800bc950 releases the
    //    `_refs` elements and frees the array (CALL 0x180006b80 at
    //    0x1800bc974), THEN releases +0x08 (0x1800bc97a..0x1800bc9a2, the
    //    inlined ~Filename), then stamps Base's table. Declared in this order,
    //    the compiler constructs `_path` before `_refs` and destroys `_refs`
    //    before `_path`, after ~External's element loop: exactly that order,
    //    with one release (the compiler's; the body no longer releases it).
    //
    //  * `_refs` names its row, kSpec_ExtArrayElem (B.4 Z MIRROR): retail's
    //    ~External is a CALL to FUN_180006b80, i.e. ~Array<ExternalArrayElement>
    //    -> FUN_180006ac0 ("release each element's leading Filename") then free
    //    the buffer and zero the header. That is exactly the member's implicit
    //    ~Array over the row (spec->destroy, then ArrayDtorImpl), run once,
    //    after ~External's (empty) body and before `_path`'s.
    System::Filename                                _path;   // +0x08  ctor 0x1800bc9f0, dtor 0x1800bc97a
    RtObject::ExternalRefArray                      _refs;   // +0x10
};
static_assert(sizeof(External) == 0x20, "Object::External is 0x20 bytes");
static_assert(offsetof(External, _path) == 0x08, "External::_path @ +0x08 (GetPath 0x180006250)");
static_assert(offsetof(External, _refs) == 0x10, "External::_refs @ +0x10 (~External 0x1800bc950)");

// ============================================================
// Object::CompiledExternal — adds no bytes; only a vtable and a destructor.
//
// `??1CompiledExternal@Object@@UEAA@XZ` (0x1800bb980) is seven instructions:
// re-stamp `??_7CompiledExternal@Object@@6B@` (0x180154a30), then TAIL-JMP
// ~External. The re-stamp is not tidying — it is what makes the base dtor
// dispatch on CompiledExternal's own table — and a real destructor chain
// reproduces it for free. `deleting_dtor.cpp:297` records the element size as
// 0x20, i.e. the same as External's: this class adds no data.
// ============================================================
class CompiledExternal : public External {
    // ?_typeInfo@CompiledExternal@Object@@0VBaseTypeInfo@2@A (0x1801f9b00):
    // slot 1 TypeName 0x18000b6b0 `MOV RAX,[_typeInfo+8]`, slot 2 Type
    // 0x18000b690 `MOV EAX,[_typeInfo]`, TypeId 0x18000b6a0 and the factory
    // pair 0x18000b6c0 / 0x18000b6d0 (both `XOR EAX,EAX; RET` -- abstract).
    // DEFINE_TYPE is in external_class.cpp (ENDGAME B.3.9 OBJ, D6 W3).
    DECLARE_TYPE(CompiledExternal)

public:
    // ??0CompiledExternal@Object@@QEAA@XZ  retail 0x1800bb920: Base's and
    // External's vptr, +0x08/+0x10/+0x18 zero (External(), the compiler's),
    // then CompiledExternal's vptr (the compiler's). No store of its own.
    CompiledExternal() = default;

    // [0] 0x18000b7e0 — retail's VECTOR deleting destructor (`MOV EDX,0x20` =
    // the element size, `LEA R9,[0x1800bb980]` = ~CompiledExternal).
    ~CompiledExternal() override;  // 0x1800bb980
};
static_assert(sizeof(CompiledExternal) == 0x20,
              "Object::CompiledExternal is 0x20 bytes -- the element size of its deleting dtor 0x18000b7e0");

// ============================================================
// Object::ExternalList — an External whose payload is an array of External*.
//
// `??_7ExternalList@Object@@6B@` @ 0x180154800, NINE slots (read 2026-09-18):
//   [0] 0x180007800 vdtor (elem 0x30, ~ExternalList 0x1800076c0)
//   [1] 0x180007370 TypeName        [2] 0x180007350 Type
//   [3] 0x1800bfa10 GetFileVersion (`MOV EAX,3; RET`)
//   [4] 0x1800066e0 External::Compile — INHERITED, the same body
//   [5] 0x1800bfa20 Serialize       [6] 0x180007570 Reconstruct
//   [7] 0x180007640 Destruct        [8] 0x1800075e0 Construct
// so `Construct` is ExternalList's OWN new virtual: External's table stops at
// eight. sizeof is 0x30 (the deleting dtor's `MOV EDX,0x30`).
//
// ENDGAME B.3.9 OBJ (B39_RULES D1/D2): CONCRETE, and every live ExternalList
// wears the COMPILER's table. `CreateNewObject` / `CreateInPlaceObject`
// (retail's factory pair, registered by name and VA from external_list.cpp)
// are MemAlloc + `::new (p) ExternalList()`, replacing the FACTREST step
// table that stamped the minted `??_7ExternalList@Object@@6B@`. The minted
// row, its RT_DELDTOR row 0x180007800, the ??1 door and the seven slot doors
// are retired (tools/rt_mint_retired/obj.txt).
// ============================================================
class ExternalList : public External {
    // ?_typeInfo@ExternalList@Object@@0VBaseTypeInfo@2@A (retail 0x1801f9be0):
    // slot 1 TypeName 0x180007370, slot 2 Type 0x180007350, TypeId 0x180007360
    // and the factory pair (0x180007460 / 0x180007510, external_list.cpp) are
    // retail's reflection block; DEFINE_TYPE is in external_class.cpp (ENDGAME
    // B.3.9 OBJ, B39_RULES D6 W3 -- the minted record is retired).
    DECLARE_TYPE(ExternalList)

public:
    // ??0ExternalList — retail's stores (the inlined half of the factory pair
    // 0x1800074c3..f4 / Construct 0x1800075e0): Base's and External's vptr and
    // +0x08/+0x10/+0x18 zero (External(), the compiler's), ExternalList's vptr
    // (the compiler's), then +0x20 and +0x28 zero as two qwords (the member
    // initialisers).
    ExternalList() : External(), _resources(nullptr), _count(0), _pad2c(0) {}

    // [0] 0x180007800 — retail's VECTOR deleting destructor (`MOV EDX,0x30` =
    // the element size, `LEA R9,[0x1800076c0]` = ~ExternalList). The key
    // function is GetFileVersion (external_class_doors.cpp).
    // ??1ExternalList 0x1800076c0; the compiler chains ~External.
    ~ExternalList() override;  // 0x1800076c0
    int32_t         GetFileVersion() const override;         // [3] 0x1800bfa10
    bool            Serialize(IO::Stream& stream) override;        // [5] 0x1800bfa20
    External*       Reconstruct() override;                  // [6] 0x180007570
    void            Destruct() override;                     // [7] 0x180007640
    virtual External* Construct();                           // [8] 0x1800075e0

    // ?GetResourceCount@ExternalList@Object@@QEBAHXZ   retail 0x180007650
    int32_t GetResourceCount() const { return _count; }

    // ?Find@ExternalList@Object@@QEBA?AV?$ExternalPtr@VExternal@Object@@@2@AEBVFilename@System@@VType@2@@Z
    //   retail 0x1800bf700.  *** THE `Type` PARAMETER IS NEVER READ *** — R9 is
    //   dead in the whole body. Kept because the slot's signature demands it.
    ExternalPtr<External> Find(const System::Filename& path, uint32_t type) const;

public:
    External** _resources;  // +0x20
    int32_t    _count;      // +0x28
    int32_t    _pad2c;      // +0x2c — NOT PINNED; probably Array<T>::_capacity
};
static_assert(sizeof(ExternalList) == 0x30,
              "Object::ExternalList is 0x30 bytes -- the element size of its deleting dtor 0x180007800");
static_assert(offsetof(ExternalList, _pad2c) == 0x2C, "ExternalList::_pad2c");
static_assert(offsetof(ExternalList, _resources) == 0x20, "ExternalList::_resources @ +0x20");
static_assert(offsetof(ExternalList, _count)     == 0x28, "ExternalList::_count @ +0x28");

// ============================================================
// The two tree row types and the path-stack entry, under retail's names.
//
// They are ALIASES, not re-declarations: `RtObject::ResourceEntry`,
// `ReferenceEntry` and `PathStackEntry` are already fully pinned above, the
// three ported Set families (`rtresset`, `rtrefset`, external_set.h) name them
// directly, and a second declaration of the same fields would be one more
// thing that can drift. B31_RULES asks for the retail NAME; it does not ask for
// a copy of the bytes.
// ============================================================
using ResourceEntry  = RtObject::ResourceEntry;
using ReferenceEntry = RtObject::ReferenceEntry;
using PathStackEntry = RtObject::PathStackEntry;
using PathStackArray = RtObject::PathStackArray;
using ResourceSet    = RtObject::ResourceSet;
using ReferenceSet   = RtObject::ReferenceSet;

namespace detail {

// Retail's tree comparisons all go through the CRT's `_stricmp`, in the "C"
// locale (the DLL never calls setlocale), so plain ASCII case folding. Spelled
// out here for the reason src/engine/system/filename.cpp spells it out: a
// runtime/ header must not depend on a CRT whose locale behaviour differs
// between toolchains (PLAN.md §4.1).
inline int StriCmpAscii(const char* a, const char* b) {
    for (;;) {
        unsigned char ca = static_cast<unsigned char>(*a++);
        unsigned char cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0)  return 0;
    }
}

// Byte-identical to `System::Filename::operator<` (0x180026cb0), which is what
// every walk site inlines: a null right-hand side orders below everything, a
// null left-hand side orders below any non-null, otherwise case-insensitive.
inline bool FilenameLess(const char* a, const char* b) {
    if (b == nullptr) return false;
    if (a == nullptr) return true;
    return StriCmpAscii(a, b) < 0;
}

// *** TWO NODE LAYOUTS, TWO FINDS — reference_one_bst_find_two_node_layouts.md.
// The resource tree's element is 0x18 wide so its node tail is parent +0x18 /
// left +0x20 / right +0x28; the reference tree's element is 0x10 wide so its
// tail is +0x10 / +0x18 / +0x20. A find written against the wrong layout
// returns the right answer for a key that happens to be the ROOT and is
// permanently wrong one level down, which is why the unit test drives at least
// three keys at two depths through BOTH of these.
//
// These are the bodies src/engine/object/external.cpp has inlined since Wave 3
// (`ResourceSetFind`, and the integer descent in GetPathForReferenceId), lifted
// here unchanged so the class and the C shims cannot descend differently. They
// are header-inline and touch nothing but pointers, so `make unit` can drive
// them against a hand-built tree with no allocator and no registry.
//
// Each find comes in TWO spellings: one over the tree ROOT and one over the
// `Set` header that forwards to it. That is not convenience — a `Collection::
// Set` object has a real `~Set` that reaches `Collection::detail::SetNodeFree`
// in set_core.cpp, which stands on the address registry and cannot link into
// `make unit`. The root overload is what lets the test drive these bodies at
// all; the Set overload is what the ported call sites use.
inline ResourceSet::Node* ResourceFind(ResourceSet::Node* root, const char* key) {
    ResourceSet::Node* n = root;
    while (n != nullptr) {
        if (FilenameLess(key, n->element.path._data))      n = n->left;
        else if (FilenameLess(n->element.path._data, key)) n = n->right;
        else                                              return n;
    }
    return nullptr;
}
inline ResourceSet::Node* ResourceFind(const ResourceSet* set, const char* key) {
    return ResourceFind(set->_root, key);
}
inline ResourceSet::Node* ResourceLeftmost(ResourceSet::Node* n) {
    while (n->left != nullptr) n = n->left;
    return n;
}
inline ResourceSet::Node* ResourceSuccessor(ResourceSet::Node* n) {
    if (n->right != nullptr) return ResourceLeftmost(n->right);
    ResourceSet::Node* p = n->parent;
    while (p != nullptr && p->right == n) { n = p; p = p->parent; }
    return p;
}
// The reference tree descends on a plain integer compare, not on `_stricmp` —
// the one Set family in this engine that does (reference_set.h's banner). The
// compare is SIGNED 32-bit, as retail's insert (0x18002d1d7, reference_set.cpp)
// and retail's lookup (GetPathForReferenceId 0x1800bf1d0: `CMP R8D,EAX; JL` /
// `CMP EAX,R8D; JGE`) order the key (review F-172).
inline ReferenceSet::Node* ReferenceFind(ReferenceSet::Node* root, uint32_t id) {
    const int32_t key = static_cast<int32_t>(id);
    ReferenceSet::Node* n = root;
    while (n != nullptr) {
        const int32_t nodeKey = static_cast<int32_t>(n->element.id);
        if (key < nodeKey)      n = n->left;    // JL  -> +0x18
        else if (nodeKey < key) n = n->right;   //        +0x20
        else                    return n;       // JGE -> found
    }
    return nullptr;
}
inline ReferenceSet::Node* ReferenceFind(const ReferenceSet* set, uint32_t id) {
    return ReferenceFind(set->_root, id);
}

}  // namespace detail

// ============================================================
// Object::ExternalFactory — the global resource table. NOT an Object::Base:
// `??0ExternalFactory@Object@@QEAA@XZ` (0x1800be7e0) writes no vptr, and the
// class has no exported `??_7`. So no vtable, no reflection record, no
// `DECLARE_TYPE`.
//
// Layout is `RtObject::ExternalFactory`'s, field for field and asserted against
// it. The class is DECLARED here and its members are one-line spellings over
// the already-ported C bodies, which is the whole of what this step changes
// about them:
//
// *** `Add` IS DELIBERATELY A PURE SPELLING CHANGE. *** B33_RULES §0 makes the
// loader-thread AVL-rotate race on `ExternalFactory::Add` its own
// investigation: no lock, no retry, no ordering change, no null check, and no
// silent removal either. `Add` below therefore forwards to the UNCHANGED body
// in src/engine/object/external_add.cpp rather than taking it over — the
// instruction sequence inside the tree mutation is byte-for-byte the one that
// is under investigation, with one extra (inlinable) call frame in front of it.
// The same reasoning covers `Clean`, `FindOrLoad`, `SerializeReferenceTable`,
// `PushPath`/`PopPath` and `ForceUnload`, every one of which mutates the same
// two trees.
// ============================================================
class ExternalFactory {
public:
    ExternalFactory();

    // ?Get@ExternalFactory@Object@@SAPEAV12@XZ — `return _singleton;`.
    static ExternalFactory* Get();

    // ?_singleton@ExternalFactory@Object@@0PEAV12@EA — retail 0x1801fc6c8 (note
    // the `0PEAV12@EA` spelling; the obvious `1PEAV12@A` is not an export).
    // The CLASS STATIC since ENDGAME B.3.9 OBJ (B39_RULES D7): defined in
    // external_class_doors.cpp, and RtObjectPortInit redirects the minted row
    // onto it, so the name door, the address door and this member are the
    // same eight bytes. Written by the ctor door (engine/singleton_ctors.cpp).
    // PUBLIC while foreign readers still reach the cell through the doors.
    static ExternalFactory* _singleton;

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/STREAMCACHE.md, DESIGN.md hook rule 5):
    // ext.stream_cache. Null by default and under the faithful profile. External::Load, with the
    // packed entry's FileStream open and before CompressedStream::Open, asks `_extLoadBegin` for
    // another stream to serialize the object from (the resource's cached record); null = retail's
    // compressed stream, which the extension then watches through CompressedStream::_extReadTap.
    // `_extLoadEnd` closes the pair after the load, with its result. Which resource loads, and
    // when, is unchanged either way.
    using ExtLoadBeginFn = IO::Stream* (*)(IO::CompressedFileStream& cfs);
    using ExtLoadEndFn   = void (*)(IO::CompressedFileStream& cfs, IO::Stream* alt, const External* result);
    static ExtLoadBeginFn _extLoadBegin;   // defined in external_load.cpp
    static ExtLoadEndFn   _extLoadEnd;     // defined in external_load.cpp

    // EXT SEAMS (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md): ext.visit_replay and
    // ext.find_fastpath (object/resource_ext.h). Null by default and under the faithful profile;
    // set once at boot. Defined in external_add.cpp.
    static const ExtVisitReplay* _extVisitReplay;
    static const ExtFindFast*    _extFindFast;

    // ---- the read-only half (ported bodies, src/engine/object/external.cpp)
    const External* Find(const System::Filename& path) const;      // 0x1800bdeb0
    const External* FindNext(uint32_t typeId, const External* prev) const;  // 0x1800be9d0
    uint32_t         GetReferenceId(const External* external) const;        // 0x1800bf100
    System::Filename GetPathForReferenceId(uint32_t id) const;              // 0x1800bf1d0
    bool             IsReferenceModeEnabled() const { return _referenceModeEnabled; }  // 0x1800bf0f0
    void             EnableReferenceMode(bool enable) { _referenceModeEnabled = enable; }  // 0x1800bcf40
    void             EnableLoad(bool enable)       { _loadEnabled = enable; }        // 0x1800bcaf0
    void             EnableSimpleLoad(bool enable) { _simpleLoadEnabled = enable; }  // 0x1800be9c0

    // ---- the path stack (external_path_stack.cpp / external_path.cpp)
    void             PushPath(const System::Filename& path);   // 0x1800bd0e0
    void             PopPath();                                // 0x1800bd040
    System::Filename GetPathStackFilename() const;             // 0x1800bcf60
    System::Filename GetPathStackDirectory() const;            // 0x1800bcfd0
    // ?ExpandRelativePath@ExternalFactory@Object@@QEBA?AVFilename@System@@AEBV34@_N@Z
    // — the trailing `_N` is a FOURTH argument after the hidden return slot.
    System::Filename ExpandRelativePath(const System::Filename& path,
                                        bool appendIdentifier) const;

    // ---- the mutating half. Spelling only; see the banner on `Add`.
    void            Add(External* external);                   // 0x1800be570
    const External* FindOrLoad(const System::Filename& path);  // 0x1800bf320
    void            Clean();                                   // 0x1800be310
    void            ForceUnload(const System::Filename& path);

    // ?SerializeReferenceTable@ExternalFactory@Object@@QEAAXAEAVStream@IO@@@Z
    //   retail 0x1800bcb60 — DECLARED AS OF ENDGAME B.3.4 STREAMS (5/6).
    //
    // It was held back by a TYPE, not by a decision about the method: retail's
    // parameter is `IO::Stream&`, and until block 4 there was no such class —
    // only two independently declared duplicate PODs (`rt::io::Stream` in
    // Serializer.h, which the ported entry point takes, and `RtIo::Stream` in
    // Stream.h), neither of which could be named from another translation unit
    // without dragging one header's private spelling into this one.
    // `IO::Stream` now exists and replaces both (B34_RULES section 1.5.2), so
    // the declaration below is finally retail's own signature.
    //
    // IT IS A FORWARDER, like every other member of the mutating half — see the
    // banner above and external_class_doors.cpp. Its body tears down and
    // rebuilds `_references`, the same tree the `ExternalFactory::Add`
    // loader-thread AVL-rotate race runs in, and B34_BRIEFS section D binds it:
    // the instruction sequence in external_reftable.cpp is untouched, with one
    // extra (inlinable) call frame in front of it.
    void            SerializeReferenceTable(IO::Stream& stream);

    // ?GetCompiledPath@ExternalFactory@Object@@SA?AVFilename@System@@AEBV34@@Z
    //   retail 0x1800bec10 — a STATIC returning by value.
    static System::Filename GetCompiledPath(const System::Filename& path);

    // ---- the typed lookups (B.4 const_api ruling) ---------------------------
    // Retail's lookups return `const External*` (mangled `PEBV`) while every
    // slot that keeps the result holds a non-const `T*` (`ExternalPtr<T>`):
    // retail drops that const once, in an inlined template. These three are
    // that template: the const drop and the downcast in one place, for every
    // caller. `T` is External or a class derived from it; `static_cast` is the
    // same downcast the callers spelled. Inline, so the call sites compile to
    // the bare lookup.
    template <class T>
    T* FindAs(const System::Filename& path) const {
        return static_cast<T*>(const_cast<External*>(Find(path)));  // CAST-OK: const_api
    }
    template <class T>
    T* FindNextAs(uint32_t typeId, const External* prev) const {
        return static_cast<T*>(const_cast<External*>(FindNext(typeId, prev)));  // CAST-OK: const_api
    }
    template <class T>
    T* FindOrLoadAs(const System::Filename& path) {
        return static_cast<T*>(const_cast<External*>(FindOrLoad(path)));  // CAST-OK: const_api
    }

    // ---- the 0x43 bytes, in retail order (ctor 0x1800be7e0 pins every one) --
    int32_t      _cleanBitIndex;         // +0x00
    uint32_t     _cleanBitMask;          // +0x04 — the 32 UpdateLoop slots
    int32_t      _generation;            // +0x08
    int32_t      _pad0c;                 // +0x0c
    ResourceSet  _resources;             // +0x10
    ReferenceSet _references;            // +0x20
    PathStackArray _pathStack;           // +0x30 data, +0x38 size
    bool         _referenceModeEnabled;  // +0x40
    bool         _loadEnabled;           // +0x41
    bool         _simpleLoadEnabled;     // +0x42
};
static_assert(offsetof(ExternalFactory, _cleanBitIndex) == 0x0, "ExternalFactory::_cleanBitIndex");
static_assert(offsetof(ExternalFactory, _cleanBitMask) == 0x4, "ExternalFactory::_cleanBitMask");
static_assert(offsetof(ExternalFactory, _generation) == 0x8, "ExternalFactory::_generation");
static_assert(offsetof(ExternalFactory, _pad0c) == 0xC, "ExternalFactory::_pad0c");
static_assert(offsetof(ExternalFactory, _resources)  == 0x10, "_resources @ +0x10");
static_assert(offsetof(ExternalFactory, _references) == 0x20, "_references @ +0x20");
static_assert(offsetof(ExternalFactory, _pathStack)  == 0x30, "_pathStack @ +0x30");
static_assert(offsetof(ExternalFactory, _pathStack)
              + offsetof(PathStackArray, _size) == 0x38,
              "path-stack SIZE @ +0x38 (GetPathStack*'s `CMP dword [RCX+0x38],0`)");
static_assert(offsetof(ExternalFactory, _referenceModeEnabled) == 0x40, "_referenceMode @ +0x40");
static_assert(offsetof(ExternalFactory, _loadEnabled)          == 0x41, "_loadEnabled @ +0x41");
static_assert(offsetof(ExternalFactory, _simpleLoadEnabled)    == 0x42, "_simpleLoad @ +0x42");

// ############################################################################
// ENDGAME B.3.9 OBJ (B39_RULES D6 W3): `External`, `ExternalList` and
// `CompiledExternal` carry retail's reflection block through DECLARE_TYPE /
// DEFINE_TYPE (external_class.cpp); their minted `?_typeInfo@` records are
// retired, so each name has exactly one record in `_firstTypeInfo`.
// ############################################################################

}  // namespace Object

// ==== B3R W0-P0c: Runtime bodies reached by retail address ====
// ENDGAME B.3R (B3R_RULES R5): each body below is registered at the retail
// address in its trailing comment and read through the registry today. It is
// declared here, copied from its definition (linkage, namespace, types), so the
// reader can call it by name. Nothing calls it through this declaration yet.

namespace RtObject {
// ---- NOT RETAIL (HITCH_SAVE, docs/extensions/handins/HITCH_SAVE.md): the per-save memo of
// ExternalFactory::GetReferenceId. A save asks for the reference id of the same few hundred
// Externals tens of thousands of times; every ask is two Filename copies (four
// AdjustDelimiters passes over the path) and a case-insensitive descent of the resource
// tree. Between Begin and End the FIRST ask for an External runs that retail body
// unchanged (so its in-place path normalisation still happens) and its answer is kept;
// a later ask for the same External returns the kept answer. Retail's answer cannot
// change inside the window: SerializeAll (save direction) brackets the window after
// SerializeReferenceTable has numbered the tree, and nothing in a save adds, removes or
// renumbers a resource or rewrites an External's path (AdjustDelimiters is idempotent).
// `verify` (COMPPROF=4) runs the retail body on every ask and counts the asks whose
// kept answer differs; End returns that count (0 when not verifying).
void ReferenceIdMemoBegin(bool verify);
int  ReferenceIdMemoEnd(long long* asks, long long* hits);
// ---- object/external_load.cpp
extern "C" ::Object::External* RtObject_External_Load(const System::Filename* path, ::Object::External* existing);  // 0x1800bc620
// ---- B3R RT-Core: object/external.cpp, external_find.cpp, external_add.cpp
System::Filename* RtObject_External_GetPath(const ::Object::External* self, System::Filename* ret);  // 0x180006250
uint32_t RtObject_ExternalFactory_GetReferenceId(const ExternalFactory* self, const ::Object::External* external);  // 0x1800bf100
System::Filename* RtObject_ExternalFactory_GetPathForReferenceId(const ExternalFactory* self,
                                                                 System::Filename* ret, uint32_t id);  // 0x1800bf1d0
::Object::External* RtObject_ExternalFactory_Find(ExternalFactory* self, const System::Filename* path);  // 0x1800be0e0 (private Find)
extern "C" void RtObject_ExternalFactory_Add(ExternalFactory* self, ::Object::External* external);  // 0x1800be570
}  // namespace RtObject

// ==== end B3R W0-P0c ====

// src/engine/object/external_load.cpp -- installs the atexit hooks that print the
// `[ExtLoad]`, `[ExtAdd]` and `[ExtSaveText]` exit lines. Called once, from
// RuntimeInit() (src/app/runtime_init.cpp).
void RtObjectExternalLoadInstallCounters();
