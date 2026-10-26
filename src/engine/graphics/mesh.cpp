// ============================================================================
// src/engine/graphics/mesh.cpp — Wave 6 lane E
//   Graphics::MeshInstance + GraphicsMesh + EdgeMesh + EdgeMeshInstance
// ============================================================================
//
// Ports the Graphics mesh-family exports our tree calls (docs/runtime_port/
// PLAN.md §4.2 wave 6, §6). Layouts and the evidence for every offset are in
// src/engine/graphics/Mesh.h; the per-function write-up, the oracle
// arguments and everything this lane could NOT verify are in
// docs/history/runtime_port/WAVE6_LANE_E.md.
//
//   | Export                                  | Retail      | Ord  | Oracle          |
//   |-----------------------------------------|-------------|------|-----------------|
//   | ??0MeshInstance@…@XZ                    | 0x1800fffc0 |  275 | FULL (scratch)  |
//   | ??0MeshInstance@…@AEBV01@@Z             | 0x1800170c0 |  274 | post-state      |
//   | ??1MeshInstance@…@XZ                    | 0x180017060 |  632 | post-state      |
//   | ?GetMaterialList@GraphicsMesh@…H@Z      | 0x1800126c0 | 2768 | FULL            |
//   | ?GetNodeIndexFromName@GraphicsMesh@…    | 0x1800fee90 | 2812 | FULL            |
//   | ?GetNodeToWorld@MeshInstance@…          | 0x180016a40 | 2813 | FULL (scratch)  |
//   | ?IsAnimating@MeshInstance@…             | 0x1800ff7d0 | 3097 | FULL            |
//   | ?PlayAnimation@MeshInstance@…           | 0x1800ff840 | 3421 | post-state      |
//   | ?RootAnimationOffset@MeshInstance@…     | 0x180016ef0 | 3589 | FULL (scratch)  |
//   | ?SetMaterialInstance@MeshInstance@…     | 0x1800ffe80 | 3908 | FULL (replay)   |
//   | ?SetMesh@MeshInstance@…                 | 0x1800ffea0 | 3915 | FULL (replay)   |
//   | ?UpdateHierarchy@MeshInstance@…         | 0x1800ff910 | 4518 | FULL (replay)   |
//   | ??0EdgeMesh@Graphics@@QEAA@XZ           | 0x1800a2830 |  161 | post-state      |
//   | ??0TriangleVertex@EdgeMesh@…            | 0x1800a2f50 |  462 | FULL (scratch)  |
//   | ??0EdgeVertex@EdgeMesh@…                | 0x1800a2df0 |  164 | FULL (scratch)  |
//   | ?Set@EdgeMesh@Graphics@@…               | 0x1800a23e0 | 3813 | post-state      |
//   | ?SetMaterials@EdgeMesh@Graphics@@…      | 0x1800a2760 | 3910 | FULL (replay)   |
//   | ??0EdgeMeshInstance@…@XZ                | 0x1800a3ab0 |  163 | FULL (scratch)  |
//   | ??0EdgeMeshInstance@…@AEBV01@@Z         | 0x1800174f0 |  162 | post-state      |
//   | ?SetMesh@EdgeMeshInstance@…             | 0x1800a3a20 | 3914 | FULL (replay)   |
//
// All twenty-four names in this lane's scope (the twenty above plus the four
// held back at the bottom of this file) were checked against the shipped export
// directory of C:\GOG Games\Banished\Runtime-x64.dll with pefile BEFORE any body
// was written — reference_resolveexport_phantom_names.md: a name the DLL does
// not contain resolves to NULL in silence, the port still registers and the
// census still counts it. **Zero phantoms.** The addresses and ordinals above
// are that query's output, not Ghidra's and not an inference.
//
// REGISTRATION: by NAME for all twenty, and additionally by ADDRESS for
// GetNodeIndexFromName, whose one address-only call site is
// `src/game/Action/showtool.cpp:169`
// (`ResolveAddr(Module::Runtime, 0x1800fee90)`, a fallback taken because the
// exe dispatch slot 0x140152970 is empty in standalone). Every other call site
// in this lane's scope reaches its function through `ResolveExport(<name>)` —
// including the ones that look address-shaped, because
// `standalone/address_registry.cpp` spells them
// `RegisterDispatchSlot(<exeSlot>, ResolveExport(<name>))`: the exe slot is
// storage, the resolution is by name. A name-only registration is therefore
// enough for those, and the census's orphan list is what will say if that read
// of the tree was wrong.
//
// ---------------------------------------------------------------------------
// WHAT STAYS IN THE DLL, AND WHY
// ---------------------------------------------------------------------------
// Everything this file calls out to falls into one of three groups.
//
// 1. Collection::Array<T> out-of-line instantiations (`Rt*Array*` below).
//    MSVC emits one copy/resize/free per element width, and each body takes the
//    allocator's own CRITICAL SECTION around MemoryAllocator::Free
//    (`EnterCriticalSection(_singleton + 0x640)`, plainly visible in the
//    decompile of every one of them). PLAN §4.1 decision 1 keeps src/engine/
//    free of <windows.h> outside a `_win` twin, and PLAN §4.3 risk 1 warns that
//    a ported Set/Array body cannot serve two element widths. So these are
//    called at their retail addresses, exactly as retail's own code does — the
//    same shape Wave 5 lane D used for Picking's eight FUN_ internals and Wave 4
//    lane C used for Map_Find / Map_Insert.
//
// 2. Neighbouring EXPORTED engine functions retail reaches by a direct internal
//    CALL, not through the export table: Geometry's ctor/copy-ctor/dtor and
//    SetBounds, AnimationState's ResetAnimationState / PlayAnimation, and
//    Quaternion::operator* / Quaternion::Rotate. These are resolved ONCE with
//    the raw DLL door and then called through the cached pointer.
//
//    Why the raw DLL door (the DLL) and not ResolveExport (whatever is
//    registered)? Because retail's `MeshInstance::UpdateHierarchy` contains a
//    direct `CALL 0x18001e280`, so calling the DLL's body IS the byte-faithful
//    thing, and it removes a registration-ORDER dependency between lanes that
//    run concurrently in one tree. It also keeps this lane's diff oracle honest:
//    a divergence reported here is this lane's, not a neighbour's.
//
//    *** NO GetProcAddress SITS ON A HOT PATH IN THIS FILE. *** rt_port.h is
//    explicit that the raw DLL door IS GetProcAddress — an export-table binary search
//    under the loader lock — and that two of them inside System::Time's body
//    cost 5.6 percentage points of sim time while hiding as ntdll self time
//    (reference_rtoriginal_is_getprocaddress.md, WAVE3_LANE_P.md). Every
//    resolution in this file is either inside an `if (the diff-oracle guard)` guard (the
//    oracle, which no shipping run enters) or wrapped in the raw DLL door /
//    a function-local static, which resolves once per call site for the life of
//    the process. `MeshInstance::UpdateHierarchy` and `IsAnimating` in
//    particular execute nothing per call that retail did not.
//
// 3. Video::VertexBuffer / IndexBuffer::PlatformConstruct, which EdgeMesh's
//    ctor calls eight times. These are Wave 6 lane C's classes AND they are the
//    seam into `src/render/dx11/` — PlatformConstruct asks Device::_singleton's plugin for
//    a companion object and then links the buffer into the device's GLOBAL
//    intrusive buffer list. Lane E calls them; it does not port them, and the
//    global list is the reason `EdgeMesh::EdgeMesh` gets no scratch-buffer
//    oracle (see the oracle note above that function).
//
// ---------------------------------------------------------------------------
// IS THE CLASS SPLIT?  (PLAN §4.1 decision 5, §4.3 risk 3 — the real hazard.)
// ---------------------------------------------------------------------------
// MeshInstance and EdgeMeshInstance are VIRTUAL classes whose objects are built
// by these ctors and carry the DLL's own exported vtable
// (??_7MeshInstance@Graphics@@6B@ at 0x180154dc0, stamped by the ctor and
// resolved BY NAME here rather than cloned). So the DLL's `Renderer::Draw` and
// `Scene::Clean` keep dispatching MeshInstance's virtuals — Update, Draw,
// AddDrawItems, Clean, GetInstanceTransform — into the DLL, and this lane
// changes none of them. What flips is the NON-virtual surface our tree calls
// directly. Both implementations then operate on ONE object with ONE layout,
// which is the condition WAVE6.md sets for a Video/Graphics lane to be safe.
//
// The two exceptions are stated where they occur: `~MeshInstance` is virtual
// (UEAA) and IS ported, because the only caller that reaches it is ours through
// dispatch slot 0x1401514a8, which is a direct call and not a vtable dispatch;
// and `AddDrawItems` is virtual and is NOT ported, for a separate reason given
// at the bottom of this file.
//
// PORTABILITY: no <windows.h>, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "graphics/Camera.h"   // Graphics::Renderer -- ENDGAME B.3.5 block GC
#include "graphics/Mesh.h"
#include "graphics/GraphicsMesh.h"   // ENDGAME B.3.5 block G
// ENDGAME B.3.5 caller batch: the cross-class edges this file opens.
#include "graphics/AnimationState.h"  // Graphics::AnimationState
#include "graphics/AnimationManager.h"   // B.3.9: AnimationManager::_singleton
#include "graphics/AnimationGroup.h"     // B.4: AnimationGroup::GetAnimation
#include "graphics/Material.h"           // B.4: MaterialInstance::_shaderClass
#include "video/Buffers.h"            // Video::{Index,Vertex}Buffer
#include "graphics/Geometry.h"        // Graphics::Geometry::SetTransform
#include "collection/Collection.h"    // rtcoll:: the array instantiations
#include "math/Math.h"                // Math::Quaternion::Lerp
#include "object/class_vptr.h"   // Object::ClassVptr (the ctor records the vptr hint)

using RtGraphics::EdgeVertex;
using RtGraphics::TriangleVertex;
using RtGraphics::kEdgeMeshVertexScale;

namespace {

// ============================================================================
// The DLL internals this lane calls, by body name (B3R). The Array<T> helpers
// are NOT exported (they are template instantiations):
//   Array<ExternalPtr<MaterialInstance>>, element 8 bytes:
//     0x180014c70  operator=   rtcoll::RtColl_ArrayAssign_180014c70
//     0x180014d00  ~Array      rtcoll::RtColl_ArrayDtor_180014d00
//   Array<AnimationState layer>, element 0x38, alignment 8:
//     0x18001b700 / 0x18001c480  rtcoll::Rt_ArrayAssign_18001b700 / AnimationState::LayerArray::~Array
//   Array<InterpolatedState<Transform>>, element 0x38, alignment 4 (a DIFFERENT
//   instantiation from the layer array even though the stride matches — its
//   SetSize default-constructs new elements to an identity Transform pair):
//     0x18001b810 / 0x18001c550 / 0x18005ac50
//       rtcoll::Rt_ArrayAssign_18001b810 / Rt_ArrayDtor_18001c550 / Rt_SetSize_18005ac50
//   Array<GraphicsMesh::TriangleList>, element 0x18, alignment 4 — EdgeMesh::Set
//   uses it for both its triangle list and its edge list:
//     0x1800136f0  rtcoll::RtColl_ArraySetSize_1800136f0
// ============================================================================

// The exported neighbours, by mangled name. Each is a direct internal CALL in
// the retail body it replaces, so going to the DLL is the byte-faithful choice
// as well as the one that removes a cross-lane registration-order dependency.
// NOTE: the "going to the DLL is the byte-faithful choice" sentence above is
// about the CALL being a direct internal one in retail, not about which door
// resolves it. ENDGAME B.3.5 caller batch: the two AnimationState typedefs that
// used to sit here (`AnimResetFn`, `AnimPlayFn`) are gone with their doors.
// ENDGAME B.3.1 step 2: Math::Quaternion's operator* and Rotate, called as
// members. The `(self, ret, arg)` SHAPE is kept because the call sites below
// work on raw offsets into GraphicsMesh's untyped node/pool storage.
//
// B.4: the two `(self, ret, arg)` wrappers (QuaternionMultiply /
// QuaternionRotate) that viewed raw node / pool storage as those classes are
// gone with the raw storage; the call sites call the members on typed operands.

// RT_EXPORT_ONCE on the four below (Wave 9 lane R, 2026-09-07). All four names
// are PORTED, and the raw door skipped the port registry by design -- so
// MeshInstance::UpdateHierarchy was running the DLL's Quaternion::operator* on
// every node of every model while Wave 1's port of it sat registered and
// unreached. A dependency belongs on the seam.
// ENDGAME B.3.9 block GFX: `RtGeometryCtor` / `RtGeometryCopyCtor` /
// `RtGeometryDtor` (the three Geometry name doors the MeshInstance /
// EdgeMeshInstance ctor, copy-ctor and dtor doors called) are gone with those
// doors; the real classes chain Geometry's members.
// ENDGAME B.3.5 caller batch: `?SetBounds@Geometry@Graphics@@` is a DEFINED
// member of the real `Graphics::Geometry` (scene.cpp:1373), and its one caller in
// this file is `EdgeMeshInstance::SetMesh`, whose `this` IS a Geometry — so the
// resolve-and-return accessor and the `GeometrySetBoundsFn` typedef are GONE and
// the call site is a qualified non-virtual member call. The door stays registered.
//
// THE NOTE THAT DIED WITH IT, kept because it names a live tool gap:
// `tools/port_bypass_audit.py --by-name` could not see this site and still
// cannot. The accessor stored the pointer and RETURNED it, so the brace-matched
// enclosing function the classifier follows was the four-line accessor, in which
// the pointer is indeed never called — the call was in the CALLER, one frame out.
// Do not read that gate's 0 as covering the resolve-and-return family until the
// audit follows a returned pointer to its callers. Three accessors of exactly
// that shape in this file converted in this batch.
// RT_EXPORT_ONCE on both, not the raw DLL door (closure batch 3b, 2026-09-12),
// for exactly the reason the four-door note above gives — and this pair is the
// same defect one more time, found the same way.
//
// WHAT WAS BELIEVED: that these two names had no port, so the raw door and the
// seam door led to the same place and the raw one was merely cheaper to spell.
// WHAT IS TRUE NOW: `?ResetAnimationState@` was ported in closure batch 3
// (src/engine/graphics/animationstate.cpp, retail 0x1800fd930) and
// `?PlayAnimation@AnimationState@` in batch 3b
// (animationstate_transforms.cpp, retail 0x1800fdf50). From the moment each
// landed these two lines were LIVE-PATH PORT BYPASSES, with nothing in this
// file changing — which is the hazard WAVE9.md's bypass row states in as many
// words ("re-run after every LANDING, not after every edit"). The diff oracle
// cannot see it either: a bypassed port never runs, so it never disagrees, and
// both bodies read `entered=0` in the summary while this file called the DLL's
// copies hundreds of times a session.
// ENDGAME B.3.5 caller batch: both are DEFINED members of the real
// `Graphics::AnimationState` today (`ResetAnimationState` in animationstate.cpp,
// `PlayAnimation` in animationstate_transforms.cpp), so both resolve-and-return
// accessors are GONE and the two call sites below are member calls through the
// 0x18-byte `_animation` PREFIX that `Mesh.h` embeds — `AnimationState.h` pins
// that boundary with `offsetof(_rootOffset) == 0x18`. `AnimResetFn` and
// `AnimPlayFn` went with them. Both doors stay registered for outside callers.
// Video buffer platform constructors, called eight times by EdgeMesh's ctor.
//
// RT_EXPORT_ONCE (Wave 9 lane W, 2026-09-08). BOTH NAMES ARE NOW PORTED —
// src/engine/video/buffers.cpp registers them, together with the four other
// Platform{Construct,Destruct} bodies — so on the raw door these two became
// live-path port BYPASSES the moment that landed, with nothing in THIS file
// changing. That is the hazard WAVE9.md's bypass row states in as many words
// ("re-run after every LANDING, not after every edit"), and it was caught here
// by the audit's own denominator moving: `ported names 573 -> 579` and
// `OK-RESOLVE-ONLY 4 -> 11`, the +7 being these two plus five siblings that
// only acquired a verdict because their name became a ported one.
// ENDGAME B.3.5 caller batch: both are DEFINED members of the real
// `Video::{Vertex,Index}Buffer`, so the two accessors are gone with their typedef
// and `EdgeMesh`'s constructor calls the members on its eight buffer sub-objects.

// The vtables the ctors stamp. All four are REAL EXPORTED SYMBOLS in the
// shipped DLL — PLAN §2.2 hazard D was corrected once already ("the DLL exports
// 147 ??_7 vtable symbols") and this lane confirms four more of them by name.
// Stamping the DLL's own vtable, rather than cloning slots into an array we
// own, is what keeps the object dispatchable by the DLL's Renderer after our
// ctor built it (§4.3 risk 3). The per-class argument PLAN demands is easy
// here: these classes are not subclassed anywhere in our tree, our ctor is the
// ONLY writer of slot 0, and every virtual the DLL then dispatches is a DLL
// body this lane did not port.
// PORT-AWARE, and it must be. This helper feeds the vtable a ported ctor
// stamps into slot 0. the raw DLL door is GetProcAddress: at cutover it returns
// NULL for a symbol the Wave 9 mint carries correctly just as much as for one
// it has never heard of, so a raw door here writes a NULL vtable into every
// object of these classes and the first virtual call is an EXEC-of-0.
// ResolveExport checks the port registry first and falls through to the same
// GetProcAddress, so while the DLL is loaded and the mint unregistered this is
// the identical pointer -- the change is a no-op today and a crash avoided at
// cutover. Audited by tools/rt_data_door_audit.py.
// ENDGAME B.3.9 block GFX: `RtVtable` and the five lazy vtable caches
// (MeshInstance, EdgeMeshInstance, EdgeMesh, Object::Base, Object::External)
// are gone -- every class these ctors built is real and the stamps are the
// compiler's.

// Graphics::AnimationManager::_singleton (0x1801fe7b0), the class static:
// GetNodeToWorld and RootAnimationOffset index the instanced pose pool
// through it. B.4: the `RtAnimationManager()` accessor and the `B` / `Rd`
// byte helpers are gone -- every read below is a named field of a typed
// record.

}  // namespace

