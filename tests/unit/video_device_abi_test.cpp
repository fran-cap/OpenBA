// ============================================================================
// tests/unit/video_device_abi_test.cpp — ENDGAME B.3.5 block D2.
//
// The ABI claims block D2 makes, and nothing else. Model:
// `tests/unit/video_data_abi_test.cpp` (block A) and `io_vtable_abi_test.cpp`.
//
// WHAT THIS FILE HAS TO ESTABLISH
// -------------------------------
// Block D2 declares nine real C++ classes over layouts the rest of the tree —
// and `src/render/dx11/`, and `src/app/video_interface.cpp`, and
// `src/game/Dialog/optionsdialog.cpp` — still reach by RAW BYTE OFFSET:
//
//   Video::Device                      0x108, lock at +0x60, dllSlots at +0xb0,
//                                      deviceData at +0x100
//   Video::Device::{RefreshRate, DisplayMode, Adapter, GraphicsInterface,
//                   InterfaceSlot}     8 / 0x18 / 0x38 / 0x18 / 0x18 strides
//   Video::Configuration               0x44 INFERRED — see PART B
//   Video::RenderBuffer                0x30
//   Video::{Index,Vertex}Buffer        0x20 each
//
// None of them is polymorphic, so the whole risk is the opposite of block A's:
// not "does GCC put slot N at N*8", but "did giving these PODs member functions,
// nested classes and a namespace change one single byte of their layout". That is
// what PART A and PART C assert, and PART C asserts it in the form that would
// actually break — a vptr appearing at +0x00 and shifting every field by eight.
//
// WHY NO TREE SOURCE LINKS. Every ported body in this block stands on something a
// host test cannot have: `src/engine/video/device.cpp` on `RT_DATA_SLOT` and
// `IO::Persistence`, `device_startup.cpp` on the exe's `ResolveAddr` and the
// Application singleton, `video_interfaces_win.cpp` and the three `_win` twins on
// `<windows.h>`, `buffers.cpp` and `dynamic_resources.cpp` on the port registry
// and dx11's live vtables. So the real classes' LAYOUT is checked directly
// (sizes and offsets are Device.h's own static_asserts; what is here is the
// relations the header leaves unsaid) and the hidden-return-slot door shape
// the block relies on is checked on a class declared here in the same shape.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "unit.h"

#include <cstdlib>

#include "video/Device.h"
#include "video/Buffers.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"

namespace {

using Dev   = Video::Device;
using Cfg   = Video::Configuration;
using RB    = Video::RenderBuffer;
using IB    = Video::IndexBuffer;
using VB    = Video::VertexBuffer;

// Raw byte access, which is exactly how dx11, src/app/video_interface.cpp and
// optionsdialog.cpp reach these objects.
template <typename T>
T& At(void* base, size_t off) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

}  // namespace

// ===========================================================================
// PART A — layout facts the headers do not static_assert (the byte-for-byte
// sizes and offsets are Device.h's own asserts).
// ===========================================================================

UNIT_TEST(video_device_layout) {
    CHECK_EQ(alignof(Dev), 8u);
}

UNIT_TEST(video_renderbuffer_and_the_two_buffers_layout) {
    // Layout-identical, which is what lets dynamic_resources.cpp drive both
    // through one template.
    CHECK_EQ(offsetof(VB, size), offsetof(IB, size));
    CHECK_EQ(offsetof(VB, dynamic), offsetof(IB, dynamic));
    CHECK_EQ(offsetof(VB, platformData), offsetof(IB, platformData));
    CHECK_EQ(offsetof(VB, listPrev), offsetof(IB, listPrev));
    CHECK_EQ(offsetof(VB, listNext), offsetof(IB, listNext));
}

// ===========================================================================
// PART B — Video::Configuration, and the ONE size in this block that is NOT
// measured.
//
// B35_RULES §13 item 1 asked block D2 to close the `sizeof == 0x44` gap from the
// retail DLL. It could not be closed, and this test states the bound that IS
// established rather than a number that is not:
//   * the constructor's last store is `MOV byte ptr [RCX+0x42],0` at 0x1800f6f64;
//   * no body in the DLL (ctor, Save, ValidateConfiguration,
//     ApplyChangedSettings) touches a byte above +0x42;
//   * there is NO allocation site — the object is a static reached only through
//     an indirect call, so no size immediate and no next-symbol bound exist.
// So 0x43 <= sizeof <= 0x44, and asserting == 0x44 would be a static_assert that
// cannot fail for the right reason.
// ===========================================================================

