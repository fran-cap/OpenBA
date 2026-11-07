#pragma once
// ============================================================================
// src/engine/graphics/Geometry.h — Graphics::Geometry binary layout
// ============================================================================
//
// Wave 6 lane D of the Runtime-x64.dll port (docs/runtime_port/PLAN.md §4.2
// wave 6, §6 step 3). Every offset below is pinned by a named retail body,
// listed next to it; nothing here is inferred from a sibling class.
//
// `Graphics::Geometry` is the POLYMORPHIC base of every drawable in the engine
// (MeshInstance, ParticleSystemInstance, the terrain geometry, and — on our
// side — StatusIconComponent's embedded billboard). Its exported vtable is
//
//     ??_7Geometry@Graphics@@6B@   retail 0x180154e60, ordinal 1439
//
// and it has exactly NINE slots. The count is not a guess: the bytes at
// 0x180154e60 are nine pointers followed immediately by the ASCII string
// "ImageBuffer\0", which is how a vtable's end is read in this project
// (reference_vtable_slot_count_from_the_name_string.md).
//
//   slot 0  +0x00  0x180015e90  scalar deleting destructor
//   slot 1  +0x08  0x18007ec28  _purecall  (Draw)
//   slot 2  +0x10  0x18007ec28  _purecall
//   slot 3  +0x18  0x18007ec28  _purecall  (AddDrawItems)
//   slot 4  +0x20  0x1800a4020  UpdateAttached
//   slot 5  +0x28  0x1800a43c0  GetSubTransform
//   slot 6  +0x30  0x1800154a0  GetInstanceTransform
//   slot 7  +0x38  0x180015590  SetSharedInstanceData
//   slot 8  +0x40  0x1800a4420  Clean
//
// Three bodies in this lane dispatch through that table rather than calling a
// sibling directly, and the slot numbers above are how they were read:
// Attach and UpdateAttached call slot 5 (`vt+0x28`), SetTransform and
// UpdateAttached tail-call slot 4 (`vt+0x20`), Scene::Clean calls slot 8
// (`vt+0x40`).
//
// ABI NOTE — see src/engine/math/Math.h. GetTransform, GetSubTransform and
// GetInstanceTransform all return a class BY VALUE, so each carries a HIDDEN
// RETURN SLOT in RDX and returns it again in RAX. Getting that wrong is a wild
// write that still produces the right answer
// (reference_hidden_retslot_wild_write.md).
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../global_types.h"   // Vector3
#include "../math/Math.h"                   // Quaternion, Transform, Matrix, Scalar
#include "../volume/Volume.h"               // Box
#include "../collection/Collection.h"       // Collection::Array
#include "InterpolatedState.h"              // Graphics::InterpolatedState<Math::Transform>