// ============================================================================
// Graphics::GraphicsMesh::GetMaterialList(int) const
//   ?GetMaterialList@GraphicsMesh@Graphics@@QEBAAEBV?$Array@V?$ExternalPtr@
//    VMaterialInstance@Graphics@@@Object@@@Collection@@H@Z
//   retail 0x1800126c0, ordinal 2768
//
// The whole body is `return *(mesh + 0x70) + index * 0x10` — the +0x70 field is
// a pointer to an ARRAY OF Array headers, one per detail level, and 0x10 is
// sizeof(Collection::Array<T>).
//
// ORACLE: FULL. Pure pointer arithmetic on a const object, no side effects, so
// both implementations can run on the live object and the two results compared
// directly.
// ============================================================================
// ENDGAME B.3.5 block G: BODY MOVED to
// `Graphics::GraphicsMesh::GetMaterialList(int32_t)`. The door keeps its name and
// its registration and delegates with a QUALIFIED non-virtual call.
// ============================================================================
// Graphics::GraphicsMesh::GetNodeIndexFromName(const char*) const
//   ?GetNodeIndexFromName@GraphicsMesh@Graphics@@QEBAHPEBD@Z
//   retail 0x1800fee90, ordinal 2812
//
// A linear scan of the node array comparing each node's name to `name`, with
// retail's own inlined strcmp. Two details that are NOT ordinary strcmp and are
// reproduced exactly rather than replaced with std::strcmp:
//
//  * NULL is a legal name on both sides and compares EQUAL to NULL. If a node's
//    name is null the body returns that node's index when `name` is null too,
//    and otherwise SKIPS the node without comparing — it never dereferences a
//    null (`if (pcVar1 == 0) { if (param_1 == 0) return i; } else if (param_1
//    != 0) {...}`). A null `name` against a named node therefore falls through
//    to "not this one", not to a crash. This is not a guard retail lacks; it is
//    retail's own control flow, and CLAUDE.md's rule about not inventing null
//    checks cuts the other way here — inventing a std::strcmp would ADD a
//    crash retail does not have.
//  * The scan returns the FIRST match, and -1 when nothing matches.
//
// ORACLE: FULL. Const, no side effects.
// ============================================================================
// ENDGAME B.3.5 block G: BODY MOVED to
// `Graphics::GraphicsMesh::GetNodeIndexFromName(const char*)`.
// ============================================================================
// Graphics::MeshInstance::IsAnimating() const
//   ?IsAnimating@MeshInstance@Graphics@@QEBA_NXZ
//   retail 0x1800ff7d0, ordinal 3097
//
// False when no animation layer is active. Otherwise it looks at layer[0]:
//   anim = *(layer->_group) + 0x20  ->  the group's Animation array
//   a    = anim + layer->_animIndex * 0x70
//   return a->_looping (+0x3c, a byte) || layer->_time (+0x18) < a->_length (+0x58)
//
// The three offsets inside Animation (+0x3c, +0x58, stride 0x70) belong to Wave
// 6 lane G and are read here, not pinned here.
//
// PERF: this is called per model per frame from
// src/game/Component/naturalresourcecomponent.cpp and through dispatch slot
// 0x1401514b0. The body below is four loads and a compare — the same work
// retail does — and resolves nothing.
//
// ORACLE: FULL. Const, no side effects.
// ============================================================================
// ENDGAME B.3.5 block E: the BODY MOVED to `Graphics::MeshInstance::IsAnimating`
// at the bottom of this file. This door keeps its name and its registration and
// delegates with a QUALIFIED non-virtual call.
namespace {

// The pool pose the two "instanced" branches share.
//
//   poolId = layers[count - 1]._poolId              (the LAST layer, not the first)
//   bank   = *(AnimationManager::_singleton + 0x20 + ((poolId >> 8) & 0xff) * 0x30)
//   slot   = bank + (poolId & 0xff) * 0x48
//
// Spelled in both decompiles as `*(int*)(count * 0x38 - 0x2c + buffer)`, which
// is `buffer + (count-1)*0x38 + 0x0c` — the last layer's pool id.
//
// *** THE TWO CALLERS DIVERGE AFTER THIS POINT AND THE DIFFERENCE IS ONE EXTRA
// DEREFERENCE. *** RootAnimationOffset reads three floats straight out of the
// slot at +0x18. GetNodeToWorld loads a POINTER out of the slot at +0x38 and
// indexes THAT by node * 0x38 — the slot holds the pool entry's per-node pose
// array, not the poses themselves. Sharing this helper past the slot would have
// made GetNodeToWorld read 0x38-byte strides out of the 0x48-byte pool entry
// itself. The helper therefore stops at the slot and each caller does its own
// last step.
//
// TRANSCRIBED FROM THE DISASSEMBLY, NOT THE DECOMPILE. Two of the three steps
// below were wrong when this was written from Ghidra's C output, and both were
// wrong in ways the C output actively invited (WAVE6_LANE_E.md §6.7, §6.8).
// The retail instructions, identical in both callers
// (0x180016f00-0x180016f39 and 0x180016acd-0x180016b0e):
//
//     MOVSXD RCX, dword ptr [R8 + RAX - 0x2c]   ; poolId, SIGN-extended to 64
//     MOV    RAX, RCX
//     SAR    RAX, 0x8                           ; ARITHMETIC shift
//     MOVZX  EAX, AL                            ; hi = that & 0xff
//     LEA    R8,  [RAX + RAX*2]                 ; hi*3 ...
//     MOV    RAX, qword ptr [0x1801fe7b0]       ; the _singleton VARIABLE's value
//     MOV    RDX, qword ptr [RAX]               ; *** SECOND dereference ***
//     MOVZX  EAX, CL                            ; lo = poolId & 0xff
//     ADD    R8,  R8                            ; ... *2  => hi*0x30
//     LEA    RCX, [RAX + RAX*8]                 ; lo*9, scaled *8 => lo*0x48
//     MOV    RAX, qword ptr [RDX + R8*8 + 0x20] ; bank
//
// Two traps, both recorded here because the decompile hides both:
//
//  1. Ghidra prints `uVar5 = (ulonglong)*(int *)(...)` and then
//     `(longlong)uVar5 >> 8`. The `(ulonglong)` reads as a ZERO extension, so
//     this was first written as a logical shift on a uint32. The instruction is
//     MOVSXD followed by SAR: a SIGN extension and an ARITHMETIC shift. For a
//     negative pool id the two pick DIFFERENT banks 0x30 bytes apart, and
//     0xffffffff is the one negative value where they happen to agree — so the
//     bug hides exactly on the id most likely to appear in a debug session.
//
//  2. `?_singleton@AnimationManager@Graphics@@0PEAV12@EA` is the address OF the
//     pointer variable, and retail dereferences it TWICE: once to get the
//     AnimationManager, and again to get the object the bank table lives in.
//     Ghidra spells that `*(longlong *)AnimationManager::_singleton`, where the
//     bare symbol already means "the contents of 0x1801fe7b0" — so the printed
//     single star is the SECOND dereference, not the first. Doing only one
//     lands `+ 0x20 + hi*0x30` inside the 0x9d8-byte AnimationManager itself,
//     which yields a plausible-looking garbage bank rather than a fault.
//
// ENDGAME B.3.5 block E: retyped from `const RtGraphics::MeshInstance*` to the
// REAL class. Its four callers were all doors in this file and all four are now
// members of `Graphics::MeshInstance`, so the POD spelling had no user left.
//
// B.4: the three steps by name. `_singleton->_instances._data` is the second
// dereference; `[hi]._states._data` the bank; `[lo]` the slot. The slot is the
// 0x48 per-frame record (`Graphics::InstancedAnimationState`).
const ::Graphics::InstancedAnimationState* RtInstancedPoolSlot(const ::Graphics::MeshInstance* mi) {
    // MOVSXD: the stored pool id is a SIGNED int32 widened to 64 bits.
    const int64_t poolId =
        mi->_animation._layers._data[mi->_animation._layers._size - 1]._handle;

    // SAR then MOVZX AL: arithmetic shift of the sign-extended value, low byte.
    const uint64_t hi = static_cast<uint64_t>(static_cast<uint8_t>(poolId >> 8));
    const uint64_t lo = static_cast<uint64_t>(static_cast<uint8_t>(poolId));

    return &::Graphics::AnimationManager::_singleton->_instances._data[hi]._states._data[lo];
}

}  // namespace

// ============================================================================
// Graphics::MeshInstance::GetNodeToWorld(int) const -> Math::Transform
//   ?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@H@Z
//   retail 0x180016a40, ordinal 2813
//
// *** HIDDEN RETURN SLOT. *** The function returns a 0x1c-byte Math::Transform
// BY VALUE, so the MS x64 ABI is RCX = this, RDX = the caller's Transform
// buffer, R8D = the node index, and RAX = RDX on return. Ghidra shows this
// plainly and confusingly at once: it types the second parameter as the `int`
// and then reads the index out of `in_R8D`. Declaring this function without the
// return slot would be the recorded WILD WRITE — a scribble through a stale RCX
// that still returns the right answer, live on every scene load
// (reference_hidden_retslot_wild_write.md, Wave 1's
// fn_Math_Transform3_Identity). tools/abi_retslot_audit.py enforces the rule.
//
// Two branches:
//  * NOT instanced (_animation._instanced == 0): copy _nodes[node]._state[0]
//    (seven dwords) into the return slot. Note it reads the FIRST of the two
//    Transforms in the InterpolatedState.
//  * Instanced: read the pool slot, take the geometry's own transform, and
//    compose — translation = geomTranslation + Rotate(geomRotation, poolPos),
//    rotation = geomRotation * poolRotation. The return slot is pre-seeded with
//    an identity (`w = 1.0f` at +0x0c, zeros elsewhere) BEFORE the two
//    quaternion calls; that store is reproduced because it is observable if
//    either call were ever to fault.
//
// ORACLE: FULL, through a scratch buffer. Ours writes the caller's real slot,
// theirs writes a private 0x1c-byte buffer, and the two are compared byte for
// byte — the diff-oracle header's "functions that write through an out-parameter" case.
// Never let the original write into live state.
// ============================================================================
// ENDGAME B.3.5 block E: the BODY MOVED to
// `Graphics::MeshInstance::GetNodeToWorld(int)`. The door keeps the RETAIL ABI —
// `this` in RCX, the caller's Math::Transform buffer in RDX, RAX = RDX on return
// — because that is what its 246 by-name call sites and the `fn_*` typedefs
// spell. The member returns by value, which is safe for a NON-virtual member
// (both ends of a member call are this same GCC); see Geometry.h's B5-12 block
// for why the same is NOT true of a vtable slot.
// ============================================================================
// Graphics::MeshInstance::RootAnimationOffset() const -> Math::Vector3
//   ?RootAnimationOffset@MeshInstance@Graphics@@QEBA?AVVector3@Math@@XZ
//   retail 0x180016ef0, ordinal 3589
//
// *** HIDDEN RETURN SLOT *** again: RCX = this, RDX = a Vector3 buffer. Same
// rule as GetNodeToWorld above.
//
// Not instanced: the instance's own _rootAnimationOffset (+0x100). Instanced:
// three floats at poolSlot + 0x18. Note the instanced branch reads the pool
// slot ITSELF, with no per-node stride — the root offset is one Vector3 per
// pool entry.
//
// src/ext/render_culling/render_culling.cpp's ANIM_BATCH note ("the move tick's walk
// speed is |MeshInstance::RootAnimationOffset()| per tick", and "instanced
// anims read the AnimationManager pool pose") describes exactly these two
// branches, and this port does not change which branch is taken: it reads the
// same `_instanced` byte perf_experimental reasons about.
//
// ORACLE: FULL, through a scratch buffer, same shape as GetNodeToWorld.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to
// `Graphics::MeshInstance::RootAnimationOffset()`.
// ============================================================================
// Graphics::MeshInstance::UpdateHierarchy()
//   ?UpdateHierarchy@MeshInstance@Graphics@@QEAAXXZ
//   retail 0x1800ff910, ordinal 4518
//
// Recomputes every node's world transform from the geometry transform and the
// mesh's node parent chain. Node 0 is seeded from Geometry::_transform; each
// later node composes its parent's already-computed world transform with its
// own local one. Both halves of each InterpolatedState are written to the same
// value.
//
// *** THIS IS THE HOTTEST FUNCTION IN THIS LANE AND THIS PROJECT HAS PAID FOR
// THIS PATH TWICE. *** `?_rootOnlyUpdates@MeshInstance@Graphics@@0_NA` being
// unregistered cost 38% of sim time
// (project_stress_perf_rootonly_and_libcalls.md), and
// src/ext/render_culling/render_culling.cpp's default-on ANIM_BATCH path reads
// MeshInstance state every frame. Three things this port does about that:
//   * It resolves NOTHING per call. `QuaternionMultiply` is a function-local static
//     resolved once for the life of the process (the raw DLL door); calling
//     the raw DLL door directly here would be the recorded 5.6-point defect.
//   * It does not touch `_rootOnlyUpdates`, which retail's UpdateHierarchy does
//     not read either. The flag is read by MeshInstance::Update, which stays in
//     the DLL, so perf_experimental's save/clear/restore of it keeps working
//     unchanged.
//   * The whole oracle, including its allocation, sits inside `if
//     (the diff-oracle guard)`. A shipping run executes the loop and nothing else.
//
// THE FLOAT MATH IS TRANSCRIBED, NOT REDERIVED. The position half is retail's
// own INLINED quaternion rotate — including the two `* 0.0f` terms, which are
// the zero w of the vector-as-quaternion and which a compiler may not fold
// away without -ffast-math (x * 0.0 is not 0.0 for NaN or -0.0). The
// association of every sum is exactly as the decompiled expression writes it,
// because a reassociated float sum is a real divergence and Wave 1 found one
// (`Transform::FromDirection`). The rotation half is the DLL's own
// Quaternion::operator* — retail emits a direct `CALL 0x18001e280` here, so
// calling the DLL's body is the faithful thing.
//
// THE PARAGRAPH THAT USED TO BE HERE IS NOW FALSE, and Wave 12 corrected it at
// its source. It read: "AND IT LEAVES A WAVE 1 PORT STILL UNEXERCISED,
// deliberately ... So Wave 1's port of `??DQuaternion@Math@@QEBA?AV01@AEBV01@@Z`
// keeps its zero-caller status (it is still on the census orphan list) ...
// Switching this call to ResolveExport would flip it and finally exercise it --
// but that trade is the wave lead's to make, not lane E's."
//
// The trade WAS made, and the comment outlived it. `QuaternionMultiply` at line 261 now
// resolves with RT_EXPORT_ONCE, which DOES consult the port registry (unlike
// the raw DLL door / the raw DLL door, which is GetProcAddress and is always the
// DLL), and it is called at lines 629 and 816. So the Wave 1 port that
// reference_uncalled_port_is_not_verified.md was written about IS exercised now,
// and it is NO LONGER on the census orphan list.
//
// This was found by the stale-verdict self-check added to tools/runtime_census.py
// in Wave 12, on that check's first run: it reported the row's old "caller-less"
// verdict as STALE, and following that back surfaced this comment. Kept as a
// record of the old claim so a reader who meets it quoted elsewhere recognises
// it -- and note the one part still true: routing a neighbouring lane's port
// into this lane's call site does fold that lane's correctness into any
// divergence reported here.
//
// ORACLE: FULL, by REPLAY. UpdateHierarchy is a pure function of (Geometry::
// _transform, mesh nodes) into _nodes — running it twice yields the same array,
// so ours runs, the result is snapshotted, theirs runs on the same object, and
// the two are compared. Nothing is mutated that the second call does not
// rewrite identically. This is the same "FULL (replay)" oracle Wave 5 lane D
// used for Picking::Scene::SetTransform.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to
// `Graphics::MeshInstance::UpdateHierarchy()` at the bottom of this file. (B3R
// W2-RES: its C door, registered by name only and read by nothing, is deleted.)

