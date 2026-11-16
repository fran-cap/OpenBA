// ============================================================================
// src/engine/video/device_enum_types.cpp — Wave 18 lane GFXIF
//   Video::Device::Adapter and ::GraphicsInterface (the enumeration value
//   types), plus the two Device methods that build and read them.
// ============================================================================
//
// WHY THIS FILE EXISTS: src/render/dx11/devicedata.cpp calls these bodies by
// name (`rec.FindOrAddMode`, `dev->GetDisplayMode`).
// (History: docs/history/runtime_port/WAVE16_BOOTGATE.md)
//
//   | Export                                        | Retail      |
//   |-----------------------------------------------|-------------|
//   | ??0GraphicsInterface@Device@Video@@QEAA@XZ    | 0x18000cd90 |
//   | ??0GraphicsInterface@…@AEBU012@@Z             | 0x18000ce10 |
//   | ??1GraphicsInterface@Device@Video@@QEAA@XZ    | 0x18000cdc0 |
//   | ??0Adapter@Device@Video@@QEAA@XZ              | 0x1800f7100 |
//   | ??0Adapter@Device@Video@@QEAA@AEBU012@@Z      | 0x18000cc70 |
//   | ??1Adapter@Device@Video@@QEAA@XZ              | 0x18000cc10 |
//   | ?FindOrAddMode@Adapter@Device@Video@@…        | 0x1800f7020 |
//   | ?GetDisplayMode@Device@Video@@QEBA?AV…@H@Z    | 0x18000d3e0 |
//
// (`?AdjustDisplay@Device@Video@@QEAAXXZ`, 0x1800f7e20, is the ninth. It is
// once pure Win32; it lives in device_adjustdisplay.cpp.)
//
// Every address above was read out of the shipped export directory of
// C:\GOG Games\Banished\Runtime-x64.dll with tools/pe_exports.py BEFORE any
// body was written, not copied from a document.
//
// THE ONE ADDRESS THE BRIEF FLAGGED. `??0Adapter@Device@Video@@QEAA@XZ` sits at
// 0x1800f7100, far from the other five (0x18000cc10-0x18000ce10), which raised
// the question of whether it is a thunk. It is NOT: the body at 0x1800f7100 is
// fourteen instructions that zero +0x00..+0x27, store -1 as a QWORD across
// +0x28..+0x2f and -1 as a DWORD at +0x30, and return `this`. Disassembled, not
// decompiled. The split is just which translation unit MSVC emitted each into.
//
// A NEAR-MISS NOT TO REPEAT: src/render/dx11/devicedata.cpp lists a different address
// beside each of these names (0x1800132d0 for the GraphicsInterface ctor, and
// so on). That column is VideoDX11's OWN IAT SLOT, not the Runtime function —
// every value in it falls inside 0x180013218-0x1800134c8. It is not a
// discrepancy and must not be "fixed".
//
// WHAT THIS FILE DOES NOT DUPLICATE
// ---------------------------------
// Seven unexported Collection::Array instantiations these bodies call were
// ported by Wave 9 lane VID2 in device_startup.cpp; ENDGAME B5 DEV-B MOVED them
// here unchanged (one copy, not two), so this file links into the host unit
// tests that construct, copy and destroy a real Adapter / GraphicsInterface:
//     0x18000fcd0  Array<DisplayMode>::operator=   RtVideo_ArrayModeAssign
//     0x180008b00  Array<bool>::operator=          RtVideo_ArrayBoolAssign
//     0x180010830  Array<DisplayMode>::~Array      RtVideo_ArrayModeDtor
//     0x180009560  Array<bool>::~Array             RtVideo_ArrayBoolDtor
//     0x180010760  Array<RefreshRate>::~Array      RtVideo_ArrayRefreshDtor
//     0x180010cf0  Array<DisplayMode> elem destroy RtVideo_ArrayModeElemDestroy
//     0x18000fbf0  Array<RefreshRate>::operator=   RtVideo_ArrayRefreshAssign
//
// Five instantiations were NOT already ported, and are added below because the
// nine bodies cannot run without them once the DLL is gone:
//     0x180010da0  Array<Adapter> per-element destroy
//     0x180010900  Array<Adapter>::~Array
//     0x18000fe10  Array<Adapter>::operator=
//     0x180057340  Array<RefreshRate>::SetSize(n, fill)
//     0x180057440  Array<DisplayMode>::SetSize(n, fill)
// None of the five is exported (checked against the export directory, not
// assumed from the mangling — `AEAA` means private, not unexported, and this
// DLL does export its private members). They are template instantiations with
// no name of their own.
//
// PORTABILITY: no <windows.h> here, per PLAN §4.1 decision 1.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <new>       // B5 DEV-B: Array<Adapter>::operator= placement-news the copy ctor

