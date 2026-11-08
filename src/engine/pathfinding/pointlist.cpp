// ============================================================================
// src/engine/pathfinding/pointlist.cpp — Pathfinding::PointList, whole class
// ============================================================================
//
// Vtable batch 8. ??_7PointList@Pathfinding@@6B@ (retail), nine slots:
//
//   0  deleting dtor        0x1800713b0  deleting_dtor.cpp row (registers ~PointList here)
//   1  TypeName             0x180070e40  here
//   2  Type                 0x180070e20  here
//   3  GetFileVersion       0x18010f880  here  (returns 5)
//   4  Compile              0x18010f6d0  here  (returns 1, External::CompileResult::Ok)
//   5  Serialize            0x18010f6e0  here
//   6  Reconstruct          0x180071060  here
//   7  Destruct             0x180071150  here
//   8  Construct            0x1800710e0  here
//
// plus ~PointList (0x18010f8e0), registered here and appended to
// src/engine/object/deleting_dtor.cpp's kSpecs.
//
// Layout (sizeof 0x50; Reconstruct/Construct zero +0x20..+0x48 as six qwords,
// factory step table src/engine/pathfinding/rt_factories_pathfinding.inc agrees):
//   +0x00  Object::External              (0x20)
//   +0x20  System::String   _name1
//   +0x28  System::String   _name2
//   +0x30  Array<Point>     _pointsA     {data, size@0x38, cap@0x3c}
//   +0x40  Array<Point>     _pointsB     {data, size@0x48, cap@0x4c}
//
// Point (0x18, from Serialize 0x18010f6e0's element loop -- String at elem+0,
// Vector3 at elem+8 via kArrayFloat(3,ptr), float at elem+0x14 via kScalarFloat):
//   +0x00  System::String  _name
//   +0x08  Vector3         _pos      (3 floats, kArrayFloat n=3)
//   +0x14  float           _radius   (kScalarFloat)
//
// Serialize order (0x18010f6e0): _name1, _name2, then _pointsB (this+0x40) count
// at +0x48, THEN _pointsA (this+0x30) count at +0x38 -- retail serializes the
// SECOND array field before the first.
// Array growth: FUN_180071730 (0x180071730) -- generic Point-stride (0x18) grow,
// same MemAlloc/copy/zero-tail shape as every other Collection::Array<T> grow
// this port has already transcribed elsewhere; align 8 (`MemAlloc(n*0x18,8)`).
// Element release: FUN_180071890 releases each Point's String (refcount idiom)
// walking with stride 3 longlongs (0x18); ~PointList (0x10f8e0) frees each
// array's own String elements via the same walk before freeing the buffer.
//
// ENDGAME B.3 step 7, block B6 (RULE B7-9): `Pathfinding::PointList` is a real
// `Object::External` class now (src/engine/pathfinding/PointList.h). The
// anonymous-namespace struct that used to carry the 0x50 layout here MOVED into
// that header under the retail name (B6-8), the slot bodies are its members,
// and every registered door below is a one-line QUALIFIED delegation — a live
// PointList wears the MINTED table, whose slots resolve back to these doors.
// The factory rows, the deleting-dtor row and ~PointList's door body stay.
// Field names follow the getters: +0x20 `_meshName` (GetMeshName), +0x28
// `_subObject` (no getter), +0x30 `_points` (Points), +0x40 `_pathPoints`
// (PathPoints) — so "_pointsB before _pointsA" above reads "_pathPoints
// before _points".
//
// THE ALLOCATOR, corrected in the same move: the Array<Point> grow and free
// used to resolve `?MemAlloc@MemoryAllocator@System@@` / `?MemFree@` by RUNTIME
// ADDRESS (0x1800c7a50 / 0x1800c7ad0) through function-local first-call
// caches. That is both a RULE B6-5 site (Serialize runs on the LOADER thread)
// and the "half-shimmed heap" Collection.h's allocator banner forbids: the
// redzone shim rewrites the exe dispatch door, not the runtime address. Both
// now go through `rtcoll::MemAlloc` / `rtcoll::MemFree`, the one door every
// other Array body uses. Same allocator on a normal run.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>          // placement new of the two String members

#include "collection/Collection.h"
#include "io/Serializer.h"
#include "io/Stream.h"       // IO::Stream::_direction (B.4 R-SYS)
#include "pathfinding/PointList.h"
#include "system/String.h"
#include "system/string_internal.h"

