// ============================================================================
// src/engine/picking/scene_grid.cpp — WAVE 9 LANE PICK
// Picking::Scene's spatial-hash internals
// ============================================================================
//
// WHERE THIS LANE CAME FROM
// -------------------------
// Not from a name-based instrument. From the ADDRESS-door census
// (docs/history/runtime_port/wave9_unregistered_addr_doors.csv, commits ce1d8e9 /
// 4ef55b6): every `ResolveAddr(Module::Runtime, 0x...)` in the tree minus every
// address anything registers. 172 call sites, 62 unregistered — and five of the
// 62 were the Picking grid, all forwarders in src/engine/picking/scene.cpp:
//
//   0x18007af70   scene.cpp:286   RtGridAdd          adds an entry, returns its index
//   0x1800724d0   scene.cpp:295   RtGridReindex      re-threads an entry if its cell moved
//   0x180072950   scene.cpp:305   RtGridUnthread     unlinks an entry from each of its cells
//   0x18007b390   scene.cpp:315   RtGridFreeEntry    destroys the entry, pushes the slot
//   0x18007b6d0   scene.cpp:336   RtCellIterAdvance  the cell iterator
//
// Wave 5 lane D ported the nine EXPORTED `Picking::Scene` bodies and left these
// five on the DLL on purpose — they allocate through `System::MemoryAllocator`
// under that allocator's own critical section, which PLAN §4.1 decision 1 keeps
// out of runtime/src. That reasoning has since been overtaken: Wave 9 lane Q's
// `rtcoll::MemAlloc` / `rtcoll::MemFree` ARE that Enter/Allocate/Leave triple,
// reached through the port-aware address door, and lane Q disassembled
// 0x1800c7ad0 to prove it instruction for instruction. So the platform boundary
// that blocked this cluster in Wave 5 is now a function call.
//
// With `Runtime-x64.dll` absent all five resolve to null and any call executes
// address 0 — the exact failure shape of the current cutover wall. No
// name-based instrument can see them, because the call sites never mention a
// name; that is why Wave 5 could close the layer above them and leave these
// open.
//
// WHY NINETEEN BODIES AND NOT FIVE
// --------------------------------
// The five call fourteen more unexported internals, and none of those fourteen
// was ported anywhere in the tree. Porting the five and calling the fourteen by
// address would have swapped five unregistered address doors for fourteen new
// ones — a census that gets WORSE while every line of it reads like progress.
// So this lane closes the transitive closure. The full call graph:
//
//   GridAdd 0x18007af70
//     -> MeshBoundsXY 0x180125bc0 -> BoxTransform 0x18001f470 -> AsTransform3*
//     -> EntryAlloc 0x18007b210
//          -> TransformArrayAssign 0x18001b360 -> ArrayDtorTransform 0x18001c100
//          -> EntryArraySetSize  0x18007bc30
//               -> EntryCopyConstruct 0x1800735c0 -> TransformArrayAssign
//               -> DestroyEntryTransforms 0x180074100
//          -> EntryAssign 0x18007b590 -> TransformArrayAssign
//     -> AddToCells 0x180072730
//          -> FindOrCreateCell 0x180072b30 -> CellArraySetSize 0x1800738c0
//   GridReindex 0x1800724d0 -> MeshBoundsXY, GridUnthread, AddToCells
//   GridUnthread 0x180072950 -> UnlinkEntryFromCell 0x180072d10
//                                    -> FreeEmptyCell 0x180072e70
//   GridFreeEntry 0x18007b390 -> EntryAssign, ArrayDtorTransform
//   CellIterAdvance 0x18007b6d0 -> (leaf)
//
//   (*) `Math::Transform::AsTransform3` 0x18001fcd0 is Wave 9 lane P's port and
//       is already registered under BOTH doors, so reaching it by address here
//       reaches the PORT, not the DLL. That is the one external dependency and
//       it needs nothing from this lane.
//
// BOTH DOORS? NO — THERE IS ONLY ONE.
// -----------------------------------
// The normal rule is that a port reached by address needs `RegisterRuntimeAddr`
// AS WELL AS `RegisterRuntimeExport`. It does not apply to any of these
// nineteen: not one of them is exported. `tools/pe_exports.py` over the retail
// export directory (4,800 ordinals) and over `Runtime-x64-profile.dll`'s 5,182
// names finds no symbol resolving to any of these addresses, and
// `docs/history/runtime_port/wave9_internals.csv` records the same for the five census
// rows ("profile_build_name: none"). So the ADDRESS registration is the only
// door and `RegisterRuntimeExport` has nothing to register under. Stated
// explicitly rather than left implied.
//
// Nor is this the allocator lane's situation. `RegisterAllocatorAddresses()`
// (src/engine/system/allocator.cpp, 309c015) is cutover-only because
// standalone/allocator_lock.cpp PATCHES the same addresses in the loaded DLL
// and `ResolveAddr` consults the port registry FIRST — registering early would
// have handed the patcher our own prologue to rewrite. Nothing patches any of
// these nineteen (grepped across standalone/, code/, engine/ and src/render/dx11/: the
// only textual hits for these addresses are scene.cpp's five forwarders and
// this file), so they register unconditionally at init like every other wave.
//
// WHAT ResolveAddr DOES FOR US HERE
// ---------------------------------
// scene.cpp's five forwarders are LEFT EXACTLY AS THEY ARE. `ResolveAddr`
// consults the port registry first (reference_resolveaddr_consults_port_registry
// _first.md), so the moment RtPickingGridRegister() runs, those five
// `ResolveAddr(Module::Runtime, 0x…)` sites stop reaching the DLL and reach the
// bodies below — and the addresses drop out of the unregistered-door census
// because they are now registered. No edit to scene.cpp was needed for either
// effect, and not editing it keeps the forwarders' analysis (which is good, and
// is where most of this lane's groundwork came from) intact.
//
// THE HAZARD IN THIS SECTION, AND WHAT WAS DONE ABOUT IT
// -----------------------------------------------------
// This is linked-list and free-list surgery over a hash grid. An unlink written
// blind corrupts the structure and no oracle necessarily notices — this project
// has the scar: a teardown 100%-CPU hang AND an `IsA` EXEC crash were ONE
// missing BST erase, and `CheckPtr` is a bare `RET` in every shipped build so
// nothing validates a handle (reference_workarea_bst_bad_entry.md). And a
// ported inline `strcmp` survived four waves of green oracles because 13 of the
// 14 values in the population were the same value
// (reference_uniform_population_is_not_a_verification.md).
//
// So the oracle is scene_grid_probe.cpp: two identical private Scenes, ours run
// on one and the SHIPPED bodies on the other, compared field by field after
// every operation. Its population is deliberately non-uniform — entries
// threaded into MULTIPLE cells, cells shared by several entries, and a free
// list that is already non-empty before the reuse paths are exercised — and it
// reports an EFFECTIVENESS control (how many comparisons did any work, per
// path) and carries a NEGATIVE control that must be flagged.
//
// READ AS DISASSEMBLY, NOT AS C. Every body below was transliterated from
// `disassemble_function`, not from the decompiler: Ghidra's C drops
// dereferences and prints MOVSXD as a zero extension, which cost Wave 6 three
// defects including a crash (reference_ghidra_c_output_vs_disassembly.md). The
// decompiler output for 0x180072d10 in particular is unreadable (it rotates
// three loop variables through each other); the disassembly is a plain
// singly-linked-list unlink and that is what is written here.
//
// PORTABILITY: no <windows.h>, per PLAN.md §4.1 decision 1.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "scene_grid.h"