UNIT_TEST(video_configuration_size_is_bounded_not_measured) {
    CHECK(sizeof(Cfg) >= 0x43u);
    CHECK(sizeof(Cfg) <= 0x44u);
    CHECK_EQ(alignof(Cfg), 4u);
    // The ctor's last store, which is the only hard fact about the tail.
}

// ===========================================================================
// PART C — RULE B5-6, and the failure mode that would actually happen.
//
// The classes gained member functions, five nested classes and a namespace. The
// one thing that would silently destroy every raw reader in the tree is a VPTR
// appearing at +0x00. `sizeof` alone would catch it for `Device` (0x108 -> 0x110)
// but NOT for a class whose tail padding could absorb it, so this is asserted
// three ways: standard-layout, trivially-copyable, and a direct byte read of the
// first field through a `uint8_t*`.
// ===========================================================================

UNIT_TEST(video_device_gained_no_vptr_and_stays_a_raw_readable_object) {
    CHECK(std::is_standard_layout<Dev>::value);
    CHECK(std::is_trivially_copyable<Dev>::value);
    CHECK(std::is_standard_layout<Cfg>::value);
    CHECK(std::is_trivially_copyable<Cfg>::value);
    CHECK(std::is_standard_layout<RB>::value);
    CHECK(std::is_trivially_copyable<RB>::value);
    CHECK(std::is_standard_layout<IB>::value);
    // B.4 wave Y: VB / IB have retail's ??1 (PlatformDestruct) now: no vptr,
    // still standard-layout, no longer trivially destructible.
    CHECK(std::is_standard_layout<VB>::value);
    CHECK(!std::is_trivially_destructible<VB>::value);

    // Three nested value types too — Collection::Array bulk-copies them.
    CHECK(std::is_trivially_copyable<Dev::RefreshRate>::value);
    CHECK(std::is_trivially_copyable<Dev::DisplayMode>::value);
    CHECK(std::is_trivially_copyable<Dev::InterfaceSlot>::value);
    // ENDGAME B5 DEV-B: Adapter and GraphicsInterface carry retail's ctor / copy
    // ctor / dtor as members and a System::String name, so they are NOT trivially
    // copyable any more — their arrays copy-construct and destroy per element
    // (PART F below runs the real members).
    CHECK(!std::is_trivially_copyable<Dev::Adapter>::value);
    CHECK(!std::is_trivially_copyable<Dev::GraphicsInterface>::value);

    alignas(8) uint8_t raw[sizeof(Dev)];
    std::memset(raw, 0, sizeof(raw));
    Dev* d = new (raw) Dev;

    // Write through the CLASS, read through raw bytes — the direction dx11 and
    // src/app/video_interface.cpp use.
    d->type             = 0x11223344;
    d->interfaceCount   = 7;
    d->activeInterface  = 1;
    d->deviceData       = reinterpret_cast<Video::DeviceData*>(static_cast<uintptr_t>(0xDEADBEEFu));
    CHECK_EQ(At<int32_t>(raw, 0x00), 0x11223344);
    CHECK_EQ(At<int32_t>(raw, 0x48), 7);
    CHECK_EQ(At<int32_t>(raw, 0xF8), 1);
    CHECK_EQ(At<uintptr_t>(raw, 0x100), static_cast<uintptr_t>(0xDEADBEEFu));

    // And the other direction: write raw, read through the class.
    At<int32_t>(raw, 0x98) = 16;                       // maxAnisotropy
    CHECK_EQ(d->maxAnisotropy, 16);
    At<int32_t>(raw, 0x04) = 1920;
    At<int32_t>(raw, 0x08) = 1080;
    CHECK_EQ(d->backBufferWidth, 1920);
    CHECK_EQ(d->backBufferHeight, 1080);
}

