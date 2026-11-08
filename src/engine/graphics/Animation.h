#pragma once
// ============================================================================
// src/engine/graphics/Animation.h — Wave 6 lane G — Graphics::AnimationEvent
// / Graphics::AnimationManager binary layouts
// ============================================================================
//
// Every offset below is MEASURED: read out of Runtime-x64.dll's disassembly
// with ghidra-mcp (program="Runtime-x64.dll"), not guessed from the decompile
// alone. The function that pins each field is named next to it, and every
// field carries a static_assert.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <type_traits>

// ENDGAME B.3.5 block H1: the real `Graphics::AnimationEvent` derives from the
// real `Object::External`, so the class header comes in at GLOBAL scope, above
// every namespace block (B34_BRIEFS section A.8).
#include "../object/External.h"
#include "../object/Internal.h"      // B.3.9: InternalMemFree (the concrete slot 0)
#include "../collection/Collection.h"
#include "../math/Math.h"            // B.4: Array<Math::Transform> _keys

// Forward declarations only, for the members that are DECLARED-not-ported. None
// of them is used by value in a defined body here, so no header is dragged in;
// `System::Time` is deliberately NOT included, because `src/engine/system/
// Value.h` pulls `engine/types/common.h`'s GLOBAL `struct Time` along with it
// (the trap `src/engine/engine/UpdateLoop.h:56-60` records). Tags match the
// real declarations — `class`, not `struct` — per RULE B4b-5.
namespace System { class String; class Time; }
namespace Math { class Transform; }
namespace IO { class Serializer; }


// ============================================================================
// Graphics::AnimationEvent — 0x20 bytes (the base class only; subclasses such
// as AnimAudioEvent, which is already ported in
// src/game/Component/animaudioevent.cpp, add their own fields after it).
//
// Pinned by the ctor (retail 0x1800a2150, disassembled): three instructions
// stamp intermediate Object::Base / Object::External vtables (0x180154788,
// 0x1801547a0) that are immediately overwritten and therefore have NO
// observable effect on the constructed object -- Ghidra's decompile shows
// them as separate `undefined ***` stores because MSVC folded the trivial
// Object::External::External() base ctor inline rather than calling it. The
// LAST vtable store (0x180154bf0, AnimationEvent::_vftable_) is the only one
// that survives, together with the three zeroed qwords at +0x08/+0x10/+0x18.
// The dtor (retail 0x1800a2120) confirms the same vtable constant and then
// TAIL-JMPs to Object::External::~External (0x1800bc950) -- a real,
// non-inlined call into an unexported internal this lane does not own the
// layout of, so it is invoked at its retail address rather than reproduced.
// ============================================================================