#include "system/MemoryAllocator.h"
#include "video/Device.h"

using Video::Device;
using Adapter           = Video::Device::Adapter;
using DisplayMode       = Video::Device::DisplayMode;
using GraphicsInterface = Video::Device::GraphicsInterface;
using RefreshRate       = Video::Device::RefreshRate;


namespace {

// ---------------------------------------------------------------------------
// THE GROWTH RULE. Identical in shape to GrowFor below, but the
// new size arrives as a PARAMETER here rather than from a source array, and the
// old size is read by the caller before `size` is overwritten. Transcribed from
// 0x180057370-0x1800573a1 (RefreshRate) and 0x18005747a-0x1800574af
// (DisplayMode), which are the same instructions with a different stride.
//
//   if (n > capacity) {
//       capacity = n;
//       if (n == old + 1 && old != 0) capacity = n * 3;   // amortise
//       data = MemAlloc(capacity * stride, align);
//   }
//
// The `n == old + 1 && old != 0` guard is the whole rule: growth by exactly one
// from a non-empty array triples, everything else takes the exact size. It
// decides the heap layout, so it is reproduced rather than simplified.
//
// B.4 (R-VIDEO): the array is its owner's named `data` / `capacity` fields.
// ---------------------------------------------------------------------------
template <class T>
void GrowTo(T*& data, int32_t& capacity, int32_t n, int32_t old, int stride, int align) {
    if (n > capacity) {
        capacity = n;
        if (n == old + 1 && old != 0) capacity = n * 3;
        data = static_cast<T*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(
                   static_cast<int64_t>(capacity) * stride), align));
    }
}

// ---------------------------------------------------------------------------
// (ENDGAME B5 DEV-B) The inlined System::String share / release that the
// Adapter and GraphicsInterface lifecycle bodies carry is the `System::String
// name` MEMBER's own copy constructor / destructor now; see the lifecycle
// section at the end of this file for the instruction-level comparison.
// ---------------------------------------------------------------------------

}  // namespace

// ============================================================================
// MOVED HERE from device_startup.cpp by ENDGAME B5 DEV-B, unchanged: the seven
// Array helpers Video::Device::ValidateConfiguration and the Adapter copy
// constructor / destructor reach. Same bodies, same address comments.
// ============================================================================

namespace {

// ---------------------------------------------------------------------------
// THE GROWTH RULE, which is identical in all four `operator=` bodies and is
// transcribed once here because getting it wrong is invisible until a resize:
//
//   n = src->size ; old = dst->size ; dst->size = n
//   if (n > dst->capacity) {
//       dst->capacity = n;
//       if (n == old + 1 && old != 0) dst->capacity = n * 3;   // amortise
//       dst->data = MemAlloc(capacity * stride, align);
//   }
//
// The `n == old + 1 && old != 0` guard is the whole rule: growth by exactly one
// from a non-empty array triples, everything else takes the exact size. A
// "simplification" to always triple, or to always take n, changes the
// allocation pattern and therefore the heap layout — which this project has
// already paid for once through a boundary-tag defect.
//
// B.4 (R-VIDEO): an array is its owner's three named fields, taken by
// reference — Device.h unrolls every `Collection::Array<T>` of these classes
// into `{xData, xCount, xCapacity}` members (see the declarations there).
// ---------------------------------------------------------------------------
template <class T>
inline void GrowFor(T*& data, int32_t& size, int32_t& capacity, int32_t srcSize,
                    int stride, int align) {
    const int n   = srcSize;
    const int old = size;
    size = n;
    if (n > capacity) {
        capacity = n;
        if (n == old + 1 && old != 0) capacity = n * 3;
        data = static_cast<T*>(System::MemoryAllocator::MemAlloc(static_cast<uint64_t>(
                   static_cast<int64_t>(capacity) * stride), align));
    }
}

}  // namespace