// RULE B5-6 in its own right: the CRITICAL_SECTION stays an opaque 0x28-byte
// block with the owner-tid and depth words at PINNED offsets, because dx11 and
// the four `Platform{Construct,Destruct}` bodies read them there. A `std::mutex`
// or a scoped guard would move or resize this.
// Not analysed: it reads the lock's depth word (GUARDED_BY the lock) on a Device that is raw bytes.
UNIT_TEST(video_device_lock_is_opaque_and_pinned_rule_b5_6) NO_THREAD_SAFETY_ANALYSIS {
    // The two bookkeeping words sit immediately after the block, with no
    // padding: 0x60 + 0x28 == 0x88.
    CHECK_EQ(offsetof(Dev, lock) + sizeof(Dev::lock), offsetof(Dev, lockOwnerThreadId));
    // Writing the depth through raw bytes is visible as the member, which is
    // what buffers_lock.cpp's hand-rolled recursive acquire does.
    alignas(8) uint8_t raw[sizeof(Dev)];
    std::memset(raw, 0, sizeof(raw));
    Dev* d = new (raw) Dev;
    At<int32_t>(raw, 0x8C) = 3;
    CHECK_EQ(d->lockRecursionCount, 3);
}

// ===========================================================================
// PART D — ONE DEFINITION, NOT TWO.
//
// The old PODs were field-identical to the retail classes, so block D2 made the
// old spellings ALIASES instead of writing a second layout. That is the claim
// that keeps four other blocks' files compiling untouched, and it is the claim a
// later "restore the POD" edit would break silently — a duplicate would still
// compile and still pass every offset test above.
// ===========================================================================

UNIT_TEST(video_device_pod_spellings_are_the_same_type_not_a_copy) {
    CHECK((std::is_same<RtVideo::Device, Video::Device>::value));
    CHECK((std::is_same<RtVideo::Configuration, Video::Configuration>::value));
    CHECK((std::is_same<RtVideo::RenderBuffer, Video::RenderBuffer>::value));
    CHECK((std::is_same<RtVideo::RefreshRate, Video::Device::RefreshRate>::value));
    CHECK((std::is_same<RtVideo::DisplayMode, Video::Device::DisplayMode>::value));
    CHECK((std::is_same<RtVideo::Adapter, Video::Device::Adapter>::value));
    CHECK((std::is_same<RtVideo::GraphicsInterface, Video::Device::GraphicsInterface>::value));
    CHECK((std::is_same<RtVideo::InterfaceSlot, Video::Device::InterfaceSlot>::value));
    // ENDGAME B5 DEV-B: the name is the real String member, not its data pointer.
    CHECK((std::is_same<decltype(Video::Device::Adapter::name), System::String>::value));
    CHECK((std::is_same<decltype(Video::Device::GraphicsInterface::name), System::String>::value));
    // Buffers.h's old GLOBAL-scope spelling (B4b-1: it occupied no namespace at
    // all) and the RtVideo one.
    CHECK((std::is_same<::IndexBuffer, Video::IndexBuffer>::value));
    CHECK((std::is_same<::VertexBuffer, Video::VertexBuffer>::value));
    CHECK((std::is_same<RtVideo::IndexBuffer, Video::IndexBuffer>::value));
    CHECK((std::is_same<RtVideo::VertexBuffer, Video::VertexBuffer>::value));
}

// ===========================================================================
// E2: THE HIDDEN-RETURN-SLOT DOOR, RULE B5-12 as amended.
//
// `Device::GetDisplayMode` and `Device::GetDLLForInterface` return a class BY
// VALUE. GCC's ms_abi and MSVC do NOT agree on where the hidden pointer goes
// relative to `this`, so the amendment's rule is: the MEMBER may return by value
// only because the REGISTERED DOOR keeps retail's explicit `(this, ret, args)`
// signature, placement-constructs the member's prvalue into the caller's slot and
// returns that slot pointer. Both of this block's retslot doors are written that
// way; this test proves the shape does what it claims:
//   * the caller's slot is never READ (it arrives uninitialised, and C++17
//     guarantees the prvalue is built in place rather than copied in);
//   * the door returns exactly the slot it was handed, which is retail's
//     `MOV RAX,RDI`.
// ===========================================================================