// ============================================================================
// Graphics::AnimationManager — 0x9d8 bytes (AllocSub(0x9d8, 8) at
// gameupdateloop_load.cpp step 22, matching this layout's 0x9d4 content
// rounded to 8-byte alignment).
//
// NO VTABLE: both ctor (??0...QEAA@XZ) and dtor (??1...QEAA@XZ) are QEAA,
// i.e. non-virtual, and the ctor never stores a vtable pointer. This matches
// the Picking::Scene precedent (reference: WAVE5_LANE_D.md §4) -- plain data,
// safe to half-port because nothing dispatches into it virtually.
//
// Pinned by the ctor (retail 0x1800fc430, decompiled): it zeroes +0x00..+0x13
// (three stores: 8 + 8 + 4 bytes) then seeds a 624-entry uint32 array
// starting at +0x14 with the textbook Mersenne-Twister `init_genrand` loop
// (`x[0] = (uint)_time64(NULL); x[i] = (1812433253 * (x[i-1] ^ (x[i-1]>>30)))
// + i` for i in 1..623 -- 0x6c078965 = 1812433253 decimal, 0x26f = 623
// decimal), ending at +0x14 + 624*4 = +0x9d4. It finishes by storing `this`
// into the exported static `Graphics::AnimationManager::_singleton`
// (0x1801fe7b0, ordinal 4799).
//
// ----------------------------------------------------------------------------
// CORRECTION, WAVE 23 LANE GFXBODY: +0x00/+0x08/+0x0c ARE NOT A LOCK.
//
// This header used to call them a "CRITICAL_SECTION-shaped, never initialised"
// triple and called the dtor's helper "a defensive no-op call against a lock
// this class never actually initialises". Both statements are WRONG, and the
// wrongness was invisible while Update and Serialize were held back, because
// nothing else in the tree read those twelve bytes.
//
// They are a `Collection::Array<AnimationGroup>` — _data / _size / _capacity —
// and the proof is in `?Update@AnimationManager@Graphics@@` (0x1800fc240),
// which this lane disassembled:
//
//     1800fc268:  CMP dword ptr [RCX + 0x8],R14D   ; +0x08 is a LOOP BOUND
//     1800fc26c:  JLE <return>
//     1800fc2a0:  MOV RBP,qword ptr [R15]          ; +0x00 is an ARRAY BASE
//                                                   ; (elements of stride 0x30)
//
// A LockCount is not a loop bound and a DebugInfo pointer is not an array base.
// The dtor's helper, read the other way round, says the same thing: FUN_18005bf60
// destroys each element, frees the block at +0x00, then zeroes +0x00/+0x08/+0x0c
// — that is `Collection::Array<T>::~Array`, and Ghidra typed its parameter
// LPCRITICAL_SECTION only because the free happens under the ALLOCATOR's own
// critical section. So the dtor's call is load-bearing whenever any group has
// been registered, not a no-op.
//
// NO BEHAVIOURAL CHANGE FOLLOWS. animation.cpp's ported ctor zeroes the same
// twelve bytes it always did (a zeroed Array is an empty Array) and its ported
// dtor still delegates to the real helper at its retail address, which is
// correct under either reading. Only the NAMES and the explanation change —
// and they are what a future lane would have built on.
//
// The Mersenne-Twister block at +0x14 is unaffected and stands as decompiled.
// ----------------------------------------------------------------------------
//
// PLAN §4.1 decision 1 (no <windows.h> outside a _win twin) is still why
// animation.cpp calls FUN_18005bf60 / FUN_18005c550 at their retail addresses
// rather than reproducing them.
//
// *** DO-NOT-DELETE NOTE — checked by tools/check_standing_notes.py. ***
// A cleanup pass that removes this paragraph, or the `UNVERIFIED` marker on
// _f10 below, fails that checker. It is here because the note has already been
// re-opened once on a guess.
//
// +0x10 is a fourth dword no body this project has read touches: not the ctor,
// not the dtor, and not Update (which reads only +0x00 and +0x08). Left
// UNVERIFIED.
//
// WAVE 23 lane GFXBODY's residual R4 asked whoever ported
// `?Serialize@AnimationManager@Graphics@@` (0x1800fc190) to settle this field,
// on the guess that Serialize probably touches it. WAVE 24 lane GFXMAT ported
// that body and ANSWERED R4: **no**. The only AnimationManager fields it reads
// are +0x00 (_data) and +0x08 (_size) -- see the transcription above
// `Graphics::AnimationManager::Serialize` in
// src/engine/graphics/wave24_lane_gfxmat.cpp. So the premise that used to
// stand here -- "Serialize is still unported" -- is no longer true, and the
// VERDICT is unchanged: +0x10 stays UNVERIFIED, and the body that was named as
// its answer is not its answer. Do not re-open it on the same guess.
// ============================================================================


// ############################################################################
// ENDGAME B.3.5 step 5, block H1 — the REAL `Graphics::AnimationEvent` and
// `Graphics::Animation`.
//
// The two `RtGraphics::` PODs above KEEP LIVING (B4b-1): every live object in
// the process is still built by the ported ctor doors in
// `src/engine/graphics/animation.cpp` and wears the MINTED vtable, and the
// doors reach these classes through a `reinterpret_cast` plus a QUALIFIED,
// non-virtual call. A converted object and a minted-vtable object are
// slot-for-slot interchangeable; `tests/unit/graphics_animation_abi_test.cpp`
// is the regression test in both directions.
//
// NOTHING IS RETIRED HERE. `RT_DELDTOR(0x180011800, 0x1800a2120, 0x020,
// "Graphics::AnimationEvent")` (src/engine/object/deleting_dtor.cpp:299), the
// `??_7AnimationEvent@Graphics@@6B@` mint row and the two `_typeInfo` reflection
// rows all stay minted, per B35_RULES section 11. (ENDGAME B.3.9 block GFX
// retired the RT_DELDTOR row and the vtable row; the `_typeInfo` row waits for
// the W3 fold.)
//
// *** GREP HAZARD, ARMED BY THIS FILE (B4b-5 / B34_BRIEFS A.7). *** Nothing in
// `src/engine/graphics/` opened `namespace Graphics` before this commit.
// Inside a `namespace Graphics {` block a bare `AnimationEvent`, `Animation`,
// `AnimationGroup`, `AnimationState` or `AnimationManager` now binds the CLASS.
// Always spell the POD `RtGraphics::X` there.
// ############################################################################