namespace RtGraphics {

// ============================================================================
// Object::ExternalPtr<T> — an 8-byte handle, passed and stored BY VALUE.
//
// MOVED HERE FROM Mesh.h BY ENDGAME B.3.5 BLOCK E, unchanged. It was declared
// in `Mesh.h` (which includes this header), but the real `Graphics::Geometry`
// class below needs it for slot 2's `Draw` parameter, so it has to be the other
// way round. Every user reaches it through `Mesh.h` exactly as before.
//
// Never dereferenced arithmetically by this family; it is copied around as an
// opaque qword and, where retail follows it, followed as a plain pointer to the
// resource object (MeshInstance::SetMesh does exactly that: it stores the
// argument at +0xc0 and then reads `*(mesh + 0x70)` off it).
//
// Typed as a distinct struct rather than `void*` so a signature cannot silently
// lose it or swap it with an `int` — the recorded 5th-argument /
// pointer-typed-as-int trap (reference_hidden_retval_and_int_pointer_args.md).
// ============================================================================
struct ExternalPtr {
    void* p;
};
static_assert(sizeof(ExternalPtr) == 8, "Object::ExternalPtr must be 8 bytes");
static_assert(offsetof(ExternalPtr, p) == 0x0, "ExternalPtr::p");

// ============================================================
// Graphics::Geometry — 0xC0 bytes.
//
// Whole-object witness: ??0Geometry@Graphics@@QEAA@XZ (retail 0x1800a4270,
// ordinal 205) writes every field below except the four bytes at +0x14 and the
// four at +0xBC, which it leaves UNINITIALISED. Independently corroborated on
// the exe side by engine/types/StatusIconComponent.h, which reserves
// `uint8_t geometry[0xC0]` at +0x28 and has been live in-game for months.
//
// Per-field evidence:
//   +0x00  vtable          ctor stores &??_7Geometry@Graphics@@6B@; the dtor
//                          (0x1800a41c0) RE-STAMPS it before doing anything
//                          else, which is what makes a subclass dtor safe.
//   +0x08  _attachParent   Attach (0x1800a3fa0) writes `child+0x08 = this`;
//                          Detach (0x1800a3f20) and ~Geometry clear it.
//   +0x10  _attachNode     Attach writes the node index there; Detach and
//                          ~Geometry write -1. Ctor writes -1.
//   +0x14  NOT PINNED, NOT INITIALISED. No decompiled body reads or writes it.
//   +0x18  _attached       Array<Geometry*>: Attach grows it through
//                          FUN_18001f990 (the stride-8 Array<T>::SetSize),
//                          ~Geometry frees it through FUN_18001bf50
//                          (Array<T>::~Array), and SetTransform / Detach /
//                          UpdateAttached all read the count at +0x20.
//   +0x28  _detailId       ?GetDetailId@ (0x180015890) is `return *(int*)(this+0x28)`.
//                          Ctor default is 2.
//   +0x2c  _dirtyFlags     ?IsDirty@ (0x180015860) tests `& 7`;
//                          ?NeedsCleaning@ (0x180015870) tests `!= 0`;
//                          SetBounds ORs 2, SetTransform ORs 3, SetOpacity ORs 4,
//                          Clean (0x1800a4420) is the whole state machine.
//                          Ctor default is 7 (all three inputs dirty).
//   +0x30  _materialTypeFlags  ?GetMaterialTypeFlags@ (0x1800157f0).
//   +0x34  _localBounds    SetBounds (0x180015700) compares then copies six
//                          floats to +0x34..+0x48, and Clean feeds &this[0x34]
//                          to the box-transform helper FUN_18001f470.
//   +0x4c  _worldBounds    Clean writes the transformed box here; the ctor
//                          seeds it EMPTY (min = +1e8, max = -1e8).
//   +0x64  _prevWorldBounds  Clean rolls +0x4c into it before overwriting.
//                          Also the box Graphics::MeshInstance::AddDrawItems
//                          unions for its LOD pick (src/diag/scene_watch.h).
//   +0x7c  _opacity        ?GetOpacity@ (0x1800156f0). Ctor 1.0f.
//   +0x80  _opacityPrev    ?SetOpacity@ (0x1800156c0) writes BOTH as one qword
//                          (`CONCAT44(v,v)`), so they only ever differ if
//                          something else writes one of them. Ctor 1.0f.
//   +0x84  _transform      SetTransform (0x1800155a0) compares all seven
//                          floats, then writes them.
//   +0xa0  _prevTransform  SetTransform rolls +0x84 into it first; Clean rolls
//                          it again on the flag-8 path; GetInstanceTransform
//                          lerps from it towards +0x84.
//   +0xbc  NOT PINNED, NOT INITIALISED — the ctor's last store is at +0xb8.
// ============================================================


// ============================================================
// Geometry::_dirtyFlags bit meanings.
//
// Read entirely out of Clean (0x1800a4420) plus the three setters that raise
// them. The low three bits are the "input changed" bits IsDirty() tests; the
// three bits above them are the "changed LAST frame, so the previous-value copy
// is still fresh" bits Clean rolls them into and then clears on the next call.
// That two-stage roll is what gives the renderer a previous transform and a
// previous world box to interpolate between for one extra frame.
// ============================================================
enum GeometryDirty : uint32_t {
    kDirtyTransform     = 0x01,  // SetTransform / UpdateAttached raise it
    kDirtyBounds        = 0x02,  // SetBounds and SetTransform raise it
    kDirtyOpacity       = 0x04,  // SetOpacity raises it
    kRolledTransform    = 0x08,  // Clean: set when 0x01 was seen, cleared next call
    kRolledBounds       = 0x10,  // Clean: set when 0x02 was seen, cleared next call
    kRolledOpacity      = 0x20,  // Clean: set when 0x04 was seen, cleared next call
    kDirtyAll           = 0x07,  // the ctor's store: all three inputs dirty, nothing rolled
};

// (B3X W0: the `GeometryVSlot` index enum -- `kVSlotAddDrawItems` .. `kVSlotClean`
// -- is deleted. It had no reader left: every Geometry slot is a by-name
// virtual call, and an index would mis-aim after the normal-virtual cutover.
// Slot 3 is `AddDrawItems`, reached as `(geometry, scene, renderer)` by
// Scene::AddCulledDrawItems' `CALL [RAX+0x18]` at 0x18010226e.)

}  // namespace RtGraphics