// ============================================================================
// 0x180010760 — Collection::Array<Device::RefreshRate>::~Array
// 0x180009560 — Collection::Array<bool>::~Array
//
// Byte-identical bodies (the element types are trivially destructible, so MSVC
// emitted the same code twice and did NOT fold them). Both:
//     if (data) MemFree(data);   // the null test is retail's, OUTSIDE the call
//     data = nullptr; *(uint64*)&size = 0;   // one qword clears size AND capacity
// The one qword store is written as the two named fields, in address order.
// ============================================================================
void RtVideo_ArrayRefreshDtor(RefreshRate*& data, int32_t& size, int32_t& capacity) {
    if (data) System::MemoryAllocator::MemFree(data);
    data = nullptr;
    size = 0;
    capacity = 0;
}
void RtVideo_ArrayBoolDtor(uint8_t*& data, int32_t& size, int32_t& capacity) {
    if (data) System::MemoryAllocator::MemFree(data);
    data = nullptr;
    size = 0;
    capacity = 0;
}

// ============================================================================
// 0x180010cf0 — the per-element destroy for Array<DisplayMode>.
//
// Each 0x18-byte DisplayMode carries an Array<RefreshRate> at +0x08. Retail
// walks with a cursor at element+8 (`LEA RBX,[RDX+8]`) and frees each one
// inline rather than calling 0x180010760.
//
// Retail's first parameter is the array object; the callee spills it at
// [RSP+8] and NEVER READS IT, so it is not passed here.
// ============================================================================
void RtVideo_ArrayModeElemDestroy(DisplayMode* data, int32_t count) {
    if (count <= 0) return;
    DisplayMode* e = data;
    for (int64_t i = count; i != 0; --i) {
        if (e->refreshData) System::MemoryAllocator::MemFree(e->refreshData);
        e->refreshData     = nullptr;
        e->refreshCount    = 0;
        e->refreshCapacity = 0;
        ++e;
    }
}

// ============================================================================
// 0x180010830 — Collection::Array<DisplayMode>::~Array
// ============================================================================
void RtVideo_ArrayModeDtor(DisplayMode*& data, int32_t& size, int32_t& capacity) {
    RtVideo_ArrayModeElemDestroy(data, size);
    if (data) System::MemoryAllocator::MemFree(data);
    data = nullptr;
    size = 0;
    capacity = 0;
}

// ============================================================================
// 0x18000fbf0 — Collection::Array<RefreshRate>::operator=   (stride 8, align 4)
//
// THE PER-ELEMENT NULL TEST IS ON THE COMPUTED ADDRESS, NOT ON `data`:
//     MOV R9,[RBX] / ADD R9,RDX / JZ skip
// the ZF comes from the ADD, so the element is skipped when `data + offset`
// is zero. With a null `data` and a non-zero size — which the growth rule can
// produce if MemAlloc fails — that skips element 0 only and copies through null
// for the rest. Retail's; reproduced rather than "fixed".
//
// Retail's self-assignment test compares the two ARRAY objects; an array here
// is its `data` field and the two after it, so the test compares those fields'
// addresses.
// ============================================================================
void RtVideo_ArrayRefreshAssign(RefreshRate*& data, int32_t& size, int32_t& capacity,
                                RefreshRate* const& srcData, const int32_t& srcSize) {
    if (&data == &srcData) return;

    RtVideo_ArrayRefreshDtor(data, size, capacity);
    GrowFor(data, size, capacity, srcSize, 8, 4);

    for (int32_t i = 0; i < size; ++i) {
        RefreshRate* d = data + i;
        if (d == nullptr) continue;
        *d = srcData[i];
    }
}

// ============================================================================
// 0x180008b00 — Collection::Array<bool>::operator=          (stride 1, align 1)
// ============================================================================
void RtVideo_ArrayBoolAssign(uint8_t*& data, int32_t& size, int32_t& capacity,
                             uint8_t* const& srcData, const int32_t& srcSize) {
    if (&data == &srcData) return;

    RtVideo_ArrayBoolDtor(data, size, capacity);
    GrowFor(data, size, capacity, srcSize, 1, 1);

    for (int32_t i = 0; i < size; ++i) {
        uint8_t* d = data + i;
        if (d == nullptr) continue;
        *d = srcData[i];
    }
}