namespace Graphics {

// ============================================================================
// Graphics::AnimationEvent — ABSTRACT, 0x20 bytes, parent `Object::External`.
//
// The parent edge is PROVED: `?_ParentTypeName@AnimationEvent@Graphics@@
// CAPEBDXZ` @ 0x1800116c0 returns the literal "Object::External"
// (B35_SLOTS.md:497, read byte-wise).
//
// `??_7AnimationEvent@Graphics@@6B@` @ 0x180154bf0, NINE slots, read out of
// `.rdata` one qword at a time (B35_SLOTS.md:30-39):
//
//   [0] +0x00  0x180011800  (unexported) the vector deleting dtor
//   [1] +0x08  0x1800116f0  ?TypeName@AnimationEvent@Graphics@@UEBAPEBDXZ
//   [2] +0x10  0x1800116d0  ?Type@AnimationEvent@Graphics@@UEBAIXZ
//   [3] +0x18  _purecall    GetFileVersion   — inherited pure from External
//   [4] +0x20  0x1800066e0  ?Compile@External@Object@@ — INHERITED BODY
//   [5] +0x28  _purecall    Serialize        — inherited pure
//   [6] +0x30  _purecall    Reconstruct      — inherited pure
//   [7] +0x38  _purecall    Destruct         — inherited pure
//   [8] +0x40  _purecall    Construct        — INTRODUCED HERE, and pure
//
// RULE B5-16: `Construct` is NOT inherited. `??_7External@Object@@6B@` and
// `??_7CompiledExternal@Object@@6B@` are eight entries each, so every
// Video/Graphics external introduces the slot itself — hence `virtual`, not
// `override`, and hence the ninth entry on a class that overrides nothing new.
//
// SLOT 0 IS LEFT PURE, which keeps the class abstract (B34_BRIEFS A.4's shape,
// as amended: abstract where nothing holds the class by value, and nothing
// holds an AnimationEvent by value anywhere in the tree). Its body is
// `rtdtor::RtDelDtor_AnimationEvent`, registered at 0x180011800 in
// deleting_dtor.cpp, which section 11 forbids retiring; declaring it `= 0` here
// re-declares the slot at the right index without touching that row and without
// giving the class a second, divergent deleting destructor.
//
// NO REAL CONSTRUCTOR (B4b-2 / ENDGAME section B.1 decision 3). `??0Animation
// Event@Graphics@@QEAA@XZ` is called on RAW MEMORY by
// `src/game/Component/animaudioevent.cpp:45`, which then stamps its own
// subclass vtable; a member constructor there would stamp GCC's table on an
// object the rest of the tree expects to be wearing the minted one. The door in
// animation.cpp stays a free function and is NOT a delegation.
//
// THE DESTRUCTOR IS DUPLICATED, NOT DELEGATED, and that is the `Object::
// External` precedent verbatim (src/engine/object/external_class.cpp:152 vs
// src/engine/object/external.cpp's `RtObject_External_Dtor`). Retail's
// 0x1800a2120 is two instructions: stamp AnimationEvent's own vtable, then
// TAIL-JMP `~External`. A real destructor chain emits BOTH for free — and it
// must not be hand-written, because a hand-written store would name the MINTED
// table while a converted object carries the compiler's. So the member body is
// empty and the existing door, which stamps the minted table for minted
// objects, is left exactly as it is.
// ============================================================================
// ENDGAME B.3 step 9 (block GFX, B39_RULES D1/D2) -- the paragraphs above about
// slot 0, the ctor door and the destructor door are HISTORY. Slot 0 is now the
// class's own vector deleting dtor; the class stays ABSTRACT only through the
// pure External slots and `Construct` (the exe's AnimAudioEvent fills them).
// The ??0 / ??1 doors, the Type / TypeName slot doors, the FACTREST row, the
// RT_DELDTOR row 0x180011800 and the minted `??_7AnimationEvent@Graphics@@6B@`
// are retired (tools/rt_mint_retired/gfx.txt). The only builder is the exe's
// AnimAudioEvent, whose C++ base ctor is the protected one below.
class AnimationEvent : public Object::External {
public:
    // [0] 0x180011800 -- retail's VECTOR deleting destructor (`MOV EDX,0x20` =
    // the element size); every exe subclass has its own scalar slot 0.
    // ??1AnimationEvent@Graphics@@UEAA@XZ  0x1800a2120. Virtual (B3X W1).
    ~AnimationEvent() override;