// ############################################################################
// ENDGAME B.3 step 5 (block E) — `Graphics::Geometry`, the REAL C++ class
// ############################################################################
//
// `B35_RULES.md` §2.1/§2.2, measured from the retail DLL this pass by block 0:
//
//   ??_7Geometry@Graphics@@6B@ @ 0x180154e60, NINE slots, sizeof 0xC0,
//   and it is a **ROOT** — NOT an `Object::Base` subclass. Its slot 0
//   (0x180015e90) and slots 1/2/3 are its own and share nothing with
//   ??_7Base@Object@@6B@ (0x180154788, whose slot 2 is `?Type@Base@Object@@`).
//   There is no `_typeInfo`, no `TypeName`, no `Type`. ABSTRACT: slots 1, 2
//   and 3 are `_purecall` (0x18007ec28).
//
// THE SLOT LIST, copied from `B35_SLOTS.md` (read as `.rdata` qwords, not
// decompiled), with the names of slots 1/2/3 read off the OVERRIDES — a pure
// slot's name can only ever come from a subclass that fills it, and all seven
// subclasses agree:
//
//   [0] +0x00  0x180015e90  (unexported) vector deleting destructor
//   [1] +0x08  _purecall    Update(const System::Time&)
//   [2] +0x10  _purecall    Draw(Context&, const Camera&, ExternalPtr<MaterialInstance>,
//                                float, int, int, int) const
//   [3] +0x18  _purecall    AddDrawItems(const Scene&, Renderer&) const
//   [4] +0x20  0x1800a4020  ?UpdateAttached@Geometry@Graphics@@UEAAXXZ
//   [5] +0x28  0x1800a43c0  ?GetSubTransform@…  -> Math::Transform BY VALUE
//   [6] +0x30  0x1800154a0  ?GetInstanceTransform@…  -> Math::Matrix BY VALUE
//   [7] +0x38  0x180015590  ?SetSharedInstanceData@…
//   [8] +0x40  0x1800a4420  ?Clean@Geometry@Graphics@@UEAA_NXZ
//
// *** A CORRECTION TO THE BANNER AT THE TOP OF THIS FILE. *** It reads
// "slot 1 _purecall (Draw)" and "slot 3 _purecall (AddDrawItems)". Slot 1 is
// **Update** and slot 2 is **Draw**: every one of the seven subclasses' tables
// puts `?Update@X@Graphics@@UEAAXAEBVTime@System@@@Z` at [1] and
// `?Draw@X@…MHHH@Z` at [2] (B35_SLOTS.md, nine independent readings). The old
// comment mis-named slot 1 and left slot 2 unnamed; the enum below never
// carried a name for either, so nothing was built on the mistake.
//
// -------------------------------------------------------------------------
// RULE B5-12 — SLOTS 5 AND 6 ARE HIDDEN-RETSLOT SLOTS
// -------------------------------------------------------------------------
// `GetSubTransform` returns `Math::Transform` (0x1c) and
// `GetInstanceTransform` returns `Math::Matrix` (0x40), both **by value
// through a vtable slot**. Those two indices are the WHOLE of the hidden-
// retslot vtable surface in Video and Graphics — nine (class, slot) pairs,
// two indices (B35_RULES §2.5). The 2026-09-13 perf-only crash (`1a4200c3`)
// was exactly one of them: a `void` port of `ParticleInstance::
// GetInstanceTransform` left RAX holding garbage at -O2 while -O0 happened to
// still hold the slot pointer. Every surviving `fn_*` typedef and hand-built
// mint row for these two slots keeps `Transform*` / `Matrix*` and must never
// be "simplified" to `void`. **Verify at -O2, not -O0.**
//
// *** AND B5-12 IS WRONG ABOUT THE FIX. MEASURED, 2026-09-19, BLOCK E. ***
//
// The rule says: "When the Geometry block declares those virtuals, they return
// the value type BY VALUE in C++ — GCC's Microsoft-ABI lowering produces the
// same hidden slot and the same RAX return, which is the point of using a real
// class here." **It does not.** GCC (ucrt64 13.1, the tree's only compiler)
// passes the hidden return pointer as the FIRST argument, BEFORE `this`, the way
// the Itanium/SysV ABI does. MSVC — and therefore retail — passes `this` FIRST
// and the return pointer SECOND.
//
// The probe (a `virtual Big Get(int) const` returning 28 bytes, its slot 0
// called twice through two hand-spelled function-pointer types):
//
//   (this, ret, n)  -> wrote the 28 bytes THROUGH `this`, corrupting the object,
//                      and left the caller's buffer untouched
//   (ret, this, n)  -> wrote the caller's buffer, returned it in RAX, object intact
//
// The same swap is what made the first version of this block's unit test
// segfault: the slot-5 call scribbled a `Math::Transform` over the probe
// object's vptr and the next slot read faulted. Retail's order is settled
// independently by the tree: every ported retslot door is spelled
// `(self, ret, args…)` — `RtGraphics_Geometry_GetSubTransform(const Geometry*,
// Transform*, int)` in `scene.cpp` is registered as slot 5's body and the game
// renders — and `1a4200c3`'s own description says "the hidden return slot
// arrives in RDX".
//
// A vtable slot is the ONE place this matters, because a slot is the shared
// contract between a GCC-compiled member and the retail-order callers that
// still reach it: the minted rows in `standalone/rt_data_mint_tables.inc`, the
// surviving `fn_*` typedefs, and the 118 hand-inlined `(*(void***)p)[N]` call
// sites. So slots 5 and 6 are declared below with an EXPLICIT out-parameter in
// RETAIL ORDER — `T* M(T* ret, args…) const` — which makes GCC emit exactly the
// convention MSVC emitted and keeps a minted row and a GCC vtable entry
// interchangeable, which is what licenses the whole coexistence argument.
//
// NON-VIRTUAL by-value returns are NOT affected and are declared as ordinary
// C++ by-value returns (`GetTransform`, `AABB`, `LocalAABB`,
// `MeshInstance::GetNodeToWorld`, …): they are never reached through a slot, so
// both ends of every call are this same GCC, and their registered doors keep the
// retail `(self, ret)` shape as separate functions.
//
// `tests/unit/graphics_geometry_abi_test.cpp` PART C pins both halves of this.
//
// -------------------------------------------------------------------------
// WHY THE `RtGraphics::Geometry` POD ABOVE STAYS
// -------------------------------------------------------------------------
// B4b-1 renames a POD out of the retail name only when the POD OCCUPIES that
// name. `RtGraphics` is a SIBLING of `Graphics`, so there is no collision and
// nothing has to move. The POD also has live users OUTSIDE this block's file
// set — `src/engine/graphics/scene.cpp`, `cull.cpp`,
// `batchc_gfx_leaves.cpp`, `animation.cpp`, `vtbatch7_animgroup_particlesystem.cpp`
// and `src/engine/graphics/Scene.h` — which this block may not touch. So
// the two descriptions coexist, exactly as `Object::External` coexists with
// `RtObject::External` (`src/engine/object/External.h`), and every offset
// is CROSS-ASSERTED against the POD below so the two cannot drift.
//
// -------------------------------------------------------------------------
// ABSTRACT, AND NOTHING HERE IS CONSTRUCTIBLE — B34_BRIEFS §A.4
// -------------------------------------------------------------------------
// (ENDGAME B.3.9 GFX: slot 0 is the class's own vector deleting destructor
// now, and the RT_DELDTOR row is retired; the class stays abstract because
// slots 1, 2 and 3 are pure. The history below is block E's.)
// Slot 0 stays `= 0`. Its body is the vector deleting destructor at
// 0x180015e90, which is a row in the minted tables, not a C++ function this
// block owns, and B35_RULES §11 forbids retiring that row in step 5. Nothing
// in the tree holds a `Graphics::Geometry` (or any subclass) BY VALUE as a
// C++ member — the four by-value mirrors of §3 are the `RtGraphics` PODs
// holding `RtGraphics::Geometry`, not the class — so §A.4's amended rule
// ("abstract where nothing holds the class by value") applies to the whole
// tree below. `sizeof` and `offsetof` work on an abstract class, so every
// layout pin still holds.
//
// (ENDGAME B.3.9 GFX: the constructor and destructor are real members in
// scene.cpp and the copy constructor's body is the member in closure_batch2b.cpp;
// all three doors are retired. The paragraph below is block E's history.)
// NO REAL CONSTRUCTOR and NO REAL DESTRUCTOR BODY (ENDGAME §B.1 decision 2,
// B4b-2, B35_RULES §11): `??0Geometry@Graphics@@QEAA@XZ` (0x1800a4270) and
// `??1Geometry@Graphics@@UEAA@XZ` (0x1800a41c0) keep their doors in
// `src/engine/graphics/scene.cpp`. The ONE exception is the COPY
// constructor, which RULE B5-10 requires be declared explicitly and never
// `= default`; see its comment below.
// ############################################################################