// ============================================================================
// 0x18000fcd0 — Collection::Array<DisplayMode>::operator=  (stride 0x18, align 8)
//
// Per element: copy width and height as two DWORDs, ZERO the embedded
// Array<RefreshRate> (so the assignment below sees an empty destination rather
// than the freed pointer the dtor left), then run the Array<RefreshRate>
// assignment. The zeroing is what makes the nested assign safe and it is
// retail's, at 0x18000fd71/0x18000fd74.
// ============================================================================
void RtVideo_ArrayModeAssign(DisplayMode*& data, int32_t& size, int32_t& capacity,
                             DisplayMode* const& srcData, const int32_t& srcSize) {
    if (&data == &srcData) return;

    RtVideo_ArrayModeDtor(data, size, capacity);
    GrowFor(data, size, capacity, srcSize, 0x18, 8);

    for (int32_t i = 0; i < size; ++i) {
        DisplayMode* d = data + i;
        if (d == nullptr) continue;
        const DisplayMode* s = srcData + i;

        d->width  = s->width;
        d->height = s->height;

        d->refreshData     = nullptr;
        d->refreshCount    = 0;
        d->refreshCapacity = 0;
        RtVideo_ArrayRefreshAssign(d->refreshData, d->refreshCount, d->refreshCapacity,
                                   s->refreshData, s->refreshCount);
    }
}

// ============================================================================
// 0x180010da0 — the per-element destroy for Array<Adapter>.
//
// STRING SPELLING B (ENDGAME B5 DEV-B). Retail's inlined String release inside
// this loop (0x180010e85) is the standalone dtors' release ("spelling A",
// 0x18000cc3a / 0x18000cde0 — byte-for-byte System::String::~String 0x180006c80,
// i.e. String::Release in src/engine/system/string.cpp) plus ONE extra test: at
// 0x180010e96 it does
//     ADD qword ptr [RSI],-0x8
//     MOV RSI,qword ptr [RSI]
//     JZ  <skip the free>
// and the JZ reads the flags of the ADD, so the MemFree is skipped when the
// decremented pointer (the allocation base) is exactly zero. Both spellings leave
// the slot at the base on the free path and null on the shared path. The loop
// calls the String member's destructor (spelling A) because the two cannot differ
// observably: a null name does nothing in both; a live name is a pointer
// `base + 8` into a block MemAlloc returned, so `base` is never zero; and the
// only value that would reach the extra test with base == 0 is a name of 8,
// whose refcount decrement at address 4 faults BEFORE either spelling gets to
// the test. Same stores, same frees, same fault, for every input.
//
// Retail walks with a cursor at element+0x10 (`LEA RBX,[RDX+0x10]`, stride
// 0x38) and destroys each Adapter in the order: the msaa Array<bool> at +0x18,
// then the Array<DisplayMode> at +0x08 (through the shared element-destroy at
// 0x180010cf0), then the String at +0x00. Both array frees are open-coded
// Enter/Free/Leave triples, which IS ?MemFree@MemoryAllocator@System@@ at
// 0x1800c7ad0 (src/engine/collection/array.cpp:191 established this).
//
// Retail's first parameter is the array object; the callee spills it at
// [RSP+8] and NEVER READS IT — same as the DisplayMode twin — so it is not
// passed here.
// ============================================================================
void RtVideo_ArrayAdapterElemDestroy(Adapter* data, int32_t count) {
    if (count <= 0) return;
    Adapter* e = data;
    for (int64_t i = count; i != 0; --i) {
        // +0x18 Array<bool> — data freed, then size AND capacity cleared by one
        // qword store at +0x20 (0x180010e2b/0x180010e2f, relative to elem+0x10),
        // written as the named fields in address order.
        uint8_t* msaa = e->multisampleData;
        if (msaa) System::MemoryAllocator::MemFree(msaa);
        e->multisampleData  = nullptr;
        e->multisampleCount = 0;
        e->multisampleCap   = 0;

        // +0x08 Array<DisplayMode> — per-element destroy, then the block.
        RtVideo_ArrayModeElemDestroy(e->modeData, e->modeCount);
        DisplayMode* modes = e->modeData;
        if (modes) System::MemoryAllocator::MemFree(modes);
        e->modeData     = nullptr;
        e->modeCount    = 0;
        e->modeCapacity = 0;

        // +0x00 System::String — retail's inlined "spelling B" release, which
        // is the String member's destructor (see STRING SPELLING B above).
        e->name.~String();

        ++e;
    }
}