namespace {

using rt::io::ReaderWriter;
using rt::io::Serializer;
using rt::io::Stream;
using Point = Pathfinding::PointList::Point;

// ENDGAME B.3.9 ENG: `InlineCtorStores` (Base / External / PointList vptr
// stamps from the MINT plus the six zero qwords) and the file's `CallSlot0`
// are gone. The stores are the real constructor now (PointList.h), the stamps
// the compiler's, and slot 0 is a virtual call on the class.

// FUN_180071890 (retail 0x180071890) -- release every Point's String in
// [0, count). Body B with the String at +0x00 and stride 0x18, the same rule
// src/engine/collection/array_destroy.cpp files as kD_Str_18at00; kept here,
// where its two callers already live, so there is one body per address.
// REGISTERED as of batch D2 (2026-09-12): retail's 0x180071730 and 0x180071600
// both reach it by rel32, and at cutover that rel32 will not exist.
void ReleasePointsRange(Point* data, int count) {
    for (int i = 0; i < count; ++i) {
        // The release idiom IS `~String` (0x180006c80), run on the element's
        // String in place: the element itself is raw storage (PointList.h).
        data[i].name.~String();
    }
}

// FUN_180071730 -- Collection::Array<Point>::SetSize (retail 0x180071730), byte-faithful
// grow/shrink: reused old buffer's live elements are moved, new tail elements get a
// zeroed Point (default), shrink-tail elements have their String released before the
// buffer is (re)used or freed.
void PointArraySetSize(Collection::Array<Point>* a, int32_t newSize) {
    // NOT RETAIL (review F-124, undefined behaviour): a negative point count from a
    // malformed PointList resource made retail release Strings at `old + newSize`, below the
    // buffer (below null on a fresh list), and left the size negative. Retail has no sign
    // check; a negative count is SetSize(0) here (review F-012 is the same clamp in
    // rtcoll::SetSizeCore). Valid data never carries one.
    if (newSize < 0) newSize = 0;
    int32_t oldSize = a->_size;
    if (newSize == oldSize) return;
    Point* oldData = a->_data;
    a->_size = newSize;
    if (a->_capacity < newSize) {
        a->_capacity = newSize;
        if (newSize == oldSize + 1 && oldSize != 0) a->_capacity = newSize * 3;
        a->_data = static_cast<Point*>(
            rtcoll::MemAlloc(static_cast<uint64_t>(a->_capacity) * sizeof(Point), 8));
    }
    int32_t copyFrom = (oldData == a->_data) ? oldSize : 0;
    for (int32_t i = copyFrom; i < newSize; ++i) {
        Point* dst = &a->_data[i];
        if (i < oldSize) {
            // *** NOT a plain struct assignment. *** Retail's copy arm nulls
            // the destination's String slot, adopts the source's pointer and
            // BUMPS the shared refcount at [str-4], then copies four plain
            // dwords. This body used to do `*dst = oldData[i]`, which moved
            // the pointer WITHOUT the bump -- and since the realloc arm below
            // then calls ReleasePointsRange over the whole old buffer, every
            // grow of a named PointList dropped one reference per element and
            // could free a String the new buffer still points at. Fixed while
            // registering the address (batch D2, 2026-09-12).
            const Point* src = &oldData[i];
            dst->name._data = nullptr;
            if (char* s = src->name._data) {
                dst->name._data = s;
                ++RtSystem::StringRefCount(s);
            }
            dst->pos.x = src->pos.x;
            dst->pos.y = src->pos.y;
            dst->pos.z = src->pos.z;
            dst->radius = src->radius;
        } else {
            // retail's else arm: THREE zero qwords over the whole 0x18 stride
            dst->name._data = nullptr;            // +0x00
            dst->pos.x  = 0.0f;                   // +0x08 qword (pos.x, pos.y), H7
            dst->pos.y  = 0.0f;
            dst->pos.z  = 0.0f;                   // +0x10 qword (pos.z, radius), H7
            dst->radius = 0.0f;
        }
    }
    if (oldData == a->_data) {
        if (newSize < oldSize) ReleasePointsRange(oldData + newSize, oldSize - newSize);
    } else {
        ReleasePointsRange(oldData, oldSize);
        if (oldData != nullptr) rtcoll::MemFree(oldData);
    }
}

// FUN_180071600 (retail 0x180071600) -- `~Array<Point>`: the element destroy
// above over [0, size), then the plain `~Array` (free the buffer, zero the
// three header fields). The J2 shape, written here rather than in
// array_destroy.cpp because its element helper already lives in this file and
// a second copy of either is exactly the drift this project keeps paying for.
void PointArrayDtor(Collection::Array<Point>* a) {
    ReleasePointsRange(a->_data, a->_size);
    if (a->_data != nullptr) rtcoll::MemFree(a->_data);
    a->_data = nullptr;
    a->_size = 0;
    a->_capacity = 0;
}

}  // namespace