// Forward declarations of the classes other blocks own. B.3.5 takes them by
// reference / pointer under their REAL retail names; the bridge happens at the
// use site, never in this header.
namespace System { class Time; class String; }
namespace Video  { class Context; }
namespace IO     { class Serializer; }
// src/engine/object/Internal.h's allocator seam (0x180006a10 is MemFree),
// declared rather than included: Internal.h pulls the IO serializer into every
// Video/Graphics TU that includes this base.
namespace Object { namespace detail { void InternalMemFree(void* p); } }

namespace Graphics {

class Camera;             // block F  (src/engine/graphics/Camera.h)
class Renderer;           // block F
class Scene;              // block F
class MaterialInstance;   // block G  (src/engine/graphics/Material.h)
class GraphicsMesh;       // block G  (src/engine/graphics/Mesh.h, the POD today)
class EdgeMesh;           // block G
class AnimationGroup;     // block H2
class Particle;           // block H2 (src/engine/graphics/Particle.h, the POD today)

// ---------------------------------------------------------------------------
// `Object::ExternalPtr<T>` IS SPELLED AS THE 8-BYTE POD HANDLE, ON PURPOSE.
//
// Retail's parameter and return type is `Object::ExternalPtr<T>` — a real class
// template that `src/engine/object/External.h` already carries. This family
// keeps `RtGraphics::ExternalPtr` (one `void* p`, `static_assert(sizeof == 8)`)
// instead:
//
//  * the two are ABI-identical — 8 bytes, trivially copyable, so the Microsoft
//    x64 ABI passes both in a register and returns both in RAX — and every
//    already-diffed body in this family (`MeshInstance::SetMesh`,
//    `SetMaterialInstance`, `PlayAnimation`, `EdgeMeshInstance::SetMesh`,
//    `ParticleInstance::SetParticle`) carries the POD handle today;
//  * the typed spelling would make these headers include External.h — and
//    through it the IO serializer — for no ABI gain, on the Geometry base every
//    Video/Graphics block includes;
//  * `T` is `GraphicsMesh`, `MaterialInstance`, `EdgeMesh`, `AnimationGroup`
//    and `Particle`, which are blocks G and H2's classes.
//
// The alias is named for what it is so the retail type is not lost.
//
// WHAT THE TYPED SPELLING ACTUALLY COSTS — corrected, ENDGAME B.3.5 closing pass.
// This banner used to end "The typed spelling lands with them, as a ONE-LINE
// change to this alias and no change to any body." That is not true and was never
// true, and the reason is in the bullet above it: ONE ALIAS SERVES FIVE DIFFERENT
// `T`. `using ExternalPtrHandle = Object::ExternalPtr<?>` has nothing to put in
// the `?`, so the alias cannot be retyped at all — it has to be DELETED and every
// use replaced with the `Object::ExternalPtr<T>` that use means.
//
// Measured on this tree: 20 uses across SIX headers — Geometry.h 2, Light.h 2,
// Material.h 1, Mesh.h 10, ParticleInstance.h 4, SunLight.h 1 — and 109
// occurrences of the name tree-wide once the bodies and callers are counted. Each
// header use must be resolved to its own `T` by reading the retail mangled name
// beside it (THE DOOR DECIDES, as everywhere else), and several sit in VIRTUAL
// slot signatures, where getting `T` wrong changes an overload rather than failing
// to compile.
//
// So the retype is NOT a cleanup that rides along with block G or H2. It is owed
// together with the RECEIVER-SIDE `Object::ExternalPtr` work — the same work the
// nine hand-inlined `ReaderWriter` slot calls and the twelve ExternalPtr exe doors
// are waiting on (B35_RULES section 11) — because that is the pass that has to
// read every one of these sites for its own reasons anyway.
// ---------------------------------------------------------------------------
using ExternalPtrHandle = RtGraphics::ExternalPtr;

// ============================================================
// Graphics::Geometry — 0xC0 bytes, nine slots, ABSTRACT, a ROOT.
//
// The fields carry the POD's names so that a body moving out of a door reads
// `_dirtyFlags` where it used to read `self->_dirtyFlags`, and the two
// descriptions can be asserted field for field.
// ============================================================
class Geometry {
public:
    // ---- RULE B5-10 --------------------------------------------------------
    // `??0Geometry@Graphics@@QEAA@AEBV01@@Z` @ 0x180015b60 is a REAL,
    // REGISTERED, EXPORTED body (393 bytes; `src/engine/graphics/
    // closure_batch2b.cpp`), not a compiler default. It zeroes the nested
    // `Array<Geometry*>` header and then runs retail's DEEP
    // `Array<Geometry*>::operator=` over it. `= default` here would replace
    // that with a bitwise copy of a heap pointer — two owners of one buffer.
    // Four POD mirrors embed these bytes by value and `Collection::Array`
    // copies of the derived classes go through this constructor, so it is
    // declared explicitly and defined in closure_batch2b.cpp.
    Geometry(const Geometry& src);

