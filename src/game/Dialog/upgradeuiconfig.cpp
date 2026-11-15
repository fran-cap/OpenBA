// src/game/Dialog/upgradeuiconfig.cpp
// Clean reimplementation of UpgradeUIConfig (the upgrade panel's config resource).
//
// UpgradeUIConfig (0x28 bytes) — extends UI::DialogControllerConfig (byte-wise
// just Object::External's 0x20 base; the subclass adds one field).
// Registered by retail 0x14014ca00 into type-id slot DWORD_1401c5868.
// Vtable at 0x14015df60. NO "UpgradeDescription" class exists in retail — this
// config IS the upgrade definition:
//
//   +0x20  Object::ExternalPtr<ComponentDescription> — the upgrade TARGET BUILDING
//          template (e.g. the stone house). UpgradeUI's click handler
//          (0x140086c90) copies it into DestroyComponent+0x60 before pumping
//          the state machine; state-5 tick 0x14011c120 clones it via
//          ComponentEntity_CopyCtor to spawn the replacement construction site.
//
// Factory        0x140016c00: MemAlloc(0x28) + base ctor + vtable + clear +0x20
// CreateInPlace  0x140016c60: same, no alloc
// Serialize      0x140086fb0 (vtable +0x28): IO::Serializer(stream, 0) +
//                ExternalPtr<ComponentDescription>::Serialize 0x140002340 on +0x20
//                (load: FindOrLoad, kept only if IsDerivedFrom the cell 0x1401c7318)
//                — ported as Object::SerializeExternal<ComponentDescription>.
// GetVersion     (vtable +0x18, 0x140087000): return 1.
// GetTypeId      (vtable +0x10, 0x140016be0): return DWORD_1401c5868.
//
// Replaces the UI_DIALOG_STUB(UpgradeUIConfig, ...) zero-fill stub, whose +0x20
// stayed null — the upgrade button could never resolve a target.
//
// Decompiled from Application-x64.exe.

#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/TypeInfo.h"
#include "Component/ComponentDescription.h"   // the typed ExternalPtr slot (_target)
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "io/Serializer.h"   // ENDGAME B.3.4 caller batch D
#include <new>

#include "object/Internal.h"
#include "Dialog/UpgradeUIConfig.h"
#include "Component/ComponentDescription.h"   // ComponentDescription::TypeId() (cell 0x1401c7318)

// ============================================================================
// ENDGAME B.3.8 PNL — UpgradeUIConfig as a REAL class (UpgradeUIConfig.h). The hand-built
// 9-slot table, its Init/Factory/CreateInPlace entry points and the lazy
// GetTypeId cache are gone; the compiler emits the table.
// ============================================================================

// [4] not declared: retail's slot is the imported External::Compile, which
// the class inherits.

// [5] 0x140086fb0 — deserialize the target-template reference
// (ExternalPtr<ComponentDescription> 0x140002340, Object::SerializeExternal<ComponentDescription>).
bool UpgradeUIConfig::Serialize(IO::Stream& stream) {
    IO::Serializer serializer(stream, 0);
    // 0x140002340: ExternalPtr<ComponentDescription>::Serialize (cell 0x1401c7318; B8 RT2).
    ::Object::SerializeExternal<ComponentDescription>(serializer, _target);
    return true;
}

// [6] 0x140016cb0 — destroy in place, construct again.
Object::External* UpgradeUIConfig::Reconstruct() {
    this->~UpgradeUIConfig();
    return new (this) UpgradeUIConfig();
}

// [7] 0x140001640, the body shared by 119 exe tables:
// `MOV RAX,[RCX]; XOR EDX,EDX; JMP [RAX]` = destroy in place, no free.
void UpgradeUIConfig::Destruct() { this->~UpgradeUIConfig(); }

// [8] 0x140016c60 — placement construction (MSVC folded it with createInPlace).
Object::External* UpgradeUIConfig::Construct() { return new (this) UpgradeUIConfig(); }

// create 0x140016c00: MemAlloc(0x28, 8), then the ctor.
::Object::Base* UpgradeUIConfig::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(UpgradeUIConfig), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) UpgradeUIConfig());
}
// createInPlace 0x140016c60.
::Object::Base* UpgradeUIConfig::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) UpgradeUIConfig());
}


DEFINE_EXE_TYPE(UpgradeUIConfig, "UI::DialogControllerConfig")