// ============================================================================
// Graphics::MeshInstance::SetMaterialInstance(int, ExternalPtr<MaterialInstance>)
//   ?SetMaterialInstance@MeshInstance@Graphics@@QEAAXHV?$ExternalPtr@
//    VMaterialInstance@Graphics@@@Object@@@Z
//   retail 0x1800ffe80, ordinal 3908
//
// One store: `materials[index] = value`. No bounds check — retail has none, and
// CLAUDE.md forbids adding one.
//
// ORACLE: FULL by replay. The store is idempotent, so running theirs after ours
// writes the same qword to the same slot and the comparison is real.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to
// `Graphics::MeshInstance::SetMaterialInstance`.
// ============================================================================
// Graphics::MeshInstance::SetMesh(ExternalPtr<GraphicsMesh>)
//   ?SetMesh@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@VGraphicsMesh@
//    Graphics@@@Object@@@Z
//   retail 0x1800ffea0, ordinal 3915
//
// In order:
//   1. store the mesh handle at +0xc0;
//   2. copy the mesh's detail-level-0 material Array into _materials, through
//      the DLL's Array<ExternalPtr>::operator= (0x180014c70);
//   3. OR `1 << *(int*)(material + 0x20)` into Geometry::_materialTypeFlags for
//      every material now in _materials — note the loop reads the count from
//      +0xd0 BEFORE the loop and re-reads the buffer pointer once, exactly as
//      decompiled;
//   4. AnimationState::ResetAnimationState;
//   5. resize _nodes to the mesh's node count (mesh + 0x68);
//   6. UpdateHierarchy;
//   7. re-read the mesh handle from +0xc0 (retail re-loads it; it does NOT
//      reuse the argument) and, if any of the six AABB floats differs from
//      Geometry::_localBounds, copy all six and OR kDirtyBounds (2) into
//      Geometry::_dirtyFlags.
//
// Step 7's comparison is a float `!=` chain, so a NaN in either box makes the
// test "different" and forces the copy. Reproduced as written.
//
// ORACLE: FULL by replay. Every step is idempotent for a fixed argument: the
// array copy re-copies the same elements, the flag ORs re-OR the same bits,
// ResetAnimationState re-clears an already-cleared state, the resize to the
// same count is the "exact-equal is a no-op" path of Array<T>::SetSize, and
// UpdateHierarchy is pure. So ours runs, the whole 0x120-byte object plus both
// heap buffers are snapshotted, theirs runs, and the two are compared.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to `Graphics::MeshInstance::SetMesh`,
// including the "read the stars, not the offsets" note that cost the first
// integration run.
// ============================================================================
// Graphics::MeshInstance::PlayAnimation(ExternalPtr<AnimationGroup>, int,
//                                       const Time&, const Time&, float)
//   ?PlayAnimation@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@
//    VAnimationGroup@Graphics@@@Object@@HAEBVTime@System@@1M@Z
//   retail 0x1800ff840, ordinal 3421
//
// A pure forward into AnimationState::PlayAnimation with two arguments the
// caller does not supply: the mesh's node array (`*(this+0xc0) + 0x60`) and a
// trailing `true`. The callee's mangled name spells its seven parameters
// exactly — (ExternalPtr, int, const Array<Node>&, const Time&, const Time&,
// float, bool) — and the retail call site fills them in that order.
//
// THE RISK HERE IS THE ABI, NOT THE LOGIC. `const Time&` is a POINTER argument
// and this project's recorded trap is that a pointer parameter typed `int` only
// goes wrong from the fifth argument onward, where the stack slot's high dword
// is garbage (reference_hidden_retval_and_int_pointer_args.md). `blendTime` is
// this function's fifth argument and `weight` its sixth, so both live on the
// stack and both are exactly where that trap bites. They are typed `const
// void*` and `float` here, counted against the mangled name, not guessed.
//
// ORACLE: POST-STATE, and it checks the thing that can actually be wrong. There
// is no replay: PlayAnimation starts or blends an animation, so calling the
// original a second time would apply the blend twice. Instead the port verifies
// that the layer AnimationState just wrote carries the group handle and the
// animation index we forwarded — which is what a mis-marshalled argument would
// corrupt. It does NOT verify the two Time references or the weight; those
// three are declared unverified in WAVE6_LANE_E.md rather than covered by a
// predicate that does not actually test them.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to `Graphics::MeshInstance::PlayAnimation`.
// ============================================================================
// Graphics::MeshInstance::MeshInstance()
//   ??0MeshInstance@Graphics@@QEAA@XZ
//   retail 0x1800fffc0, ordinal 275
//
// Geometry base ctor, then the vtable stamp, then this class's own fields. The
// disassembly is unambiguous about the widths and this port matches them: three
// qwords at +0xc0/+0xc8/+0xd0, a single BYTE at +0xd8 (`MOV byte ptr [RAX],CL`),
// qwords at +0xe0/+0xe8, qwords at +0xf0/+0xf8/+0x100, a DWORD at +0x108, four
// 1.0f dwords at +0x10c..+0x118, and a WORD at +0x11c. The last store being a
// word — not a dword — is why sizeof is 0x120 and why +0x11e is left alone.
//
// Note what is NOT written: +0x104 gets no store of its own; it is the second
// half of the +0x100 qword. And Geometry's own ctor leaves +0x14 and +0xbc
// uninitialised (lane D's header records that); this port does not fill them.
//
// ORACLE: FULL, through a scratch buffer. `Geometry::Geometry` is pure field
// initialisation — no allocation, no registration, no global list (checked at
// 0x1800a4270) — so running theirs into a private 0x120-byte buffer has no side
// effect at all, and the two objects can be compared byte for byte. The two
// uninitialised gaps are excluded from the comparison, because comparing
// uninitialised bytes is comparing whatever two different stacks happened to
// hold.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED. Geometry() and the stamp are the
// compiler's chain of the real `Graphics::MeshInstance()`, whose body (the field
// stores) is defined below; every builder spells `::new (p) MeshInstance()`.

// ============================================================================
// Graphics::MeshInstance::MeshInstance(const MeshInstance&)
//   ??0MeshInstance@Graphics@@QEAA@AEBV01@@Z
//   retail 0x1800170c0, ordinal 274
//
// Geometry copy ctor, vtable stamp, then: copy +0xc0; ZERO the three Array
// headers before handing each to its DLL operator= (an operator= that does not
// first see a zeroed destination would free a garbage pointer); copy the
// _instanced byte; copy the eight scalars at +0x100..+0x11c, the last as a WORD.
//
// The zero-then-copy order matters and is the reason this is not a memcpy.
//
// ORACLE: POST-STATE (invariant), and deliberately NOT a scratch comparison.
// Running the original into a scratch buffer would allocate three arrays and
// leave them to be freed, and the only way to free them is ~MeshInstance, which
// runs `Geometry::~Geometry` — and Geometry's copy ctor copied the ATTACH ARRAY,
// so the scratch object's dtor would walk live sibling geometries and clear
// their parent link and their sceneId. That is corruption of live state to run
// an oracle, which the diff-oracle header forbids in as many words ("Never let the original
// write into live state"). The check therefore verifies that every field this
// port itself writes equals the source's, plus that the three copied arrays
// have the source's element counts.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED; its body is the real member copy
// constructor, re-read against retail 0x1800170c0's listing: `CALL 0x180015b60`
// (Geometry's copy ctor -- the base in the init list), the MeshInstance stamp
// (the compiler's), then the stores below in retail's order.
Graphics::MeshInstance::MeshInstance(const MeshInstance& other) : Geometry(other), _mesh(other._mesh) {
    Object::ClassVptr<MeshInstance>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
    _materials._data = nullptr;
    _materials._size = 0;
    _materials._capacity = 0;
    rtcoll::RtColl_ArrayAssign_180014c70(&_materials, &other._materials);

    _animation._instanced = other._animation._instanced;

    _animation._layers._data = nullptr;
    _animation._layers._size = 0;
    _animation._layers._capacity = 0;
    rtcoll::Rt_ArrayAssign_18001b700(&_animation._layers, &other._animation._layers);

    _nodes._data = nullptr;
    _nodes._size = 0;
    _nodes._capacity = 0;
    rtcoll::Rt_ArrayAssign_18001b810(&_nodes, &other._nodes);

    _rootAnimationOffset = other._rootAnimationOffset;
    _color[0] = other._color[0];
    _color[1] = other._color[1];
    _color[2] = other._color[2];
    _color[3] = other._color[3];
    _sortBias = other._sortBias;
}

// ============================================================================
// Graphics::MeshInstance::~MeshInstance()
//   ??1MeshInstance@Graphics@@UEAA@XZ
//   retail 0x180017060, ordinal 632
//
// Free the node array, the animation layer array and the material array — in
// that order, each through its own DLL Array<T>::~Array — then run
// Geometry::~Geometry, which re-stamps Geometry's vtable, detaches every child,
// and frees the attach array.
//
// TWO MANGLING / LIFETIME NOTES, both recorded landmines:
//  * `??1` and `UEAA`. This dtor is VIRTUAL, so it mangles `UEAA`, not `QEAA`.
//    The non-virtual spelling is a name the DLL does not contain and would
//    resolve to NULL in silence behind whatever guard the caller has
//    (reference_phantom_dtor_mangling_qeaa_vs_ueaa.md). The name registered at
//    the bottom of this file was verified against the export directory.
//  * `MeshInstance` is NOT an `Object::Internal` and does NOT live in the
//    handle table, so the swap-remove that Wave 5 found missing from
//    `~Goal` — and that
//    reference_internalfactory_swapremove_wild_write.md describes — has no
//    analogue here. The class derives from Graphics::Geometry, whose dtor is
//    the only registry-like teardown, and it is called. (`EdgeMesh` DOES derive
//    from Object::External; its dtor is not in this lane's scope and is not
//    ported.)
//
// ORACLE: POST-STATE only. There is no way to run both: the original would
// double-free three buffers. The check asserts the three arrays are cleared,
// which is the observable the DLL's own ~Array guarantees and the thing a
// wrong-offset port would get wrong.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED with the RT_DELDTOR row
// 0x180017330 it served (and the exe import slot 0x1401514a8 it filled); the
// real public `~MeshInstance()` below is the body, ~Geometry the compiler's.

// ============================================================================
// Graphics::MeshInstance::MeshInstance() / ~MeshInstance(): the bodies of
// ??0MeshInstance 0x1800fffc0 after Geometry() and the stamp, and of
// ??1MeshInstance 0x180017060 before ~Geometry -- the B.3.8 `InitFields` /
// `ReleaseFields` split, now the members themselves (B.4: by name).
// ============================================================================
//
// B.4 Z MIRROR2 GFX: `_animation` is the real `AnimationState`; its member ctor
// (0x1800fdaf0, inlined by retail at 0x180100009..0x18010000f) is the byte and
// header stores, and ~MeshInstance's body is empty: retail's three releases
// (0x18001c550 / 0x18001c480 / 0x180014d00) are the implicit member dtors of
// _nodes, _animation and _materials, in that reverse declaration order.
Graphics::MeshInstance::MeshInstance() : _mesh(nullptr) {   // +0xc0 first, 0x1800fffe8
    Object::ClassVptr<MeshInstance>::Record(this);   // NOT RETAIL: the vptr hint (object/class_vptr.h)
    _materials._data = nullptr;
    _materials._size = 0;
    _materials._capacity = 0;
    _nodes._data = nullptr;
    _nodes._size = 0;
    _nodes._capacity = 0;
    _rootAnimationOffset.x = 0.0f;
    _rootAnimationOffset.y = 0.0f;
    _rootAnimationOffset.z = 0.0f;
    _color[0] = 1.0f;
    _color[1] = 1.0f;
    _color[2] = 1.0f;
    _color[3] = 1.0f;
    _sortBias = 0;
}

Graphics::MeshInstance::~MeshInstance() {
}

// ============================================================================
// Graphics::EdgeMesh::TriangleVertex::TriangleVertex(const Math::Vector3&)
//   ??0TriangleVertex@EdgeMesh@Graphics@@QEAA@AEBVVector3@Math@@@Z
//   retail 0x1800a2f50, ordinal 462
//
// Three floats scaled by 512.0f and TRUNCATED toward zero to int16 (the retail
// instruction is CVTTSS2SI into a 32-bit register followed by a 16-bit store,
// which is C's float->int conversion followed by a narrowing store — not a
// round), then a zero int16 at +6. The 512.0f is DAT_180161728, read out of the
// shipped DLL as the bytes 00 00 00 44; it is not an inferred "probably 256".
//
// ORACLE: FULL, through a scratch buffer. No side effects.
// ============================================================================
// B3R RT-VG: external linkage (declared in types/graphics/Mesh.h) for the
// exe-side reader in src/game/Player/tool_base_render.cpp.
// B.4 wave Y: `self` is typed. B.4 S4 GFX2: `vec3` is retail's
// `const Math::Vector3&` (as a pointer): its caller's ribbon endpoints
// (tool_base_render.cpp) are Math::Vector3s now, held in Vector3Slots so no
// zeroing constructor runs (retail 0x14004b930 builds each with the 3-float
// ctor, IAT 0x140152a30: three stores, as RelVec's).
TriangleVertex* RtGraphics_EdgeMesh_TriangleVertex_Ctor(TriangleVertex* self, const Math::Vector3* vec3) {
    TriangleVertex* tv = self;
    const Math::Vector3* v = vec3;
    tv->x = static_cast<int16_t>(static_cast<int>(v->x * kEdgeMeshVertexScale));
    tv->y = static_cast<int16_t>(static_cast<int>(v->y * kEdgeMeshVertexScale));
    tv->z = static_cast<int16_t>(static_cast<int>(v->z * kEdgeMeshVertexScale));
    tv->w = 0;

    return self;
}

// ============================================================================
// Graphics::EdgeMesh::EdgeVertex::EdgeVertex(const TriangleVertex&,
//                                            const TriangleVertex&, int)
//   ??0EdgeVertex@EdgeMesh@Graphics@@QEAA@AEBUTriangleVertex@12@0H@Z
//   retail 0x1800a2df0, ordinal 164
//
// The two vertices' xyz shorts side by side, the int argument truncated to
// int16 in the first half's w, and 0 in the second half's w. The `0` in the
// mangled name is a back-reference to the first TriangleVertex parameter, which
// is why the second argument is also a `const TriangleVertex&` and not the
// non-const reference an eye-parse of the decompile suggests.
//
// ORACLE: FULL, through a scratch buffer. No side effects.
// ============================================================================
// B3R RT-VG: external linkage (declared in types/graphics/Mesh.h), as above.
EdgeVertex* RtGraphics_EdgeMesh_EdgeVertex_Ctor(EdgeVertex* self, const TriangleVertex* a,
                                                const TriangleVertex* b, int flags) {
    EdgeVertex* ev = self;
    const TriangleVertex* ta = a;
    const TriangleVertex* tb = b;

    ev->a.x = ta->x;  ev->b.x = tb->x;
    ev->a.y = ta->y;  ev->b.y = tb->y;
    ev->a.z = ta->z;
    ev->a.w = static_cast<int16_t>(flags);
    ev->b.z = tb->z;
    ev->b.w = 0;

    return self;
}

// ============================================================================
// Graphics::EdgeMesh::SetMaterials(ExternalPtr x4)
//   ?SetMaterials@EdgeMesh@Graphics@@QEAAXV?$ExternalPtr@VMaterialInstance@
//    Graphics@@@Object@@000@Z
//   retail 0x1800a2760, ordinal 3910
//
// Four stores — and THE MIDDLE TWO LOOK CROSSED. Retail writes arg1 to +0x28,
// arg2 to +0x38, arg3 to +0x30 and arg4 to +0x40. That is not a decompiler
// artefact (the four MOVs name their source registers explicitly).
//
// *** ENDGAME B.3.5 BLOCK G: NOTHING IS CROSSED, AND THE FOUR GETTERS SAY SO. ***
//     ?GetMaskMaterial@     0x180012ef0  MOV RAX,[RCX+0x28]
//     ?GetSkinMaskMaterial@ 0x180012f10  MOV RAX,[RCX+0x30]
//     ?GetEdgeMaterial@     0x180012f00  MOV RAX,[RCX+0x38]
//     ?GetSkinEdgeMaterial@ 0x180012f20  MOV RAX,[RCX+0x40]
// The FIELDS are laid out (mask, skinMask, edge, skinEdge) — each skin variant
// next to its base — and the ARGUMENTS come in as (mask, edge, skinMask,
// skinEdge). Two groupings of the same four things; no crossing to reproduce and
// nothing to warn a future reader about. The member below names all four
// parameters, so the pairing is visible at the call site instead of being a note.
//
// ORACLE: FULL by replay — four idempotent stores.
//
// ENDGAME B.3.5 block G: BODY MOVED to `Graphics::EdgeMesh::SetMaterials`.
// ============================================================================
// ============================================================================
// Graphics::EdgeMesh::EdgeMesh()
//   ??0EdgeMesh@Graphics@@QEAA@XZ
//   retail 0x1800a2830, ordinal 161
//
// Object::External's ctor inlined (stamp Base's vtable, then External's, then
// zero +0x08/+0x10/+0x18), then EdgeMesh's own vtable, then zero +0x20..+0x40,
// then eight (buffer header, PlatformConstruct) pairs, then the two list Arrays
// and an EMPTY bounds box (min = +1e8, max = -1e8; the constants 0x4cbebc20 and
// 0xccbebc20 were read out of the DLL, not guessed).
//
// The three successive vtable stamps at +0x00 are the base-class chain MSVC
// emits and only the last one survives; they are reproduced because a partially
// constructed object is observable if any of the eight PlatformConstruct calls
// re-enters (it calls into src/render/dx11/ through Device::_singleton's plugin).
//
// ORACLE: POST-STATE only, and the reason is not laziness.
// `Video::VertexBuffer::PlatformConstruct` (0x1800f57e0) asks
// `Video::Device::_singleton`'s plugin for a companion object AND LINKS THE
// BUFFER INTO THE DEVICE'S GLOBAL INTRUSIVE LIST (`_firstBuffer`, under the
// device's critical section). Running the original into a scratch buffer would
// therefore register eight phantom buffers in a live global list and create
// eight orphan dx11 companions — real corruption of live state, which the diff-oracle header
// forbids. The check verifies the fields THIS port writes, and states plainly
// that the eight PlatformConstruct calls are unchecked because they are the
// DLL's own, called unchanged.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED; this is the real
// `Graphics::EdgeMesh()`. The three stamps and External's three zero qwords are
// the compiler's base chain (Object::External()); the body is the rest, in
// retail's order, through the field-identical POD view.
Graphics::EdgeMesh::EdgeMesh() {
    _reserved20 = nullptr;
    _maskMaterial     = nullptr;
    _skinMaskMaterial = nullptr;
    _edgeMaterial     = nullptr;
    _skinEdgeMaterial = nullptr;

    Video::VertexBuffer* const vbufs[4] = { &_vertexBuffer, &_skinVertexBuffer,
                                            &_edgeVertexBuffer, &_edgeSkinVertexBuffer };
    Video::IndexBuffer* const ibufs[4]  = { &_indexBuffer, &_skinIndexBuffer,
                                            &_edgeIndexBuffer, &_edgeSkinIndexBuffer };
    for (int i = 0; i < 4; ++i) {
        // Retail's order is strictly vertex, index, vertex, index, ... and each
        // pair is (zero the dword, zero the flag byte, PlatformConstruct).
        // ENDGAME B.3.5 caller batch: both `PlatformConstruct` bodies are DEFINED
        // members of the real Video buffer classes (video/buffers.cpp).
        vbufs[i]->size = 0;  vbufs[i]->dynamic = false;
        vbufs[i]->PlatformConstruct();
        ibufs[i]->size = 0;  ibufs[i]->dynamic = false;
        ibufs[i]->PlatformConstruct();
    }

    _triangleLists._data = nullptr;
    _triangleLists._size = 0;
    _triangleLists._capacity = 0;
    _edgeLists._data = nullptr;
    _edgeLists._size = 0;
    _edgeLists._capacity = 0;

    // An EMPTY box: +1e8 (0x4cbebc20) x3 then -1e8 (0xccbebc20) x3. Both are
    // exact floats, so the decimal spelling stores retail's bits.
    _aabb.min.x = 1e8f;  _aabb.min.y = 1e8f;  _aabb.min.z = 1e8f;
    _aabb.max.x = -1e8f; _aabb.max.y = -1e8f; _aabb.max.z = -1e8f;
}