// B7 RT (B6 C8): `Entry::_userValue2` is retail's unwritten window on the stack
// Entry both insert paths build (`Entry tmp;`): the copy into the array moves
// the qword as is, and no reader tests it. With the String / Array release
// family inline, GCC's -O2 inliner sees the temporary and reports the copy as
// a use of an uninitialised value, attributing it to the copy's line while
// ignoring a pragma at that line or around either caller; the whole-file
// pragma is the only region that silences it. Nothing is zeroed: it is
// retail's byte.
#pragma GCC diagnostic ignored "-Wuninitialized"

// Wave 9 lane Q's allocator boundary. These two ARE the Enter/Allocate/Leave
// and Enter/Free/Leave triples retail inlines at every site below, reached
// through the port-aware address door — see array.cpp's banner for the
// instruction-for-instruction argument. Declared rather than included:
// array_internal.h says only two files include it, and this lane needs exactly
// these two symbols.
namespace rtcoll {
void* MemAlloc(uint64_t bytes, int align);
void  MemFree(void* p);
}

namespace rtpickgrid {


namespace {

// ---------------------------------------------------------------------------
// The .rdata constants, read out of the shipped DLL with pefile rather than
// trusted from a decompile (a "-NAN" in Ghidra is 0xffffffff, not a float):
//
//   DAT_180161694 = 0x3f000000 = 0.5f                 (the box half-extent)
//   DAT_180161740 = 0x4cbebc20 = 1.0e8f               (empty-box min sentinel)
//   DAT_18016177c = 0xccbebc20 = -1.0e8f              (empty-box max sentinel)
//   DAT_1801617b0 = 0x7fffffffffffffff                (the double sign mask)
//
// The sign mask is used as `(float)((double)x & mask)`: CVTPS2PD, ANDPD,
// CVTPD2PS. Widening a float to double is exact, clearing the sign bit of an
// exact value is exact, and narrowing back is exact, so the whole sandwich is
// bit-identical to `fabsf(x)` for every finite input AND for infinities. It is
// written as the sandwich anyway, because a NaN payload could in principle
// differ and this lane's job is not to decide that it cannot.
// ---------------------------------------------------------------------------
const float kHalf   =  0.5f;
const float kBigPos =  1.0e8f;
const float kBigNeg = -1.0e8f;

inline float RtFAbs(float v) {
    double d = static_cast<double>(v);          // CVTPS2PD
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    bits &= 0x7fffffffffffffffull;              // ANDPD DAT_1801617b0
    std::memcpy(&d, &bits, sizeof(d));
    return static_cast<float>(d);               // CVTPD2PS
}

// The 2D spatial hash. Retail: `y * -0x27e9c7bf + x * -0x72594cbd`, computed
// with IMUL/SUB on 32-bit registers, so it wraps. Signed overflow is UB in
// C++, hence the unsigned intermediate — the bit pattern is identical.
inline int32_t CellHash(int32_t x, int32_t y) {
    const uint32_t h = static_cast<uint32_t>(y) * 0xd8163841u   // -0x27e9c7bf
                     + static_cast<uint32_t>(x) * 0x8da6b343u;  // -0x72594cbd
    return static_cast<int32_t>(h);
}

// `IDIV` truncates toward zero and so does C++ `%`, so this is the retail
// sequence exactly: remainder, then one conditional add of the divisor.
// A zero divisor faults here as it would there; `_cellHeads._size` is 10000
// from the ctor and is never resized.
inline int32_t HashBucket(int32_t hash, int32_t size) {
    int32_t r = hash % size;
    if (r < 0) r += size;
    return r;
}

}  // namespace

// ============================================================================
// 0x18001c100 — Collection::Array<T>::~Array, trivially-destructible T
// ============================================================================
// The J1 body Wave 9 lane Q ported and registered at twelve OTHER addresses.
// This is the thirteenth, and it is the same body: 0x18001c100's disassembly is
// `if (_data) { Enter(_singleton+0x640); Free(_singleton, _data); Leave; }` then
// `_data = 0` and one qword store covering `_size` and `_capacity` — the same
// instructions, the same four resolved operands (0x1801fc718, 0x18014b390,
// 0x1800c6c00, 0x18014b388), and lane Q's "J1 has NO parameters at all, not
// even a stride" conclusion covers it unchanged.
//
// It is written here rather than added to array.cpp's J1_ENTRY list on purpose:
// three lanes are editing that file this wave and a thirteenth macro line is a
// guaranteed conflict, while this costs eight lines. The registration below IS
// the address's only registration, so nothing is registered twice.
//
// `rtcoll::MemFree` performs the null test itself (0x1800c7ad0's first
// instruction is `TEST RCX,RCX; JZ ret`), which is why the pointer is handed
// over unguarded exactly as retail does.
void ArrayDtorTransform(TransformArray* a) {
    if (a->_data) rtcoll::MemFree(a->_data);
    a->_data = nullptr;
    a->_size = 0;
    a->_capacity = 0;
}

// ============================================================================
// 0x18001b360 — Collection::Array<Math::Transform>::operator=
// ============================================================================
// Self-assignment is a NO-OP that still returns `this` (`CMP RCX,RDX; JZ ret`)
// — and it is not a theoretical branch: `EntryAssign` below can be handed the
// same Entry twice by a caller, which would make this a free-then-read.
//
// Note the shape: it destroys the destination FIRST (freeing its buffer), then
// re-grows to the source's size. So `_capacity` after an assignment is the
// GROWN capacity of a fresh array, never the destination's old one. The growth
// rule is retail's, including the `oldSize != 0` guard on the trebling — the
// guard lane Q's negative control removes, because an append to an EMPTY array
// allocates exactly 1 in retail and 3 without it.
//
// The element copy is seven dwords (0x1c bytes) done as seven MOVs, not a
// memcpy: Math::Transform is 0x1c bytes and the tail dword of a 0x20-byte copy
// would be a write past the element.
TransformArray* TransformArrayAssign(TransformArray* dst, const TransformArray* src) {
    if (dst == src) return dst;

    ArrayDtorTransform(dst);                       // CALL 0x18001c100

    const int32_t n       = src->_size;
    const int32_t oldSize = dst->_size;            // always 0 after the dtor
    dst->_size = n;
    if (n > dst->_capacity) {
        dst->_capacity = n;
        if (n == oldSize + 1 && oldSize != 0) dst->_capacity = n * 3;
        dst->_data = static_cast<Math::Transform*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(dst->_capacity) * 0x1cu, 4));
    }
    for (int32_t i = 0; i < dst->_size; ++i) {
        Math::Transform* d = dst->_data + i;
        if (d == nullptr) continue;                // retail's `ADD R8,RDX; JZ`
        *d = src->_data[i];                        // seven dword moves
    }
    return dst;
}

