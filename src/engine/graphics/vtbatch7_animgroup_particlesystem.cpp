// ============================================================================
// src/engine/graphics/vtbatch7_animgroup_particlesystem.cpp
//   VTABLE BATCH 7 — Graphics::AnimationGroup and Graphics::ParticleSystem
// ============================================================================
//
// Both classes are Object::External subclasses (no CompiledExternal step --
// their ctors stamp Base/External then their OWN final vtable directly, unlike
// Video::VertexLayout's three-level chain in vertexlayout.cpp). All addresses
// below are RETAIL Runtime-x64.dll (read with objdump against the shipped
// file, not Ghidra's decompile -- reference_ghidra_c_output_vs_disassembly.md).
//
// ---------------------------------------------------------------------------
// Graphics::AnimationGroup — ??_7AnimationGroup@Graphics@@6B@, 0x58 bytes
// ---------------------------------------------------------------------------
//   slot0 deleting dtor   0x1800167f0  src/engine/object/deleting_dtor.cpp
//   slot1 TypeName        0x180016310  here (reads ?_typeInfo@AnimationGroup@Graphics@@+8)
//   slot2 Type            0x1800162f0  here (reads ?_typeInfo@AnimationGroup@Graphics@@+0)
//   slot3 GetFileVersion  0x1800fb680  here (returns 8)
//   slot4 Compile         0x1800fb690  here (returns CompileResult::Success == 1,
//                                       no-op -- AnimationGroup has no separate
//                                       compile step, it loads pre-baked)
//   slot5 Serialize       0x1800fb6a0  here
//   slot6 Reconstruct     0x180016550  here
//   slot7 Destruct        0x180016650  here
//   slot8 Construct       0x1800165d0  here
//
// Layout (ctor 0x180016550/0x1800165d0, both inline the SAME stores; the
// intermediate Base/External vtable stamps are folded away by MSVC and only
// the LAST store — AnimationGroup's own vtable — survives, exactly the
// AnimationEvent/Particle pattern documented in Particle.h):
//   +0x00 vtable = ??_7AnimationGroup@Graphics@@6B@
//   +0x08/+0x10/+0x18  External fields, zeroed
//   +0x20 Array<AnimationClip>   _clips     {data, size@+0x28, cap@+0x2c}
//   +0x30 float                  _f30       default 0.1f (0x3dcccccd)
//   +0x38 Array<{String,String,float}> _tags {data, size@+0x40, cap@+0x44}
//         (stride 0x18: two System::String refs then a float; ctor's
//          `imul $0x70` helper at 0x180016660 pins the CLIP stride, not this
//          one -- this one's stride is read off Serialize's element loop,
//          see below)
//   +0x48 Array<float>           _floats    {data, size@+0x50, cap@+0x54}
// Total 0x58, matches the deleting dtor's measured elemSize.
//
// AnimationClip (0x70 bytes, the +0x20 array's element -- pinned by
// 0x180016660's `imul $0x70` GetElement helper AND by Serialize's element loop
// 0x18005a040, which is what supplies every field offset below):
//   +0x00 Array<{int32 tag, ExternalPtr<AnimationEvent>}> _events (0x10 header)
//   +0x10 String  _str1     (semantics unread -- byte position only)
//   +0x18 String  _str2
//   +0x20 String  _str3
//   +0x28 int32   _i28
//   +0x2c bool    _b2c
//   +0x30 int32   _i30
//   +0x34 int32   _i34
//   +0x38 float   _f38
//   +0x3c bool    _b3c
//   +0x40 int32   _i40
//   +0x44 float   _f44
//   +0x48 float   _f48
//   +0x4c int32   _i4c
//   +0x50 int32   _i50
//   +0x58 int64   _i58
//   +0x60 Array<Math::Transform> _pose (0x10 header)
// Total 0x70.
//
// Non-slot helpers this Serialize calls, and how each is reached:
//   0x1800c9930  System::String::Serialize             ALREADY PORTED (string.cpp)
//   0x180059820  Array<Math::Transform>::Serialize      ALREADY PORTED
//                (src/engine/graphics/animationstate.cpp,
//                 RtGraphics_SerializeTransformArray)
//   0x1800594a0  Object::ExternalPtr<AnimationEvent>::Serialize
//                PORTED IN THIS BATCH, appended to
//                src/engine/object/external_ptr_serialize.cpp (the shared
//                748-byte ExternalPtr<T>::Serialize template, one more
//                instantiation; typeInfo = ?_typeInfo@AnimationEvent@Graphics@@)
//   0x180059e70  Array<AnimationClip>::SetSize          *** ALL FOUR ARE NOW
//   0x18005a220  Array<{String,String,float}>::SetSize  PORTED AND REGISTERED
//   0x18005a410  Array<float>::SetSize                  (VTABLE BATCH 7b) ***
//   0x180059930  Array<{int32,ExternalPtr}>::SetSize    as J3 rows in
//                src/engine/collection/array.cpp: kSpec_AnimClip70,
//                kSpec_TagStrStrF18, kSpec_I32 and kSpec_IdExtPtr10. The call
//                sites below are UNCHANGED -- they already went through
//                ResolveAddr, which consults the port registry first
//                (reference_resolveaddr_consults_port_registry_first.md), so
//                registering the four addresses is what moved them from the
//                DLL to our bodies.
//                (This banner used to end "adding four more ArraySpec rows ...
//                is out of this batch's scope", which was true of batch 7.)
//
// ---------------------------------------------------------------------------
// Graphics::ParticleSystem — ??_7ParticleSystem@Graphics@@6B@, 0x108 bytes
// ---------------------------------------------------------------------------
//   slot0 deleting dtor   0x180020a70  src/engine/object/deleting_dtor.cpp
//   slot1 TypeName        0x1800203e0  here
//   slot2 Type            0x1800203c0  here
//   slot3 GetFileVersion  0x1800a4d10  here (returns 0x1001)
//   slot5 Serialize       0x1800a4d20  here
//   slot6 Reconstruct     0x180020560  here (calls vtable[0](this,0), then the
//                                       real ctor 0x1800a5320, reached by
//                                       address -- see below)
//   slot7 Destruct        0x180020590  here (tail-calls vtable[0](this,0))
//   slot8 CreateSystem    0x1800a5140  here (thin forward -- see below)
//   slot9 Construct       0x180020580  here
//
// ParticleSystem extends Graphics::Particle (0x48 bytes, src/engine/graphics/
// Particle.h); its OWN fields start at +0x48 and were all independently
// confirmed here as a WRITER (Serialize), agreeing field-for-field with the
// kPS_* offsets wave28_lane_partsys.cpp already measured as a READER
// (ParticleSystemInstance::Update) -- two independent witnesses.
//
// *** VTABLE BATCH 7b PORTED BOTH OF THE BODIES BATCH 7 THUNKED. ***
// `??0ParticleSystem@Graphics@@QEAA@XZ` (the real ctor, 0x1800a5320, ord 302)
// and `?CreateSystem@ParticleSystem@Graphics@@` (0x1800a5140, ord 2134, which
// allocates a ParticleInstance of size 0xe0 and calls ITS ctor 0x1800a55a0) are
// both written out below, and Construct/Reconstruct/CreateSystem now call OUR
// bodies rather than forwarding through ResolveAddr. The ctor is registered at
// its own export name and address as well, so a caller that reaches it directly
// (Object::External's factory path does) gets ours too.
// (This banner used to say both "are NOT reimplemented here ... ported as THIN
// FORWARDING THUNKS", which was true of batch 7.)
// 0x1800a55a0, the ParticleInstance ctor, is still reached by address: it is a
// different class's row (wave28_lane_partsys.cpp owns that class).
//
// ---------------------------------------------------------------------------
// ENDGAME B.3.5 step 5, block H2 — what changed in the ParticleSystem half
// ---------------------------------------------------------------------------
// The file-local `struct ParticleSystem` is gone: the real
// `Graphics::ParticleSystem` (and its base `Graphics::Particle`) live in
// `src/engine/graphics/Particle.h`, and all eight non-destructor slot bodies
// are MEMBERS. Every door above keeps its mangled name, its retail address and
// its registration and delegates with a QUALIFIED non-virtual call. The ctor
// door and the four hand-written vtable stamps STAY (B4b-2, B35_RULES section 11).
//
// *** THE SLOT ORDER IS RULE B5-16's ONE EXCEPTION IN THE WHOLE OF VIDEO AND
// GRAPHICS, AND IT IS WHY THIS CLASS HAS ITS OWN UNIT TEST. *** `Particle`
// introduces `CreateSystem` PURE at slot 8; `Construct` therefore enters the
// branch at slot 9, behind it. The slot list above is the byte-read order and it
// is the order the member declarations are in.
// The three hand-inlined int64 `ReaderWriter*` slot calls in `Serialize` became
// `IO::Serializer::Serialize(int64_t&)` member calls, closing this file's share
// of the nine sites B35_RULES section 11 deferred as "receiver-side work"
// (block H1 closed the fourth, in `AnimationGroup`'s clip loop).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "system/String.h"
#include "collection/Collection.h"
#include "io/Serializer.h"
#include "io/Stream.h"      // B.4: IO::Stream::_direction
#include "object/External.h"
// ENDGAME B.3.5 step 5, block H1: the real `Graphics::AnimationGroup` and
// `Graphics::Animation`. The two file-local `struct AnimationGroup` /
// `struct AnimationClip` this file used to declare (batch 7's) are GONE —
// they moved into `src/engine/graphics/AnimationGroup.h` and `Animation.h`
// under retail's own names, with the same offsets and the same asserts.
//
// GREP HAZARD (B4b-5 / B34_BRIEFS A.7), RESTATED FOR BLOCK H2: inside a
// `namespace Graphics { }` block in this file, a bare `Animation`,
// `AnimationGroup`, `AnimationEvent`, `Particle` OR `ParticleSystem` binds the
// REAL CLASS. Block H1's note said a bare `Particle` bound nothing; that was
// true only until block H2 added `Graphics::Particle` and
// `Graphics::ParticleSystem` to `Particle.h`. Every POD spelling inside such a
// block must be written `RtGraphics::`.
#include "graphics/AnimationGroup.h"
#include "graphics/AnimationState.h"   // B.4: RtGraphics_SerializeTransformArray
#include "graphics/Particle.h"
#include "system/Timer.h"   // ENDGAME B.3.9 SS1: Timer::_secondsPerTick
#include "collection/Collection.h"   // rtcoll::MemAlloc