namespace {

struct MirrorByValue {
    int32_t x;
    int32_t y;
    void*   owned;
};

int g_mirrorCtorCount = 0;

// The MEMBER: returns by value, deep-copying an owned pointer the way
// `Device::GetDisplayMode` deep-copies its refresh array.
MirrorByValue MirrorMake(int x, int y, void* owned) {
    ++g_mirrorCtorCount;
    MirrorByValue out;
    out.x = x;
    out.y = y;
    out.owned = owned;
    return out;
}

// The DOOR, in the block's shape.
MirrorByValue* MirrorDoor(void* ret, int x, int y, void* owned) {
    return new (ret) MirrorByValue(MirrorMake(x, y, owned));
}

}  // namespace

UNIT_TEST(video_device_retslot_door_builds_in_place_and_returns_the_slot) {
    // An UNINITIALISED slot, poisoned so a read of it would be visible.
    alignas(8) uint8_t slot[sizeof(MirrorByValue)];
    std::memset(slot, 0xCD, sizeof(slot));

    g_mirrorCtorCount = 0;
    void* owned = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234u));
    MirrorByValue* got = MirrorDoor(slot, 1920, 1080, owned);

    // Retail's MOV RAX,RDI: the door hands back the slot it was given.
    CHECK_EQ(reinterpret_cast<const void*>(got), reinterpret_cast<const void*>(slot));
    CHECK_EQ(got->x, 1920);
    CHECK_EQ(got->y, 1080);
    CHECK_EQ(reinterpret_cast<uintptr_t>(got->owned), static_cast<uintptr_t>(0x1234u));
    // Built once, in place — not built then copied.
    CHECK_EQ(g_mirrorCtorCount, 1);
    // The poison is gone from every byte the value covers, which is what "the
    // slot is written, never read" looks like from outside.
    CHECK_EQ(*reinterpret_cast<const int32_t*>(slot + 0), 1920);
    CHECK_EQ(*reinterpret_cast<const int32_t*>(slot + 4), 1080);
}

// The member's own return type is the class, by value, and the class is the same
// type the door writes — the two cannot drift.
UNIT_TEST(video_device_getdisplaymode_returns_the_nested_class_by_value) {
    CHECK((std::is_same<decltype(std::declval<const Dev&>().GetDisplayMode(0)),
                        Dev::DisplayMode>::value));
    // A `DisplayMode` is trivially copyable, so the deep copy the member makes is
    // handed back by a bitwise move of {width, height, data, count, capacity} —
    // which transfers ownership of the refresh array, as retail's does.
    CHECK(std::is_trivially_copyable<Dev::DisplayMode>::value);
}

// ===========================================================================
// PART F — ENDGAME B5 DEV-B: Adapter and GraphicsInterface's REAL lifecycle.
//
// The six bodies (retail 0x1800f7100 / 0x18000cc70 / 0x18000cc10 and
// 0x18000cd90 / 0x18000ce10 / 0x18000cdc0) are the classes' members now, in
// src/engine/video/device_enum_types.cpp, which links here (tests/unit/
// sources.cmake). The String member is the host String (string.cpp, malloc seam);
// the Array helpers allocate through System::MemoryAllocator, whose instrument
// seat is armed with a counting malloc / free for the duration of each test and
// cleared after it (allocator_seat_test.cpp expects it empty).
//
// What is checked is what the doors did and the members must still do: the
// default ctor's exact stores on POISONED storage (zeros, the three -1s, +0x34
// untouched), the copy ctor's String SHARE (same buffer, refcount + 1) and DEEP
// array copies (new blocks, equal contents, +0x34 untouched), and the dtor's
// releases (every MemAlloc matched by a MemFree; the shared-path name slot left
// null, retail's release asymmetry).
// ===========================================================================

namespace {

int g_devbAllocs = 0;
int g_devbFrees  = 0;

void* DevbAlloc(uint64_t size, int /*align*/) {
    ++g_devbAllocs;
    return std::malloc(size ? static_cast<size_t>(size) : 1u);
}
void DevbFree(void* p) {
    if (p) ++g_devbFrees;
    std::free(p);
}

struct DevbSeat {
    DevbSeat() {
        g_devbAllocs = g_devbFrees = 0;
        System::g_memoryAllocatorInstrument.memAlloc = &DevbAlloc;
        System::g_memoryAllocatorInstrument.memFree  = &DevbFree;
    }
    ~DevbSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree  = nullptr;
    }
};