    // [1] 0x1800116f0 TypeName / [2] 0x1800116d0 Type (the class's record,
    // retail 0x1801f9978), TypeId 0x1800116e0, `_TypeName` / `_ParentTypeName`
    // and the factory pair: retail's macro (ENDGAME B.3.9 TI-REST, B39_RULES
    // D6); the record is `DEFINE_TYPE`d in
    // src/engine/graphics/graphics_type_records.cpp.
    DECLARE_TYPE(AnimationEvent)
public:

    // [8] +0x40 — `_purecall` in retail. RULE B5-16: introduced, so `virtual`.
    // The event's ACTION, fired by Animation::ApplyEvents: retail's one caller
    // 0x1800faa53..5e passes RDX = the `const Math::Transform&` ApplyEvents
    // received (R13) and ignores RAX; the one override, the exe's AnimAudioEvent
    // 0x140087e40, keeps RDX (MOV RBX,RDX) and reads the translation at
    // +0x10/+0x14/+0x18, returning nothing. The name `Construct` is the
    // Runtime's historical spelling (the slot is pure: retail has no symbol).
    virtual void Construct(const Math::Transform& transform) = 0;

    // ?CreateNewObject@AnimationEvent@Graphics@@SAPEAVBase@Object@@XZ 0x180011700
    // ?CreateInPlaceObject@AnimationEvent@Graphics@@SAPEAVBase@Object@@PEAX@Z 0x180011710
    // Retail's abstract factory pair: both `XOR EAX,EAX ; RET` (animation.cpp);
    // declared by DECLARE_TYPE above.