// 0x180059820 — Array<Math::Transform>::Serialize, ported in
// src/engine/graphics/animationstate.cpp and declared in AnimationState.h.

namespace {

// B.4: the local `{ stream, rw }` pair every External::Serialize in this file
// builds, the encoding's CreateReader / CreateWriter call and the closing
// `if (rw) delete rw` are `IO::Serializer`'s ctor and dtor, inlined; the
// ReaderWriter members are called by name on `ser._readerWriter`, re-read
// before each call as retail re-reads it (`name` nullptr, as retail passes).
bool IsReading(const IO::Stream& s) { return s._direction == IO::kReading; }

// (ENDGAME B.3.9 block GFX: the minted Base / External stamp accessors and
// `CallSlot0` went with the two ConstructBody helpers; slot 0 is a virtual
// call on the class now.)

}  // namespace

// ============================================================================
// Graphics::AnimationGroup
// ============================================================================
namespace {

// The element type and the record are `Graphics::Animation` (0x70) and
// `Graphics::AnimationGroup` (0x58), declared in src/engine/graphics/.
// Batch 7's two file-local structs used to sit here; they carried exactly these
// offsets and exactly these static_asserts, and the headers carry them now --
// plus the eleven retail accessor addresses block H1 disassembled in order to
// NAME each field instead of numbering it.
using AnimationClip  = ::Graphics::Animation;
using AnimationGroup = ::Graphics::AnimationGroup;

// ENDGAME B.3.9 block GFX: batch 7's `ConstructBody` (the Base / External /
// AnimationGroup stamps out of the MINT plus the field stores) is gone. The
// stores are the real `AnimationGroup()` (AnimationGroup.h) and the stamps the
// compiler's; Construct / Reconstruct placement-new the class.

// 0x1800594a0 — Object::ExternalPtr<AnimationEvent>::Serialize: the shared
// 748-byte template, `Object::SerializeExternal<AnimationEvent>` (External.h).

// 0x18005a040 — `Array<Graphics::Animation>::Serialize`'s element loop, called
// by `AnimationGroup::Serialize` with (Serializer*, int count, Animation* data).
//
// IT STAYS A FREE FUNCTION. Retail 0x18005a040 is an unexported
// `Collection::Array<T>::Serialize` instantiation, not a member of either class,
// and src/engine/collection/array.cpp owns that family. What changed in ENDGAME
// B.3.5 block H1 is the SPELLING: every `c->i30`-style position name is now the
// retail field name off `Graphics::Animation`, whose layout is pinned in its
// header against the eleven accessor bodies listed there.
//
// B.4: typed on `IO::Serializer&` and (B.4 Y RT) on the element: `_clips`
// carries `Graphics::Animation`, whose three names are real Strings
// (?Serialize@String@System@@ 0x1800c9930, by name).
void RtGraphics_AnimationGroup_SerializeClips(IO::Serializer& ser, int count, AnimationClip* c) {
    if (count <= 0) return;
    for (int i = 0; i < count; ++i, ++c) {
        c->_name.Serialize(ser);
        c->_meshName.Serialize(ser);
        c->_sourceName.Serialize(ser);

        ser._readerWriter->SerializeInt  (c->_startFrame, nullptr);
        ser._readerWriter->SerializeInt  (c->_endFrame, nullptr);
        ser._readerWriter->SerializeFloat(c->_rate, nullptr);
        ser._readerWriter->SerializeBool (c->_looping, nullptr);
        ser._readerWriter->SerializeInt  (c->_loopFrame, nullptr);
        ser._readerWriter->SerializeInt  (c->_instanceCount, nullptr);
        ser._readerWriter->SerializeBool (c->_instanced, nullptr);

        // The nested Array<Animation::Event> at c->+0x00, stride 0x10.
        int32_t n = c->_events._size;
        ser._readerWriter->SerializeInt(n, nullptr);
        if (IsReading(*ser._stream)) {
            rtcoll::RtColl_ArraySetSize_180059930(&c->_events, n);   // 0x180059930
        }
        ::Graphics::Animation::Event* e = c->_events._data;
        for (int32_t k = 0; k < n; ++k, ++e) {
            ser._readerWriter->SerializeInt(e->_frame, nullptr);
            Object::SerializeExternal<Graphics::AnimationEvent>(ser, e->_event);   // 0x1800594a0
        }

        ser._readerWriter->SerializeInt  (c->_frameCount, nullptr);
        ser._readerWriter->SerializeFloat(c->_frameRate, nullptr);
        ser._readerWriter->SerializeFloat(c->_loopScale, nullptr);
        ser._readerWriter->SerializeInt  (c->_boneCount, nullptr);

        // Already-ported Array<Math::Transform>::Serialize (animationstate.cpp).
        RtGraphics_SerializeTransformArray(&c->_keys, ser);

        // ENDGAME B.3.5 block H1 — ONE OF THE NINE HAND-INLINED int64 SLOT CALLS
        // B35_RULES section 11 listed as "receiver-side work". Block H1 owns this
        // receiver (`Graphics::Animation::_duration`), and
        // `IO::Serializer::Serialize(int64_t&, const char*)` @ 0x180035000 is a
        // real member, so it is spelled as a member call. That member's own body
        // still calls the ReaderWriter's +0x90 slot (Serializer.h section 3.1:
        // the 22 members keep their raw slot calls until the minted tables
        // retire), so this is a spelling change and nothing else. The three int64
        // calls in `RtGraphics_ParticleSystem_Serialize` below are NOT touched:
        // that class is block H2's and its half of this file stays byte-identical.
        ser.Serialize(c->_duration, nullptr);
    }
}

}  // namespace

// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — `Graphics::AnimationGroup`'s member bodies.
//
// Their own `namespace Graphics { }` block, after the anonymous namespace above
// has closed, because a member definition has to appear in a namespace
// ENCLOSING the class and `Graphics` / `RtGraphics` are siblings (B34_BRIEFS
// section A.2). The anonymous namespace's helpers — `ConstructBody`,
// `CallSlot0`, `AnimGroupTypeInfo`, `MakeReaderWriter`, `RwInt`, `RwFloats`,
// `IsReading`, `RtAddr` — are reachable from here unqualified: they are members
// of the global namespace's unnamed namespace, in this same translation unit.
//
// ALL EIGHT NON-DESTRUCTOR SLOT BODIES MOVED. ENDGAME B.3.9 block GFX: the
// class is concrete and every live AnimationGroup wears the compiler's table,
// so the eight slot doors that delegated into these members are gone with the
// minted `??_7AnimationGroup@Graphics@@6B@`.
// ############################################################################

namespace Graphics {

// [1] ?TypeName@AnimationGroup@Graphics@@UEBAPEBDXZ  0x180016310
//     reads `?_typeInfo@AnimationGroup@Graphics@@` + 8 (BaseTypeInfo::name).
// ENDGAME B.3.9 TI-REST: TypeName / Type / TypeId are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).

// [2] ?Type@AnimationGroup@Graphics@@UEBAIXZ  0x1800162f0 — the record's +0x00.

// [3] ?GetFileVersion@AnimationGroup@Graphics@@UEBAHXZ  0x1800fb680
//     `MOV EAX,8; RET`. EIGHT — the version every AnimationGroup in every save
//     was written with. B35_RULES section 6.1: `VertexLayout::GetFileVersion`
//     returning 1 instead of retail's 3 silently dropped every draw item once.
int32_t AnimationGroup::GetFileVersion() const { return 8; }

// [4] ?Compile@AnimationGroup@Graphics@@UEAA?AW4CompileResult@External@Object@@
//     AEBVFilename@System@@@Z  0x1800fb690. `MOV EAX,1; RET` — a no-op that
//     answers CompileResult::Success: an AnimationGroup loads pre-baked and has
//     no separate compile step. `path` is genuinely dead (RDX is never read).
//     The four-byte `?AW4` enum comes back in EAX with NO hidden return slot.
AnimationGroup::CompileResult AnimationGroup::Compile(const System::Filename& /*path*/) {
    return 1u;
}

// [8] ?Construct@AnimationGroup@Graphics@@UEAAPEAVExternal@Object@@XZ 0x1800165d0
//     the inlined constructor, i.e. placement construction. (Retail's null test
//     sat in the retired door; a virtual call always has a `this`.) `::new`:
//     the class declares its own, undefined, placement operator new.
Object::External* AnimationGroup::Construct() {
    return ::new (this) AnimationGroup();
}

// [6] ?Reconstruct@AnimationGroup@Graphics@@UEAAPEAVExternal@Object@@XZ 0x180016550
//     `vtable[0](this, 0)` then the same in-place construction. Slot 0 is called
//     VIRTUALLY on purpose — retail dispatches it, and a subclass's deleting
//     destructor is what has to run.
Object::External* AnimationGroup::Reconstruct() {
    this->~AnimationGroup();
    return ::new (this) AnimationGroup();
}

// [7] ?Destruct@AnimationGroup@Graphics@@UEAAXXZ  0x180016650 -- `MOV RAX,[RCX]
//     ; XOR EDX,EDX ; JMP [RAX]`: slot 0, dispatched.
void AnimationGroup::Destruct() { this->~AnimationGroup(); }

// [5] ?Serialize@AnimationGroup@Graphics@@UEAA_NAEAVStream@IO@@@Z  0x1800fb6a0
//
// *** THE EMISSION ORDER IS RETAIL'S AND IT IS NOT THE DECLARATION ORDER. ***
// The +0x30 float goes out AFTER the tag array (see the comment at that call).
// Getting it wrong presents as a HANG, not a crash.
bool AnimationGroup::Serialize(IO::Stream& streamv) {
    IO::Stream& stream = streamv;
    IO::Serializer ser(stream, 0);

    // +0x20 Array<Graphics::Animation>
    int32_t n = _clips._size;
    ser._readerWriter->SerializeInt(n, nullptr);
    if (IsReading(stream)) {
        rtcoll::RtColl_ArraySetSize_180059e70(&_clips, n);
    }
    RtGraphics_AnimationGroup_SerializeClips(ser, n, _clips._data);

    // +0x38 Array<{String,String,float}>, stride 0x18.
    n = _tags._size;
    ser._readerWriter->SerializeInt(n, nullptr);
    if (IsReading(stream)) {
        rtcoll::RtColl_ArraySetSize_18005a220(&_tags, n);
    }
    {
        Tag* p = _tags._data;
        for (int32_t i = 0; i < n; ++i, ++p) {           // stride 0x18
            p->_string00.Serialize(ser);
            p->_string08.Serialize(ser);
            ser._readerWriter->SerializeFloat(p->_float10, nullptr);
        }
    }

    // +0x30 scalar float -- AFTER the tag array, not before it. Retail
    // 0x1800fb6a0 orders the emissions clips, tags, +0x30, floats (the
    // `(*rw+0x80)(this+0x30)` call sits between the tag loop and the third
    // count). This body was written with +0x30 before the tags, and because
    // it is reached only through the minted vtable it never ran DLL-present:
    // the first cutover boot to get this far read the tag count out of the
    // float's bytes (count=1288519116), asked CompressedStream::Read for bytes
    // past the end of the file, and spun forever in Read's chunk loop.
    ser._readerWriter->SerializeFloat(_defaultBlend, nullptr);

    // +0x48 Array<float>, bulk kArrayFloat.
    n = _blendTimes._size;
    ser._readerWriter->SerializeInt(n, nullptr);
    if (IsReading(stream)) {
        rtcoll::RtColl_ArraySetSize_18005a410(&_blendTimes, n);
    }
    ser._readerWriter->SerializeFloatArray(n, _blendTimes._data, nullptr);

    return true;   // ~Serializer: `if (rw) delete rw`
}

// ??1AnimationGroup@Graphics@@UEAA@XZ  0x1800fb620 -- retail's body, moved here
// from the retired `RtGraphics_AnimationGroup_Dtor` door
// (deleting_dtor_leaves.cpp, ENDGAME B.3.9 block GFX). The entry restamp is the
// compiler's, and so is the tail into ~External. The three Arrays go in
// retail's order, each through its own ~Array<T> instantiation (the ported
// bodies in src/engine/collection/, by name):
//   0x18001c3b0(+0x48)  ~Array<float>                         (J1)
//   0x18001c2d0(+0x38)  ~Array<{String,String,float}>         (J2, two Strings each)
//   0x18001c1e0(+0x20)  ~Array<Graphics::Animation>           (each clip's ~Animation)
// Each frees AND zeroes its header, so the compiler-emitted ~Array<uint8_t> of
// the three members that runs after this body sees an empty header: no
// element destroy and MemFree(null) -- a no-op.
AnimationGroup::~AnimationGroup() {
    rtcoll::RtColl_ArrayDtor_18001c3b0(&_blendTimes);
    rtcoll::RtColl_ArrayDtorElem_18001c2d0(&_tags);
    rtcoll::RtColl_ArrayDtorAnim_18001c1e0(&_clips);
}

// ?TypeId@AnimationGroup@Graphics@@SAIXZ 0x180016300 -- the static twin of
// slot 2: the minted record's +0x00 until the W3 fold (B39_RULES D6 step 1).

// ?CreateNewObject@AnimationGroup@Graphics@@SAPEAVBase@Object@@XZ 0x180016400
// -- `?MemAlloc@MemoryAllocator` (0x1800c7a50) of 0x58, align 8; retail's null
// branch; the inlined ctor. Replaces the FACTREST row (B39_RULES D2).
Object::Base* AnimationGroup::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(AnimationGroup), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) AnimationGroup();
}
// ?CreateInPlaceObject@AnimationGroup@Graphics@@SAPEAVBase@Object@@PEAX@Z 0x1800164d0
Object::Base* AnimationGroup::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) AnimationGroup();
}

}  // namespace Graphics

