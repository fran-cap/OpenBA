// ============================================================================
// src/engine/ui/dialog_factory.cpp — UI::Dialog's two reflection factories
//
//   ?CreateNewObject@Dialog@UI@@SAPEAVBase@Object@@XZ        retail 0x180038530
//   ?CreateInPlaceObject@Dialog@UI@@SAPEAVBase@Object@@PEAX@Z retail 0x180038610
// ============================================================================
//
// `?CreateNewObject@Dialog@UI@@` and `?CreateInPlaceObject@Dialog@UI@@` have
// **ZERO CODE XREFS** in Runtime-x64.dll. Every reference to either is DATA:
//
//   0x1801faaa8  ?_typeInfo@Dialog@UI@@ + 0x20   (factoryFn)
//   0x1801faab0  ?_typeInfo@Dialog@UI@@ + 0x28   (inPlaceFactoryFn)
//   0x1801b2a44 / 0x1801b282c   .pdata unwind
//   0x18018e09c / 0x18018dfe0   relocations
//   Entry Point [EXTERNAL]      the export table
//
// So the ONE route into either body is an indirect call through a function
// pointer sitting in the DLL's own `.data`, put there by the DLL's static
// initialiser with an in-module `LEA`.
//
// The reader is generic and our tree owns a copy of it:
// `?CreateObject@Factory@Object@@` (src/engine/object/type_reflection.cpp)
// ends in `rec->info->factoryFn()`, and `?AddController@DialogInstance@UI@@`
// (src/engine/ui/dialog_create.cpp) does the same by hand. The record they read
// is `UI::Dialog`'s own (DEFINE_TYPE in src/engine/ui/dialog_vtable.cpp).
//
// Both bodies are unconditional; the pair has no arming flag
// (docs/runtime_port/DIRECTION.md §2, item 3).
// docs/history/runtime_port/WAVE10E_LANE_FINAL.md §3 holds the
// `??_7Dialog@UI@@6B@` promotion expiry that names this pair, and
// docs/history/runtime_port/WAVE11_KICKOFF.md restates it.
//
// `CreateNewObject` allocates: retail inlines `System::MemAlloc(0x88, 8)`
// (0x1800c7a50) — lock `_singleton@MemoryAllocator + 0x640`, call
// `?Allocate@MemoryAllocator@System@@` (0x1800c6e60), unlock. The port calls
// `Object::detail::InternalMemAlloc`.
//
// `CreateInPlaceObject` allocates NOTHING — it is placement construction on a
// caller-owned buffer.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <new>

#include "ui/Dialog.h"

// ============================================================================
// ENDGAME B.3.9 block UI-D (B39_RULES D2): the pair is the class's OWN statics,
// `UI::Dialog::CreateNewObject` / `CreateInPlaceObject`, building a real
// `UI::Dialog` with `::new`. The attendance report (`[DlgFactory]`) goes with
// the two doors it counted.
// ============================================================================
namespace UI {

// ?CreateInPlaceObject@Dialog@UI@@SAPEAVBase@Object@@PEAX@Z   0x180038610
// Retail's body is the `??0Dialog@UI@@` constructor INLINED (0x180038626..
// 0x180038696, the same stores, the +0x44 hole). THE NULL CHECK IS RETAIL'S:
// 0x180038621 `TEST RCX,RCX / JZ 0x18003869b`, the null branch returns 0.
Object::Base* Dialog::CreateInPlaceObject(void* p) {
    if (p == nullptr) return nullptr;      // 180038621 / 18003869b
    return ::new (p) Dialog();             // 180038696 MOV RAX,RCX
}

// ?CreateNewObject@Dialog@UI@@SAPEAVBase@Object@@XZ           0x180038530
// The inlined `MemAlloc(0x88, 8)` (0x180038561..0x180038571: `MOV EDX,0x88 ;
// LEA R8D,[RDX-0x80]`), then the inlined ctor. The null check at 0x18003858a
// is retail's and covers an allocation failure.
Object::Base* Dialog::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(Dialog), 8);
    if (p == nullptr) return nullptr;      // 18003858a / 180038601
    return ::new (p) Dialog();             // 1800385fc MOV RAX,RDI
}

}  // namespace UI