    // ??0AnimationEvent@Graphics@@QEAA@AEBV01@@Z  0x180011720  — NOT PORTED
    // ??4AnimationEvent@Graphics@@QEAAAEAV01@AEBV01@@Z 0x180011790 — NOT PORTED
    AnimationEvent(const AnimationEvent&);
    AnimationEvent& operator=(const AnimationEvent&);

protected:
    // ??0AnimationEvent@Graphics@@QEAA@XZ  0x1800a2150 — the REAL constructor
    // (ENDGAME B.3.8 block RB, RULE B8-1). Retail's body is the inlined
    // External() (three zero stores) and the stamps, NOTHING else — so the
    // compiler-emitted chain is the whole port and the body is empty; the
    // door `RtGraphics_AnimationEvent_Ctor` has no field of its own to share.
    AnimationEvent() {}

};
static_assert(sizeof(AnimationEvent) == 0x20,
              "Graphics::AnimationEvent is 0x20 bytes -- the MOV EDX,0x20 in its "
              "own vector deleting destructor at 0x180011800");
static_assert(alignof(AnimationEvent) == 8, "Graphics::AnimationEvent aligns to 8");

// ============================================================================
// Graphics::Animation — NON-POLYMORPHIC, 0x70 bytes. One baked animation clip.
//
// *** B35_RULES section 13 item 2 SAID THIS LAYOUT WAS UNDOCUMENTED. IT IS
// DOCUMENTED HERE, AND THE SIZE IS MEASURED THREE INDEPENDENT WAYS. ***
//
//  (a) EMBEDDING. `?GetAnimation@AnimationGroup@Graphics@@QEBAAEBVAnimation@2@
//      H@Z` @ 0x180016660 is the `imul $0x70` element helper over
//      `AnimationGroup::_clips` — i.e. an `Animation` is an ELEMENT of that
//      array at stride 0x70, and the mangled return type names the element type
//      `Animation` outright. `Array<Animation>::SetSize` (0x180059e70) and
//      `Array<Animation>::Serialize` (0x18005a040) carry the same stride, and
//      `src/engine/collection/array.cpp`'s row for it is `kSpec_AnimClip70`.
//  (b) THE DEFAULT CONSTRUCTOR'S STORE LIST. `??0Animation@Graphics@@QEAA@XZ`
//      @ 0x1800fb1d0, disassembled this pass, writes +0x00 q, +0x08 q, +0x10 q,
//      +0x18 q, +0x20 q, +0x28 dword=1, +0x2c byte=0, +0x30 q, +0x38 dword,
//      +0x3c byte, +0x40 q, +0x48 q, +0x50 dword, +0x58 q, +0x60 q, +0x68 q,
//      and stops. Last byte written is +0x6f.
//  (c) `AnimationState::Update`'s own clip indexing: `IMUL RBP,RBP,0x70` at
//      0x1800fdcf4 (recorded in AnimationState.h as `AnimationClipPrefix`,
//      which is a 0x60 PREFIX of this record and stays where it is).
//
// THE THREE GAPS RETAIL NEVER WRITES, kept as explicit holes rather than
// initialised (the B5-13 class of divergence): [0x2d,0x30), [0x3d,0x40) and
// [0x54,0x58). A constructor with an init-list covering every member would
// write them; this class has no constructor at all, which is the same outcome
// for a stronger reason.
//
// EVERY FIELD BELOW HAS A NAMED READER, and eleven of them were read from the
// retail disassembly THIS PASS rather than inherited:
//   0x180015f10  GetName            MOV RAX,[RCX+0x10]
//   0x180015f50  GetMeshName        MOV RAX,[RCX+0x18]
//   0x180015f90  GetSourceName      MOV RAX,[RCX+0x20]
//   0x180016030  GetInstanceCount   MOV EAX,[RCX+0x28]
//   0x180016020  IsInstanced        MOVZX EAX,byte [RCX+0x2c]
//   0x180015fd0  GetStartTime       MOV EAX,[RCX+0x30]
//   0x180015fe0  GetEndTime         MOV EAX,[RCX+0x34]
//   0x180015ff0  GetRate            MOVSS XMM0,[RCX+0x38]
//   0x180016010  IsLooping          MOVZX EAX,byte [RCX+0x3c]
//   0x180016000  GetLength          MOV RAX,[RCX+0x58]   (System::Time, retslot)
//   0x180016080  GetEventCount      MOV EAX,[RCX+0x8]
//   0x180016090  GetEvent           (RCX) + EDX*0x10     -> stride 0x10
//   0x180016040  GetLoopPoint       ((+0x40 - +0x30) / (+0x34 - +0x30))
//                                   * (float)+0x50 * +0x48
//
// GetLoopPoint is what settles TWO names that AnimationState.h and
// AnimationManager.h disagreed about. It divides a FRAME DIFFERENCE by a FRAME
// SPAN, multiplies by `cvtdq2ps(+0x50)` and then by `+0x48` — so +0x50 is an
// INTEGER COUNT (AnimationManager.h's reading: the frame count) and +0x48 is a
// float SCALE, which is what AnimationState.h called `loopScale`. Each header
// was right about one of them. +0x44 is the frames-per-second
// (AnimationManager.h's `anim_clip::kFrameRate`, from GetPoseAtTime); +0x38 is
// `GetRate`'s own float and is a DIFFERENT field.
//
// NO REAL CONSTRUCTOR OR DESTRUCTOR IS DEFINED. Both are declared for the
// record (retail exports them) and left undefined: nothing in this tree calls
// `??0Animation@Graphics@@QEAA@XZ`, and `??1Animation@Graphics@@QEAA@XZ` is
// already a registered door in `src/engine/graphics/batchc_gfx_leaves.cpp`,
// which is NOT this block's file set. An uncalled port is an unverified port.
// ============================================================================
class Animation {
public:
    // ?GetEvent@Animation@Graphics@@QEBAAEBUEvent@12@H@Z — the mangled name
    // spells `@U`, so retail's nested type is a STRUCT named `Event`, and
    // 0x180016090's `SHL RAX,0x4` pins its stride at 0x10.
    struct Event {
        int32_t  _frame;      // +0x00  serialized as an int by 0x18005a040
        int32_t  _pad04;      // +0x04
        AnimationEvent* _event;  // +0x08  Object::ExternalPtr<AnimationEvent>
    };