// ENDGAME B.3.9 block GFX: AnimationGroup's eight slot doors
// (`RtGraphics_AnimationGroup_Type` .. `_Serialize`) are gone -- their only
// reader was the minted table. The Array<Animation>::Serialize helper
// 0x18005a040 stays registered (a by-address callee, not a slot).

// ============================================================================
// Graphics::ParticleSystem
// ============================================================================
namespace {

// ENDGAME B.3.5 step 5, block H2: batch 7's file-local `struct ParticleSystem`
// USED TO SIT HERE. It is GONE — it moved into
// `src/engine/graphics/Particle.h` as the real `Graphics::ParticleSystem`,
// under retail's own name, with the same offsets, the same `static_assert`s and
// every position name (`f4c`, `f78`, `fac`, …) replaced by the name
// `wave28_lane_partsys.cpp` measured for the same offset as a READER. The only
// fields that keep a position name are the ones no reader has identified. See
// that header's banner for the two-witness argument.
using ParticleSystem = ::Graphics::ParticleSystem;

}  // namespace

// ENDGAME B.3.9 block GFX: ParticleSystem's slot doors (Type, TypeName,
// GetFileVersion, Serialize, Reconstruct, Destruct, CreateSystem, Construct)
// and its ??0 / ??1 doors are gone -- their only readers were the minted
// `??_7ParticleSystem@Graphics@@6B@`, its RT_DELDTOR row and FACTREST. Every
// live ParticleSystem wears the compiler's table.