// ============================================================================
// Graphics::EdgeMesh::Set(...)  — 25 parameters
//   ?Set@EdgeMesh@Graphics@@QEAA_NHPEBUTriangleList@GraphicsMesh@2@H0HPEAGHPEAX
//    HH1H2HH1H2HH1H2HAEBVBox@Volume@@@Z
//   retail 0x1800a23e0, ordinal 3813
//
// Copy the bounds box, resize and fill the two TriangleList arrays, then upload
// four (index, vertex) buffer pairs and return whether every upload succeeded.
// Each upload is two VIRTUAL calls on the embedded Video buffer object: slot 2
// at vt+0x08 (index 1) to size it and vt+0x10 (index 2) to fill it, and any false
// short-circuits the whole chain — the nesting in the decompile is a chain of
// `if (a && b) { if (c && d) { ... } }`, so a failure at pair 2 leaves pairs 3
// and 4 untouched. That early-out is behaviour, not an optimisation, and is
// reproduced.
//
// The four pairs are, in retail's order:
//   indices @ +0x70 (count*2), vertices @ +0x50 (stride*count)
//   indices @ +0xb0,           vertices @ +0x90
//   indices @ +0xf0,           vertices @ +0xd0
//   indices @ +0x130,          vertices @ +0x110
// Note the INDEX buffer of each pair is uploaded FIRST and that the buffer
// object pointer is at header + 0x08 (what PlatformConstruct wrote), which is
// why the offsets above are 0x08 past the header offsets in the type header.
//
// The 25-parameter signature is counted against the MANGLED NAME, not against
// the decompiler's parameter list — the recorded rule for anything past the
// fourth argument (reference_hidden_retval_and_int_pointer_args.md). Reading it
// out: (int, const TriangleList*, int, const TriangleList*, then four groups of
// (int, ushort*, int, void*, int, int), then const Box&) — where `1` and `2`
// are back-references to `PEAG` (ushort*) and `PEAX` (void*).
//
// ORACLE: POST-STATE only. The four uploads go to the GPU through src/render/dx11/; a
// replay would re-create and re-fill four vertex and four index buffers per
// call, and the second Create on an already-sized buffer is not a proven no-op.
// The check verifies the bounds copy and the two array sizes — the part of the
// body that is this port's own arithmetic — and leaves the eight virtual
// uploads to the return value, which the caller already tests.
// ============================================================================
// ENDGAME B.3.5 closing pass: BODY MOVED to `Graphics::EdgeMesh::Set` (defined
// beside the class's other members, below). Mesh.h declares the member with the
// 25-slot list tokenised out of the mangled name; this door keeps its
// registration and its address and delegates with the QUALIFIED non-virtual call.
// `Set` is not virtual, so the qualification is belt-and-braces, not a recursion
// guard -- but it is the shape every other moved body in this file uses.
// ============================================================================
// Graphics::EdgeMeshInstance::EdgeMeshInstance()
//   ??0EdgeMeshInstance@Graphics@@QEAA@XZ
//   retail 0x1800a3ab0, ordinal 163
//
// Geometry base ctor, vtable stamp, and ONE store: `_mesh = 0`.
//
// *** +0xc8 IS DELIBERATELY LEFT UNINITIALISED, BECAUSE RETAIL LEAVES IT
// UNINITIALISED. *** The copy ctor (0x1800174f0) copies both +0xc0 and +0xc8,
// so the field exists and is propagated, but the default ctor never writes it;
// only EdgeMeshInstance::SetMesh does. This is an uninitialised read in the
// SHIPPED DLL of exactly the kind Wave 5 found in `RemoveMesh`
// (`Entry::_userValue2`). Zeroing it here would make our ctor differ from
// retail's on a live shared object, which is the one thing WAVE6.md says a
// Video/Graphics lane must not do.
//
// ORACLE: FULL, through a scratch buffer — `Geometry::Geometry` has no side
// effects. +0xc8 is excluded from the comparison for the reason above: two
// different stacks hold two different pieces of garbage there, and comparing
// them would report a divergence on every single call.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED; the real public
// `EdgeMeshInstance()` (Mesh.h) is this body -- Geometry() and the stamp the
// compiler's, `_mesh = 0` its one store, NO store to `_followed`.

// ============================================================================
// Graphics::EdgeMeshInstance::EdgeMeshInstance(const EdgeMeshInstance&)
//   ??0EdgeMeshInstance@Graphics@@QEAA@AEBV01@@Z
//   retail 0x1800174f0, ordinal 162
//
// Geometry copy ctor, vtable stamp, copy +0xc0 and +0xc8. Note that copying
// +0xc8 propagates whatever the source holds there, INCLUDING the garbage a
// default-constructed source still has — again, faithful.
//
// A STALE COMMENT THIS PORT DISPROVES, quoted so the next reader recognises it.
// `engine/funcs/engine_funcs.h:5515` says
//     "// ??0EdgeMeshInstance@Graphics@@QEAA@XZ   (Runtime 0x1800174f0)"
// and `engine/funcs/engine_funcs.cpp:3389-3392` uses that address as a FALLBACK
// for the DEFAULT ctor, taken when the by-name resolve of the default ctor
// returns null. The export directory of the shipped DLL says 0x1800174f0
// is the COPY ctor (ordinal 162) and the default ctor is 0x1800a3ab0 (ordinal
// 163). The fallback is latent, not live — the name resolves — but if it ever
// fired it would run the copy ctor with an uninitialised RDX and dereference it
// twice. Lane E is not allowed to edit engine/funcs/, so the correction is
// reported to the wave lead instead; see WAVE6_LANE_E.md.
//
// ORACLE: POST-STATE (invariant), for the same reason as MeshInstance's copy
// ctor: destroying a scratch copy would run Geometry::~Geometry over an attach
// array shared with the source and clear live siblings' parent links.
// ============================================================================
//
// ENDGAME B.3.9 block GFX: the door is RETIRED; its body is the real member
// copy constructor -- Geometry's copy ctor in the init list, the stamp the
// compiler's, then +0xc0 and +0xc8.
Graphics::EdgeMeshInstance::EdgeMeshInstance(const EdgeMeshInstance& other)
    : Geometry(other), _mesh(other._mesh) {      // +0xc0 (typed: copied, not zeroed first)
    _followed = other._followed;
}

// ============================================================================
// Graphics::EdgeMeshInstance::~EdgeMeshInstance()
//   ??1EdgeMeshInstance@Graphics@@UEAA@XZ   retail 0x1800174d0, ordinal 581
//
// ENDGAME B.3.9 block GFX (INVENTORY_B finding 2): until now NOTHING registered
// this address, so RT_DELDTOR row 0x1800175d0 (slot 0 of the minted
// ??_7EdgeMeshInstance@) resolved its ~Class to NULL. Retail's whole body, read
// from the listing:
//     MOV [RSP+8],RCX ; SUB RSP,0x38 ; MOV qword [RSP+0x20],-2
//     ADD RSP,0x38    ; JMP 0x1800a41c0                     ; ~Geometry
// No restamp, and `_mesh` (+0xc0) is NOT released -- the ExternalPtr is a plain
// handle to retail's dtor. The member (below) is therefore empty and the chain
// into ~Geometry is the compiler's; this door, for the minted objects the row
// still destroys, is the same tail into ~Geometry by address (the
// ~MeshInstance door's shape). Row and door both go with the W2 mint row.
// ============================================================================

// ============================================================================
// Graphics::EdgeMeshInstance::SetMesh(ExternalPtr<EdgeMesh>, MeshInstance*)
//   ?SetMesh@EdgeMeshInstance@Graphics@@QEAAXV?$ExternalPtr@VEdgeMesh@
//    Graphics@@@Object@@PEAVMeshInstance@2@@Z
//   retail 0x1800a3a20, ordinal 3914
//
// Store the followed MeshInstance FIRST (+0xc8) and the mesh handle second
// (+0xc0) — retail's order, kept — then OR the four materials' type flags into
// Geometry::_materialTypeFlags in the order +0x38, +0x28, +0x40, +0x30, then
// Geometry::SetBounds(mesh->_bounds).
//
// The OR order is observable: retail writes _materialTypeFlags back to memory
// after EACH of the four ORs (the decompile shows four separate stores), so a
// re-entrant read between them would see the partial value. Reproduced.
//
// ORACLE: FULL by replay. Two idempotent stores, four idempotent ORs, and
// SetBounds — whose own body compares before it writes and only ORs a dirty bit
// — so a second call changes nothing.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to
// `Graphics::EdgeMeshInstance::SetMesh`.
// ============================================================================
// Registration.
//
// Every name below is a string LITERAL at the registration call, never one of
// the local constants, because tools/runtime_census.py cannot see through a
// NAMED CONSTANT — which is exactly how Wave 3's Object count stood at 21/41
// when it was really 27/41 for a whole wave
// (reference_census_cannot_see_nonliteral_registrations.md). The census CAN
// join adjacent literals since Wave 5 lane E added concat_literal() to both
// tools, so the long template names are wrapped for readability.
//
// GetNodeIndexFromName is ALSO registered by ADDRESS. It is the one function in
// this lane with a live `ResolveAddr(Module::Runtime, ...)` call site
// (src/game/Action/showtool.cpp:169); leaving it name-only would put the
// held-item bone lookup on the DLL while every other consumer used the port —
// two implementations taking turns, which is the failure mode Wave 5 lane D
// documented for Picking::Scene::AddMesh.
//
// The four functions in this lane's scope that are NOT registered are listed
// below the register block, each with its reason.
// ============================================================================
// ============================================================================
// WAVE 27 LANE GFXMESH — four more vtable slot bodies on the two instance
// tables. Everything below this banner is that lane's; nothing above it moved.
//
// SUBJECT AND DENOMINATOR. The Wave 27 census prints
//     ??_7EdgeMeshInstance@Graphics@@6B@   4 / 9      (5 / 9 after slot 0)
//     ??_7MeshInstance@Graphics@@6B@       2 / 9      (3 / 9 after slot 0)
// and the four ported here are
//     ?Update@EdgeMeshInstance@               slot 1   0x1800a39b0  ord 4487
//     ?GetInstanceTransform@EdgeMeshInstance@  slot 6   0x1800a3840  ord 2719
//     ?GetSubTransform@MeshInstance@           slot 5   0x180101200  ord 2950
//     ?GetInstanceTransform@MeshInstance@      slot 6   0x180101280  ord 2721
// every address confirmed against the export directory BY VA with pefile
// against the RETAIL Runtime-x64.dll, not by name pattern and not against the
// profile build. They leave EdgeMeshInstance at 7/9 (slots 2 Draw and 3
// AddDrawItems open) and MeshInstance at 6/9 (1 Update, 2 Draw, 3 AddDrawItems
// open). Neither table is COMPLETE and neither is promoted here.
//
// ALL FOUR READ FROM THE DISASSEMBLY, never from Ghidra's C output. Two of the
// four hinge on details that output actively hides — a MOVSXD that prints as a
// zero extension, and a W-COLUMN THAT IS NOT PART OF THE MATRIX — and both are
// called out at the body.
//
// THE CLASS-SPLIT BANNER AT THE TOP OF THIS FILE STILL APPLIES AND IS THE
// REASON THESE FOUR ARE REGISTERED ANYWAY. These are virtual slots the DLL
// dispatches out of its own table, so registering the export redirects nothing
// that runs today — the value is that the four rows become PORTED, which is
// what an unpromoted table's missing slots are (WAVE27_KICKOFF.md §1), and that
// they are there when the table is promoted or the DLL is gone. `calls=` will
// read 0 for all four on a normal run and that is not verification
// (reference_uncalled_port_is_not_verified.md).
//
// KILL SWITCH: RT_GFXMESHVT=0 leaves all four on the DLL.
// ============================================================================

namespace {

// Math::Quaternion::Lerp — retail 0x18001d500, ordinal 3191: the member, by
// value (B3R). HIDDEN RETURN SLOT in retail: RCX = the FROM quaternion, RDX =
// the return buffer, R8 = the TO quaternion, R9 = &t.

// Math::Transform::AsMatrix — retail 0x18001e5f0, ordinal 1619: the member,
// called on typed operands (B.4: the `(xf, ret)` wrapper is gone).

// ENDGAME B.3.5 block E: `RtNodeToWorldInterp` and its `NodeToWorldInterpFn`
// typedef are DELETED. They were a `RT_EXPORT_ONCE` door onto
// `?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@HM@Z` (retail
// 0x180016c20, ordinal 2814 — the (int, float) INTERPOLATED overload, a
// DIFFERENT body from the (int) overload at 0x180016a40; the two mangled names
// differ only in a trailing `M`, which is the pair
// reference_resolveexport_phantom_names.md exists for).
//
// The old note said "it is NOT ported anywhere in this tree … until then it is
// the DLL's" and named it in WAVE27_LANE_GFXMESH.md §6 as INHERITED CUTOVER
// WORK. *** THAT ROW IS NOW CLOSED. *** It was ported in this same file (the
// `RtGraphics_MeshInstance_GetNodeToWorldInterp` door registers 0x180016c20 by
// name), and block E moved that body onto
// `Graphics::MeshInstance::GetNodeToWorld(int, float)`. Its one caller,
// `EdgeMeshInstance::GetInstanceTransform`, now calls the member directly and
// qualified, so the registry hop is gone along with the door.

// ?Lerp@Transform@Math@@QEBA?AV12@AEBV12@AEBVScalar@2@@Z — retail 0x18001d690,
// ordinal 3193. ENDGAME B.3.1 step 2: `Math::Transform::Lerp`, called as the
// member it is; there is no address door left to resolve. RCX = the FROM
// transform, R8 = the TO transform, R9 = &t — the argument order
// wave9_lane_gfx.cpp records from the shipped call site, kept in the shape
// below because the caller works on raw offsets into the interpolated-state
// pair.
// B.4: called as `prev.Lerp(cur, t)` on the typed pair; the wrapper is gone.

// ?SetTransform@Geometry@Graphics@@QEAAXAEBVTransform@Math@@@Z — retail
// 0x1800155a0, ordinal 3992: Graphics::Geometry::SetTransform, the member (B3R).

// ---------------------------------------------------------------------------
// THE SHARED ARITHMETIC OF BOTH GetInstanceTransform BODIES.
//
// Both interpolate a PAIR of Math::Transforms — 0x38 bytes, `cur` at +0x00 and
// `prev` at +0x1c — from prev towards cur by `t`, and both spell it the same way
// the already-ported Graphics::Geometry::GetInstanceTransform (0x1800154a0,
// scene.cpp:1285) spells it:
//     rotation    = Quaternion::Lerp(prev.rotation, cur.rotation, t)
//     translation = prev + (cur - prev) * t,  component by component, x then y
//                                             then z, in the order the shipped
//                                             SUBSS/MULSS/ADDSS triples emit
// The ORDER of the three components is preserved because float arithmetic is
// what it is, not because a comparator could see it: the three lanes do not
// interact, so any order leaves the same bits. It is kept so the body reads as
// a transcription.
//
// A NOTE ON WHICH HALF IS WHICH, because getting it backwards is invisible at
// t = 0 and at t = 1 and wrong everywhere between. Retail loads the FROM
// quaternion from pair+0x1c and the TO from pair+0x00 (`LEA RCX,[R8+0x1c]`
// with R8 = pair, then R8 stays as the third argument), and subtracts
// pair+0x2c.. from pair+0x10.. for the translation. So +0x00 is CURRENT and
// +0x1c is PREVIOUS, and that matches Geometry's +0x84 `_transform` /
// +0xa0 `_prevTransform` ordering in src/engine/graphics/Geometry.h.
// ---------------------------------------------------------------------------
//
// B.4: the pair is named -- `cur` / `prev` are the two halves of an
// `InterpolatedState<Math::Transform>`, or Geometry's `_transform` /
// `_prevTransform`, which are laid out the same way.
void RtLerpTransformPair(const Transform& cur, const Transform& prev, float t,
                         Transform* out) {
    out->rotation = prev.rotation.Lerp(cur.rotation, Math::Scalar(t));

    out->translation.x = prev.translation.x + (cur.translation.x - prev.translation.x) * t;
    out->translation.y = prev.translation.y + (cur.translation.y - prev.translation.y) * t;
    out->translation.z = prev.translation.z + (cur.translation.z - prev.translation.z) * t;
}

}  // namespace