// ============================================================================
// 0x180074100 — destroy `count` Entries' inner Array<Transform>, in place
// ============================================================================
// The shrink/realloc tail of the Array<Entry> grower. It frees each element's
// buffer at +0x20 and zeroes the two qwords at +0x20 and +0x28 — i.e. the whole
// 16-byte Array header — WITHOUT going through ~Array, and it does its own null
// test before the free rather than leaving it to MemFree.
//
// `param_1` is dead: the body never reads RCX. It is kept in the signature
// because the two call sites in 0x18007bc30 pass whatever happens to be in RCX
// and the ABI has to match.
void DestroyEntryTransforms(void* /*ignored*/, Entry* base, int count) {
    if (count <= 0) return;
    Entry* e = base;
    for (int i = 0; i < count; ++i, ++e) {
        Math::Transform* buf = e->_transforms._data;
        if (buf) rtcoll::MemFree(buf);
        e->_transforms._data     = nullptr;        // the two qword stores at
        e->_transforms._size     = 0;              // +0x20 and +0x28, as the
        e->_transforms._capacity = 0;              // three named fields (H7)
    }
}

// ============================================================================
// 0x1800735c0 — Picking::Scene::Entry copy-CONSTRUCT
// ============================================================================
// Distinct from operator= below: it ZEROES the destination's Array<Transform>
// header before assigning into it, because the destination is raw storage the
// allocator just handed back and its +0x20 qword is garbage. Assigning without
// that zeroing would hand the garbage to `~Array` and free a wild pointer.
//
// +0x14..+0x17 (the four slack bytes that 8-align `_mesh`) are NOT written by
// this body, and are not written by operator= either. Any oracle that memcmps
// a whole Entry has to skip them, or it fails 100% of cases and reads like a
// catastrophic port defect (reference_oracle_must_not_compare_unwritten_bytes.md).
Entry* EntryCopyConstruct(Entry* d, const Entry* s) {
    d->_queryStamp = s->_queryStamp;               // +0x00..+0x0f, four dwords
    d->_nextFree   = s->_nextFree;
    d->_cellX      = s->_cellX;
    d->_cellY      = s->_cellY;
    d->_cellW      = s->_cellW;                    // +0x10, +0x12, two words
    d->_cellH      = s->_cellH;

    d->_mesh = s->_mesh;                           // +0x18 _mesh

    d->_transforms._data     = nullptr;            // +0x20 Array header, zeroed
    d->_transforms._size     = 0;
    d->_transforms._capacity = 0;
    TransformArrayAssign(&d->_transforms, &s->_transforms);   // CALL 0x18001b360

    d->_userData   = s->_userData;
    d->_userValue  = s->_userValue;
    d->_userValue2 = s->_userValue2;
    d->_callback   = s->_callback;
    return d;
}

// ============================================================================
// 0x18007b590 — Picking::Scene::Entry::operator=
// ============================================================================
// Same field-for-field shape as the copy-construct, minus the zeroing of the
// destination's Array header — the destination here is a LIVE Entry whose
// buffer must be released, which is what TransformArrayAssign's leading
// ~Array does.
//
// Note what it copies at +0x08: one QWORD covering _cellX and _cellY, then two
// separate WORD moves at +0x10 and +0x12. That is why _pad14 stays untouched:
// the qword store stops at +0x0f and the word stores are 2 bytes each.
Entry* EntryAssign(Entry* d, const Entry* s) {
    d->_queryStamp = s->_queryStamp;
    d->_nextFree   = s->_nextFree;
    d->_cellX      = s->_cellX;                    // one qword in retail (H7)
    d->_cellY      = s->_cellY;
    d->_cellW      = s->_cellW;
    d->_cellH      = s->_cellH;
    d->_mesh       = s->_mesh;

    TransformArrayAssign(&d->_transforms, &s->_transforms);   // CALL 0x18001b360

    d->_userData   = s->_userData;
    d->_userValue  = s->_userValue;
    d->_userValue2 = s->_userValue2;
    d->_callback   = s->_callback;
    return d;
}

// ============================================================================
// 0x18001f470 — Volume::Box transformed by a Math::Transform, as an AABB
// ============================================================================
// Centre-and-extent form: transform the box centre by the full 3x4 matrix, then
// add the ABSOLUTE-valued matrix applied to the half-extents. Written with
// retail's exact term order and association — this is float arithmetic and a
// MOVED TERM is algebraically identical and bit-different exactly where the
// terms nearly cancel (reference_moved_term_hides_where_terms_cancel.md: one
// call in 23,654, so a clean re-run is not an exoneration).
//
// The Transform3 the matrix comes from is Wave 9 lane P's ported
// `Math::Transform::AsTransform3`, the member, called by name (B3R).
// `?AsTransform3@Transform@Math@@QEBA?AVTransform3@2@XZ` (0x18001fcd0).

