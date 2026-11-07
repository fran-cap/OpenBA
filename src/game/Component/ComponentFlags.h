#pragma once
// ============================================================================
// src/game/Component/ComponentFlags.h — the component capability bits and the
// entity word built from them
// ============================================================================
//
// Component::_flags (+0x20) says which of a component's lifecycle slots the
// entity walks call; ComponentEntity::_entityFlags (+0x40) is the OR of every
// component's low word (RefreshEntityFlags 0x1400c3470) plus the entity-only
// removal bit. Each walk tests the entity bit first, then the component bit of
// the slot it calls. No retail name: the profile build carries no reflection
// table for these words, so the names are the slots the bits gate.
//
// Only the bits a reader establishes are named. Leaf constructors also set 0x20
// and 0x40 (OrFlagsInCtor) and FlagForRemoval 0x140107260 sets 0x100000 in the
// component word; no ComponentEntity walk reads them.
//
// Standalone (<cstdint> and EnumFlags.h only) so tick_list.h and its unit test
// can use it without the entity headers. Component.h includes it.
// ============================================================================

#include <cstdint>
#include "system/EnumFlags.h"

enum class ComponentFlags : uint32_t {
    OnAdded          = 0x001,   // slot 6 OnAdded: ComponentEntity::OnAddedPass 0x1400c2f60
    OnRemove         = 0x002,   // slot 7 OnRemove: ComponentEntity::MarkForRemoval 0x1400c2ea0
    Tick             = 0x004,   // slot 8 Tick: TickComponents 0x1400c2de0; a change re-buckets the
                                //   entity in EntityList::stagingUpdate (RefreshEntityFlags)
    Update           = 0x008,   // slot 9 Update: EntityList::Update's timed bucket walk (0x1400c4090)
    PostUpdate       = 0x010,   // slot 10 PostUpdate: PostUpdateComponents 0x1400c2c70; a change
                                //   re-buckets the entity in stagingPostUpdate (RefreshEntityFlags)
    Startup          = 0x080,   // slot 4 Startup: StartupComponents 0x1400c30a0; the Component
                                //   ctor 0x1401188b0 sets it on every component
    Shutdown         = 0x100,   // slot 5 (Slot5): ShutdownComponents 0x1400c3000
    MarkedForRemoval = 0x400,   // entity word only: MarkForRemoval ORs it in; every walk skips the
                                //   entity, or stops its component loop, once it is set
};
OPENBA_ENUM_FLAGS(ComponentFlags)