// ============================================================================
// 0x180010f00 — the per-element destroy for Array<GraphicsInterface>.
//
// B.4 (R-VIDEO): MOVED here from video_resources.cpp (where ??1Device, its one
// caller, composed it out of lane D2's byte-offset bodies) beside its two
// siblings. Per element, stride 0x18, in retail's order: the nested per-Adapter
// destroy (0x180010da0, above) over the element's Array<Adapter>, then that
// array's block freed and its 16-byte header cleared (BODY A @+0x08: the free is
// behind the null test, then two qword zero stores, written as the three named
// fields), then the String at +0x00 (BODY B, spelling B).
// ============================================================================
void RtVideo_ArrayInterfaceElemDestroy(GraphicsInterface* data, int32_t count) {
    if (count <= 0) return;
    GraphicsInterface* g = data;
    for (int64_t i = count; i != 0; --i) {
        RtVideo_ArrayAdapterElemDestroy(g->adapterData, g->adapterCount);   // 0x180010f3f
        Adapter* adapters = g->adapterData;
        if (adapters) System::MemoryAllocator::MemFree(adapters);
        g->adapterData     = nullptr;
        g->adapterCount    = 0;
        g->adapterCapacity = 0;
        g->name.~String();                       // spelling B, as above
        ++g;
    }
}

// ============================================================================
// 0x180010900 — Collection::Array<Adapter>::~Array
// ============================================================================
void RtVideo_ArrayAdapterDtor(Adapter*& data, int32_t& size, int32_t& capacity) {
    RtVideo_ArrayAdapterElemDestroy(data, size);
    if (data) System::MemoryAllocator::MemFree(data);
    data = nullptr;
    size = 0;       // one store clears size AND capacity in retail
    capacity = 0;
}

// ============================================================================
// 0x18000fe10 — Collection::Array<Adapter>::operator=   (stride 0x38, align 8)
//
// Destroy the destination, grow it, then COPY-CONSTRUCT each element with the
// Adapter copy ctor (0x18000cc70) — not a memcpy. Retail calls the copy ctor
// per element at 0x18000fe92.
//
// THE PER-ELEMENT NULL TEST IS ON THE COMPUTED ADDRESS, NOT ON `data`:
//     MOV RCX,[RBX] / ADD RCX,RDX / JZ skip
// the ZF comes from the ADD, so the element is skipped when `data + offset` is
// zero — which with a null `data` (a failed MemAlloc) skips element 0 only and
// copies through null for the rest. Retail's, and reproduced rather than
// guarded: the fault is the signal. Same shape as RtVideo_ArrayRefreshAssign
// above.
// ============================================================================
void RtVideo_ArrayAdapterAssign(Adapter*& data, int32_t& size, int32_t& capacity,
                                Adapter* const& srcData, const int32_t& srcSize) {
    if (&data == &srcData) return;

    RtVideo_ArrayAdapterDtor(data, size, capacity);

    const int32_t n   = srcSize;
    const int32_t old = size;
    size = n;
    GrowTo(data, capacity, n, old, 0x38, 8);

    for (int32_t i = 0; i < size; ++i) {
        Adapter* d = data + i;
        if (d == nullptr) continue;
        ::new (static_cast<void*>(d)) Adapter(srcData[i]);   // 0x18000cc70 on raw storage
    }
}

// ============================================================================
// 0x180057340 — Collection::Array<RefreshRate>::SetSize(int n, const T& fill)
//                                                       (stride 8, align 4)
//
// Grow or shrink to `n`, filling any NEW element from `fill` and copying the
// surviving ones out of the old block. The `i` cursor starts at 0 when the
// block MOVED (everything must be copied across) and at `old` when it did not
// (the survivors are already in place) — that is the `CMOVZ EAX,ESI` at
// 0x1800573a9, and it is the only reason this reads as one loop instead of two.
//
// RefreshRate is trivially destructible, so a shrink needs no element destroy.
// ============================================================================
void RtVideo_ArrayRefreshSetSize(RefreshRate*& data, int32_t& size, int32_t& capacity,
                                 int32_t n, const RefreshRate& fill) {
    const int32_t old = size;
    if (n == old) return;

    RefreshRate* oldData = data;
    size = n;
    GrowTo(data, capacity, n, old, 8, 4);

    int32_t i = (oldData == data) ? old : 0;
    for (; i < n; ++i) {
        RefreshRate* d = data + i;
        if (d == nullptr) continue;
        const RefreshRate* s = (i < old) ? (oldData + i) : &fill;
        *d = *s;
    }

    if (oldData != data && oldData) System::MemoryAllocator::MemFree(oldData);
}