int32_t RefCountOf(const System::String& s) { return RtSystem::StringRefCount(s._data); }

// Fills an Adapter the way dx11's enumeration does: a name, one mode with one
// refresh rate (through the real FindOrAddMode), a 4-entry msaa table and the
// three ints.
void FillAdapter(Dev::Adapter& a, const char* name) {
    a.name = System::String(name);
    Dev::DisplayMode m;
    m.width = 1920; m.height = 1080;
    m.refreshData = nullptr; m.refreshCount = 0; m.refreshCapacity = 0;
    Dev::RefreshRate r;
    r.numerator = 60; r.denominator = 1;
    a.FindOrAddMode(m, r);
    a.multisampleData  = static_cast<uint8_t*>(System::MemoryAllocator::MemAlloc(4, 1));
    a.multisampleCount = 4;
    a.multisampleCap   = 4;
    a.multisampleData[0] = 1; a.multisampleData[1] = 0;
    a.multisampleData[2] = 1; a.multisampleData[3] = 0;
    a.maxFilterLevel   = 16;
    a.dxgiAdapterIndex = 0;
    a.dxgiOutputIndex  = 2;
}

}  // namespace

UNIT_TEST(video_device_adapter_ctor_copy_dtor_are_retails) {
    DevbSeat seat;

    // ??0Adapter 0x1800f7100 on poisoned storage.
    alignas(8) uint8_t rawA[sizeof(Dev::Adapter)];
    std::memset(rawA, 0xCD, sizeof(rawA));
    Dev::Adapter* a = new (rawA) Dev::Adapter;
    CHECK_EQ(At<uintptr_t>(rawA, 0x00), 0u);            // name
    CHECK(a->modeData == nullptr);
    CHECK_EQ(a->modeCount, 0);
    CHECK_EQ(a->modeCapacity, 0);
    CHECK(a->multisampleData == nullptr);
    CHECK_EQ(a->multisampleCount, 0);
    CHECK_EQ(a->multisampleCap, 0);
    CHECK_EQ(a->maxFilterLevel, -1);
    CHECK_EQ(a->dxgiAdapterIndex, -1);
    CHECK_EQ(a->dxgiOutputIndex, -1);
    CHECK_EQ(At<uint32_t>(rawA, 0x34), 0xCDCDCDCDu);    // +0x34 NOT written

    FillAdapter(*a, "Radeon");
    CHECK_EQ(RefCountOf(a->name), 1);

    // ??0Adapter(const&) 0x18000cc70 on poisoned storage.
    alignas(8) uint8_t rawB[sizeof(Dev::Adapter)];
    std::memset(rawB, 0xAB, sizeof(rawB));
    Dev::Adapter* b = new (rawB) Dev::Adapter(*a);
    CHECK(b->name._data == a->name._data);              // shared, not copied
    CHECK_EQ(RefCountOf(a->name), 2);
    CHECK(b->name == "Radeon");
    CHECK(b->modeData != nullptr);
    CHECK(b->modeData != a->modeData);                  // deep
    CHECK_EQ(b->modeCount, 1);
    CHECK_EQ(b->modeData[0].width, 1920);
    CHECK_EQ(b->modeData[0].height, 1080);
    CHECK(b->modeData[0].refreshData != a->modeData[0].refreshData);   // deep, nested
    CHECK_EQ(b->modeData[0].refreshCount, 1);
    CHECK_EQ(b->modeData[0].refreshData[0].numerator, 60);
    CHECK_EQ(b->modeData[0].refreshData[0].denominator, 1);
    CHECK(b->multisampleData != a->multisampleData);    // deep
    CHECK_EQ(b->multisampleCount, 4);
    CHECK_EQ(std::memcmp(b->multisampleData, a->multisampleData, 4), 0);
    CHECK_EQ(b->maxFilterLevel, 16);
    CHECK_EQ(b->dxgiAdapterIndex, 0);
    CHECK_EQ(b->dxgiOutputIndex, 2);
    CHECK_EQ(At<uint32_t>(rawB, 0x34), 0xABABABABu);    // +0x34 NOT copied

    // ??1Adapter 0x18000cc10: msaa, modes, then the name. The copy's name is
    // still shared, so the release takes the NULLING path.
    b->~Adapter();
    CHECK_EQ(RefCountOf(a->name), 1);
    CHECK_EQ(At<uintptr_t>(rawB, 0x00), 0u);
    CHECK_EQ(At<uintptr_t>(rawB, 0x08), 0u);
    CHECK_EQ(At<uintptr_t>(rawB, 0x18), 0u);

    a->~Adapter();
    CHECK(g_devbAllocs > 0);
    CHECK_EQ(g_devbAllocs, g_devbFrees);                // every block released once
}