// ============================================================================
// Graphics::EdgeMeshInstance::Update(const System::Time&)   [VIRTUAL, slot 1]
// retail 0x1800a39b0, ordinal 4487
//   ?Update@EdgeMeshInstance@Graphics@@UEAAXAEBVTime@System@@@Z
//
// The whole body, transcribed:
//
//     MOV RDX,[RCX+0xc8] ; TEST RDX,RDX ; JZ out
//     seven dwords from [RDX+0x84 .. +0x9c] into a 0x1c stack local
//     LEA RDX,[local] ; CALL 0x1800155a0            (Geometry::SetTransform)
//
// So an EdgeMeshInstance FOLLOWS the MeshInstance it was attached to: each tick
// it copies that instance's CURRENT Geometry transform (+0x84, the 0x1c
// `_transform`, NOT the interpolated pair) onto itself through the ordinary
// SetTransform setter — which is what raises the dirty bits, so the outline's
// world box is recomputed by Clean like any other geometry's.
//
// THE `Time&` PARAMETER IS NEVER READ. Not an inference: RDX is overwritten by
// the first instruction of the body, before anything could have used it. It is
// kept in the signature because the vtable slot is still called with it.
//
// *** THE NULL TEST IS RETAIL'S OWN AND IS A REAL CASE, NOT A PARANOIA. ***
// src/engine/graphics/Mesh.h records that the default
// ??0EdgeMeshInstance@ ctor leaves +0xc8 UNINITIALISED — only
// EdgeMeshInstance::SetMesh fills it — so `_followed` here can be allocator
// garbage on an instance that was default-constructed and never given a mesh.
// Retail's `TEST RDX,RDX ; JZ` catches a NULL and nothing else; it does not and
// cannot catch garbage. This port reproduces exactly that test and adds nothing
// (CLAUDE.md, "Don't fix a faithful crash with a null check"): a non-null
// garbage pointer faults here precisely as it faults in the DLL, and if it ever
// does, the bug is the missing SetMesh upstream.
//
// ORACLE: NONE, and the reason is a WRITE. SetTransform mutates `this` — it
// rolls _transform into _prevTransform and raises two dirty bits — so a
// differential replay would roll the pair TWICE and hand the renderer a
// previous transform that is one frame too new. That is the same refusal
// wave24_lane_gfxmat.cpp makes for the Serialize pair, for the same structural
// reason. Attendance is recorded; no comparison is emitted.
// ============================================================================

// ============================================================================
// Graphics::MeshInstance::GetSubTransform(int) const        [VIRTUAL, slot 5]
// retail 0x180101200, ordinal 2950
//   ?GetSubTransform@MeshInstance@Graphics@@UEBA?AVTransform@Math@@H@Z
//
//     TEST R8D,R8D ; JNS  -> CALL 0x180016a40 (GetNodeToWorld(node)); return ret
//     otherwise             copy seven dwords [RCX+0x84..+0x9c] -> ret
//
// The OVERRIDE of Geometry::GetSubTransform (0x1800a43c0, scene.cpp:1239), and
// the difference is the whole point of the override: the base IGNORES the node
// index and hands back the geometry's own transform, while MeshInstance looks
// the node up in the mesh skeleton. A NEGATIVE index means "no node" and falls
// back to the base's answer — which is why the base can ignore the argument at
// all.
//
// `JNS`, not `JG`: node == 0 takes the LOOKUP path. Reading this as "> 0" would
// send the root node down the fallback and be wrong only for the root, which is
// the node most likely to look right by accident.
//
// GetNodeToWorld (0x180016a40) IS PORTED, in this same file, and is called
// DIRECTLY rather than through a resolver: retail's own call is a rel32 into
// that body and a direct call is the faithful spelling. Its own diff plumbing
// rides along, which is instrumentation and not behaviour.
//
// HIDDEN RETURN SLOT in RDX, node index in R8D, RAX = RDX on return.
//
// ORACLE: FULL on the fallback arm only. The lookup arm is left to
// GetNodeToWorld's own oracle rather than compared again here — running theirs
// would call the DLL's GetNodeToWorld into a scratch buffer, which is exactly
// the comparison that body already makes, and doing it twice would report one
// divergence as two.
// ============================================================================

// ============================================================================
// Graphics::EdgeMeshInstance::GetInstanceTransform(int, float) const
//                                                            [VIRTUAL, slot 6]
// retail 0x1800a3840, ordinal 2719
//   ?GetInstanceTransform@EdgeMeshInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z
//
// Returns Math::Matrix (64 bytes) BY VALUE: hidden return slot in RDX, a PACKED
// int in R8D, the interpolation factor in XMM3.
//
// THE PACKED ARGUMENT, decoded from the shipped instructions and not guessed:
//     MOV   EAX,R8D
//     SHR   R8D,0xf          ; a LOGICAL shift of the 32-bit half
//     AND   EAX,0x7fff       ; index    = packed & 0x7fff
//     TEST  R8B,0x1          ; whichList = (packed >> 15) & 1
// so this is a 15-bit index plus ONE selector bit — a different packing from
// MeshInstance::GetInstanceTransform's 12 + 4 below, and the two were decoded
// separately for exactly that reason.
//
//   whichList = 1 -> the EdgeMesh's _edgeLists     (+0x158)
//   whichList = 0 -> the EdgeMesh's _triangleLists (+0x148)
// and the entry is `data + index * 0x18`, the TriangleList stride this file
// already uses for EdgeMesh::Set. `entry + 0x14` is the node index.
//
// *** THE NODE INDEX IS READ AS A PLAIN 32-BIT MOV HERE ***
// (`MOV R8D,[RCX+0x14]`), where MeshInstance's body one function over reads the
// same field with MOVSXD. Both are then tested with `TEST/JS`, so the SIGN is
// what both use, and the difference does not change the branch — but it does
// mean this body never widens the value, and the port does not widen it either.
// The distinction is written down because the two neighbouring bodies disagree
// and a reader will assume a transcription error.
//
// TWO ARMS:
//   node >= 0 AND _followed != null:
//       Transform t = _followed->GetNodeToWorld(node, tFactor);   (0x180016c20)
//       AsMatrix(t, ret)
//   otherwise:
//       lerp this's OWN Geometry transform pair (+0x84 cur / +0xa0 prev) by t
//       and AsMatrix that — which is Geometry::GetInstanceTransform's body,
//       inlined by MSVC. It is reproduced through the shared helper above
//       rather than by calling the ported Geometry body, because retail's
//       instructions are here and not a call; the arithmetic is identical
//       either way.
//
// NOTE WHAT THIS ARM DOES **NOT** DO: it does not touch a colour. Compare
// MeshInstance's body below, which overwrites the matrix's fourth column with
// four floats. EdgeMeshInstance has no such field and retail writes the matrix
// AsMatrix produced, untouched. Two sibling overrides of one slot that differ in
// their last four stores; carrying either body's ending over to the other would
// be a silent, plausible, wrong port.
//
// ORACLE: FULL. Pure read on both arms; both sides write private Matrix buffers
// and all 64 bytes are compared.
// ============================================================================

// ============================================================================
// Graphics::MeshInstance::GetInstanceTransform(int, float) const
//                                                            [VIRTUAL, slot 6]
// retail 0x180101280, ordinal 2721
//   ?GetInstanceTransform@MeshInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z
//
// Hidden return slot in RDX, a PACKED int in R8D, the factor in XMM3.
//
// THE PACKED ARGUMENT — 12 + 4, NOT 15 + 1, and NOT unsigned:
//     MOVSXD R9,R8D          ; SIGN-extended to 64 bits first
//     MOV    R8,R9
//     AND    R9D,0xfff       ; element = packed & 0xfff
//     SAR    R8,0xc          ; an ARITHMETIC shift of the SIGN-EXTENDED value
//     AND    R8D,0xf         ; level   = (packed >> 12) & 0xf
//     IMUL   R8,R8,0xb0      ; ... times the detail-level stride
// The MOVSXD/SAR pair is the trap reference_ghidra_c_output_vs_disassembly.md
// names and that this very file already hit once (the pool-id shift in
// RtInstancedPoolSlot above): Ghidra prints the widening as `(ulonglong)`, which
// reads as a zero extension, and a logical shift here would pick a different
// detail level for any negative `packed`. The `AND 0xf` masks the result back
// into range afterwards, so on THIS body the two spellings agree for every
// input — which is worse, not better, because it means no test can tell them
// apart and the next reader has to re-derive it. The port writes the arithmetic
// shift and says so.
//
// THE ADDRESS CHAIN, transcribed:
//     mesh   = this->_mesh                                   (+0xc0)
//     levels = mesh->_detailLevels._data                     (mesh + 0x50)
//     lists  = *(void**)(levels + level*0xb0 + 0x98)
//     node   = *(int32*)(lists + element*0x18 + 0x14)        ; MOVSXD here
// 0xb0 as the detail-level stride and +0x98 as the TriangleList array inside it
// are what src/engine/graphics/Mesh.h's GraphicsMeshView already records for
// `_detailLevels`; 0x18 is the TriangleList stride EdgeMesh::Set uses. Nothing
// in the chain is new to this tree.
//
// WHICH PAIR OF TRANSFORMS GETS LERPED — three cases, and the third is the one
// that matters:
//   node < 0                      -> this's own Geometry pair, +0x84 / +0xa0
//   node >= 0, _animation._instanced == 0
//                                 -> this->_nodes._data + node*0x38
//   node >= 0, instanced          -> the animation pool slot + 0x38, then
//                                    + node*0x38 — the SAME indirection
//                                    RtInstancedPoolSlot above already
//                                    reproduces for GetNodeToWorld, reused here
//                                    rather than re-derived so there is one
//                                    place holding the MOVSXD/SAR/second-deref
//                                    corrections WAVE6_LANE_E.md §6.7-6.8 paid
//                                    for.
//
// *** THE FOURTH COLUMN OF THE RETURNED MATRIX IS NOT PART OF THE MATRIX. ***
// This is the finding of this body and nothing in the decompiled C says it. The
// tail emits SIXTEEN separate MOVSS stores into the return slot, and only
// TWELVE of them come from the matrix AsMatrix produced:
//     ret+0x00,+0x04,+0x08   <- matrix+0x00,+0x04,+0x08
//     ret+0x10,+0x14,+0x18   <- matrix+0x10,+0x14,+0x18
//     ret+0x20,+0x24,+0x28   <- matrix+0x20,+0x24,+0x28
//     ret+0x30,+0x34,+0x38   <- matrix+0x30,+0x34,+0x38
//     ret+0x0c               <- this+0x10c     _color[0]
//     ret+0x1c               <- this+0x110     _color[1]
//     ret+0x2c               <- this+0x114     _color[2]
//     ret+0x3c               <- this+0x118     _color[3]
// The matrix's own +0x0c/+0x1c/+0x2c/+0x3c — which AsMatrix sets to 0,0,0,1 —
// are DISCARDED and the instance colour is packed into that column instead. So
// the returned "Matrix" is a transform whose w column carries RGBA, which is
// how per-instance tint reaches the vertex shader in one buffer. A port that
// copied 64 bytes and then wrote the colour would be RIGHT; a port that copied
// 64 bytes and stopped would look right in every geometric test and render
// everything untinted. The four `_color` floats are at exactly the offsets
// src/engine/graphics/Mesh.h already pins from the constructor's four
// 0x3f800000 stores, which is the independent witness for this reading.
//
// The four column stores are UNCONDITIONAL — they happen on all three arms,
// after the branch rejoins. Nothing about the node lookup changes them.
//
// ORACLE: FULL. Pure read; both sides write private Matrix buffers and all 64
// bytes are compared, the colour column included — it is the half of the result
// a geometric check cannot see.
// ============================================================================

// ============================================================================
// Graphics::MeshInstance::GetNodeToWorld(int, float) const -> Math::Transform
//   ?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@HM@Z
//   retail 0x180016c20, ordinal 2814
//
// *** THIS IS NOT THE FUNCTION 900 LINES ABOVE. *** That one is
// `…?AVTransform@Math@@H@Z`, retail 0x180016a40, ordinal 2813. The two mangled
// names differ only in a trailing `M` and BOTH are exported, which is the pair
// reference_resolveexport_phantom_names.md exists for; both VAs were confirmed
// against the retail export directory with pefile. They are separate bodies:
// 0x180016a40 reads a pose, 0x180016c20 INTERPOLATES between two.
//
// WHY IT IS HERE AND NOT IN THE NEXT LANE'S WORKLIST. It is not a vtable slot of
// any of this lane's three tables, so strictly it is out of scope. It is ported
// because `?GetInstanceTransform@EdgeMeshInstance@` above CALLS it, and porting
// that body made this address newly `called`
// (reference_porting_can_grow_the_cutover_surface.md): the census `called` column
// went 1372 -> 1373 and the BODY gap 18 -> 19. A lane that opens a row and hands
// it on has moved work, not done it — so this closes the one row this lane
// opened, and closes it in the file that already holds the shared helper.
//
// RELATION TO 0x180016a40, WHICH IS EXACT AND IS THE WHOLE DESIGN. Instruction
// for instruction, this is that function with every pose read replaced by a LERP
// of a Transform PAIR by `t`. Same two arms, same `_instanced` byte, same pool
// indirection, same identity seeding, same two-call composition, same hidden
// return slot. Nothing else differs.
//
// *** HIDDEN RETURN SLOT. *** RCX = this, RDX = the caller's Math::Transform
// buffer, R8D = the node index, XMM3 = t, RAX = RDX on return. Declaring it
// without the return slot is the recorded WILD WRITE that still returns the
// right answer (reference_hidden_retslot_wild_write.md).
//
// TWO ARMS:
//
//  * NOT instanced (`_animation._instanced == 0`) — retail emits a CALL, not
//    inline arithmetic:
//        Transform::Lerp(pair+0x1c, ret, pair+0x00, &t)     0x18001d690
//    where pair = _nodes._data + node*0x38. That helper is PORTED and registered
//    BY ADDRESS (wave9_lane_gfx.cpp:555), so the call reaches our own body, and
//    calling it rather than re-deriving its arithmetic is the faithful spelling
//    AND the one that keeps a single owner for it.
//
//  * INSTANCED — retail INLINES the interpolation twice, with
//    Quaternion::Lerp + three SUBSS/MULSS/ADDSS triples each, so the port
//    reproduces it through the shared RtLerpTransformPair helper rather than
//    calling Transform::Lerp. The two spellings compute the same thing; this one
//    is what the shipped instructions are.
//        geomXf = lerp(this->_transform pair at +0x84 / +0xa0, t)
//        poolXf = lerp(poolPoses + node*0x38, t)
//    then the composition is byte for byte 0x180016a40's:
//        ret = identity  (qword at +0x0c = 0x3f800000, everything else 0)
//        ret.translation = geomXf.translation
//                        + Rotate(geomXf.rotation, poolXf.translation)
//        ret.rotation    = geomXf.rotation * poolXf.rotation
//    `poolSlot + 0x38` is a POINTER to the pool entry's per-node pose array and
//    the node index strides THAT, not the slot — the correction
//    RtInstancedPoolSlot's banner paid for, reused here rather than re-derived.
//
// THE IDENTITY SEEDING IS REPRODUCED even though both following calls overwrite
// everything it wrote, for the reason 0x180016a40's port gives: it is observable
// if either call were ever to fault, and a port that computes the same bytes by a
// different route is a different function under any interruption.
//
// ORACLE: FULL, through a scratch buffer. Pure read on both arms; ours writes the
// caller's real slot, theirs writes a private 0x1c buffer, and all seven dwords
// are compared. Never let the original write into live state.
// ============================================================================

// ============================================================================
// ============================================================================
// WAVE 28 LANE SLOTS — the two `AddDrawItems` overrides, DERIVED TOGETHER
// ============================================================================
//
//   ?AddDrawItems@MeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//       retail 0x180100f20, ordinal 1550 — slot 3 of ??_7MeshInstance@
//   ?AddDrawItems@EdgeMeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//       retail 0x1800a2fd0, ordinal 1549 — slot 3 of ??_7EdgeMeshInstance@
//
// **THEY ARE PORTED IN ONE GO ON PURPOSE.** WAVE27_LANE_GFXMESH.md §7 warned
// that porting one without the other "would leave the two overrides of one slot
// derived from different readings of [Renderer::AddDrawItem's] call", and its §4
// is the standing proof that this family goes wrong exactly there: the two
// `GetInstanceTransform` overrides differ ONLY in their last four stores, and
// carrying either body's ending over to the other is silent, plausible and
// wrong. So the nine-argument frame below was decoded once, from the CALLEE's
// mangled name and prologue, and then each override's nine arguments were read
// out of its OWN listing independently.
//
// ---------------------------------------------------------------------------
// THE NINE-ARGUMENT FRAME, decoded once
// ---------------------------------------------------------------------------
// `?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@PEBVMaterialInstance@2@
//  GGMMHPEBXI@Z` — `this` in RCX, then Geometry*, MaterialInstance*, `G`
// (unsigned short) in R9W, and six stack slots at [rsp+0x20 .. +0x48] carrying
// `G M M H PEBX I`. src/engine/graphics/wave9_lane_gfx.cpp ported it in Wave 9
// and confirmed the six stack slots against the prologue; nothing here
// re-derives that.
//
//      arg2  Geometry*           the instance itself, BOTH overrides
//      arg3  MaterialInstance*   selected per list, DIFFERENTLY in each
//      arg4  unsigned short      sort key
//      arg5  unsigned short      packed (group << shift) | listIndex
//      arg6  float               0.0f in BOTH overrides
//      arg7  float               opacity / blend
//      arg8  int
//      arg9  const void*
//      arg10 unsigned
//
// ---------------------------------------------------------------------------
// THE SEVEN PLACES THE TWO OVERRIDES DISAGREE — count the STORES, not the shape
// ---------------------------------------------------------------------------
// Every one of these was read from the override's own bytes. A port that
// derived either from the other would be wrong in all seven.
//
//  1. **LOD.** `MeshInstance` selects a detail level by squared distance and
//     draws ONE level. `EdgeMeshInstance` has no LOD at all and draws TWO
//     fixed lists (triangles then edges), back to back.
//  2. **arg4.** `MeshInstance` computes `(unsigned short)(_sortBias +
//     (unsigned short)Geometry::_detailId)` — a 16-BIT ADD (`ADD 0x28(%rbx),%r9w`),
//     so it WRAPS at 0x10000 rather than promoting. `EdgeMeshInstance`
//     hard-codes **0x2000** for the triangle pass and **0x2001** for the edge
//     pass and never looks at either field.
//  3. **arg5's packing.** `MeshInstance` is `(level << 12) | listIndex`
//     (`SHL $0xc,%r15w`). `EdgeMeshInstance` is `listIndex` for the triangle
//     pass and `listIndex | 0x8000` for the edge pass. Different shift,
//     different width, different meaning.
//  4. **arg7.** `MeshInstance` interpolates: `a*_opacity + (1-a)*_opacityPrev`
//     with `a = Renderer::frameAlpha` (+0x40). `EdgeMeshInstance` passes the
//     CONSTANT **1.0f** — the outline never fades. Both read the same 1.0f at
//     0x18016169c; only one of them subtracts `frameAlpha` from it.
//  5. **arg8.** `MeshInstance` reads it out of the selected detail level
//     (`level + 0xa8`), and OVERRIDES it with 1 on one branch (§below).
//     `EdgeMeshInstance` hard-codes **1**.
//  6. **arg9.** `MeshInstance` passes the GraphicsMesh pointer it re-reads from
//     `_mesh` on every iteration. `EdgeMeshInstance` passes **null**.
//  7. **arg10.** `MeshInstance` passes the animation pool slot id, or
//     0xffffffff when the list is not skinned. `EdgeMeshInstance` hard-codes
//     **0xffffffff**.
//
// What they DO share is the per-list material choice being a SIGN TEST on the
// list's +0x14 field — and even there the two sides of the test select from
// different places.
//
// ---------------------------------------------------------------------------
// ONE CONSTANT, READ FROM THE PE
// ---------------------------------------------------------------------------
// 0x180161694 = `c3 f5 a8 3e 00 00 00 3f` -> the second dword is **0x3f000000
// = 0.5f**, the bounds-centre multiplier. 0x18016169c = **0x3f800000 = 1.0f**.
// Both read with `objdump -s -j .rdata` against the retail file on disk, not
// recognised by eye (kickoff warning 4).
// ============================================================================