// ############################################################################
// Pathfinding::PointList's member bodies (ENDGAME B.3.7 block B6) — after the
// anonymous namespace, inside `namespace Pathfinding`.
// ############################################################################
namespace Pathfinding {

// TypeId 0x180070e30 / [2] 0x180070e20 / [1] 0x180070e40: DECLARE_TYPE's
// loads over the class's own record (pathfinding_type_records.cpp; ENDGAME
// B.3.9 TI-REST).
// [3] 0x18010f880: `MOV EAX,5 ; RET`.
int32_t PointList::GetFileVersion() const { return 5; }
// [4] 0x18010f6d0: returns 1 (the same CompileResult value External's returns).
Object::External::CompileResult PointList::Compile(const System::Filename&) { return 1; }

// [8] 0x1800710e0 -- the inlined constructor, i.e. placement construction.
// (Retail's `TEST RCX,RCX` null test sat in the retired door; a virtual call
// always has a `this`.) `::new`: the class declares its own placement new.
Object::External* PointList::Construct() {
    return ::new (this) PointList();
}
// [6] 0x180071060: slot0(this, 0) dispatched VIRTUALLY (a subclass's deleting
// destructor is what has to run), then the same inlined constructor.
Object::External* PointList::Reconstruct() {
    this->~PointList();
    return ::new (this) PointList();
}
// [7] 0x180071150: `MOV RAX,[RCX] ; XOR EDX,EDX ; JMP [RAX]`.
void PointList::Destruct() { this->~PointList(); }

// ?CreateNewObject@PointList@Pathfinding@@SAPEAVBase@Object@@XZ  0x180070f30
// The FACTREST row this replaces (rt_factories_pathfinding.inc): MemAlloc of
// 0x50 bytes, align 8 (the locked `?MemAlloc@MemoryAllocator@System@@`
// Object::detail::InternalMemAlloc reaches); retail's null branch; then the
// inlined constructor.
Object::Base* PointList::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(PointList), 8);
    if (p == nullptr) return nullptr;
    return ::new (p) PointList();
}
// ?CreateInPlaceObject@PointList@Pathfinding@@SAPEAVBase@Object@@PEAX@Z  0x180070ff0
// retail's null test, then the same inlined constructor.
Object::Base* PointList::CreateInPlaceObject(void* place) {
    if (place == nullptr) return nullptr;
    return ::new (place) PointList();
}

// ============================================================================
// [5] ?Serialize@PointList@Pathfinding@@UEAA_NAEAVStream@IO@@@Z   0x18010f6e0
// ============================================================================
// Order: _meshName, _subObject, then _pathPoints (+0x40, count at +0x48) and
// only THEN _points (+0x30, count at +0x38) — retail serializes the SECOND
// array field before the first. Per Point: String, kArrayFloat(n=3) at +8,
// kScalarFloat at +0x14.
bool PointList::Serialize(IO::Stream& stream) {
    // B.4 R-SYS: retail's `??0Serializer@IO@@` (0x1800b9960): the stream, a
    // null ReaderWriter, then the encoding's CreateReader (+0x18) / CreateWriter
    // (+0x20) by direction, version 0. `~Serializer` (0x1800b99b0) deletes the
    // ReaderWriter at scope exit, where the hand-written `delete` stood.
    IO::Serializer ser(stream, 0);

    // `?Serialize@String@System@@QEAAXAEAVSerializer@IO@@@Z` (0x1800c9930).
    _meshName.Serialize(ser);
    _subObject.Serialize(ser);

    // SerializeInt +0xa0, SerializeFloat +0x80, SerializeFloatArray +0x28.
    IO::ReaderWriter* rwc = ser._readerWriter;

    auto serializeArray = [&](Collection::Array<Point>* arr) {
        int32_t n = arr->_size;
        rwc->SerializeInt(n, nullptr);
        if (stream._direction == rt::io::kDirectionReading) PointArraySetSize(arr, n);
        for (int32_t i = 0; i < n; ++i) {
            Point* p = &arr->_data[i];
            p->name.Serialize(ser);
            rwc->SerializeFloatArray(3, &p->pos.x, nullptr);   // Vector3::Serialize 0x1800150e0, inlined
            rwc->SerializeFloat(p->radius, nullptr);
        }
    };

    serializeArray(&_pathPoints);   // this+0x40 first
    serializeArray(&_points);       // then this+0x30

    return true;   // ~Serializer deletes the ReaderWriter
}

// ??1PointList@Pathfinding@@UEAA@XZ  0x18010f8e0 -- retail's body, moved here
// from the retired `RtPathfinding_PointList_Dtor` door (ENDGAME B.3.9 ENG).
// The entry restamp is the compiler's. Retail CALLs 0x180071600 (~Array<Point>)
// on BOTH arrays at 0x18010f904 and 0x18010f90e -- NOT SetSize(0), which would
// keep (leak) the buffer. That body frees and ZEROES each header, so the
// compiler-emitted `~Array` of each member afterwards is a no-op (MemFree of
// null). The compiler then releases `_subObject` and `_meshName` and chains
// ~External -- retail's order and retail's tail call.
PointList::~PointList() {
    PointArrayDtor(&_pathPoints);
    PointArrayDtor(&_points);
}

}  // namespace Pathfinding

// ---- CUTOVER CLOSURE BATCH D2 "helpers-high" (2026-09-12) -----------------

// ============================================================================
// ENDGAME B.3.9 ENG: the eight slot doors (`RtPathfinding_PointList_Type` ..
// `_Serialize`) and the `??1PointList` door are gone. Their only reader was
// the minted `??_7PointList@Pathfinding@@6B@` (and, for the ??1 door, its
// RT_DELDTOR row): every live PointList now wears the compiler's table.
// ============================================================================

// B3R (registry teardown, W1 RT-Core): every row this function registered had
// no reader by name or by address, so they went. It registers nothing and
// stays defined only because standalone/resolve_standalone.cpp's
// RuntimePortInit() still calls it (the coordinator removes the call).