// ============================================================================
// 0x180057440 — Collection::Array<DisplayMode>::SetSize(int n, const T& fill)
//                                                       (stride 0x18, align 8)
//
// Same skeleton as the RefreshRate twin, with three differences, all of them
// consequences of DisplayMode owning a nested Array<RefreshRate> at +0x08:
//   1. the per-element copy zeroes that nested array before assigning into it
//      (0x180057518/0x18005751b), so the assignment sees an empty destination
//      rather than the old block's pointer;
//   2. when the block MOVED, the old elements are destroyed (0x180057559);
//   3. when it did NOT move and the array SHRANK, only the tail beyond `n` is
//      destroyed (0x1800575b2). Retail has both, and they are exclusive.
// ============================================================================
void RtVideo_ArrayModeSetSize(DisplayMode*& data, int32_t& size, int32_t& capacity,
                              int32_t n, const DisplayMode& fill) {
    const int32_t old = size;
    if (n == old) return;

    DisplayMode* oldData = data;
    size = n;
    GrowTo(data, capacity, n, old, 0x18, 8);

    int32_t i = (oldData == data) ? old : 0;
    for (; i < n; ++i) {
        DisplayMode* d = data + i;
        if (d == nullptr) continue;
        const DisplayMode* s = (i < old) ? (oldData + i) : &fill;

        d->width  = s->width;
        d->height = s->height;

        d->refreshData     = nullptr;
        d->refreshCount    = 0;
        d->refreshCapacity = 0;
        RtVideo_ArrayRefreshAssign(d->refreshData, d->refreshCount, d->refreshCapacity,
                                   s->refreshData, s->refreshCount);
    }

    if (oldData != data) {
        RtVideo_ArrayModeElemDestroy(oldData, old);
        if (oldData) System::MemoryAllocator::MemFree(oldData);
    } else if (old > n) {
        RtVideo_ArrayModeElemDestroy(oldData + n, old - n);
    }
}

// ============================================================================
// THE LIFECYCLE MEMBERS (ENDGAME B5 DEV-B, B5_RULES D6). Until B5 these six
// bodies were the `extern "C"` doors RtVideo_{GraphicsInterface,Adapter}_{Ctor,
// CopyCtor,Dtor}; they are the classes' constructors, copy constructors and
// destructors now, statement for statement.
//
// THE `name` MEMBER IS RETAIL'S INLINED STRING CODE. Compared instruction by
// instruction with the door helpers they replace:
//   * default ctor  — the door stored `name = nullptr`; System::String()
//     (0x180006760, string.cpp) is `_data = nullptr` and nothing else.
//   * copy ctor     — the door's `StringShare` nulled the destination, then, if
//     the source was non-null, adopted it and incremented [-4];
//     String(const String&) (0x180006cb0) is `_data(nullptr)` + `Adopt`, the
//     same two steps.
//   * dtor          — the door's "spelling A" `StringRelease` (0x18000cc3a /
//     0x18000cde0) is byte-for-byte ~String (0x180006c80) = String::Release:
//     null test, decrement [-4], on zero leave the slot at base-8 and MemFree
//     it, else null the slot.
// C++ runs the member's destructor AFTER the class destructor's body, and `name`
// is the only member with one, so "arrays first, then the name" — retail's order
// in both ~Adapter and ~GraphicsInterface — is what the language does.
// ============================================================================

// ============================================================================
// ??0GraphicsInterface@Device@Video@@QEAA@XZ      retail 0x18000cd90
//
// Zeroes exactly three qwords — name, adapterData, and the {count, capacity}
// pair as one store (written as the two named fields) — and returns `this`.
// That is the whole body.
// ============================================================================
Video::Device::GraphicsInterface::GraphicsInterface()
    : name(),                 // +0x00
      adapterData(nullptr),   // +0x08
      adapterCount(0),        // +0x10 \ one qword
      adapterCapacity(0) {    // +0x14 /
}