namespace {

// B.4: every offset these two bodies read is a named field now:
//   the draw group's view position (+0x90) is `_camera._cameraToWorld
//   .translation` (+0x08 + 0x64 + 0x24; Camera.h's correction 7 names the
//   group's head);
//   GraphicsMesh::DetailLevel (stride 0xb0) is `_distance` (+0x10, compared
//   SQUARED), `_triangleLists` (+0x98 / count +0xa0) and `_maxInstances`
//   (+0xa8, AddDrawItem's arg8);
//   GraphicsMesh::TriangleList (stride 0x18) is `_f10` (+0x10, the index into
//   MeshInstance::_materials) and `_f14` (+0x14, NEGATIVE means "skinned").

// The name door for the append. Spelled on ONE line: runtime_census.py's
// called-scan cannot see a mangled name split across a C++ string-literal
// concatenation (reference_census_three_blind_spots.md), and
// wave27_lane_gfxpart.cpp:625 has exactly such a split at ITS AddDrawItem door
// — so that door may be invisible to the census today. Recorded, not fixed;
// it is another lane's file and this wave builds no tooling.
//
//   SYMBOL:      ?AddDrawItem@Renderer@Graphics@@QEAAXPEBVGeometry@2@PEBVMaterialInstance@2@GGMMHPEBXI@Z
//                retail 0x1801057b0, ordinal 1547.
//   CONDITION:   a public export, ALREADY PORTED (wave9_lane_gfx.cpp). Reached
//                BY NAME so ResolveExport's registry-first lookup hands back our
//                body rather than the DLL's.
//   CONSEQUENCE: null means nothing this instance owns is ever queued — a
//                silent empty screen, which is why the miss is announced once.
// The `AddDrawItemFn` typedef and the `RtRendererAddDrawItem()` resolver that
// used to sit here are GONE -- block GC item 6. Their last two callers are
// `MeshInstance::AddDrawItems` and `EdgeMeshInstance::AddDrawItems` below, and
// both now call `Graphics::Renderer::AddDrawItem` as a member. The cache and its
// one-shot NULL announcement went with the guard, per the block brief's "a
// file-local cache whose last caller you converted goes with its guard".

uint64_t g_addDrawItemsMesh   = 0;   // MeshInstance calls entered
uint64_t g_addDrawItemsMeshQ  = 0;   // ... items actually queued
uint64_t g_addDrawItemsEdge   = 0;
uint64_t g_addDrawItemsEdgeQ  = 0;

}  // namespace

// ============================================================================
// void Graphics::MeshInstance::AddDrawItems(const Scene&, Renderer&) const
//   retail 0x180100f20 — slot 3
// ============================================================================
// THE `Scene&` ARGUMENT IS ACCEPTED AND NEVER READ. RDX is overwritten before
// any use. Stated rather than left to be noticed, because "it takes a Scene, so
// it must consult the scene" is exactly the kind of inference that produces a
// body which compiles and queries the wrong object.
//
// STEP 1 — LOD SELECTION, against the UNION of the two world boxes.
//   `max` of the two boxes' maxima, `min` of the two minima, centre = (min+max)
//   * 0.5f, then the squared distance from the current draw group's view
//   position. Retail sums the terms as `(dy2 + dx2) + dz2` — the ORDER of a
//   float sum is part of the body (reference_moved_term_hides_where_terms_cancel.md),
//   so it is reproduced rather than tidied.
//   The level is then the FIRST whose `switchDistance²` EXCEEDS that distance;
//   if none does, level 0 is used (the counter never advances past the loop
//   because retail falls through to the same join point with r15d still 0).
//   **With one detail level or fewer the whole of step 1 is skipped** — retail
//   tests `levelCount <= 1`, not `< 1`, so a single-level mesh never even reads
//   the bounds.
//
//   A DEAD BRANCH IS REPRODUCED AS DEAD AND NOT AS CODE: after picking a level
//   retail does `TEST EDX,EDX ; JS <epilogue>`. `edx` started at 0 and only
//   increments, so the jump can never be taken. It is noted here instead of
//   being written as an `if` that would read like a real guard.
//
// STEP 2 — one AddDrawItem per list of the selected level.
//   `_mesh` is RE-READ FROM THE INSTANCE ON EVERY ITERATION (`MOV R11,[RBX+0xc0]`
//   is inside the loop), and so is the detail-level array through it. Hoisting
//   them is what a tidy port does and it is not what the machine does; the
//   pointer is also arg9, so a hoist changes what is PASSED, not just what is
//   read.
//
//   THE SKINNED BRANCH, and the one store that makes it worth a paragraph:
//   when a list's `+0x14` is NEGATIVE and the instance is NOT instanced
//   (`_animation._instanced == 0`), retail overwrites **arg8 with 1**, having
//   already loaded the level's own `+0xa8` into it. When it IS instanced it
//   keeps `+0xa8` and puts the pool slot id into arg10 instead. So the two
//   branches differ in WHICH ARGUMENT they touch, not in the value of one — and
//   a port that computed arg10 and left arg8 alone would be wrong on every
//   non-instanced skinned mesh while rendering every other mesh correctly.
// ============================================================================
// ENDGAME B.3.5 block E: BODY MOVED to
// `Graphics::MeshInstance::AddDrawItems`.
extern "C" void RtGraphics_MeshInstance_AddDrawItems(const ::Graphics::MeshInstance* self,
                                                     const ::Graphics::Scene* scene,
                                                     ::Graphics::Renderer* renderer) {
    self->::Graphics::MeshInstance::AddDrawItems(*scene, *renderer);
}

// ============================================================================
// void Graphics::EdgeMeshInstance::AddDrawItems(const Scene&, Renderer&) const
//   retail 0x1800a2fd0 — slot 3
// ============================================================================
// TWO FLAT PASSES over the EdgeMesh's two list arrays — triangles first, then
// edges. No LOD, no bounds, no distance, no opacity: the selection outline is
// drawn at full strength whenever it exists.
//
// `Scene&` is again accepted and never read.
//
// The EdgeMesh pointer is loaded ONCE into RSI for the loop CONDITION (the
// counts) and RE-READ from `_mesh` on every iteration for the MATERIAL (`MOV
// R8,[RDI+0xc0]` inside both loops). That asymmetry is retail's and is
// reproduced: the counts come from the cached pointer, the materials from a
// fresh load.
//
// **AND THE SECOND LOOP RE-READS `_mesh` INTO RSI AGAIN before it starts**
// (0x1800a3087), so the two passes do not necessarily see the same EdgeMesh.
// Nothing in this tree can make them differ today; it is reproduced because a
// reload is an observable act, not a redundancy.
//
// The material choice is a SIGN TEST on each list's +0x14, selecting from
// `_material0`/`_material1` (+0x28/+0x30) in the triangle pass and
// `_material2`/`_material3` (+0x38/+0x40) in the edge pass. Mesh.h already
// records that `SetMaterials` stores its four arguments into those slots with
// THE MIDDLE TWO CROSSED; this body reads them in slot order and inherits that
// note without re-deriving it.
// ============================================================================

// ============================================================================
// HELD BACK — the four names in this lane's scope that are NOT registered.
// ============================================================================
//
// 1. ?AddDrawItems@MeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//    retail 0x180100f20, ordinal 1550.
//    TWO independent reasons, either of which is sufficient.
//    (a) It is VIRTUAL and it is the DLL that dispatches it. Objects built by
//        the ctor above carry ??_7MeshInstance@Graphics@@6B@, the DLL's own
//        vtable, and the only caller is the DLL's Graphics::Renderer walking
//        the scene. Registering the export would redirect nothing that runs;
//        the port would be a registered function with zero callers, which
//        reference_uncalled_port_is_not_verified.md is explicitly about.
//    (b) Registering it by ADDRESS would be ACTIVE HARM. Our tree's only use of
//        0x180100f20 is `src/diag/scene_watch.cpp:401`, and it is not a call
//        — it is an IDENTITY TOKEN. `IsMeshInstance()` resolves the address and
//        compares it against `vtable[3]` of a live object to decide whether the
//        object is a MeshInstance. Register the address and ResolveAddr hands
//        back OUR function, the compare fails for every object, and
//        `BANISHED_LOD_CENSUS` / `BANISHED_FORCE_LOD0` silently see an empty
//        scene. Exactly the shape of `0x1400f2f00` in PLAN §1.1 ("must stay
//        unregistered": registering makes the dispatcher call itself).
//    The body is reversed and understood (it is transcribed in
//    WAVE6_LANE_E.md); it depends on Graphics::Renderer::AddDrawItem, which is
//    lane F's. If lane F ever needs it, it must first give scene_watch a
//    different identity token.
//
//    *** REASON (b) IS DEAD AS OF WAVE 27 LANE GFXMESH. DO NOT REPEAT IT. ***
//    `src/diag/scene_watch.cpp` no longer asks ResolveAddr for that address.
//    `IsMeshInstance` now matches vtable[3] against ResolveRuntimeRaw's
//    registry-FREE answer -- what the DLL's constructor actually stamped, which
//    no registration can move -- OR against RtPortLookupAddr's, our own port if
//    one exists. So registering this symbol can no longer blind the LOD census,
//    and the RT_UNPROMOTABLE tag at standalone/reg_mint_promoted.cpp:2257 is
//    RETIRED (the retirement is recorded beside the tag, not only here).
//    REASON (a) STILL STANDS UNCHANGED, and it is the one that keeps the body
//    unwritten: it is a virtual the DLL dispatches, so a port of it has zero
//    callers until ??_7MeshInstance@ is promoted or the DLL is gone. Lane
//    GFXMESH removed the blocker and did NOT write the body; the addressee for
//    the body itself is named at reg_mint_promoted.cpp:2288.
//
// 2. ?Serialize@MeshInstance@Graphics@@QEAAXAEAVSerializer@IO@@@Z
//    retail 0x1800ff680, ordinal 3707.
//    NO SAFE ORACLE, and the failure mode is the worst one this project has.
//    The body forwards to AnimationState::Serialize, then serialises the
//    material count through the Encoding vtable slot at +0xa0, then (on the
//    read path) resizes the material array and streams it. A differential run
//    would consume or emit the record TWICE and desync the stream — and a
//    stream desync does not surface where it happens, it surfaces later on an
//    innocent record (reference_slot3_wrapper_must_resolve_first.md,
//    reference_serialize_lazy_resolve_and_wrong_slot.md). Wave 5 held four
//    functions back for the same reason (constructing an IO::Serializer emits a
//    12-byte header and admits no oracle) and this lane applies that standard
//    unchanged. There is a second, independent hazard: the Encoding vtable
//    OFFSETS DIFFER between retail and the profile build (+0xa0 here vs +0xb8
//    there, reference_encoding_vtable_offsets_differ_between_builds.md), so a
//    port written from the profile decompile would desync every save while
//    looking correct.
//    It stays on the DLL, where dispatch slot 0x140152990 already points it.
//
// 3. ?_rootOnlyUpdates@MeshInstance@Graphics@@0_NA
//    retail 0x1801fc70b, ordinal 4796.
//    A DATA export — a single static bool, not a function. The runtime registry
//    redirects CALLS; there is nothing to register, and its three consumers
//    (citizenai_executor.cpp, address_registry.cpp:1086,
//    render_culling.cpp:125) already resolve it correctly and read/write it
//    in place. Wave 3 and Wave 4 left `FileSystem::_singleton` and
//    `MemoryAllocator::_singleton` unregistered for the same reason: a data
//    symbol cannot be registered before cutover. Verified present in the export
//    directory so that the counts in WAVE6.md are not silently short by one.
//
// 4. ?_typeInfo@GraphicsMesh@Graphics@@0VBaseTypeInfo@Object@@A
//    retail 0x1801fa708, ordinal 4869.
//    Also a DATA export — the class's Object::BaseTypeInfo instance, resolved
//    by `standalone/reg_unreg_lane3.cpp:193` and stored into an exe .rdata
//    slot. Same reasoning as 3.
//
// ============================================================================
// NOT A HOLD-BACK, BUT SAY IT PLAINLY: the EdgeMesh family may not execute.
//
// `standalone/address_registry.cpp:584` describes RegisterPlayerRenderBatch —
// the selection-outline chain that is the ONLY consumer of EdgeMesh,
// EdgeMeshInstance and the two vertex ctors — as "GATED default-OFF on
// BANISHED_SELECTION_OUTLINE". If that is still true, all seven EdgeMesh ports
// above will report `calls=0` in the diff summary, and a port with zero callers
// is not verified no matter how clean the run looks
// (reference_uncalled_port_is_not_verified.md). The wave lead should take one
// run with BANISHED_SELECTION_OUTLINE=1 and a building selected, and read the
// `calls=` column, not just `divergences=`.
// ============================================================================

// ############################################################################
// ENDGAME B.3 step 5 (block E) — THE BODIES ABOVE, MOVED ONTO THE MEMBERS
// ############################################################################
//
// Fifteen bodies in this file belong to `Graphics::MeshInstance` and
// `Graphics::EdgeMeshInstance`, which are real C++ classes as of block E
// (`src/engine/graphics/Mesh.h`). Each body MOVED here; each registered door
// above kept its name and its address and became a one-line delegation with a
// QUALIFIED non-virtual call (`reinterpret_cast<Ns::C*>(self)->Ns::C::M(args)`).
// The qualification is load-bearing, not decoration: a live instance in this
// build wears the MINTED table out of `standalone/rt_data_mint_tables.inc`,
// whose slots resolve back to these very doors, so an unqualified virtual call
// inside a door would dispatch into itself (B34_BRIEFS §A.1).
//
// WHAT DID **NOT** MOVE, and why:
//
//  * `??0MeshInstance`, `??0MeshInstance(const&)`, `??1MeshInstance`,
//    `??0EdgeMeshInstance`, `??0EdgeMeshInstance(const&)`. ENDGAME §B.1
//    decision 2 and B35_RULES §11 keep the constructor and destructor doors:
//    there is NO real constructor on a polymorphic class here, and the classes
//    are ABSTRACT (slot 0's body is a minted row), so a door physically cannot
//    placement-new one. The destructor additionally must not move: it frees the
//    three `Array` buffers through retail's own out-of-line `~Array` and then
//    runs `~Geometry`, and a compiler-emitted member/base destructor chain on
//    top of that would be retail's own decomposition run twice (the
//    `Object::External::_refs` discussion in External.h).
//  * `GraphicsMesh::GetMaterialList`, `GraphicsMesh::GetNodeIndexFromName`,
//    `EdgeMesh::*` (the two vertex ctors, `SetMaterials`, `EdgeMesh::EdgeMesh`,
//    `EdgeMesh::Set`). `Graphics::GraphicsMesh` and `Graphics::EdgeMesh` are
//    BLOCK G's classes; this file holds a fragment of them and the brief leaves
//    such a body as a door untouched. All seven are listed in block E's report.
//
// NAME-LOOKUP HYGIENE (B34B_RULES §7.2, armed here for the first time in this
// directory): inside `namespace Graphics` a bare `Geometry`, `MeshInstance`,
// `EdgeMeshInstance` or `EdgeMesh` binds the REAL CLASS, not the `RtGraphics`
// POD the `using` declarations at the top of this file introduce. Every POD
// spelling below is therefore qualified `RtGraphics::`, and the file-local
// helpers (`B`, `Rd`, `RtInstancedPoolSlot`, `RtLerpTransformPair`,
// `QuaternionRotate`, `TransformAsMatrix`, `RtRendererAddDrawItem`, the
// `kDetail_*`/`kList_*` constants and the four counters) are reached by
// unqualified lookup falling through to the global unnamed namespace, which is
// what B34_BRIEFS §A.2 records as the arrangement that works.
// ############################################################################