float* BoxTransform(float* out, const Math::Transform* transform, const ::Box* b) {
    // Math::Transform3 AsTransform3(const Math::Transform&) — hidden return
    // slot in RDX, `this` in RCX. m[0..11] are the three basis vectors and the
    // translation, in that order.
    const Math::Transform3 t3 = transform->AsTransform3();   // 0x18001fcd0
    const float m[12] = { t3.axisX.x, t3.axisX.y, t3.axisX.z,
                          t3.axisY.x, t3.axisY.y, t3.axisY.z,
                          t3.axisZ.x, t3.axisZ.y, t3.axisZ.z,
                          t3.translation.x, t3.translation.y, t3.translation.z };

    const float ez = (b->max.z - b->min.z) * kHalf;
    const float ey = (b->max.y - b->min.y) * kHalf;
    const float ex = (b->max.x - b->min.x) * kHalf;
    const float cy = (b->max.y + b->min.y) * kHalf;
    const float cx = (b->max.x + b->min.x) * kHalf;
    const float cz = (b->max.z + b->min.z) * kHalf;

    // Centre, transformed. Retail's association, left to right as written.
    const float tcx = ((m[3] * cy + m[0] * cx) + m[6] * cz) + m[9];
    const float tcy = ((m[4] * cy + m[1] * cx) + m[7] * cz) + m[10];
    const float tcz = ((m[5] * cy + m[2] * cx) + m[8] * cz) + m[11];

    // Half-extent, through the absolute-valued basis.
    const float ax = (RtFAbs(m[0]) * ex + RtFAbs(m[3]) * ey) + RtFAbs(m[6]) * ez;
    const float ay = (RtFAbs(m[1]) * ex + RtFAbs(m[4]) * ey) + RtFAbs(m[7]) * ez;
    const float az = (RtFAbs(m[2]) * ex + RtFAbs(m[5]) * ey) + RtFAbs(m[8]) * ez;

    out[0] = tcx - ax;
    out[1] = tcy - ay;
    out[2] = tcz - az;
    out[3] = ax + tcx;
    out[4] = ay + tcy;
    out[5] = az + tcz;
    return out;
}

// ============================================================================
// 0x180125bc0 — a mesh's world AABB, projected to XY
// ============================================================================
// `descTail` is Entry+0x18 shaped: { PickingMesh* _mesh; Array<Transform>
// _transforms; }. GridAdd hands it the AddDesc (identical layout, which is why
// the entry allocator can copy the descriptor field for field); GridReindex
// hands it `&entry->_mesh`.
//
// It walks the mesh's SubMesh nodes (stride 0x80, AABB at +0x28), transforms
// each node's box by the matching world transform, and unions the results — but
// writes only the X and Y components out. The Z min/max ARE computed and then
// discarded, and they are computed here too: they cost nothing, they are not a
// side effect, and deleting a computed-then-unused value is how a
// SIDE-EFFECT CARRIER gets removed as dead code (lane FIND's FindFiles finding).
//
// The min/max updates are COMISS + JA, not MINSS/MAXSS, and the two differ on
// NaN: `COMISS cur,v / JA skip` assigns whenever `cur > v` is false, which
// includes unordered. Written as `if (!(cur > v))` so a NaN box behaves the way
// retail's does rather than the way the obvious `if (v > cur)` would.
//
// An empty mesh (`_transforms._size <= 0`) returns the INVERTED sentinel
// rectangle min=+1e8, max=-1e8 — which the callers then truncate into cell
// indices, giving a huge negative w/h. That is retail's behaviour and it is not
// guarded here: retail's unguarded arithmetic is load-bearing and a mesh with
// no transforms never reaches AddMesh in the first place.
template <class Desc>
float* MeshBoundsXY(const Desc* desc, float* outRect) {
    float minX = kBigPos, maxX = kBigNeg;
    float minY = kBigPos, maxY = kBigNeg;
    float minZ = kBigPos, maxZ = kBigNeg;

    int32_t count = desc->_transforms._size;
    for (int32_t i = 0; i < count; ++i) {
        const PickingMesh* mesh = desc->_mesh;
        const PickingMesh::SubMesh* nodes = mesh->_nodes._data;
        const Math::Transform* xforms = desc->_transforms._data;

        // Retail copies the node's box (six dwords) to a stack Box at
        // [RSP+0x20] and hands BoxTransform that copy (0x180125c38..c69):
        // `Box`'s implicit copy, no default ctor.
        const ::Box box = nodes[i]._aabb;

        float t[6];
        BoxTransform(t, xforms + i, &box);

        if (!(maxZ > t[5])) maxZ = t[5];
        if (!(maxY > t[4])) maxY = t[4];
        if (!(maxX > t[3])) maxX = t[3];
        if (!(t[2] > minZ)) minZ = t[2];
        if (!(t[1] > minY)) minY = t[1];
        if (!(t[0] > minX)) minX = t[0];

        // re-read the loop bound exactly as retail does (`CMP EDI,[RSI+0x10]`)
        count = desc->_transforms._size;
    }
    (void)minZ; (void)maxZ;   // computed by retail, never stored

    outRect[0] = minX;
    outRect[1] = minY;
    outRect[2] = maxX;
    outRect[3] = maxY;
    return outRect;
}

// ============================================================================
// 0x1800738c0 — Collection::Array<Cell>::SetSize(n, fill)
// 0x18007bc30 — Collection::Array<Entry>::SetSize(n, fill)
// ============================================================================
// Lane Q's SetSize algorithm, at two addresses lane Q's survey did not reach
// (nothing in our tree called them until this file did). Both follow the shape
// documented in array.cpp:
//
//   if (n == _size) return;                    // NOT "if n <= capacity"
//   old = _data;  _size = n;
//   if (n > _capacity) { _capacity = n;
//                        if (n == old_size+1 && old_size != 0) _capacity = n*3;
//                        _data = MemAlloc(_capacity * stride, align); }
//   i = (old == _data) ? old_size : 0;
//   for (; i < n; ++i) assign(_data + i*stride, i < old_size ? old + i*stride : fill);
//   ...then the per-instantiation destroy/free tail.
//
// The `oldSize != 0` guard on the trebling is the one lane Q's negative control
// removes; an append to an EMPTY array allocates exactly 1 in retail.
void CellArraySetSize(Collection::Array<Cell>* a, int newSize, const Cell* fill) {
    const int32_t oldSize = a->_size;
    if (newSize == oldSize) return;

    Cell* old = a->_data;
    a->_size = newSize;
    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) a->_capacity = newSize * 3;
        a->_data = static_cast<Cell*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(a->_capacity) << 4, 4));
    }
    int32_t i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        Cell* dst = a->_data + i;
        if (dst == nullptr) continue;
        const Cell* src = (i < oldSize) ? old + i : fill;
        *dst = *src;                                // four dword moves
    }
    // Cell is trivially destructible, so there is no destroy tail — only the
    // free of the old buffer, and only when the buffer actually moved.
    if (old != a->_data && old != nullptr) rtcoll::MemFree(old);
}