// ============================================================================
// ??0GraphicsInterface@Device@Video@@QEAA@AEBU012@@Z   retail 0x18000ce10
// ============================================================================
Video::Device::GraphicsInterface::GraphicsInterface(const GraphicsInterface& other)
    : name(other.name),       // the inlined share (0x18000ce29)
      // Zero the Array<Adapter> before assigning into it: the assignment starts
      // by destroying its destination, and an uninitialised destination would be
      // destroyed through garbage. Retail's, at 0x18000ce3e/0x18000ce41.
      adapterData(nullptr),
      adapterCount(0),
      adapterCapacity(0) {
    RtVideo_ArrayAdapterAssign(adapterData, adapterCount, adapterCapacity,
                               other.adapterData, other.adapterCount);
}

// ============================================================================
// ??1GraphicsInterface@Device@Video@@QEAA@XZ      retail 0x18000cdc0
//
// Array<Adapter>::~Array on +0x08, then the String at +0x00 (the member's
// destructor, after this body). In that order.
// ============================================================================
Video::Device::GraphicsInterface::~GraphicsInterface() {
    RtVideo_ArrayAdapterDtor(adapterData, adapterCount, adapterCapacity);
}   // ~String on +0x00 (0x18000cde0)

// ============================================================================
// ??0Adapter@Device@Video@@QEAA@XZ                retail 0x1800f7100
//
// Zeroes +0x00..+0x27 (five qwords), stores -1 as a QWORD over +0x28..+0x2f and
// -1 as a DWORD at +0x30, returns `this`. +0x34 is NOT written — the ctor
// leaves it, so this port leaves it too (`pad34` has no initialiser). The
// qword stores are written as the named fields they cover, in address order.
//
// This is the body the brief asked to be confirmed as not-a-thunk. It is not;
// see the file banner.
// ============================================================================
Video::Device::Adapter::Adapter()
    : name(),                    // +0x00
      modeData(nullptr),         // +0x08
      modeCount(0),              // +0x10
      modeCapacity(0),           // +0x14
      multisampleData(nullptr),  // +0x18
      multisampleCount(0),       // +0x20
      multisampleCap(0),         // +0x24
      maxFilterLevel(-1),        // +0x28 \ one qword -1
      dxgiAdapterIndex(-1),      // +0x2c /
      dxgiOutputIndex(-1) {      // +0x30
}

// ============================================================================
// ??0Adapter@Device@Video@@QEAA@AEBU012@@Z        retail 0x18000cc70
//
// String share, then the two owned arrays (each zeroed then assigned), then
// three plain DWORDs. +0x34 is not copied, matching the ctor. The multisample
// triple is zeroed in the BODY, after the mode assign, because that is where
// retail zeroes it.
// ============================================================================
Video::Device::Adapter::Adapter(const Adapter& other)
    : name(other.name),          // the inlined share (0x18000cc95)
      modeData(nullptr),
      modeCount(0),
      modeCapacity(0) {
    RtVideo_ArrayModeAssign(modeData, modeCount, modeCapacity,
                            other.modeData, other.modeCount);

    multisampleData  = nullptr;
    multisampleCount = 0;
    multisampleCap   = 0;
    RtVideo_ArrayBoolAssign(multisampleData, multisampleCount, multisampleCap,
                            other.multisampleData, other.multisampleCount);

    maxFilterLevel   = other.maxFilterLevel;   // +0x28
    dxgiAdapterIndex = other.dxgiAdapterIndex; // +0x2c
    dxgiOutputIndex  = other.dxgiOutputIndex;  // +0x30
}

// ============================================================================
// ??1Adapter@Device@Video@@QEAA@XZ                retail 0x18000cc10
//
// Destruction order is +0x18 (msaa), then +0x08 (modes), then +0x00 (name, the
// member's destructor after this body) — the REVERSE of construction, and the
// reverse of the field order. Read from 0x18000cc26/0x18000cc30/0x18000cc3a.
// ============================================================================
Video::Device::Adapter::~Adapter() {
    RtVideo_ArrayBoolDtor(multisampleData, multisampleCount, multisampleCap);
    RtVideo_ArrayModeDtor(modeData, modeCount, modeCapacity);
}   // ~String on +0x00 (0x18000cc3a)