namespace Graphics {

// ---------------------------------------------------------------------------
// ?IsAnimating@MeshInstance@Graphics@@QEBA_NXZ            retail 0x1800ff7d0
// ---------------------------------------------------------------------------
bool MeshInstance::IsAnimating() const {
    if (_animation._layers._size == 0)
        return false;

    // `layer->_group->_clips + animIndex * 0x70`: GetAnimation, inlined.
    const AnimationState::Layer& layer = _animation._layers._data[0];
    const Animation& anim = layer._group->GetAnimation(layer._clipIndex);
    if (anim._looping)
        return true;
    return layer._time._ticks < anim._duration;
}

// ---------------------------------------------------------------------------
// ?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@H@Z
//                                                        retail 0x180016a40
// NON-VIRTUAL, so the C++ by-value return is safe: it is never reached through
// a vtable slot, both ends of every call are this same GCC, and the registered
// door keeps retail's `(self, ret, node)` shape as a separate function. (A
// VIRTUAL by-value return would NOT be safe — see Geometry.h's B5-12 block.)
// ---------------------------------------------------------------------------
Math::Transform MeshInstance::GetNodeToWorld(int node) const {
    Math::Transform result;

    if (!_animation._instanced) {
        result = _nodes._data[node]._current;             // seven dwords, the CURRENT half
    } else {
        // The geometry transform, copied into a local first exactly as retail
        // does (it materialises all seven floats before either call).
        const Math::Transform geomXf = _transform._current;

        // The slot's `_nodes._data` is the pool entry's per-node pose array; the
        // node index strides THAT, not the slot. See RtInstancedPoolSlot.
        const Math::Transform poolXf = RtInstancedPoolSlot(this)->_nodes._data[node]._current;

        // Retail seeds the return slot with the identity transform before the
        // two calls: qword at +0x0c = 0x3f800000, everything else zero.
        result.rotation.x = 0.0f; result.rotation.y = 0.0f; result.rotation.z = 0.0f;
        result.rotation.w = 1.0f;
        result.translation.x = 0.0f; result.translation.y = 0.0f; result.translation.z = 0.0f;

        // translation = geom.translation + Rotate(geom.rotation, pool.translation)
        const Math::Vector3 rotated = geomXf.rotation.Rotate(poolXf.translation);
        result.translation.x = geomXf.translation.x + rotated.x;
        result.translation.y = geomXf.translation.y + rotated.y;
        result.translation.z = geomXf.translation.z + rotated.z;
        // rotation = geom.rotation * pool.rotation
        result.rotation = geomXf.rotation * poolXf.rotation;
    }

    return result;
}

// ---------------------------------------------------------------------------
// ?GetNodeToWorld@MeshInstance@Graphics@@QEBA?AVTransform@Math@@HM@Z
//                                       retail 0x180016c20 — the INTERPOLATED
// overload, a DIFFERENT body from the one above (the mangled names differ only
// in the trailing `M`; both VAs were confirmed against the export directory).
// Also non-virtual, so also a safe C++ by-value return.
// ---------------------------------------------------------------------------
Math::Transform MeshInstance::GetNodeToWorld(int node, float t) const {
    Math::Transform result;

    if (!_animation._instanced) {
        // Transform::Lerp(prev, ret, cur, &t) -- 0x18001d690, a CALL in retail.
        const InterpolatedState<Math::Transform>& pair = _nodes._data[node];
        result = pair._previous.Lerp(pair._current, Math::Scalar(t));
    } else {
        Transform geomXf;
        RtLerpTransformPair(_transform._current, _transform._previous, t, &geomXf);

        const InterpolatedState<Math::Transform>& pose =
            RtInstancedPoolSlot(this)->_nodes._data[node];
        Transform poolXf;
        RtLerpTransformPair(pose._current, pose._previous, t, &poolXf);

        result.rotation.x = 0.0f; result.rotation.y = 0.0f; result.rotation.z = 0.0f;
        result.rotation.w = 1.0f;
        result.translation.x = 0.0f; result.translation.y = 0.0f; result.translation.z = 0.0f;

        const Math::Vector3 rotated = geomXf.rotation.Rotate(poolXf.translation);
        result.translation.x = geomXf.translation.x + rotated.x;
        result.translation.y = geomXf.translation.y + rotated.y;
        result.translation.z = geomXf.translation.z + rotated.z;
        result.rotation = geomXf.rotation * poolXf.rotation;
    }

    return result;
}

// ---------------------------------------------------------------------------
// ?RootAnimationOffset@MeshInstance@Graphics@@QEBA?AVVector3@Math@@XZ
//                                                        retail 0x180016ef0
// ---------------------------------------------------------------------------
Math::Vector3 MeshInstance::RootAnimationOffset() const {
    Math::Vector3 result;
    if (!_animation._instanced) {
        result = _rootAnimationOffset;
    } else {
        result = RtInstancedPoolSlot(this)->_rootOffset;   // the slot's +0x18
    }
    return result;
}

// ---------------------------------------------------------------------------
// ?UpdateHierarchy@MeshInstance@Graphics@@QEAAXXZ         retail 0x1800ff910
//
// THE HOTTEST BODY IN THIS FILE. It resolves NOTHING per call and it does not
// read `_rootOnlyUpdates` (retail's UpdateHierarchy does not either — the flag
// is `MeshInstance::Update`'s, in wave29_lane_slots.cpp). Every float
// association below is transcribed, including the two `* 0.0f` terms.
// ---------------------------------------------------------------------------
void MeshInstance::UpdateHierarchy() {
    const int count = _nodes._size;
    if (count == 0)
        return;

    InterpolatedState<Math::Transform>* const xf = _nodes._data;
    const GraphicsMesh* const mesh = GetMesh().Get();

    // Node 0: the geometry's own transform, in both halves.
    xf[0]._current  = _transform._current;
    xf[0]._previous = xf[0]._current;

    const GraphicsMesh::Node* const nodes = mesh->GetNodes()._data;
    for (int i = 1; i < count; ++i) {
        const GraphicsMesh::Node& node = nodes[i];
        const int32_t parent = node._parent;
        const Math::Transform& p = xf[parent]._current;

        const float qx = p.rotation.x;
        const float qy = p.rotation.y;
        const float qz = p.rotation.z;
        const float qw = p.rotation.w;
        const float px = p.translation.x;
        const float py = p.translation.y;
        const float pz = p.translation.z;

        const Math::Transform& localXf = node._local;
        const float vx = localXf.translation.x;
        const float vy = localXf.translation.y;
        const float vz = localXf.translation.z;

        // Conjugate components; the w component is deliberately NOT negated.
        const float cx = -qx, cy = -qy, cz = -qz;

        // t = q * (v, 0) -- association exactly as decompiled
        const float tx = (qw * vx + qx * 0.0f + qz * vy) - qy * vz;
        const float ty = (qy * 0.0f - qz * vx) + qw * vy + qx * vz;
        const float tz = ((qy * vx + qz * 0.0f) - qx * vy) + qw * vz;
        const float tw = ((qw * 0.0f - qx * vx) - qy * vy) - qz * vz;

        // world.translation = p + (t * conj(q)).xyz, with p added LAST.
        const float rx = (qw * tx + tw * cx + tz * cy) - ty * cz;
        const float ry = (qw * ty - tz * cx) + tw * cy + tx * cz;
        const float rz = ((ty * cx + qw * tz) - tx * cy) + tw * cz;
        const float wx = px + rx;
        const float wy = py + ry;
        const float wz = pz + rz;

        const Math::Quaternion q = p.rotation * localXf.rotation;

        InterpolatedState<Math::Transform>& dst = xf[i];
        dst._current.rotation      = q;
        dst._current.translation.x = wx;
        dst._current.translation.y = wy;
        dst._current.translation.z = wz;
        dst._previous = dst._current;             // second half = first half
    }
}

// ---------------------------------------------------------------------------
// ?SetMaterialInstance@MeshInstance@Graphics@@QEAAXHV?$ExternalPtr@…@Z
//                                                        retail 0x1800ffe80
// One store, no bounds check — retail has none and CLAUDE.md forbids adding one.
// ---------------------------------------------------------------------------
void MeshInstance::SetMaterialInstance(int index, Object::ExternalPtr<MaterialInstance> value) {
    _materials._data[index] = value;
    // EXT SEAM (NOT RETAIL): ext.create_on_demand's prefetch (Mesh.h has the contract).
    if (_extMaterialAssigned != nullptr) _extMaterialAssigned(value.Get());
}

void (*MeshInstance::_extMaterialAssigned)(const MaterialInstance* material) = nullptr;
void (*MeshInstance::_extLevelGathered)(const MeshInstance& instance, int32_t level) = nullptr;

// ---------------------------------------------------------------------------
// ?SetMesh@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@…@Z retail 0x1800ffea0
//
// The one trap this body cost an integration run for is preserved in the
// comment at the array copy: GraphicsMesh+0x70 is the `_skinMaterials` Array
// of Arrays, so the source of the copy is its `_data` (the first skin's
// Array header), not the address of the field. Read the stars, not the offsets.
// ---------------------------------------------------------------------------
void MeshInstance::SetMesh(Object::ExternalPtr<GraphicsMesh> mesh) {
    _mesh = mesh;
    const GraphicsMesh* const newMesh = mesh.Get();
    rtcoll::RtColl_ArrayAssign_180014c70(&_materials, newMesh->_skinMaterials._data);

    if (_materials._size > 0) {
        uint32_t flags = _materialTypeFlags;
        const Object::ExternalPtr<MaterialInstance>* mats = _materials._data;
        for (int32_t n = _materials._size; n != 0; --n) {
            const MaterialInstance* mat = mats->Get();
            ++mats;
            flags |= 1u << (static_cast<uint8_t>(mat->_shaderClass) & 0x1f);
            _materialTypeFlags = flags;          // retail stores back each time
        }
    }

    _animation.ResetAnimationState();
    rtcoll::Rt_SetSize_18005ac50(&_nodes, newMesh->_nodes._size);
    this->MeshInstance::UpdateHierarchy();

    const GraphicsMesh* const m = GetMesh().Get();   // re-read, as retail does
    if (_localBounds.min.x != m->_aabb.min.x || _localBounds.min.y != m->_aabb.min.y ||
        _localBounds.min.z != m->_aabb.min.z || _localBounds.max.x != m->_aabb.max.x ||
        _localBounds.max.y != m->_aabb.max.y || _localBounds.max.z != m->_aabb.max.z) {
        _localBounds.min.x = m->_aabb.min.x;
        _localBounds.min.y = m->_aabb.min.y;
        _localBounds.min.z = m->_aabb.min.z;
        _localBounds.max.x = m->_aabb.max.x;
        _localBounds.max.y = m->_aabb.max.y;
        _localBounds.max.z = m->_aabb.max.z;
        _dirtyFlags |= 2u;
    }

    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): ext.create_on_demand queues
    // the creates this instance's first draw needs (Mesh.h). Null: nothing.
    if (_extMeshAssigned != nullptr) _extMeshAssigned(*this);
}

void (*MeshInstance::_extMeshAssigned)(const MeshInstance& instance) = nullptr;

// ---------------------------------------------------------------------------
// ?PlayAnimation@MeshInstance@Graphics@@QEAAXV?$ExternalPtr@…HAEBVTime@…1M@Z
//                                                        retail 0x1800ff840
// `startTime` and `blendTime` stay `const void*` in the declaration: that is the
// signature code/Game's callers hold. They are the FIFTH and SIXTH arguments,
// which is exactly where the recorded pointer-typed-as-int trap bites, so they
// are typed against the callee's mangled name rather than guessed.
// ---------------------------------------------------------------------------
void MeshInstance::PlayAnimation(Object::ExternalPtr<AnimationGroup> group, int index,
                                 const System::Time* startTime, const System::Time* blendTime,
                                 float weight) {
    // ENDGAME B.3.5 caller batch. `GetMesh()->_nodes` is the embedded
    // `Array<Node>` at mesh+0x60, handed over by ADDRESS (Mesh.h's banner).
    _animation.PlayAnimation(
            group,
            index,
            GetMesh()->GetNodes(),
            *startTime,
            *blendTime,
            weight, true);
}

// ---------------------------------------------------------------------------
// ?GetSubTransform@MeshInstance@Graphics@@UEBA?AVTransform@Math@@H@Z  [slot 5]
//                                                        retail 0x180101200
// `JNS`, not `JG`: node == 0 takes the LOOKUP path.
// The hidden return slot is spelled OUT, in retail's order — see Geometry.h.
// ---------------------------------------------------------------------------
Math::Transform* MeshInstance::GetSubTransform(Math::Transform* ret, int node) const {
    if (node >= 0) {                              // JNS: zero takes this arm
        *ret = this->MeshInstance::GetNodeToWorld(node);
        return ret;
    }
    *ret = _transform._current;                            // seven dwords
    return ret;
}

// ---------------------------------------------------------------------------
// ?GetInstanceTransform@MeshInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z [slot 6]
//                                                        retail 0x180101280
//
// THE PACKED ARGUMENT IS 12 + 4 AND IT IS SIGNED (MOVSXD then SAR), and THE
// FOURTH COLUMN OF THE RETURNED MATRIX IS NOT PART OF THE MATRIX — it carries
// the instance colour. Both findings are written out at the door above; neither
// is re-derived here.
// ---------------------------------------------------------------------------
Math::Matrix* MeshInstance::GetInstanceTransform(Math::Matrix* ret, int packed,
                                                 float t) const {
    // MOVSXD then SAR: a SIGNED widening and an ARITHMETIC shift.
    const int64_t wide    = static_cast<int64_t>(packed);
    const int64_t element = wide & 0xfff;
    const int64_t level   = (wide >> 12) & 0xf;

    const GraphicsMesh* const mesh = GetMesh().Get();
    const GraphicsMesh::TriangleList* const lists =
        mesh->_detailLevels._data[level]
            ._triangleLists._data;
    const int64_t node = lists[element]._f14;     // MOVSXD

    Transform xf;
    if (node < 0) {
        RtLerpTransformPair(_transform._current, _transform._previous, t, &xf);   // +0x84 cur / +0xa0 prev
    } else if (!_animation._instanced) {
        const InterpolatedState<Math::Transform>& pair = _nodes._data[node];
        RtLerpTransformPair(pair._current, pair._previous, t, &xf);
    } else {
        const InterpolatedState<Math::Transform>& pair =
            RtInstancedPoolSlot(this)->_nodes._data[node];
        RtLerpTransformPair(pair._current, pair._previous, t, &xf);
    }

    const Matrix m = xf.AsMatrix();

    // Twelve matrix stores, then the four colour stores. `m.m[3]`, `m.m[7]`,
    // `m.m[11]` and `m.m[15]` are deliberately never read.
    float* const out = ret->m;
    out[0]  = m.m[0];  out[1]  = m.m[1];  out[2]  = m.m[2];   out[3]  = _color[0];
    out[4]  = m.m[4];  out[5]  = m.m[5];  out[6]  = m.m[6];   out[7]  = _color[1];
    out[8]  = m.m[8];  out[9]  = m.m[9];  out[10] = m.m[10];  out[11] = _color[2];
    out[12] = m.m[12]; out[13] = m.m[13]; out[14] = m.m[14];  out[15] = _color[3];

    return ret;
}