void EntryArraySetSize(Collection::Array<Entry>* a, int newSize, const Entry* fill) {
    const int32_t oldSize = a->_size;
    if (newSize == oldSize) return;

    Entry* old = a->_data;
    a->_size = newSize;
    if (newSize > a->_capacity) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) a->_capacity = newSize * 3;
        a->_data = static_cast<Entry*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(a->_capacity) * 0x48u, 8));
    }
    int32_t i = (old == a->_data) ? oldSize : 0;
    for (; i < newSize; ++i) {
        Entry* dst = a->_data + i;
        if (dst == nullptr) continue;
        const Entry* src = (i < oldSize) ? old + i : fill;
        EntryCopyConstruct(dst, src);               // CALL 0x1800735c0
    }
    // Entry OWNS an Array<Transform>, so the tail has two arms. The buffer
    // moved: every element of the OLD buffer is destroyed and the buffer freed.
    // The buffer did not move and we SHRANK: only the elements past the new end
    // are destroyed.
    if (old != a->_data) {
        DestroyEntryTransforms(a->_data, old, oldSize);
        if (old != nullptr) rtcoll::MemFree(old);
    } else if (oldSize > newSize) {
        DestroyEntryTransforms(nullptr, old + newSize, oldSize - newSize);
    }
}

// ============================================================================
// 0x180072e70 — a Cell became empty: unlink it from its bucket, free the slot
// ============================================================================
// Called by UnlinkEntryFromCell when the cell's link list runs dry. It is a
// second singly-linked-list unlink, this time over the hash BUCKET chain, and
// it has the same "walk while `_nextInBucket >= 0`" shape — so the match test
// on the LAST node in a chain is never performed and the last node is unlinked
// regardless. That is retail's, and it is safe there because the cell being
// freed is by construction in this bucket.
//
// Ask what should have ERASED an entry before asking what WROTE it: this
// function IS the erase for the cell level, and the one below is the erase for
// the link level. A grid whose cells are never freed grows a bucket chain that
// is walked on every query — which is what a missing erase looks like from the
// outside (reference_workarea_bst_bad_entry.md).
void FreeEmptyCell(Scene* scene, Cell* cell) {
    const int32_t x = cell->_x;
    const int32_t y = cell->_y;
    const int32_t bucket = HashBucket(CellHash(x, y), scene->_cellHeads._size);

    int32_t* heads = scene->_cellHeads._data;
    Cell*    cells = scene->_cells._data;

    int32_t cur  = heads[bucket];
    int32_t prev = -1;
    int32_t next = cells[cur]._nextInBucket;
    while (next >= 0) {
        if (cells[cur]._x == x && cells[cur]._y == y) break;
        prev = cur;
        cur  = next;
        next = cells[cur]._nextInBucket;
    }
    if (prev != -1) cells[prev]._nextInBucket = cells[cur]._nextInBucket;
    else            heads[bucket]             = cells[cur]._nextInBucket;

    // Push the cell onto the free-cell list. Retail re-reads `_cells._data`
    // here; nothing can have moved it, and the re-read is kept for shape.
    scene->_cells._data[cur]._nextInBucket = scene->_freeCell;
    scene->_freeCell = cur;
}

// ============================================================================
// 0x180072d10 — unlink one entry's Link from one Cell's link list
// ============================================================================
// THE ERASE. This is the function whose absence is the recorded catastrophe in
// this project, one level over: a cancel path that never removed its node left
// dangling pointers behind a `CheckPtr` that is a bare `RET` in every shipped
// build, and the symptom was a 100%-CPU teardown hang plus an EXEC crash in
// `IsA`. Both were ONE missing erase.
//
// Retail's loop tests `_next >= 0` BEFORE testing the entry match, so the last
// link in a chain is unlinked whether or not it matches. That is not a bug to
// be fixed: `entryId` is always present when this is called (GridUnthread only
// visits the cells the entry is threaded into), so the "not found" path is
// unreachable and retail's shape is what keeps the loop to one test per node.
// A "helpful" not-found guard here would change the shape and prove nothing.
//
// The freed link is stamped `_entry = -1` before it goes on the free list —
// which is what makes a stale link visible in the probe's comparison rather
// than merely absent.
void UnlinkEntryFromCell(Scene* scene, Cell* cell, int32_t entryId) {
    Link* links = scene->_links._data;

    int32_t cur  = cell->_firstLink;
    int32_t prev = -1;
    int32_t next = links[cur]._next;
    while (next >= 0) {
        if (links[cur]._entry == entryId) break;
        prev = cur;
        cur  = next;
        next = links[cur]._next;
    }
    if (prev != -1) links[prev]._next = links[cur]._next;
    else            cell->_firstLink  = links[cur]._next;

    // Retail re-reads `_links._data` for each of the next three accesses.
    scene->_links._data[cur]._next  = scene->_freeLink;
    scene->_links._data[cur]._entry = -1;
    scene->_freeLink = cur;

    if (cell->_firstLink < 0) FreeEmptyCell(scene, cell);
}