// ============================================================================
// ??0ParticleSystem@Graphics@@QEAA@XZ — retail 0x1800a5320, ord 302.
//
// BATCH 7 LEFT THIS AS A THUNK ("stays on the DLL / port registry"); batch 7b
// ports the body. It is 60 stores and one RET, no calls, and every value below
// is transcribed from the disassembly — including MSVC's merged 8-byte stores
// at misaligned offsets (+0x74, +0x7c, +0x84, +0xac, +0xb4, +0xc4, +0xd0,
// +0xdc), which are written here as the two 4-byte fields they cover.
//
// FOUR vtable stamps, in retail's order (Object::Base, Object::External,
// Graphics::Particle, Graphics::ParticleSystem) -- the compiler's, since
// ENDGAME B.3.9 block GFX made this body the REAL constructor: `Particle()` in
// the init list is the inlined External() plus Particle's six stores.
//
// THE TWO .rdata DOUBLES WERE READ, NOT RECOGNISED (pefile, raw section bytes):
//     0x180161760 = 00 00 00 00 00 00 f0 bf = -1.0
//     0x1801616b8 = 00 00 00 00 00 00 d0 3f =  0.25
// so +0x58 is `(int64)(-1.0 / _secondsPerTick)` — a NEGATIVE tick count, i.e.
// the "no explicit duration" sentinel — and +0xe8/+0xf0 are both
// `(int64)(0.25 / _secondsPerTick)`, computed by two separate DIVSDs off one
// loaded XMM1 (0x1800a5470 and 0x1800a5484).
// ============================================================================
namespace {

double SecondsPerTick() {
    // ENDGAME B.3.9 SS1 (B39_RULES D7): the class static now, read fresh.
    return ::System::Timer::_secondsPerTick;
}

}  // namespace