    // ---- the members retail exports, in mangled-name order ------------------
    // Addresses are `standalone/rt_export_addrs.inc`'s. "door" names the file
    // that owns the body; a member marked NOT PORTED is declared and
    // deliberately left undefined (nothing calls it).

    // ??0Animation@Graphics@@QEAA@XZ                0x1800fb1d0  NOT PORTED
    Animation();
    // ??0Animation@Graphics@@QEAA@AEBV01@@Z         0x1800160a0
    //   door: src/engine/graphics/batchc_gfx_leaves.cpp (not this file set)
    Animation(const Animation&);
    // ??1Animation@Graphics@@QEAA@XZ                0x1800fb290
    //   door: src/engine/graphics/batchc_gfx_leaves.cpp (not this file set)
    ~Animation();
    // ??4Animation@Graphics@@QEAAAEAV01@AEBV01@@Z   0x180016190  NOT PORTED
    Animation& operator=(const Animation&);

    // ?ApplyEvents@   0x1800fa9a0  PORTED — the body IS this member, in
    //   `wave28_lane_gfxbody.cpp`'s `namespace Graphics` block, and the registered
    //   door delegates (ENDGAME B.3.5 block GC). This note used to read "door:
    //   wave28_lane_gfxbody.cpp (not ours)", which was true of block H1 and is no
    //   longer true of the tree.
    void ApplyEvents(const Math::Transform& base, const System::Time& from,
                     const System::Time& to) const;
    // ?GetEndTime@    0x180015fe0  NOT PORTED  (`MOV EAX,[RCX+0x34]`)
    int32_t GetEndTime() const;
    // ?GetEvent@      0x180016090  NOT PORTED
    const Event& GetEvent(int index) const;
    // ?GetEventCount@ 0x180016080  NOT PORTED
    int32_t GetEventCount() const;
    // ?GetInstanceCount@ 0x180016030  NOT PORTED
    int32_t GetInstanceCount() const;
    // ?GetLength@     0x180016000  NOT PORTED — hidden retslot (System::Time)
    System::Time GetLength() const;
    // ?GetLoopPoint@  0x180016040  NOT PORTED
    float GetLoopPoint() const;
    // ?GetMeshName@   0x180015f50  NOT PORTED — hidden retslot (System::String)
    System::String GetMeshName() const;
    // ?GetName@       0x180015f10  NOT PORTED — hidden retslot
    System::String GetName() const;

    // ?GetPoseAtTime@Animation@Graphics@@QEBAXAEBVTime@System@@0PEAVTransform@
    // Math@@H@Z   0x1800faca0 — PORTED HERE. The body moved out of
    // `src/engine/graphics/animationstate.cpp`'s
    // `RtGraphics_Animation_GetPoseAtTime` into this member; that door keeps its
    // registration and both addresses and is now a one-line qualified call.
    //
    // THE FOURTH ARGUMENT IS NEVER READ (the bone count comes from `_boneCount`
    // at +0x4c, not from the caller). Kept because the registered entry point
    // must have retail's arity, not a tidied one.
    void GetPoseAtTime(const System::Time& time, const System::Time& delta,
                       Math::Transform* out, int unused) const;