// ============================================================================
// 0x180072b30 — find the Cell for (x, y), creating it if it does not exist
// ============================================================================
// Two things in here are easy to get wrong and both are load-bearing.
//
// FIRST: the bucket index is computed TWICE from the same raw hash. The lookup
// normalises `hash % size` into a local; the insert path normalises the RAW
// remainder again. Identical value, and written the same way here so a reader
// comparing against the disassembly finds what the disassembly has.
//
// SECOND: on the APPEND path the new cell's `_nextInBucket` is read from the
// bucket head BEFORE `CellArraySetSize` runs, through the ORIGINAL
// `_cellHeads._data` pointer — and the write-back afterwards goes through a
// RE-READ of that pointer. `_cellHeads` never reallocates (SetSize(10000) once,
// in the ctor), so the two are the same address; the re-read is retail's and is
// preserved because a future reader should see that retail did not assume it.
Cell* FindOrCreateCell(Scene* scene, const int32_t* xy) {
    const int32_t y = xy[1];
    const int32_t x = xy[0];
    const int32_t size = scene->_cellHeads._size;

    const int32_t raw = CellHash(x, y) % size;
    int32_t bucket = raw;
    if (bucket < 0) bucket += size;

    int32_t* heads = scene->_cellHeads._data;
    int32_t c = heads[bucket];
    if (c >= 0) {
        Cell* cells = scene->_cells._data;
        do {
            if (cells[c]._x == x && cells[c]._y == y) return &cells[c];
            c = cells[c]._nextInBucket;
        } while (c >= 0);
    }

    int32_t b2 = raw;
    if (b2 < 0) b2 += size;

    const int32_t freeCell = scene->_freeCell;
    if (freeCell < 0) {
        // Append. The fill element is the complete new Cell; SetSize copies it
        // into the one new slot.
        Cell fill;
        fill._x            = x;
        fill._y            = y;
        fill._nextInBucket = heads[b2];
        fill._firstLink    = -1;
        CellArraySetSize(&scene->_cells, scene->_cells._size + 1, &fill);
        scene->_cellHeads._data[b2] = scene->_cells._size - 1;
        return &scene->_cells._data[scene->_cells._size - 1];
    }

    Cell* nc = &scene->_cells._data[freeCell];
    scene->_freeCell = nc->_nextInBucket;
    nc->_x            = xy[0];
    nc->_y            = xy[1];
    nc->_nextInBucket = heads[b2];
    nc->_firstLink    = -1;
    scene->_cellHeads._data[b2] = freeCell;
    return &scene->_cells._data[freeCell];
}

// ============================================================================
// 0x180072730 — thread an entry into every cell of its rectangle
// ============================================================================
// The mirror of GridUnthread. Two details from the disassembly that the
// decompile blurs:
//
//   * `xy[0]` is RE-READ at the top of every ROW (`MOV R12D,[RDX]`), while the
//     x LIMIT was computed once from the value at entry. Nothing in the loop
//     writes through `xy`, so the re-read is a no-op — but it is retail's shape
//     and a local caching of it would be a claim this lane has not proved.
//   * the new Link's `_next` is the cell's OLD head, captured before
//     `_firstLink` is overwritten.
//
// The Array<Link> grow path is written inline in retail (there is no
// SetSize COMDAT for Array<Link>); it is the same algorithm as the two SetSize
// bodies above with an 8-byte element and a plain qword copy.
void AddToCells(Scene* scene, const int32_t* xy, uint16_t w, uint16_t h,
                int32_t entryId) {
    const int32_t xLimit = static_cast<int32_t>(w) + xy[0];
    int32_t y = xy[1];
    const int32_t yLimit = static_cast<int32_t>(h) + y;

    for (; y < yLimit; ++y) {
        for (int32_t x = xy[0]; x < xLimit; ++x) {
            int32_t key[2] = { x, y };
            Cell* cell = FindOrCreateCell(scene, key);
            const int32_t oldHead = cell->_firstLink;

            if (scene->_freeLink < 0) {
                // Grow the link array by one and construct the new link into it.
                cell->_firstLink = scene->_links._size;
                Link fill;
                fill._entry = entryId;
                fill._next  = oldHead;

                Collection::Array<Link>* a = &scene->_links;
                const int32_t oldSize = a->_size;
                const int32_t newSize = oldSize + 1;
                if (newSize == oldSize) continue;   // retail's guard; never taken

                Link* old = a->_data;
                a->_size = newSize;
                if (newSize > a->_capacity) {
                    a->_capacity = newSize;
                    if (newSize == oldSize + 1 && oldSize != 0) a->_capacity = newSize * 3;
                    a->_data = static_cast<Link*>(
                        rtcoll::MemAlloc(static_cast<uint64_t>(a->_capacity) << 3, 4));
                }
                int32_t i = (old == a->_data) ? oldSize : 0;
                for (; i < newSize; ++i) {
                    Link* dst = a->_data + i;
                    if (dst == nullptr) continue;
                    const Link* src = (i < oldSize) ? old + i : &fill;
                    *dst = *src;                    // one qword move
                }
                if (old != a->_data && old != nullptr) rtcoll::MemFree(old);
            } else {
                // Pop the link free list.
                cell->_firstLink = scene->_freeLink;
                Link* links = scene->_links._data;
                scene->_freeLink = links[scene->_freeLink]._next;
                links[cell->_firstLink]._entry = entryId;
                links[cell->_firstLink]._next  = oldHead;
            }
        }
    }
}

// ============================================================================
// 0x18007b210 — allocate an Entry and fill it from the descriptor
// ============================================================================
// Both arms build the SAME complete Entry on the stack and then either hand it
// to the array grower (append) or assign it over the reused slot. The temporary
// owns an Array<Transform> of its own — copied from the descriptor — and it is
// destroyed after the hand-off, which is why the same buffer is not owned
// twice.
//
// TWO BYTES THIS BODY DOES NOT WRITE, both from the stack temporary:
//   +0x14..+0x17  the slack that 8-aligns `_mesh`
// and on the REUSE arm the free-list pop happens BEFORE the temporary is built,
// so `scene->_freeEntry` is already the NEXT free slot by the time anything is
// copied. Doing it after would leave the popped slot on the list.
//
// `_nextFree` is set to -2 ("in use") on both arms.
int32_t EntryAlloc(Scene* scene, const AddDesc* desc, const int32_t* xy,
                   uint16_t w, uint16_t h) {
    const int32_t freeIdx = scene->_freeEntry;

    // NOT value-initialised: retail leaves +0x14..+0x17 holding whatever the
    // frame held, and an oracle that compares those bytes fails 100% of calls.
    // The probe excludes them; zeroing here would make the port differ from
    // retail in a way nothing would ever report. `_transforms` is the one
    // member with a constructor: its zeroed header is retail's "Array header
    // zeroed first", and its destructor at the end of the scope is retail's
    // closing CALL 0x18001c100 on the temporary.
    Entry tmp;
    tmp._nextFree = -2;

    if (freeIdx >= 0) {
        scene->_freeEntry = scene->_entries._data[freeIdx]._nextFree;
    }

    tmp._queryStamp = 0;
    tmp._cellX      = xy[0];
    tmp._cellY      = xy[1];
    tmp._cellW      = w;
    tmp._cellH      = h;
    tmp._mesh       = desc->_mesh;
    TransformArrayAssign(&tmp._transforms, &desc->_transforms);   // CALL 0x18001b360
    tmp._userData   = desc->_userData;
    tmp._userValue  = desc->_userValue;
    tmp._userValue2 = desc->_userValue2;
    tmp._callback   = desc->_callback;

    int32_t result;
    if (freeIdx < 0) {
        EntryArraySetSize(&scene->_entries, scene->_entries._size + 1, &tmp);
        result = scene->_entries._size - 1;
    } else {
        // Retail RE-READS `_entries._data` here (`MOV RCX,[RSI+0x48]` at
        // 0x18007b348) rather than reusing the pointer it popped the free list
        // through. Nothing between the two can move the array — the only call
        // is TransformArrayAssign, which touches the temporary's own buffer —
        // but the re-read is reproduced rather than optimised away, because
        // "nothing can move it" is a claim about code this lane did not port.
        EntryAssign(&scene->_entries._data[freeIdx], &tmp);
        result = freeIdx;
    }
    return result;                                  // ~tmp: CALL 0x18001c100
}