// ENDGAME B.3.9 block GFX: batch 7b's `ParticleSystemConstructBody` (the four
// minted stamps and the 60 stores) IS the real constructor now. The stores are
// byte for byte batch 7b's; +0x54 and +0xe4 stay unwritten.
Graphics::ParticleSystem::ParticleSystem() : Particle() {
    ParticleSystem* const self = this;
    const double spt = SecondsPerTick();

    self->_f48 = 5.0f;  self->_rateAccelA = 0.0f;         // 1800a538d  one qword
    self->_rateAccelB = 1.0f;                             // 1800a5395
    self->_durationTicks = static_cast<int64_t>(-1.0 / spt); // 1800a539c..b1
    self->_emitDirX = 0.0f; self->_emitDirY = 0.0f;       // 1800a53b5  one qword
    self->_emitDirZ = 1.0f;                               // 1800a53b9
    self->_spreadX  = 1.0f;                               // 1800a53c0
    self->_spreadY  = 1.0f;                               // 1800a53c7
    self->_spreadZ  = 1.0f; self->_spreadScale = 0.0f;    // 1800a53ce  one qword
    self->_angleBRange = 0.0f; self->_angleARange = 0.0f; // 1800a53d6  one qword
    self->_speedBase = 1.0f; self->_speedRange = 0.0f;    // 1800a53da  one qword
    self->_accelScale = 0.0f;                             // 1800a53e5
    self->_damping = 1.0f; self->_rotBase = 0.0f;         // 1800a53eb  one qword
    self->_rotRange = 0.0f; self->_spinBase = 0.0f;       // 1800a53f6  one qword
    self->_spinRange = 0.0f; self->_spinAccel = 0.0f;     // 1800a53fd  one qword
    self->_spinDamp = 1.0f;                               // 1800a5404
    self->_lifeBase = 3.0f; self->_lifeRange = 0.0f;      // 1800a540e  one qword
    self->_gravScale = 1.0f; self->_gravX = 0.0f;         // 1800a5419  one qword
    self->_gravY = 0.0f;                                  // 1800a5424
    self->_gravZ = -9.8f;                                 // 1800a542a  0xc11ccccd
    self->_sizeBase = 0.5f; self->_sizeRange = 0.0f;      // 1800a5434  one qword
    self->_sizeAccel = 0.0f;                              // 1800a543f
    self->_sizeDamp = 1.0f; self->_colorT0 = 0.0f;        // 1800a5445  one qword
    self->_colorT1 = 1.0f;                                // 1800a5450
    self->_color0 = System::kColorWhite;                  // 1800a545a  one qword
    self->_color1 = System::kColorWhite;                  //            of -1
    self->_fadeInTicks  = static_cast<int64_t>(0.25 / spt); // 1800a5465..7d
    self->_fadeOutTicks = static_cast<int64_t>(0.25 / spt); // 1800a5484..91
    // 1800a5498 / 1800a549f — two qword stores over +0xf8 and +0x100, i.e. the
    // `_pool` Array header zeroed. Batch 7b spelled it `memset(tail, 0, 0x10)`
    // over an unmodelled byte array; the three named fields are the same bytes.
    self->_pool._data     = nullptr;                      // 1800a5498  +0xf8
    self->_pool._size     = 0;                            // 1800a549f  +0x100
    self->_pool._capacity = 0;                            //            +0x104
}

// Construct/Reconstruct/Destruct are vtable slots [9]/[6]/[7]; each is retail's
// `JMP 0x1800a5320` (Construct) / `vtable[0](this,0)` then the same ctor
// (Reconstruct) / a tail call to `vtable[0](this,0)` (Destruct).
//
// ?CreateSystem@ParticleSystem@Graphics@@ — retail 0x1800a5140, ord 2134.
// Eleven instructions:
//
//   1800a5154  RBX = *0x1801fc718 + 0x640          the allocator's own CS
//   1800a516a  EnterCriticalSection(RBX)
//   1800a5183  MemAlloc(_singleton, 0xe0, 8)       0x1800c6e60
//   1800a518e  LeaveCriticalSection(RBX)
//   1800a519c  if (result) 0x1800a55a0(result)     ParticleSystemInstance's ctor
//              else EAX = 0
//   1800a51b5  RET
//
// `this` IS NEVER READ; the returned instance is bound to its system later, by
// the caller. SLOT 8, NOT SLOT 9 (RULE B5-16's trap).

// ############################################################################
// ENDGAME B.3.5 step 5, block H2 — `Graphics::ParticleSystem`'s member bodies.
//
// Their own `namespace Graphics { }` block, after the anonymous namespace above
// has closed, because a member definition has to appear in a namespace ENCLOSING
// the class and `Graphics` / `RtGraphics` are siblings (B34_BRIEFS section A.2).
// The anonymous namespace's helpers — `ParticleSystemTypeInfo`,
// `MakeReaderWriter`, `RwInt`, `RwFloats`, `IsReading`, `RtAddr` — are
// reachable from here unqualified.
//
// `Graphics::Particle` GETS NO BODIES HERE. Its `Serialize` and its three
// type-info forwarders are in `wave27_lane_gfxpart.cpp` (also block H2's), which
// is where that slot has always lived.
// ############################################################################