UNIT_TEST(video_device_graphicsinterface_ctor_copy_dtor_are_retails) {
    DevbSeat seat;

    // ??0GraphicsInterface 0x18000cd90: three zero qwords.
    alignas(8) uint8_t rawG[sizeof(Dev::GraphicsInterface)];
    std::memset(rawG, 0xCD, sizeof(rawG));
    Dev::GraphicsInterface* g = new (rawG) Dev::GraphicsInterface;
    CHECK_EQ(At<uintptr_t>(rawG, 0x00), 0u);
    CHECK_EQ(At<uintptr_t>(rawG, 0x08), 0u);
    CHECK_EQ(At<uint64_t>(rawG, 0x10), 0u);

    g->name = System::String("DirectX 11");
    {
        Dev::Adapter proto;
        FillAdapter(proto, "GeForce");
        Dev::Adapter* const protoData = &proto;
        const int32_t one = 1;
        // Array<Adapter>::operator= 0x18000fe10: copy-constructs each element.
        RtVideo_ArrayAdapterAssign(g->adapterData, g->adapterCount, g->adapterCapacity,
                                   protoData, one);
        CHECK_EQ(g->adapterCount, 1);
        CHECK(g->adapterData[0].name._data == proto.name._data);
        CHECK_EQ(RefCountOf(proto.name), 2);
    }   // ~Adapter(proto)
    CHECK_EQ(RefCountOf(g->adapterData[0].name), 1);

    // ??0GraphicsInterface(const&) 0x18000ce10.
    alignas(8) uint8_t rawH[sizeof(Dev::GraphicsInterface)];
    std::memset(rawH, 0xAB, sizeof(rawH));
    Dev::GraphicsInterface* h = new (rawH) Dev::GraphicsInterface(*g);
    CHECK(h->name._data == g->name._data);
    CHECK_EQ(RefCountOf(g->name), 2);
    CHECK_EQ(h->adapterCount, 1);
    CHECK(h->adapterData != g->adapterData);            // deep
    CHECK(h->adapterData[0].name == "GeForce");
    CHECK_EQ(RefCountOf(g->adapterData[0].name), 2);    // the adapter's name shared
    CHECK(h->adapterData[0].modeData != g->adapterData[0].modeData);
    CHECK_EQ(h->adapterData[0].modeData[0].width, 1920);
    CHECK_EQ(h->adapterData[0].maxFilterLevel, 16);

    // ??1GraphicsInterface 0x18000cdc0: the adapter array, then the name.
    h->~GraphicsInterface();
    CHECK_EQ(RefCountOf(g->name), 1);
    CHECK_EQ(RefCountOf(g->adapterData[0].name), 1);
    CHECK_EQ(At<uintptr_t>(rawH, 0x00), 0u);            // shared path: nulled
    CHECK_EQ(At<uintptr_t>(rawH, 0x08), 0u);

    // The Device-side teardown (~Device's 0x180010f00) over a one-element
    // block: the open-coded loop releases the same things the dtor does.
    Dev::GraphicsInterface* block = static_cast<Dev::GraphicsInterface*>(
        System::MemoryAllocator::MemAlloc(sizeof(Dev::GraphicsInterface), 8));
    new (block) Dev::GraphicsInterface(*g);
    CHECK_EQ(RefCountOf(g->name), 2);
    RtVideo_ArrayInterfaceElemDestroy(block, 1);
    CHECK_EQ(RefCountOf(g->name), 1);
    CHECK_EQ(RefCountOf(g->adapterData[0].name), 1);
    System::MemoryAllocator::MemFree(block);

    g->~GraphicsInterface();
    CHECK(g_devbAllocs > 0);
    CHECK_EQ(g_devbAllocs, g_devbFrees);
}
