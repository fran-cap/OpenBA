// ============================================================================
// S-035 harness: a ComboDialog (the open drop-down) and the ComboElement that
// opened it, freed in each order the UI can free them. ASan, clang-19 (WSL).
//
// REAL code under test, compiled from the tree given to build.sh:
//   src/engine/ui/combodialog.cpp        ~ComboDialog   (retail 0x180111cd0)
//   src/engine/ui/comboelement.cpp       ~ComboElement  (retail 0x180112490)
//   src/engine/ui/combodialog_close.cpp  ForgetClosingComboDialog (the fix;
//                                        absent from the trees before it)
// Stubbed: UserInterface::CloseDialog, modelled by its Block C ("already in
// _removedDialogs: return") and the push; it never deletes, as the real one.
// With the fix it calls ForgetClosingComboDialog where the real one does
// (right after Block C). Also stubbed: the base-class teardown, the Internal
// ctor / dtor, the type record ctor and the array destructors (the arrays are
// empty). The drop-down is built by its default ctor in a malloc'd block (so
// its vptr is real); the combo is zeroed storage with `_openDialog` set.
//
//   outside   a caller other than the drop-down's four close sites closes it
//             (no such caller exists in the tree today: the drop-down is not a
//             child of its host, ctor 0x180111d31, so the finding's "host
//             close, Block A" route does not happen); CleanRemovedDialogs
//             deletes it, then the host's teardown runs ~ComboElement.
//   combo     the combo is torn down with its drop-down open: ~ComboElement
//             closes the drop-down, the combo is freed, the queued drop-down
//             is deleted later.
//   closefree the drop-down closes itself (OnUpdate: click outside,
//             0x180111c84 then 0x180111c9d; SetCurrentItem, ComboDescription
//             and OnElementEvent do the same), the host then rebuilds its
//             elements (the combo is freed), and the next CleanRemovedDialogs
//             deletes the drop-down.
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ui/ComboDialog.h"
#include "ui/ComboElement.h"
#include "ui/UserInterface.h"

namespace {
int g_closes = 0;
UI::DialogInstance* g_removed[8];
int g_removedCount = 0;
}  // namespace

namespace UI {
void UserInterface::CloseDialog(Object::InternalPtr<DialogInstance>* dialog) {
    ++g_closes;
    DialogInstance* const dlg = dialog->Get();
    for (int i = 0; i < g_removedCount; ++i) {
        if (g_removed[i] == dlg)
            return;                                   // Block C
    }
#ifdef S035_HAVE_FORGET
    ForgetClosingComboDialog(dlg);
#endif
    g_removed[g_removedCount++] = dlg;
}
void DialogInstance::ReleaseFields() {}
void Element::ReleaseFields() {}
ButtonElement::~ButtonElement() {}
}  // namespace UI
namespace Object {
Internal::Internal() {}
Internal::~Internal() {}
// The DEFINE_TYPE records' static ctor: no Factory in the harness, so every
// type id stays 0 (ComboDialog's and the drop-down's alike).
BaseTypeInfo::BaseTypeInfo(const char*, const char*, const char*, Base* (*)(), Base* (*)(void*)) {}
}  // namespace Object
namespace rtcoll {
void* RtColl_ArrayDtor_180024aa0(void* a) {
    return a;
}
void* RtColl_ArrayDtor_180037310(void* a) {
    return a;
}
void MemFree(void* p) {
    std::free(p);
}
}  // namespace rtcoll
namespace RtSystem {
void RtMemFree(void* p) {
    std::free(p);
}
}  // namespace RtSystem

namespace {
void DeleteDropDown(UI::ComboDialog* dlg) {
    dlg->UI::ComboDialog::~ComboDialog();
    std::free(dlg);
}
void DeleteCombo(UI::ComboElement* combo) {
    combo->UI::ComboElement::~ComboElement();
    std::free(combo);
}
}  // namespace

int main(int argc, char** argv) {
    const char* route = argc > 1 ? argv[1] : "outside";
    alignas(16) static unsigned char uiBytes[sizeof(UI::UserInterface)];
    auto* ui = reinterpret_cast<UI::UserInterface*>(uiBytes);

    auto* combo = static_cast<UI::ComboElement*>(std::calloc(1, sizeof(UI::ComboElement)));
    auto* dlg = ::new (std::malloc(sizeof(UI::ComboDialog))) UI::ComboDialog();
    dlg->_ui = ui;
    dlg->_combo = combo;          // the ComboDialog ctor, 0x180111d00
    combo->_openDialog = dlg;     // ComboElement::Update: the drop-down is open
    combo->_currentItem = -1;

    if (std::strcmp(route, "outside") == 0) {
        Object::InternalPtr<UI::DialogInstance> slot(dlg);
        ui->CloseDialog(&slot);   // an outside close of the drop-down
        DeleteDropDown(dlg);      // CleanRemovedDialogs
        DeleteCombo(combo);       // the host's element teardown
    } else if (std::strcmp(route, "combo") == 0) {
        DeleteCombo(combo);
        DeleteDropDown(dlg);
    } else if (std::strcmp(route, "closefree") == 0) {
        if (combo->_openDialog != nullptr)
            combo->_openDialog = nullptr;             // 0x180111c84
        Object::InternalPtr<UI::DialogInstance> slot(dlg);
        ui->CloseDialog(&slot);                       // 0x180111c9d
        DeleteCombo(combo);       // the host rebuilds its elements
        DeleteDropDown(dlg);      // the next CleanRemovedDialogs
    } else {
        std::fprintf(stderr, "usage: s035_combo outside|combo|closefree\n");
        return 2;
    }
    std::printf("[S-035] %s route: done, CloseDialog calls=%d\n", route, g_closes);
    return 0;
}
