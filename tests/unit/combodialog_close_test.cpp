// ============================================================================
// tests/unit/combodialog_close_test.cpp — review S-035
// ============================================================================
// UI::ForgetClosingComboDialog (src/engine/ui/combodialog_close.cpp, NOT
// RETAIL) is what UserInterface::CloseDialog calls on a dialog's first close.
// A closing drop-down clears its combo's `_openDialog` when that still names
// it, so ~ComboElement never reads a drop-down CleanRemovedDialogs freed
// first. It touches nothing else, and reads no ComboDialog field of a dialog
// that is not one.
//
// The whole route (the real ~ComboDialog and ~ComboElement, freed in
// CleanRemovedDialogs' order) needs comboelement.cpp and combodialog.cpp,
// which the unit build cannot link (ui_dialog_abi_test.cpp defines
// ComboDialog's slots as markers); it runs under ASan in
// docs/history/review_20261103/static/poc/s035/.
//
// The host never runs Factory::Startup, so ComboDialog::TypeId() and the
// probe's Type() are both 0xFFFFFFFF; the plain dialog answers its own id.
// ============================================================================
#include "unit.h"

#include "ui/ComboDialog.h"
#include "ui/ComboElement.h"
#include "ui/DialogInstance.h"
#include "object/Internal.h"

#include <cstdint>
#include <cstdlib>

namespace Object { namespace detail { extern InternalFactory* g_unitInternalFactory; } }

namespace {

struct S035FactoryScope {
    ::Object::InternalFactory  factory;
    ::Object::InternalFactory* saved;
    S035FactoryScope() : factory(), saved(::Object::detail::g_unitInternalFactory) {
        ::Object::detail::g_unitInternalFactory = &factory;
    }
    ~S035FactoryScope() { ::Object::detail::g_unitInternalFactory = saved; }
};

struct DropDownProbe final : UI::ComboDialog {};

struct PlainDialogProbe final : UI::DialogInstance {
    uint32_t Type() const override { return 0x5035u; }
};

// Heap storage of the exact size, so an ASan unit run sees any read past it.
// ComboDialog's own operator new / delete are not linked into the unit build.
template <class T>
T* NewDialog() {
    return ::new (std::malloc(sizeof(T))) T();
}

template <class T>
void DeleteDialog(T* dialog) {
    dialog->~T();
    std::free(dialog);
}

// A ComboElement in raw zeroed storage: the helper reads `_openDialog` only.
UI::ComboElement* NewCombo() {
    return static_cast<UI::ComboElement*>(std::calloc(1, sizeof(UI::ComboElement)));
}

}  // namespace

UNIT_TEST(combodialog_close_clears_the_combo_that_still_names_it) {
    S035FactoryScope scope;
    DropDownProbe* dropDown = NewDialog<DropDownProbe>();
    UI::ComboElement* combo = NewCombo();
    dropDown->_combo = combo;
    combo->_openDialog = dropDown;
    CHECK_EQ(dropDown->Type(), UI::ComboDialog::TypeId());

    UI::ForgetClosingComboDialog(dropDown);

    CHECK(combo->_openDialog == nullptr);   // ~ComboElement now skips its CloseDialog
    CHECK(dropDown->_combo == combo);       // the drop-down's own field is retail's
    DeleteDialog(dropDown);
    std::free(combo);
}

UNIT_TEST(combodialog_close_leaves_a_combo_that_names_another_drop_down) {
    S035FactoryScope scope;
    DropDownProbe* closing = NewDialog<DropDownProbe>();
    DropDownProbe* current = NewDialog<DropDownProbe>();
    UI::ComboElement* combo = NewCombo();
    closing->_combo = combo;
    current->_combo = combo;
    combo->_openDialog = current;

    UI::ForgetClosingComboDialog(closing);
    CHECK(combo->_openDialog == current);

    combo->_openDialog = nullptr;           // already cleared by a retail site
    UI::ForgetClosingComboDialog(closing);
    CHECK(combo->_openDialog == nullptr);
    DeleteDialog(current);
    DeleteDialog(closing);
    std::free(combo);
}

UNIT_TEST(combodialog_close_ignores_a_drop_down_with_no_combo) {
    S035FactoryScope scope;
    DropDownProbe* dropDown = NewDialog<DropDownProbe>();   // the factory ctor zeroes `_combo`
    CHECK(dropDown->_combo == nullptr);
    UI::ForgetClosingComboDialog(dropDown);
    CHECK(dropDown->_combo == nullptr);
    DeleteDialog(dropDown);
}

// A dialog that is not a ComboDialog is 0xb8 bytes: `_combo` (+0xc8) lies past it.
UNIT_TEST(combodialog_close_reads_nothing_of_another_dialog) {
    S035FactoryScope scope;
    PlainDialogProbe* plain = NewDialog<PlainDialogProbe>();
    CHECK(plain->Type() != UI::ComboDialog::TypeId());
    UI::ForgetClosingComboDialog(plain);
    DeleteDialog(plain);
}
