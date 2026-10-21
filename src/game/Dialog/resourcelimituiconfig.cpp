// src/game/Dialog/resourcelimituiconfig.cpp
// Clean reimplementation of ResourceLimitUIConfig — the per-row config behind each
// Town Hall resource-limit spinner.
//
// ResourceLimitUIConfig (0x28 bytes) — extends UI::DialogControllerConfig.
// Vtable at 0x14015d250.
//
//   +0x20  int  RESOURCE INDEX for this row:
//               Log=0 Stone=1 Iron=2 Herbs=3 Tools=4 Fuel=5 Food=6 Clothing=7
//               Alcohol=8 Coal=9 Textile=10, then the custom limits Custom0..Custom9 = 11..20
//               ->  townMgr(gw+0xf0)+0x170[idx*0x20+8]
//
// Factory       0x140013780: MemAlloc(0x28) + base ctor + vtable + zero +0x20 (4 bytes)
// CreateInPlace 0x1400137e0
// Serialize     0x140077b10 (vtable idx5, Ghidra "ResourceLimitUI_vslot17" — the name
//               lies, it is a ResourceLimitUIConfig method):
//                   IO::Serializer(stream, 0)   slot 0x140152ad8
//                   Serialize(int&, cfg+0x20)
//                   ~Serializer                 slot 0x140152ad0
//                   return 1
//               Retail INLINED the int serialize into the stream's own virtual call
//               (`MOV RCX,[RSP+0x30]; CALL [RAX+0x98]` — serializer+8 is the stream
//               impl, 0x98/8 = vtable idx 19). Dispatch slot 0x140152ac8 IS that
//               non-inlined helper; ControlUIConfig's Serialize (0x14005fb70) calls it
//               directly and was verified live this session, so we use the slot.
//
// The ResourceLimitUI controller reads this index (idx5 below fills it from the .rsc) as retail's
// Slot4 / Slot7 / Slot9 do: IsDerivedFrom(config) ? config : null, then +0x20 unguarded, no bound.
// (An earlier workaround derived the index from the dispatch order while this class was a stub;
// it is gone.)
//
// Decompiled from Application-x64.exe.

#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>

#include "object/Internal.h"
#include "Dialog/ResourceLimitUIConfig.h"

// ============================================================================
// ENDGAME B.3.8 PNL — ResourceLimitUIConfig as a REAL class (ResourceLimitUIConfig.h). The hand-built
// 9-slot table, its Init/Factory/CreateInPlace entry points and the lazy
// GetTypeId cache are gone; the compiler emits the table.
// ============================================================================

// [4] not declared: retail's slot is the imported External::Compile, which
// the class inherits.

// [5] 0x140077b10 — read this row's resource index.
bool ResourceLimitUIConfig::Serialize(IO::Stream& stream) {
    IO::Serializer serializer(stream, 0);
    serializer.Serialize(_resourceIndex, nullptr);
    return true;
}

// [6] 0x140013830 — destroy in place, construct again.
Object::External* ResourceLimitUIConfig::Reconstruct() {
    this->~ResourceLimitUIConfig();
    return new (this) ResourceLimitUIConfig();
}

// [7] 0x140001640, the body shared by 119 exe tables:
// `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]` = destroy in place, no free.
void ResourceLimitUIConfig::Destruct() { this->~ResourceLimitUIConfig(); }

// [8] 0x1400137e0 — placement construction (MSVC folded it with createInPlace).
Object::External* ResourceLimitUIConfig::Construct() { return new (this) ResourceLimitUIConfig(); }

// create 0x140013780: MemAlloc(0x28, 8), then the ctor.
::Object::Base* ResourceLimitUIConfig::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(ResourceLimitUIConfig), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) ResourceLimitUIConfig());
}
// createInPlace 0x1400137e0.
::Object::Base* ResourceLimitUIConfig::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) ResourceLimitUIConfig());
}


DEFINE_EXE_TYPE(ResourceLimitUIConfig, "UI::DialogControllerConfig")