namespace Graphics {

// [1] ?TypeName@ParticleSystem@Graphics@@UEBAPEBDXZ  0x1800203e0
//     reads `?_typeInfo@ParticleSystem@Graphics@@` + 8 (BaseTypeInfo::name).
// ENDGAME B.3.9 TI-REST: TypeName / Type / TypeId are DECLARE_TYPE's bodies over the class's own
// record (graphics_type_records.cpp).

// [2] ?Type@ParticleSystem@Graphics@@UEBAIXZ  0x1800203c0 — the record's +0x00.

// [3] ?GetFileVersion@ParticleSystem@Graphics@@UEBAHXZ  0x1800a4d10.
//     0x1001 == 4097, and it is NOT `Particle`'s 0x1000 — the two were decoded
//     separately and the difference is why each is its own literal (B35_RULES
//     section 6.1: a wrong GetFileVersion silently drops content).
int32_t ParticleSystem::GetFileVersion() const { return 0x1001; }

// [9] ?Construct@ParticleSystem@Graphics@@UEAAPEAVExternal@Object@@XZ 0x180020580
//     `JMP 0x1800a5320` — the constructor body, in place. SLOT NINE. `::new`:
//     the class declares its own, undefined, placement operator new.
Object::External* ParticleSystem::Construct() {
    return ::new (this) ParticleSystem();
}

// [6] ?Reconstruct@ParticleSystem@Graphics@@UEAAPEAVExternal@Object@@XZ 0x180020560
//     `vtable[0](this, 0)` then the same in-place construction. Slot 0 is called
//     VIRTUALLY on purpose — retail dispatches it, and a subclass's deleting
//     destructor is what has to run.
Object::External* ParticleSystem::Reconstruct() {
    this->~ParticleSystem();
    return ::new (this) ParticleSystem();
}

// [7] ?Destruct@ParticleSystem@Graphics@@UEAAXXZ  0x180020590
void ParticleSystem::Destruct() { this->~ParticleSystem(); }

// ??1ParticleSystem@Graphics@@UEAA@XZ  0x1800a51c0 — retail's body, moved here
// from the retired door in batchc_gfx_leaves.cpp (ENDGAME B.3.9 block GFX):
//
//   for (i = 0; i < this->[0x100]; ++i) {         // bound RE-READ each pass
//       e = ((void**)this->[0xf8])[i];            // base RE-READ each pass
//       if (!e) continue;
//       if (e[0]) MemFree(e[0]);
//       e[0] = 0; e[1] = 0;
//       MemFree(e);
//   }
//   if (this->[0xf8]) MemFree(this->[0xf8]);
//   this->[0xf8] = 0; this->[0x100] = 0;          // ONE qword: size AND capacity
//   tail ~Particle(this)
//
// i.e. an `Array<Array<T>*>` of OWNED inner arrays. The restamp is the
// compiler's, and so is the tail into ~Particle. The member `_pool`'s implicit
// ~Array then sees the zeroed header: a no-op.
ParticleSystem::~ParticleSystem() {
    for (int32_t i = 0; i < _pool._size; ++i) {
        RtGraphics::ParticleEmitter* const e = _pool._data[i];
        if (e == nullptr) continue;
        if (e->_extra._data != nullptr) rtcoll::MemFree(e->_extra._data);
        e->_extra._data     = nullptr;
        e->_extra._size     = 0;                          // e[1]: _size AND _capacity
        e->_extra._capacity = 0;
        rtcoll::MemFree(e);
    }
    if (_pool._data != nullptr) rtcoll::MemFree(_pool._data);
    _pool._data     = nullptr;
    _pool._size     = 0;                                  // _size AND _capacity
    _pool._capacity = 0;
}

// ?TypeId@ParticleSystem@Graphics@@SAIXZ 0x1800203d0 -- the static twin of slot
// 2: the minted record's +0x00 until the W3 fold (B39_RULES D6 step 1).

// ?CreateNewObject@ParticleSystem@Graphics@@SAPEAVBase@Object@@XZ 0x1800204d0
// -- the FACTREST row it replaces called ??0ParticleSystem after the locked
// MemAlloc(0x108, 8); retail's null branch (B39_RULES D2).
Object::Base* ParticleSystem::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ParticleSystem), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) ParticleSystem();
}
// ?CreateInPlaceObject@ParticleSystem@Graphics@@SAPEAVBase@Object@@PEAX@Z 0x180020550
Object::Base* ParticleSystem::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) ParticleSystem();
}

// [8] ?CreateSystem@ParticleSystem@Graphics@@UEBAPEAVParticleInstance@2@XZ
//     retail 0x1800a5140 -- ENDGAME B.3.9 GFX: DEFINED in
//     wave28_lane_partsys.cpp, which can include ParticleInstance.h (this TU's
//     `<windows.h>` cannot); the by-address ??0ParticleSystemInstance door it
//     called here is retired.