    // ?GetRate@       0x180015ff0  NOT PORTED
    float GetRate() const;
    // ?GetRootPoseAtTime@ 0x1800faaa0  PORTED — hidden retslot (Math::Transform).
    //   This note read "door: wave29_lane_gfxbody.cpp (not ours)", which was true
    //   of block H1 and stale by the time block GC moved the body: it is a member
    //   now, defined in `src/engine/graphics/wave29_lane_gfxbody.cpp`, and the
    //   door in that file delegates. ENDGAME B.3.5's closing pass converted its
    //   two remaining runtime-internal callers (wave28_lane_gfxbody.cpp) onto it.
    Math::Transform GetRootPoseAtTime(const System::Time& time,
                                      const System::Time& delta) const;
    // ?GetSourceName@ 0x180015f90  NOT PORTED — hidden retslot
    System::String GetSourceName() const;
    // ?GetStartTime@  0x180015fd0  NOT PORTED
    int32_t GetStartTime() const;
    // ?IsInstanced@   0x180016020  NOT PORTED
    bool IsInstanced() const;
    // ?IsLooping@     0x180016010  NOT PORTED
    bool IsLooping() const;
    // ?Serialize@     0x1800fb080  NOT PORTED (the CLIP's own serializer; the
    //   ARRAY element loop 0x18005a040 IS ported, in
    //   vtbatch7_animgroup_particlesystem.cpp)
    void Serialize(IO::Serializer& s);
    // ?Set@Animation@Graphics@@QEAAXHHPEBVTransform@Math@@@Z  0x1800fafa0
    //   NOT PORTED
    void Set(int boneCount, int frameCount, const Math::Transform* keys);

    // ---- the 0x70 bytes ----------------------------------------------------
    // PUBLIC for the same reason `IO::Serializer`'s two fields are: unconverted
    // bodies across the tree reach them at these offsets through a cast, and
    // `offsetof` on them has to be well formed for the pins below.

    // +0x00  Collection::Array<Event>, stride 0x10 (GetEvent 0x180016090,
    //        GetEventCount 0x180016080 reads `_size` at +0x08).
    // B.4: typed; ~Animation / Animation() are declared and never defined, so
    // no Array member ctor / dtor is instantiated through this class.
    Collection::Array<Event> _events;