    // `??4Geometry@Graphics@@QEAAAEAV01@AEBV01@@Z` @ 0x180015cf0.
    // NOT PORTED and no caller anywhere in the tree: declared so no caller
    // batch has to edit this header, deliberately left undefined.
    Geometry& operator=(const Geometry& src);

    // ---- the nine slots, in measured retail order -------------------------
    // [0] 0x180015e90 -- retail's MSVC VECTOR deleting destructor (0xc0-byte
    // elements, ~Geometry, MemFree on & 1). THE DESTRUCTOR IS VIRTUAL (the
    // normal-virtual cutover, B3X W1): GCC spends TWO slots on it (D1 + D0), so
    // retail slot N >= 1 (the `[N]` comments below) is compiler slot N + 1;
    // every caller reaches a slot by name. The body is
    // `??1Geometry@Graphics@@UEAA@XZ` @ 0x1800a41c0 (scene.cpp): the compiler
    // restamps on entry; the body detaches every child and frees `_attached`,
    // whose implicit ~Array after it is a no-op on the zeroed header. Its ??1
    // door is retired. Geometry stays ABSTRACT (slots 1, 2 and 3 are
    // `_purecall`).
    virtual ~Geometry();

    // Retail's `flags & 1` arm: MemoryAllocator::MemFree for the whole tree
    // (src/engine/object/root_operator_delete.cpp; unit build: free).
    static void operator delete(void* p);