// [5] ?Serialize@ParticleSystem@Graphics@@UEAA_NAEAVStream@IO@@@Z  0x1800a4d20.
//
// *** THE EMISSION ORDER IS RETAIL'S AND IT IS NOT THE DECLARATION ORDER. ***
// `_spreadScale` (+0x78) goes out BEFORE `_spreadX/Y/Z` (+0x6c), which is why the
// header declares the fields in ascending memory order and the order lives here.
bool ParticleSystem::Serialize(IO::Stream& streamv) {
    IO::Stream& stream = streamv;

    // `Particle::Serialize` (0x180103f00) — the base slot. Retail's `CALL` is a
    // direct call of the base body; until ENDGAME B.3.9 this reached it BY
    // ADDRESS through the registered door. Block GFX retired that door with the
    // minted `??_7Particle@` (its only other reader), so the call is the
    // qualified member -- the same body, no registry hop. The bool is discarded,
    // as retail discards it.
    (void)Particle::Serialize(streamv);

    IO::Serializer ser(stream, 0);
    IO::ReaderWriter* rw = ser._readerWriter;

    rw->SerializeFloat(_f48, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_rateAccelA, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_rateAccelB, nullptr);
    // ENDGAME B.3.5 block H2 — THE FIRST OF THE THREE HAND-INLINED int64 SLOT
    // CALLS B35_RULES section 11 listed in this file as "receiver-side work".
    // Block H2 owns this receiver, and `IO::Serializer::Serialize(int64_t&, const
    // char*)` @ 0x180035000 is a real member, so it is spelled as a member call.
    // That member's own body still calls the ReaderWriter's +0x90 slot
    // (Serializer.h section 3.1: the 22 members keep their raw slot calls until
    // the minted tables retire), so this is a spelling change and nothing else.
    ser.Serialize(_durationTicks, nullptr);
    rw = ser._readerWriter; rw->SerializeFloatArray(3, &_emitDirX, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_spreadScale, nullptr); // retail's order: 0x78 before 0x6c
    rw = ser._readerWriter; rw->SerializeFloatArray(3, &_spreadX, nullptr);

    // The fifteen consecutive float scalars, +0x7c..+0xb4. Written out one field
    // at a time rather than walked as `float*`: retail emits fifteen separate
    // scalar calls, the names are what a reader needs, and `Particle.h` carries a
    // `static_assert` that the run is still contiguous.
    rw = ser._readerWriter; rw->SerializeFloat(_angleBRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_angleARange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_speedBase, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_speedRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_accelScale, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_damping, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_rotBase, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_rotRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_spinBase, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_spinRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_spinAccel, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_spinDamp, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_lifeBase, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_lifeRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_gravScale, nullptr);

    rw = ser._readerWriter; rw->SerializeFloatArray(3, &_gravX, nullptr);

    rw = ser._readerWriter; rw->SerializeFloat(_sizeBase, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_sizeRange, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_sizeAccel, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_sizeDamp, nullptr);

    rw = ser._readerWriter; rw->SerializeUInt(_color0, nullptr);
    rw = ser._readerWriter; rw->SerializeUInt(_color1, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_colorT0, nullptr);
    rw = ser._readerWriter; rw->SerializeFloat(_colorT1, nullptr);
    // The second and third int64 sites — same conversion, same reason.
    ser.Serialize(_fadeInTicks, nullptr);
    ser.Serialize(_fadeOutTicks, nullptr);

    // READ-ONLY tail (retail 0x1800a505c..0x1800a5107): when reading and the
    // resource is active (Particle::_active, +0x20), rebuild the pool-pointer
    // array at self+0xf8 to Particle::_f24 entries and allocate one 0x18-byte
    // record per index.
    //
    // *** BATCH 7 GOT TWO THINGS WRONG HERE AND BATCH 7b FIXES BOTH, at the
    // source (PLAN.md §6 step 8). *** The old comment said 0x180020e90 is a
    // "record ctor"; it is not. Both addresses are Collection::Array<T>::SetSize
    // bodies of this tree's J3 family, and both are now PORTED AND REGISTERED
    // (src/engine/collection/array.cpp: kSpec_Fill8 for the stride-8 pointer
    // array, kSpec_Rec14 for the record's own stride-0x14 inner array). So the
    // second call is `SetSize(&record->innerArray, ParticleSystem::_f28)`, and
    // these two ResolveAddr calls now land on OUR bodies rather than the DLL's.
    //
    // And the dword at record+0x10 is a RAW BIT COPY of self->+0x48
    // (0x1800a50df `MOV EAX,dword ptr [RSI+0x48]` / `MOV [RDI+0x10],EAX`), NOT
    // a float-to-int conversion. Batch 7 wrote `static_cast<int32_t>(self->f48)`,
    // which for the ctor's default 5.0f stores 5 where retail stores
    // 0x40a00000. Transcribed as a memcpy so the width and the bits both match.
    //
    // The allocation goes through `rtcoll::MemAlloc` (array.cpp's accessor,
    // declared in types/collection/Collection.h), which IS retail's
    // Enter/Allocate(0x18, align 8)/Leave triple on the same singleton, rather
    // than re-opening address doors onto 0x1801fc718+0x640 / 0x18014b390 /
    // 0x1800c6e60 / 0x18014b388 that array.cpp already owns.
    if (IsReading(stream) && _active != 0) {
        rtcoll::RtColl_ArraySetSize_180021220(&_pool, _recordCount);    // 1800a507a
        RtGraphics::ParticleEmitter** poolArr = _pool._data;            // 1800a50f1
        for (int32_t i = 0; i < _recordCount; ++i) {                    // 1800a5103
            // The raw 0x18-byte block viewed as the record the stores build
            // (no C++ constructor runs: retail's are these stores).
            RtGraphics::ParticleEmitter* rec =
                static_cast<RtGraphics::ParticleEmitter*>(rtcoll::MemAlloc(0x18, 8));
            if (rec) {                                                  // 1800a50d4
                rec->_extra._data     = nullptr;                        // 1800a50d8/db
                rec->_extra._size     = 0;
                rec->_extra._capacity = 0;
                rec->_rate = _f48;                                      // 1800a50df/e2 (a dword move)
                rtcoll::RtColl_ArraySetSize_180020e90(&rec->_extra, _subCount);  // 1800a50eb
            }
            // Retail stores RDI unconditionally, so a failed allocation parks a
            // null in the slot rather than skipping the index.
            poolArr[i] = rec;                                           // 1800a50f8
        }
    }

    return true;   // ~Serializer: `if (rw) delete rw`
}

}  // namespace Graphics