// ============================================================================
// (*) 0x18007af70 — the grid insert
// ============================================================================
// Project the mesh's world bounds to XY, truncate to cell indices, allocate the
// entry, thread it in, return its index.
//
// The cell index is CVTTSS2SI — TRUNCATION toward zero, NOT floor. For a
// negative coordinate that means the cell boundary at 0 is shared by [-1,0] and
// [0,1], which makes the grid slightly wrong near the origin. It is retail's
// arithmetic and the whole point of a byte-faithful port is that our grid is
// wrong in exactly the same places.
//
// `w` and `h` are 16-BIT differences (`SUB SI,[RSP+0x60]` on the low words),
// so a rectangle wider than 65535 cells wraps. Reproduced as a uint16_t
// subtraction rather than an int one.
uint32_t GridAdd(Scene* scene, const AddDesc* desc) {
    float rect[4];
    MeshBoundsXY(desc, rect);

    const float inv = scene->_invCellSize;
    const int32_t cellX = static_cast<int32_t>(rect[0] * inv);
    const int32_t cellY = static_cast<int32_t>(rect[1] * inv);
    const int32_t maxX  = static_cast<int32_t>(rect[2] * inv);
    const int32_t maxY  = static_cast<int32_t>(rect[3] * inv);

    const uint16_t w = static_cast<uint16_t>(
        static_cast<uint16_t>(maxX) - static_cast<uint16_t>(cellX) + 1);
    const uint16_t h = static_cast<uint16_t>(
        static_cast<uint16_t>(maxY) - static_cast<uint16_t>(cellY) + 1);

    const int32_t xy[2] = { cellX, cellY };
    const int32_t id = EntryAlloc(scene, desc, xy, w, h);
    AddToCells(scene, xy, w, h, id);
    return static_cast<uint32_t>(id);
}

// ============================================================================
// (*) 0x1800724d0 — the grid re-index behind SetTransform
// ============================================================================
// Recompute the entry's cell rectangle and, ONLY IF IT CHANGED, unthread from
// the old cells and rethread into the new. The early-out is the reason this
// function's `calls=` is not its coverage: on a settled scene most transform
// changes leave the rectangle alone and this does nothing at all. That is
// exactly Wave 9 lane M's Finding 1 — `calls=12896` on a site where ten calls
// did any work — so the probe counts CHANGED rectangles separately.
//
// The order matters and is retail's: unthread FIRST with the entry's OLD
// rectangle (which is still in the entry), then thread with the new, and only
// then write the new rectangle into the entry. Writing it first would unthread
// from cells the entry was never in.
void GridReindex(Scene* scene, uint32_t id) {
    Entry* entry = &scene->_entries._data[id];

    float rect[4];
    MeshBoundsXY(entry, rect);

    const float inv = scene->_invCellSize;
    const int32_t cellX = static_cast<int32_t>(rect[0] * inv);
    const int32_t cellY = static_cast<int32_t>(rect[1] * inv);
    const int32_t maxX  = static_cast<int32_t>(rect[2] * inv);
    const int32_t maxY  = static_cast<int32_t>(rect[3] * inv);

    const uint16_t w = static_cast<uint16_t>(
        static_cast<uint16_t>(maxX) - static_cast<uint16_t>(cellX) + 1);
    const uint16_t h = static_cast<uint16_t>(
        static_cast<uint16_t>(maxY) - static_cast<uint16_t>(cellY) + 1);

    const int32_t  oldX = entry->_cellX;
    const int32_t  oldY = entry->_cellY;
    const uint16_t oldW = entry->_cellW;
    const uint16_t oldH = entry->_cellH;

    if (oldX == cellX && oldY == cellY && oldW == w && oldH == h) return;

    int32_t newXY[2] = { cellX, cellY };
    GridUnthread(scene, &entry->_cellX, oldW, oldH, static_cast<int32_t>(id));
    AddToCells(scene, newXY, w, h, static_cast<int32_t>(id));
    entry->_cellX = cellX;
    entry->_cellY = cellY;
    entry->_cellW = w;
    entry->_cellH = h;
}

// ============================================================================
// (*) 0x180072950 — unthread an entry from every cell of its rectangle
// ============================================================================
// `cellXY` points at `entry->_cellX` and the y is read from `cellXY[1]`. Both
// the x START and the y START are re-read from it per row exactly as retail
// does, while the two LIMITS are computed once from the values at entry.
//
// The bucket hash is carried INCREMENTALLY (`SUB EBP,0x72594cbd` per column,
// `SUB R14D,0x27e9c7bf` per row) rather than recomputed. That is identical
// modulo 2^32 to recomputing it, and it is written the incremental way here so
// that a reader diffing this against the disassembly sees the same arithmetic
// rather than having to re-derive the identity.
//
// The inner lookup ends with `linkIdx = -1` when the cell is not in the bucket,
// and then calls UnlinkEntryFromCell on `_cells._data + (-1)*0x10` — a read
// sixteen bytes BEFORE the array. That is retail, and it is unreachable in
// practice (the entry is threaded into every cell of its own rectangle, so
// every one of them exists), which is why it is left unguarded.
void GridUnthread(Scene* scene, int32_t* cellXY, uint16_t w, uint16_t h,
                  int32_t id) {
    const int32_t x0 = cellXY[0];
    int32_t y = cellXY[1];
    const int32_t xLimit = static_cast<int32_t>(w) + x0;
    const int32_t yLimit = static_cast<int32_t>(h) + y;
    if (y >= yLimit) return;

    uint32_t rowHash = static_cast<uint32_t>(y) * 0xd8163841u;
    do {
        int32_t x = cellXY[0];
        if (x < xLimit) {
            uint32_t hash = rowHash + static_cast<uint32_t>(x) * 0x8da6b343u;
            do {
                int32_t bucket = HashBucket(static_cast<int32_t>(hash),
                                            scene->_cellHeads._size);
                int32_t c = scene->_cellHeads._data[bucket];
                if (c >= 0) {
                    Cell* cells = scene->_cells._data;
                    for (;;) {
                        if (cells[c]._x == x && cells[c]._y == y) goto found;
                        c = cells[c]._nextInBucket;
                        if (c < 0) break;
                    }
                }
                c = -1;
            found:
                UnlinkEntryFromCell(scene, scene->_cells._data + c, id);
                ++x;
                hash -= 0x72594cbdu;
            } while (x < xLimit);
        }
        ++y;
        rowHash -= 0x27e9c7bfu;
    } while (y < yLimit);
}