    virtual void Update(const System::Time& time) = 0;                   // [1] _purecall
    virtual void Draw(Video::Context& context, const Camera& camera,
                      ExternalPtrHandle material,
                      float opacity, int a, int b, int c) const = 0;     // [2] _purecall
    virtual void AddDrawItems(const Scene& scene,
                              Renderer& renderer) const = 0;             // [3] _purecall
    virtual void UpdateAttached();                                       // [4] 0x1800a4020
    // Slots 5 and 6: the hidden return slot is spelled OUT, in retail's order
    // (`this`, then the caller's storage), and returned again as the result.
    // See the B5-12 block above for why a C++ by-value return is wrong HERE and
    // right for every non-virtual member.
    virtual Math::Transform* GetSubTransform(Math::Transform* ret,
                                             int node) const;            // [5] 0x1800a43c0 RETSLOT
    virtual Math::Matrix* GetInstanceTransform(Math::Matrix* ret, int node,
                                               float t) const;           // [6] 0x1800154a0 RETSLOT
    virtual int  SetSharedInstanceData(Video::Context& context,
                                       int a, float t) const;            // [7] 0x180015590
    virtual bool Clean();                                                // [8] 0x1800a4420

    // ---- the non-virtual exported surface ---------------------------------
    // ALL of these are PORTED, and every body lives in
    // `src/engine/graphics/scene.cpp` — which is NOT this block's file (it
    // holds `Graphics::Scene` too, and block F owns it). They are declared
    // here so no later block or caller batch has to edit this header; the
    // five whose retail bodies exist keep their registered doors and their
    // bodies move onto these members when scene.cpp converts.
    void Attach(Geometry* child, int node);              // 0x1800a3fa0  door in scene.cpp
    void Detach(Geometry* child);                        // 0x1800a3f20  door in scene.cpp
    void SetTransform(const Math::Transform& xf);        // 0x1800155a0  door in scene.cpp
    void SetBounds(const ::Box& box);                    // 0x180015700  door in scene.cpp
    Math::Transform GetTransform() const;                // 0x180015800  door in scene.cpp, RETSLOT

