#pragma once
// ============================================================================
// src/game/Component/TrackerComponent.h — ENDGAME B.3.8 block VIS
//
// `TrackerComponent` as a real C++ class with a compiler-generated vtable
// (B38_RULES.md D1-D4).
//
// RETAIL (Application-x64.exe, B38_SLOTS.md §6):
//   reflected "TrackerComponent", parent "Component", ti 0x1401c4b10
//   create 0x14000d7a0: MemAlloc(0x50, 8) + the INLINED ctor: Component ctor,
//                       stamp 0x14015ab80, +0x28/+0x30/+0x38/+0x40 = 0,
//                       +0x48 = -1 (no flag OR: flags stay Component's 0x80)
//   dtor   0x140059280: MemFree(+0x38), +0x38 = +0x40 = 0, MemFree(+0x28),
//                       +0x28 = +0x30 = 0, restamp, ~Internal
//   table  0x14015ab80, 16 slots:
//     [0] 0x14000d9a0 scalar deleting dtor   [1]/[2] TypeName/Type
//     [3] 0x140059130 Persist                every other slot = Component's
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Component/Component.h"
#include "Object/exe_type.h"
#include "collection/Collection.h"   // Collection::Array (the two Entry lists)

namespace rtcoll { extern const ArraySpec kSpec_Fill16; }   // stride 0x10, align 8, no destroy (src/engine/collection/array.cpp)

class ComponentDescription;   // Component/ComponentDescription.h

class TrackerComponent final : public Component {
    DECLARE_EXE_TYPE(TrackerComponent)

public:
    TrackerComponent();                                           // inlined in 0x14000d7a0

    // Retail 0x140059280: frees +0x38 (produced) then +0x28 (displayed), each
    // MemFree then its data / count+capacity qwords zeroed, then ~Component. That is
    // the two Array members' destructors in reverse declaration order.
    ~TrackerComponent() override = default;                       // [0] 0x14000d9a0
    void Persist(IO::Stream& stream) override;                          // [3] 0x140059130
    void Upsert(ComponentDescription* material, int value);       // 0x140058fd0

    // PUBLIC while the ported bodies read them (B38 decision 1).
    // One tracked resource, 0x10 bytes (Persist 0x140059130: ExternalPtr 0x140002340, int).
    struct Entry {
        ComponentDescription* _resource;   // +0x00 ExternalPtr<ComponentDescription>
        int32_t _count;                    // +0x08
        uint8_t _pad0C[4];                 // +0x0C
    };

    // Array<Entry> (H5, typed: the ctor inlined in 0x14000d7a0 zeroes +0x28 / +0x30 /
    // +0x38 / +0x40 as qwords, the dtor 0x140059280 frees +0x38 then +0x28). No element
    // destroy (the ExternalPtr is held bare); resized and copied by the 16-byte core
    // 0x14000da70 / 0x14000d9d0 (Action/exe_bodies.h), so the spec row only serves ~Array.
    using EntryList = Collection::Array<Entry, Collection::DefaultAllocator,
                                        Collection::ExplicitSpec<&rtcoll::kSpec_Fill16>>;
    // ProductionUI 0x140075a50 moves the produced list into the displayed one when the
    // world's generation changes.
    EntryList displayed;          // +0x28 the list the production panel shows
    EntryList produced;           // +0x38 the list the producers upsert into
    int32_t  trackerId;    // +0x48 game-world item-count generation stamp (ctor -1)
    uint8_t  _pad4C[4];    // +0x4C
};

static_assert(sizeof(TrackerComponent) == 0x50, "TrackerComponent is 0x50 (create 0x14000d7a0)");
static_assert(offsetof(TrackerComponent, _pad4C) == 0x4C, "TrackerComponent::_pad4C");
static_assert(alignof(TrackerComponent) == 8, "TrackerComponent alignment");
static_assert(sizeof(TrackerComponent::Entry) == 0x10, "tracker entry stride 0x10 (0x14000da70)");
static_assert(offsetof(TrackerComponent::Entry, _resource) == 0x0, "Entry::_resource");
static_assert(offsetof(TrackerComponent::Entry, _count) == 0x8, "Entry::_count");
static_assert(offsetof(TrackerComponent::Entry, _pad0C) == 0xC, "Entry::_pad0C");
static_assert(offsetof(TrackerComponent, displayed) == 0x28, "");
static_assert(offsetof(TrackerComponent, displayed._size) == 0x30, "");
static_assert(offsetof(TrackerComponent, displayed._capacity) == 0x34, "");
static_assert(offsetof(TrackerComponent, produced) == 0x38, "");
static_assert(offsetof(TrackerComponent, produced._size) == 0x40, "");
static_assert(offsetof(TrackerComponent, produced._capacity) == 0x44, "");
static_assert(offsetof(TrackerComponent, trackerId) == 0x48, "");
