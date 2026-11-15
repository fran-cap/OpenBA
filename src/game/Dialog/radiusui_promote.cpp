// src/game/Dialog/radiusui_promote.cpp
//
// RadiusUI: standalone vtable promotion (out of UI_DIALOG_STUB).
//
// RETAIL VTABLE — read byte-wise, not guessed. `vtable_RadiusUI` @ 0x14015d160,
// 11 slots. The 0x20 bytes BEFORE it (0x14015d140) are the RTTI type descriptor
// (shared pointer 0x14003a978 + the ASCII class name "RadiusUI"), exactly the trap
// reference_rtti_descriptor_mistaken_for_vtable warns about.
//
//   idx0  (+0x00) 0x140013640  ScalarDtor
//   idx1  (+0x08) 0x140013580
//   idx2  (+0x10) 0x140013570  GetTypeId
//   idx3  (+0x18) 0x14003a95a  \
//   idx4  (+0x20) 0x14003a984   |  6-byte `FF 25` jmp thunks into the exe's Runtime
//   idx7  (+0x38) 0x14003a97e   |  dispatch slots (0x1401505e0/e8/f0/f8/0x140150600/
//   idx8  (+0x40) 0x14003a96c   |  608/678/0x140151770) — i.e. INHERITED
//   idx9  (+0x48) 0x14003a972   |  UI::DialogController base methods, no exe override.
//   idx10 (+0x50) 0x14003a978  /
//   idx5  (+0x28) 0x140077650  RadiusUI::Slot5 — build the coverage overlay
//   idx6  (+0x30) 0x140077600  RadiusUI::Slot6 — tear the overlay down
//
// So RadiusUI overrides exactly two slots, and radiusui.cpp's header comments
// (idx5 = +0x28 = 0x140077650, idx6 = +0x30 = 0x140077600) are CORRECT — unlike
// the CropUI ones. Base slots are UI::DialogController's (inherited), matching orchardui.cpp /
// eventlogui.cpp: they are Runtime DialogController methods with no exe body to port.
//
// DialogController slot semantics (see the verified map at the top of
// eventlogui.cpp): idx5 = setup on show, idx6 = teardown, idx7 = per-frame sync.
//
// WHY THIS FILE EXISTS: `UI_DIALOG_STUB(RadiusUI, "UI::DialogController", 0x38)`
// gave RadiusUI a 48-slot table where only idx0/idx2 were real, so both ported
// bodies were dead code and no info panel ever showed its coverage radius.
// RadiusUI is the FIRST controller in the UIDescription list of FishermansDock.rsc /
// HunterLodge.rsc / GathererShelter.rsc.
//
// The overlay submit itself (fn_RadiusCalc = 0x140055430) only became safe today:
// Player_HighlightAdd_55430 used to pass `gameWorld + 0x110` instead of
// `*(int64_t*)(gameWorld + 0x110)` and faulted instantly, which is almost certainly
// why RadiusUI was left stubbed.

#include "Dialog/ui_panels.h"
#include <new>
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "Object/factory_registration.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

// ~RadiusUI: the tree's own teardown (its hand-built slot 0 ran this, then freed
// WITHOUT ~DialogController); moved here unchanged. The compiler now chains
// ~DialogController -> ~Internal after it.
RadiusUI::~RadiusUI() {
    // Retail's dtor path runs the base DialogController dtor, which for a still-open
    // panel has already had idx6 dispatched. Drop the overlay here too so a destroyed
    // controller cannot leak a highlight slot.
    RadiusUI::OnClosed();
}



// create 0x140013590: MemAlloc(0x38, 8), then the ctor (DialogController ->
// Internal: AddObject, the compiler's stamps, the field initialisers).
::Object::Base* RadiusUI::CreateNewObject() {
    void* p = Object::detail::InternalMemAlloc(sizeof(RadiusUI), 8);
    if (!p) return nullptr;
    return static_cast<Object::Base*>(new (p) RadiusUI());
}
// Retail's createInPlace does not build a controller in place (B8-13); the
// tree's in-place entry did, and that is kept.
::Object::Base* RadiusUI::CreateInPlaceObject(void* storage) {
    if (!storage) return nullptr;
    return static_cast<Object::Base*>(new (storage) RadiusUI());
}

DEFINE_EXE_TYPE(RadiusUI, "UI::DialogController")