// ============================================================================
// (*) 0x18007b390 — release an Entry back to the free list
// ============================================================================
// Assign an all-zero Entry over the live one (which releases its
// Array<Transform> through the assignment operator), destroy the temporary's
// own empty array, then push the slot: `entry->_nextFree = scene->_freeEntry;
// scene->_freeEntry = id`.
//
// *** THE TEMPORARY IS NOT FULLY INITIALISED, AND THAT IS RETAIL. ***
// 0x18007b390 writes eleven fields of its stack Entry and leaves TWO windows
// untouched — +0x14..+0x17 (the alignment slack) and +0x3c (`_userValue2`).
// The assignment then copies both into the live entry, so after any
// RemoveMesh the entry's `_userValue2` holds a dword of the DLL's stack frame.
// This is the same finding Wave 5 lane D's RemoveMesh shadow surfaced
// (reference_shadow_predicate_must_name_its_failing_term.md). The padding is
// still left unwritten; `_userValue2` is zeroed since review F-146, because
// copying an indeterminate int (EntryAssign) is undefined behaviour in C++. An
// oracle comparing +0x3c still has to exclude it.
//
// `_nextFree` is -1 in the temporary and is immediately overwritten by the
// free-list push, so the -1 is never observable. It is written anyway.
void GridFreeEntry(Scene* scene, uint32_t id) {
    {
        Entry tmp;                                  // `_transforms` zeroed by its ctor
        tmp._queryStamp = 0;
        tmp._nextFree   = -1;
        tmp._cellX      = 0;                        // one qword in retail (H7)
        tmp._cellY      = 0;
        tmp._cellW      = 0;                        // one dword in retail (H7)
        tmp._cellH      = 0;
        // +0x14..+0x17 deliberately NOT written — see the banner above.
        tmp._mesh       = nullptr;
        tmp._userData   = nullptr;
        tmp._userValue  = 0;
        // NOT RETAIL (review F-146, indeterminate read): retail never writes +0x3c
        // (_userValue2) and EntryAssign copies its stack garbage into the live entry
        // (see the banner above). Copying an indeterminate int is undefined in C++,
        // so it is zero here. Nothing reads the field of a freed entry.
        tmp._userValue2 = 0;
        tmp._callback   = nullptr;

        EntryAssign(&scene->_entries._data[id], &tmp);   // 0x18007b590
    }                                               // ~tmp: CALL 0x18001c100

    // retail re-reads `_entries._data`
    scene->_entries._data[id]._nextFree = scene->_freeEntry;
    scene->_freeEntry = static_cast<int32_t>(id);
}

// ============================================================================
// (*) 0x18007b6d0 — the cell-rectangle iterator advance
// ============================================================================
// Advances `it` to the next (cell, link) pair whose entry has NOT already been
// stamped with `it->stamp`, and sets `it->link = -1` when the rectangle is
// exhausted. The stamp is what makes an entry spanning several cells get tested
// once per query rather than once per cell it covers.
//
// `it->maxX` is loaded into EBX ONCE, before the loop, and every column test
// uses that cached copy. Nothing in the body writes it, so caching it here is
// the same program; it is written as a local for exactly that reason and the
// fact is stated rather than left for a reader to re-derive.
//
// Two branches in retail are unreachable and are kept: `TEST R8D,R8D; JS` on a
// bucket index that was just proved non-negative, and the redundant `_firstLink
// < 0` test. They cost nothing and their absence would be an unproved claim.
void CellIterAdvance(CellIter* it) {
    const int32_t maxX = it->maxX;

    for (;;) {
        // ---- advance the cursor, row-wrapping, until a non-empty cell -------
        int32_t c;
        const Scene* s;
        int32_t x, y;
        for (;;) {
            it->curX = it->curX + 1;
            if (it->curX > maxX) {
                it->curY = it->curY + 1;
                it->curX = it->minX;
                if (it->curY > it->maxY) { it->link = -1; return; }
            }
            s = it->scene;
            x = it->curX;
            y = it->curY;
            c = s->_cellHeads._data[HashBucket(CellHash(x, y), s->_cellHeads._size)];
            if (c >= 0) break;
        }

        // ---- walk the bucket chain looking for (x, y) -----------------------
        Cell* cells = s->_cells._data;
        bool  found = false;
        for (;;) {
            if (cells[c]._x == x && cells[c]._y == y) { found = true; break; }
            c = cells[c]._nextInBucket;
            if (c < 0) break;
        }
        if (!found) continue;
        if (c < 0) continue;                        // retail's dead test

        int32_t link = s->_cells._data[c]._firstLink;
        if (link < 0) continue;                     // retail's dead test

        // ---- skip links whose entry is already stamped ----------------------
        const int32_t stamp = it->stamp;
        it->link = link;
        Link* links = s->_links._data;
        int32_t entryStamp = s->_entries._data[links[link]._entry]._queryStamp;
        if (entryStamp != stamp) return;            // an unstamped entry: done

        it->link = links[link]._next;
        while (it->link >= 0) {
            links = s->_links._data;
            const int32_t li = it->link;
            entryStamp = s->_entries._data[links[li]._entry]._queryStamp;
            if (entryStamp != stamp) return;
            it->link = links[li]._next;
        }
        // it->link < 0 here: the cell is exhausted, move to the next one.
    }
}

}  // namespace rtpickgrid