// ============================================================================
// ?FindOrAddMode@Adapter@Device@Video@@QEAAXAEBVDisplayMode@23@AEBVRefreshRate@23@@Z
//                                                 retail 0x1800f7020
//
// Idempotent insert into the adapter's mode list, then into that mode's refresh
// list. Two linear searches, each followed by a SetSize(count + 1, value) when
// the search missed.
//
// TWO THINGS THE DISASSEMBLY SETTLES AND THE DECOMPILED C WOULD NOT:
//
//  1. Both SetSize calls take THREE arguments. `R8` still holds the incoming
//     `mode` pointer at 0x1800f7088 (the search loop uses R9/R10/R11 and never
//     touches R8), so 0x180057440 is called as (array, count+1, mode) — the new
//     element is FILLED with the mode, not default-constructed. A two-argument
//     reading would grow the array and leave the new mode's width and height as
//     whatever the allocator returned, and the mode would then never match on
//     any later call. That is the "drops whole arguments" defect class.
//
//  2. The element pointer is recomputed from `*R14` AFTER the SetSize
//     (0x1800f7094), not carried across it. It has to be: SetSize may
//     reallocate.
//
// The `TEST EDI,EDI / JNS` at 0x1800f7075 is retail checking a loop counter
// that started at 0 and only increments, so it always takes the jump. Kept as
// the unconditional it provably is, rather than transcribed as a live branch.
// ============================================================================
void Video::Device::Adapter::FindOrAddMode(const DisplayMode& mode,
                                           const RefreshRate& rate) {
    int32_t idx = 0;
    bool found = false;
    const int64_t n = modeCount;
    if (n > 0) {
        const DisplayMode* p = modeData;
        for (int64_t i = 0; i < n; ++i, ++idx, ++p) {
            if (p->width == mode.width && p->height == mode.height) { found = true; break; }
        }
    }
    if (!found) {
        idx = modeCount;
        RtVideo_ArrayModeSetSize(modeData, modeCount, modeCapacity, modeCount + 1, mode);
    }

    DisplayMode* m = modeData + idx;   // reloaded: SetSize may have moved it
    bool rfound = false;
    const int64_t rn = m->refreshCount;
    if (rn > 0) {
        const RefreshRate* q = m->refreshData;
        for (int64_t i = 0; i < rn; ++i, ++q) {
            if (q->numerator == rate.numerator &&
                q->denominator == rate.denominator) { rfound = true; break; }
        }
    }
    if (!rfound) {
        RtVideo_ArrayRefreshSetSize(m->refreshData, m->refreshCount, m->refreshCapacity,
                                    m->refreshCount + 1, rate);
    }
}

// ============================================================================
// ?GetDisplayMode@Device@Video@@QEBA?AVDisplayMode@12@H@Z   retail 0x18000d3e0
//
// Returns a DisplayMode BY VALUE, which on this ABI means a HIDDEN RETURN SLOT
// in RDX and the index in R8. A typedef that omits the hidden slot writes the
// return value through whatever was in RDX and still hands the caller a
// plausible answer, which is a wild write that verifies green.
//
// The returned mode is a DEEP copy: the embedded Array<RefreshRate> is zeroed
// and then assigned, so the caller owns its own refresh list and must destroy
// it. Retail, 0x18000d41b-0x18000d426.
// ============================================================================
// ENDGAME B.3.5 block D2, RULE B5-12: the MEMBER returns the `DisplayMode` BY
// VALUE, which is what retail's `?AVDisplayMode@12@` mangling says, and GCC's
// Microsoft-ABI lowering gives it the same hidden slot in RDX and the same RAX
// return (`MOV RAX,RDI` at 0x18000d42e).
Video::Device::DisplayMode Video::Device::GetDisplayMode(int index) const {
    const DisplayMode* m = modeData + index;

    DisplayMode out;
    out.width  = m->width;
    out.height = m->height;
    out.refreshData     = nullptr;
    out.refreshCount    = 0;
    out.refreshCapacity = 0;
    RtVideo_ArrayRefreshAssign(out.refreshData, out.refreshCount, out.refreshCapacity,
                               m->refreshData, m->refreshCount);
    return out;
}

// `Video::Device::AdjustDisplay` is defined in device_adjustdisplay.cpp
// (B.4 R-VIDEO: the member itself; the `RtVideo_Device_AdjustDisplay(void*)`
// C body and this one-line forward are gone).