// ---------------------------------------------------------------------------
// ?AddDrawItems@MeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z [slot 3]
//                                                        retail 0x180100f20
//
// The `Scene&` argument is accepted and NEVER READ (RDX is overwritten at
// 0x180100f65).
// ---------------------------------------------------------------------------
void MeshInstance::AddDrawItems(const Scene& scene, Renderer& renderer) const {
    (void)scene;                      // RDX is overwritten at 0x180100f65
    ++g_addDrawItemsMesh;

    const GraphicsMesh* const mesh = GetMesh().Get();          // 180100f32
    const float alpha = renderer._alphaT;                      // Renderer::_alphaT

    int32_t level = 0;                                         // r15d
    const int32_t levelCount = mesh->_detailLevels._size;      // 180100f57

    if (levelCount > 1) {                                      // 180100f5b  JLE
        // The current draw group's view position: its camera's world
        // translation (group + 0x08 + 0x64 + 0x24 == group + 0x90).
        const Math::Vector3& view =
            renderer._drawGroups._data[renderer._currentDrawGroup]
                ._camera._cameraToWorld.translation;

        const float maxZ = (_prevWorldBounds.max.z > _worldBounds.max.z) ? _prevWorldBounds.max.z : _worldBounds.max.z;
        const float maxY = (_prevWorldBounds.max.y > _worldBounds.max.y) ? _prevWorldBounds.max.y : _worldBounds.max.y;
        const float maxX = (_prevWorldBounds.max.x > _worldBounds.max.x) ? _prevWorldBounds.max.x : _worldBounds.max.x;
        const float minZ = (_worldBounds.min.z > _prevWorldBounds.min.z) ? _prevWorldBounds.min.z : _worldBounds.min.z;
        const float minY = (_worldBounds.min.y > _prevWorldBounds.min.y) ? _prevWorldBounds.min.y : _worldBounds.min.y;
        const float minX = (_worldBounds.min.x > _prevWorldBounds.min.x) ? _prevWorldBounds.min.x : _worldBounds.min.x;

        const float kHalf = 0.5f;                              // 0x180161694
        const float cx = (minX + maxX) * kHalf;
        const float cy = (minY + maxY) * kHalf;
        const float cz = (minZ + maxZ) * kHalf;

        float dx = view.x - cx;
        float dy = view.y - cy;
        float dz = view.z - cz;
        dx *= dx; dy *= dy; dz *= dz;
        const float distSq = (dy + dx) + dz;                   // retail's order

        const GraphicsMesh::DetailLevel* const lvl =
            mesh->_detailLevels._data;
        for (int32_t i = 0; i < levelCount; ++i) {             // 180101070
            const float d = lvl[i]._distance;
            if (d * d > distSq) { level = i; break; }          // COMISS / JA
        }
        // The `TEST EDX,EDX ; JS` that follows is unreachable — see the door.
    }

    const GraphicsMesh::DetailLevel* const levels =                              // 18010109a
        mesh->_detailLevels._data;
    // EXT SEAM (NOT RETAIL, docs/extensions/handins/CREATEONDEMAND.md): the gather's prefetch
    // (Mesh.h has the contract).
    if (_extLevelGathered != nullptr) _extLevelGathered(*this, level);
    const int32_t listCount = levels[level]._triangleLists._size;
    if (listCount <= 0) return;                                // 1801010c0

    const uint16_t packedLevel = static_cast<uint16_t>(
        static_cast<uint16_t>(level) << 12);                   // SHL $0xc,%r15w
    const float kOne = 1.0f;                                   // 0x18016169c
    const float oneMinusAlpha = kOne - alpha;                  // SUBSS XMM6,XMM10

    for (int32_t i = 0; i < listCount; ++i) {                  // 1801010f0
        // BOTH re-read inside the loop, exactly as retail does.
        const GraphicsMesh* const meshNow = GetMesh().Get();
        const GraphicsMesh::TriangleList* const lists = levels[level]._triangleLists._data;

        uint32_t arg10 = 0xffffffffu;                          // OR $-1,%r10d
        int32_t arg8 = meshNow->_detailLevels._data[level]._maxInstances;

        if (lists[i]._f14 < 0) {                               // 180101103  JGE
            if (!_animation._instanced) {                      // 180101116
                arg8 = 1;                                      // 180101138
            } else {
                arg10 = static_cast<uint32_t>(
                    _animation._layers._data[_animation._layers._size - 1]._handle);
            }
        }

        const int32_t matIndex = lists[i]._f10;
        const MaterialInstance* material = _materials._data[matIndex].Get();

        // 16-BIT add: `ADD 0x28(%rbx),%r9w`. It WRAPS; it does not promote.
        const uint16_t arg4 = static_cast<uint16_t>(
            static_cast<uint16_t>(_sortBias) +
            static_cast<uint16_t>(_detailId));

        const uint16_t arg5 = static_cast<uint16_t>(packedLevel |
                                                    static_cast<uint16_t>(i));

        const float arg7 = (alpha * _opacity) + (oneMinusAlpha * _opacityPrev);

        renderer.Graphics::Renderer::AddDrawItem(
                static_cast<const Geometry*>(this), material, arg4, arg5,
            0.0f, arg7, arg8, meshNow, arg10);                 // 18010119e
        ++g_addDrawItemsMeshQ;
    }
}

// ??1EdgeMeshInstance@Graphics@@UEAA@XZ 0x1800174d0 -- retail's body is the
// bare tail into ~Geometry (see `RtGraphics_EdgeMeshInstance_Dtor`), which the
// compiler's chain is. Empty. (ENDGAME B.3.9 block GFX.)
EdgeMeshInstance::~EdgeMeshInstance() {}

// ---------------------------------------------------------------------------
// ?Update@EdgeMeshInstance@Graphics@@UEAAXAEBVTime@System@@@Z          [slot 1]
//                                                        retail 0x1800a39b0
//
// The `Time&` parameter is NEVER READ: RDX is overwritten by the first
// instruction of the body. The NULL test on `_followed` is RETAIL'S OWN and is a
// real case — Mesh.h records that the default constructor leaves +0xc8
// uninitialised — so nothing is added to it (CLAUDE.md: never fix a faithful
// crash with a null check). A non-null garbage pointer faults here exactly as it
// faults in the DLL, and if it ever does the bug is the missing SetMesh upstream.
// ---------------------------------------------------------------------------
void EdgeMeshInstance::Update(const System::Time& time) {
    (void)time;   // RDX is clobbered by retail's first instruction

    MeshInstance* const followed = _followed;
    if (followed) {
        const Math::Transform xf = followed->_transform._current;   // seven dwords
        this->::Graphics::Geometry::SetTransform(xf);
    }
}

// ---------------------------------------------------------------------------
// ?GetInstanceTransform@EdgeMeshInstance@Graphics@@UEBA?AVMatrix@Math@@HM@Z
//                                              [slot 6]  retail 0x1800a3840
//
// THE PACKED ARGUMENT IS 15 + 1 HERE, not 12 + 4 — a different packing from
// MeshInstance's, decoded separately. And note what this arm does NOT do: it
// does not touch a colour. Carrying MeshInstance's four colour stores over here
// would be silent, plausible and wrong.
// ---------------------------------------------------------------------------
Math::Matrix* EdgeMeshInstance::GetInstanceTransform(Math::Matrix* ret, int packed,
                                                     float t) const {
    const int  index     = packed & 0x7fff;
    const bool whichList = ((static_cast<uint32_t>(packed) >> 15) & 1u) != 0;

    const EdgeMesh* const mesh = _mesh.Get();
    const GraphicsMesh::TriangleList* const listData =
        whichList ? mesh->_edgeLists._data : mesh->_triangleLists._data;
    const int32_t node = listData[index]._f14;   // plain MOV dword; see the door

    if (node >= 0 && _followed != nullptr) {
        // ENDGAME B.3.5: `MeshInstance::GetNodeToWorld(int, float)` is now a
        // member of the real class and the body is in this file, so retail's
        // direct internal CALL becomes a direct member call.
        const Transform got = _followed->MeshInstance::GetNodeToWorld(node, t);
        *ret = got.AsMatrix();
    } else {
        Transform xf;
        RtLerpTransformPair(_transform._current, _transform._previous, t, &xf);
        *ret = xf.AsMatrix();
    }

    return ret;
}

// ---------------------------------------------------------------------------
// ?SetMesh@EdgeMeshInstance@Graphics@@QEAAXV?$ExternalPtr@…PEAVMeshInstance@2@@Z
//                                                        retail 0x1800a3a20
// `_followed` is stored FIRST and the mesh handle second — retail's order. The
// four OR's write `_materialTypeFlags` back after EACH one, which is observable
// to a re-entrant reader, so the four stay four statements. Read in the order
// edge (+0x38), mask (+0x28), skinEdge (+0x40), skinMask (+0x30).
// ---------------------------------------------------------------------------
void EdgeMeshInstance::SetMesh(Object::ExternalPtr<EdgeMesh> mesh, MeshInstance* followed) {
    _followed = followed;
    EdgeMesh* const em = mesh.Get();
    _mesh = em;
    _materialTypeFlags |= 1u << (static_cast<uint8_t>(em->_edgeMaterial->_shaderClass) & 0x1f);
    _materialTypeFlags |= 1u << (static_cast<uint8_t>(em->_maskMaterial->_shaderClass) & 0x1f);
    _materialTypeFlags |= 1u << (static_cast<uint8_t>(em->_skinEdgeMaterial->_shaderClass) & 0x1f);
    _materialTypeFlags |= 1u << (static_cast<uint8_t>(em->_skinMaskMaterial->_shaderClass) & 0x1f);

    // ENDGAME B.3.5 caller batch: `Graphics::Geometry::SetBounds` is a DEFINED
    // member (scene.cpp:1373) and `this` IS a Geometry, so the call is qualified
    // and non-virtual, as retail's is.
    this->::Graphics::Geometry::SetBounds(em->_aabb);
}

// ---------------------------------------------------------------------------
// ?AddDrawItems@EdgeMeshInstance@Graphics@@UEBAXAEBVScene@2@AEAVRenderer@2@@Z
//                                              [slot 3]  retail 0x1800a2fd0
//
// TWO flat passes, triangles then edges. No LOD, no bounds, no opacity. The
// counts come from a CACHED EdgeMesh pointer and the materials from a FRESH load
// of `_mesh` on every iteration, and the second loop RELOADS `_mesh` before it
// starts — all three asymmetries are retail's and are reproduced.
// ---------------------------------------------------------------------------
void EdgeMeshInstance::AddDrawItems(const Scene& scene, Renderer& renderer) const {
    (void)scene;
    ++g_addDrawItemsEdge;

    const float kOne = 1.0f;                                   // 0x18016169c

    // Each of the four loads of `_mesh` below is retail's own reload of it.

    // ---- pass 1: the triangle lists, sort key 0x2000 -----------------------
    {
        const EdgeMesh* const em = _mesh.Get();                    // 1800a2fe2
        const int32_t n = em->_triangleLists._size;
        for (int32_t i = 0; i < n; ++i) {                      // 1800a3020
            const EdgeMesh* const emNow = _mesh.Get();
            const GraphicsMesh::TriangleList* const lists = em->_triangleLists._data;

            const MaterialInstance* material =
                (lists[i]._f14 >= 0)                            // 1800a305f  JGE
                    ? emNow->_maskMaterial.Get()                // +0x28
                    : emNow->_skinMaskMaterial.Get();           // +0x30

            renderer.Graphics::Renderer::AddDrawItem(
                static_cast<const Geometry*>(this), material,
                0x2000, static_cast<uint16_t>(i),
                0.0f, kOne, 1, nullptr, 0xffffffffu);           // 1800a3078
            ++g_addDrawItemsEdgeQ;
        }
    }

    // ---- pass 2: the edge lists, sort key 0x2001, index | 0x8000 -----------
    {
        const EdgeMesh* const em = _mesh.Get();                    // 1800a3087 RELOAD
        const int32_t n = em->_edgeLists._size;
        for (int32_t i = 0; i < n; ++i) {                      // 1800a30b0
            const EdgeMesh* const emNow = _mesh.Get();
            const GraphicsMesh::TriangleList* const lists = em->_edgeLists._data;

            const MaterialInstance* material =
                (lists[i]._f14 >= 0)                            // 1800a30ef  JGE
                    ? emNow->_edgeMaterial.Get()                // +0x38
                    : emNow->_skinEdgeMaterial.Get();           // +0x40

            const uint16_t arg5 =
                static_cast<uint16_t>(static_cast<uint16_t>(i) | 0x8000u);

            renderer.Graphics::Renderer::AddDrawItem(
                static_cast<const Geometry*>(this), material,
                0x2001, arg5,
                0.0f, kOne, 1, nullptr, 0xffffffffu);           // 1800a310f
            ++g_addDrawItemsEdgeQ;
        }
    }
}

// ############################################################################
// ENDGAME B.3.5 step 5, block G — `Graphics::GraphicsMesh`'s two members whose
// live bodies were this file's doors. `GraphicsMesh` is block G's class; the two
// doors above keep their registrations and now delegate here.
// ############################################################################

// ---------------------------------------------------------------------------
// ?GetMaterialList@GraphicsMesh@Graphics@@QEBAAEBV?$Array@V?$ExternalPtr@
//  VMaterialInstance@Graphics@@@Object@@@Collection@@H@Z   retail 0x1800126c0
//
// The whole body is `return *(mesh + 0x70) + index * 0x10`. `_skinMaterials` is
// an `Array<Array<ExternalPtr<MaterialInstance>>>`, so `_data` is the buffer of
// 0x10-byte Array HEADERS and `index` selects a header, not an element.
// `_skinMaterials._size` is what `?GetSkinCount@` returns, i.e. there is one
// material list per skin.
// ---------------------------------------------------------------------------
const Collection::Array<Object::ExternalPtr<MaterialInstance> >&
GraphicsMesh::GetMaterialList(int32_t skin) const {
    return _skinMaterials._data[skin];
}

// ---------------------------------------------------------------------------
// ?GetNodeIndexFromName@GraphicsMesh@Graphics@@QEBAHPEBD@Z  retail 0x1800fee90
//
// A linear scan of `_nodes` comparing each node's name to `name`, with retail's
// own inlined strcmp. Two details that are NOT ordinary strcmp and are
// reproduced exactly rather than replaced with std::strcmp:
//
//  * NULL is a legal name on both sides and compares EQUAL to NULL. If a node's
//    name is null the body returns that node's index when `name` is null too,
//    and otherwise SKIPS the node without comparing — it never dereferences a
//    null. This is not a guard retail lacks; it is retail's own control flow.
//  * The scan returns the FIRST match, and -1 when nothing matches.
// ---------------------------------------------------------------------------
int32_t GraphicsMesh::GetNodeIndexFromName(const char* name) const {
    const int32_t count = _nodes._size;
    const Node* const nodes = GetNodes()._data;

    int32_t ours = -1;
    for (int32_t i = 0; i < count; ++i) {
        const char* nodeName = nodes[i]._name._data;
        if (nodeName == nullptr) {
            if (name == nullptr) { ours = i; break; }
            continue;                       // named-vs-null: skip, do not deref
        }
        if (name == nullptr) continue;      // ditto, the other way round

        // Retail's inlined strcmp: walk while the bytes agree and neither has
        // hit its terminator, then subtract the two bytes at the stopping
        // index. Reproduced literally so the returned "equal / not equal" is
        // decided on the same index retail decides it on.
        int64_t k = 0;
        if (nodeName[0] == name[0]) {
            while (nodeName[k] != '\0' && name[k] != '\0' && nodeName[k + 1] == name[k + 1])
                ++k;
        }
        if (static_cast<int>(nodeName[k]) - static_cast<int>(name[k]) == 0) { ours = i; break; }
    }

    return ours;
}

// ----------------------------------------------------------------------------
// bool Graphics::EdgeMesh::Set(25 args)   retail 0x1800a23e0, ordinal 3813
//
// Copy the bounds box, resize and fill the two TriangleList arrays, then upload
// four (index, vertex) buffer pairs and return whether every upload succeeded.
// Each upload is the buffer's companion slot 1 (`Create`: size it) then slot 2
// (`SetData`: fill it) -- retail inlines the two `Video::*Buffer::Create` /
// `Set` trampolines (buffers.cpp) and calls through `platformData`'s vtable;
// B.4 calls the trampolines, the same two dispatches. Any false short-circuits
// the whole chain -- a failure at pair 2 leaves pairs 3 and 4 untouched. That
// early-out is retail behaviour, not an optimisation.
//
// The four pairs are, in retail's order: (index +0x68, vertex +0x48), (+0xa8,
// +0x88), (+0xe8, +0xc8), (+0x128, +0x108) -- the INDEX buffer of each pair is
// uploaded FIRST.
// ----------------------------------------------------------------------------
bool EdgeMesh::Set(int triCount, const GraphicsMesh::TriangleList* triLists,
                   int edgeCount, const GraphicsMesh::TriangleList* edgeLists,
                   int i0, uint16_t* ib0, int v0s, void* vb0, int v0n, int v0x,
                   uint16_t* ib1, int v1s, void* vb1, int v1n, int v1x,
                   uint16_t* ib2, int v2s, void* vb2, int v2n, int v2x,
                   uint16_t* ib3, int v3s, void* vb3, int v3n,
                   const ::Box& box) {
    _aabb = box;

    const GraphicsMesh::TriangleList* const tris  = triLists;
    const GraphicsMesh::TriangleList* const edges = edgeLists;

    rtcoll::RtColl_ArraySetSize_1800136f0(&_triangleLists, triCount);
    for (int i = 0; i < _triangleLists._size; ++i)
        _triangleLists._data[i] = tris[i];

    rtcoll::RtColl_ArraySetSize_1800136f0(&_edgeLists, edgeCount);
    for (int i = 0; i < _edgeLists._size; ++i)
        _edgeLists._data[i] = edges[i];

    return _indexBuffer.Create(i0 * 2, false, nullptr)
        && _indexBuffer.Set(i0 * 2, 0, ib0)
        && _vertexBuffer.Create(v0s * v0n, false, nullptr)
        && _vertexBuffer.Set(v0s * v0n, 0, vb0)
        && _skinIndexBuffer.Create(v0x * 2, false, nullptr)
        && _skinIndexBuffer.Set(v0x * 2, 0, ib1)
        && _skinVertexBuffer.Create(v1s * v1n, false, nullptr)
        && _skinVertexBuffer.Set(v1s * v1n, 0, vb1)
        && _edgeIndexBuffer.Create(v1x * 2, false, nullptr)
        && _edgeIndexBuffer.Set(v1x * 2, 0, ib2)
        && _edgeVertexBuffer.Create(v2s * v2n, false, nullptr)
        && _edgeVertexBuffer.Set(v2s * v2n, 0, vb2)
        && _edgeSkinIndexBuffer.Create(v2x * 2, false, nullptr)
        && _edgeSkinIndexBuffer.Set(v2x * 2, 0, ib3)
        && _edgeSkinVertexBuffer.Create(v3s * v3n, false, nullptr)
        && _edgeSkinVertexBuffer.Set(v3s * v3n, 0, vb3);
}

void EdgeMesh::SetMaterials(Object::ExternalPtr<MaterialInstance> mask,
                            Object::ExternalPtr<MaterialInstance> edge,
                            Object::ExternalPtr<MaterialInstance> skinMask,
                            Object::ExternalPtr<MaterialInstance> skinEdge) {
    _maskMaterial     = mask;       // +0x28  <- arg 1
    _edgeMaterial     = edge;       // +0x38  <- arg 2
    _skinMaskMaterial = skinMask;   // +0x30  <- arg 3
    _skinEdgeMaterial = skinEdge;   // +0x40  <- arg 4
}

}  // namespace Graphics

// ?_rootOnlyUpdates@MeshInstance@Graphics@@0_NA  retail 0x1801fc70b -- the
// class static (ENDGAME B.3.9 block GFX, B39_RULES D7 row A22), redirected onto
// the mint's storage row by RtGraphicsPortInit (foreign readers remain).
bool Graphics::MeshInstance::_rootOnlyUpdates = false;