    // B.4 Y RT (`behaviour:`): the three names are the real `System::String`.
    // An Animation is built, copied and destroyed only by `Array<Animation>`'s
    // registered row (kSpec_AnimClip70: init 0x180059f2d, copy 0x1800160a0 /
    // RtGraphics_Animation_CopyCtor, ~Animation 0x1800fb290 /
    // RtGraphics_Animation_Dtor, which releases +0x20, +0x18, +0x10 in that
    // order); the C++ ctor / dtor stay undefined, so each name is released once.
    System::String _name;          // +0x10  GetName 0x180015f10
    System::String _meshName;      // +0x18  GetMeshName 0x180015f50
    System::String _sourceName;    // +0x20  GetSourceName 0x180015f90
    int32_t  _instanceCount; // +0x28  GetInstanceCount; ctor default 1
    bool     _instanced;     // +0x2c  IsInstanced 0x180016020
    uint8_t  _pad2d[3];      // +0x2d  NEVER WRITTEN by the ctor — a real hole
    int32_t  _startFrame;    // +0x30  GetStartTime 0x180015fd0
    int32_t  _endFrame;      // +0x34  GetEndTime   0x180015fe0
    float    _rate;          // +0x38  GetRate      0x180015ff0
    bool     _looping;       // +0x3c  IsLooping    0x180016010
    uint8_t  _pad3d[3];      // +0x3d  NEVER WRITTEN — a real hole
    int32_t  _loopFrame;     // +0x40  anim_clip::kLoopFrame; 0 short-circuits
    float    _frameRate;     // +0x44  anim_clip::kFrameRate, frames per second
    float    _loopScale;     // +0x48  GetLoopPoint's final multiplier
    int32_t  _boneCount;     // +0x4c  anim_clip::kBoneCount
    int32_t  _frameCount;    // +0x50  anim_clip::kFrameCount
    uint8_t  _pad54[4];      // +0x54  NEVER WRITTEN — a real hole
    int64_t  _duration;      // +0x58  System::Time ticks; GetLength 0x180016000
    // +0x60  Collection::Array<Math::Transform>, stride 0x1c, _boneCount *
    //        _frameCount keys (anim_clip::kKeys, 0x1800fade4).
    Collection::Array<Math::Transform> _keys;
};
static_assert(sizeof(Animation) == 0x70,
              "Graphics::Animation is 0x70 bytes -- the imul 0x70 in GetAnimation "
              "(0x180016660) and the ctor store list at 0x1800fb1d0");
static_assert(offsetof(Animation, _events) == 0x0, "Animation::_events");
static_assert(offsetof(Animation, _pad2d) == 0x2D, "Animation::_pad2d");
static_assert(offsetof(Animation, _pad3d) == 0x3D, "Animation::_pad3d");
static_assert(offsetof(Animation, _pad54) == 0x54, "Animation::_pad54");
static_assert(alignof(Animation) == 8, "Graphics::Animation aligns to 8");
static_assert(sizeof(Animation::Event) == 0x10,
              "Animation::Event stride 0x10 -- SHL RAX,0x4 at 0x180016093");
static_assert(offsetof(Animation::Event, _frame) == 0x0, "Event::_frame");
static_assert(offsetof(Animation::Event, _pad04) == 0x4, "Event::_pad04");
static_assert(offsetof(Animation::Event, _event) == 0x8, "Event::_event");
static_assert(offsetof(Animation, _name)          == 0x10, "Animation::_name @ +0x10");
static_assert(offsetof(Animation, _meshName)      == 0x18, "Animation::_meshName @ +0x18");
static_assert(offsetof(Animation, _sourceName)    == 0x20, "Animation::_sourceName @ +0x20");
static_assert(offsetof(Animation, _instanceCount) == 0x28, "Animation::_instanceCount @ +0x28");
static_assert(offsetof(Animation, _instanced)     == 0x2c, "Animation::_instanced @ +0x2c");
static_assert(offsetof(Animation, _startFrame)    == 0x30, "Animation::_startFrame @ +0x30");
static_assert(offsetof(Animation, _endFrame)      == 0x34, "Animation::_endFrame @ +0x34");
static_assert(offsetof(Animation, _rate)          == 0x38, "Animation::_rate @ +0x38");
static_assert(offsetof(Animation, _looping)       == 0x3c, "Animation::_looping @ +0x3c");
static_assert(offsetof(Animation, _loopFrame)     == 0x40, "Animation::_loopFrame @ +0x40");
static_assert(offsetof(Animation, _frameRate)     == 0x44, "Animation::_frameRate @ +0x44");
static_assert(offsetof(Animation, _loopScale)     == 0x48, "Animation::_loopScale @ +0x48");
static_assert(offsetof(Animation, _boneCount)     == 0x4c, "Animation::_boneCount @ +0x4c");
static_assert(offsetof(Animation, _frameCount)    == 0x50, "Animation::_frameCount @ +0x50");
static_assert(offsetof(Animation, _duration)      == 0x58, "Animation::_duration @ +0x58");
static_assert(offsetof(Animation, _keys)          == 0x60, "Animation::_keys @ +0x60");
static_assert(!std::is_polymorphic<Animation>::value,
              "Graphics::Animation must never acquire a vtable: retail exports no "
              "vtable for Graphics::Animation and both its ctor and dtor are QEAA");
static_assert(std::is_standard_layout<Animation>::value,
              "the ported doors reach these fields through a reinterpret_cast");

}  // namespace Graphics

// ==== B3R G-Game (R5): Runtime bodies code/Game calls by name ====
namespace Graphics { class AnimationManager; }
extern "C" Graphics::AnimationManager* RtGraphics_AnimationManager_Ctor(Graphics::AnimationManager* self);  // 0x1800fc430: placement-new AnimationManager()
extern "C" void RtGraphics_AnimationManager_Dtor(Graphics::AnimationManager* self);  // 0x1800fc400: ~AnimationManager()
// ==== end B3R G-Game ====

// ==== B3R RT-Core: the Array<Animation> element ops collection/array.cpp calls ====
extern "C" void* RtGraphics_Animation_CopyCtor(void* selfv, const void* srcv);  // 0x1800160a0
extern "C" void  RtGraphics_Animation_Dtor(void* selfv);                        // 0x1800fb290
// ==== end B3R RT-Core ====