    // Exported, NOT PORTED anywhere in the tree, and no caller: declared,
    // deliberately undefined. Each is one or two instructions in retail and
    // the offsets they read are pinned in the POD's banner above.
    void     SetOpacity(float opacity);                  // 0x1800156c0  not ported
    float    GetOpacity() const;                         // 0x1800156f0  not ported
    ::Box    AABB() const;                               // 0x180015780  not ported, RETSLOT
    ::Box    LocalAABB() const;                          // 0x1800157c0  not ported, RETSLOT
    uint32_t GetMaterialTypeFlags() const;               // 0x1800157f0  not ported
    bool     IsDirty() const;                            // 0x180015860  not ported
    bool     NeedsCleaning() const;                      // 0x180015870  not ported
    void     SetDetailId(int id);                        // 0x180015880  not ported
    int      GetDetailId() const;                        // 0x180015890  not ported

protected:
    // `??0Geometry@Graphics@@QEAA@XZ` @ 0x1800a4270 — the REAL constructor
    // (scene.cpp): the vptr store is the compiler's, the 46 field stores are
    // the body. ENDGAME B.3.9 GFX: the ??0 door is retired with the minted table.
    Geometry();

public:

public:
    // ---- the 0xB8 bytes after the vptr ------------------------------------
    Geometry*                    _attachParent;      // +0x08
    int32_t                      _attachNode;        // +0x10
    int32_t                      _pad14;             // +0x14  uninitialised, unused
    Collection::Array<Geometry*> _attached;          // +0x18
    int32_t                      _detailId;          // +0x28
    uint32_t                     _dirtyFlags;        // +0x2c
    uint32_t                     _materialTypeFlags; // +0x30
    ::Box                        _localBounds;       // +0x34
    ::Box                        _worldBounds;       // +0x4c
    ::Box                        _prevWorldBounds;   // +0x64
    float                        _opacity;           // +0x7c
    float                        _opacityPrev;       // +0x80
    // +0x84 `_current` / +0xa0 `_previous`: ONE InterpolatedState<Transform>.
    // StatusIconComponent's and the game geometry's Draw hand &this+0x84 to
    // ?Lerp@?$InterpolatedState@VTransform@Math@@@Graphics@@ (0x18001aff0);
    // SetTransform (0x1800155a0) rolls _current into _previous. Its default
    // ctor is the two identity Transforms retail's ctor (0x1800a4270) stores
    // inline at +0x84..+0xb8; trivially destructible, as retail's dtor
    // (0x1800a41c0) has no store there.
    InterpolatedState<Math::Transform> _transform;   // +0x84 (0x38)
    uint32_t                     _padBC;             // +0xbc  uninitialised, unused
};

static_assert(sizeof(Geometry) == 0xC0,
              "Graphics::Geometry is 0xC0 bytes (MOV EDX,0xc0 in its own slot-0 "
              "vector deleting destructor, 0x180015e90)");
static_assert(offsetof(Geometry, _pad14) == 0x14, "Geometry::_pad14");
static_assert(alignof(Geometry) == 8, "Graphics::Geometry is qword aligned (the vptr)");

// Cross-asserts against the POD: every field at the same offset in both.

// And against the retail bodies directly, so a POD edit cannot move both.
static_assert(offsetof(Geometry, _attachParent)      == 0x08, "Geometry::_attachParent @ +0x08 (Attach 0x1800a3fa0)");
static_assert(offsetof(Geometry, _attachNode)        == 0x10, "Geometry::_attachNode @ +0x10");
static_assert(offsetof(Geometry, _attached)          == 0x18, "Geometry::_attached @ +0x18");
static_assert(offsetof(Geometry, _detailId)          == 0x28, "Geometry::_detailId @ +0x28 (GetDetailId 0x180015890)");
static_assert(offsetof(Geometry, _dirtyFlags)        == 0x2c, "Geometry::_dirtyFlags @ +0x2c (IsDirty 0x180015860)");
static_assert(offsetof(Geometry, _materialTypeFlags) == 0x30, "Geometry::_materialTypeFlags @ +0x30 (0x1800157f0)");
static_assert(offsetof(Geometry, _localBounds)       == 0x34, "Geometry::_localBounds @ +0x34 (SetBounds 0x180015700)");
static_assert(offsetof(Geometry, _worldBounds)       == 0x4c, "Geometry::_worldBounds @ +0x4c (Clean 0x1800a4420)");
static_assert(offsetof(Geometry, _prevWorldBounds)   == 0x64, "Geometry::_prevWorldBounds @ +0x64");
static_assert(offsetof(Geometry, _opacity)           == 0x7c, "Geometry::_opacity @ +0x7c (GetOpacity 0x1800156f0)");
static_assert(offsetof(Geometry, _opacityPrev)       == 0x80, "Geometry::_opacityPrev @ +0x80");
static_assert(offsetof(Geometry, _transform)         == 0x84, "Geometry::_transform @ +0x84 (SetTransform 0x1800155a0)");
static_assert(offsetof(Geometry, _transform) + offsetof(InterpolatedState<Math::Transform>, _previous) == 0xa0,
              "Geometry::_transform._previous @ +0xa0");
static_assert(offsetof(Geometry, _padBC)             == 0xbc, "Geometry::+0xbc is the never-written tail dword");

}  // namespace Graphics
