// ============================================================================
// src/ext/mods_persist/mods_in_game_warning.cpp -- fix.mods_persist, the in-game Mods warning (NOT RETAIL)
// ============================================================================
// docs/retail_bugs.md §23; the header has the behaviour. The box is the game's MessageDialog over
// our Dialog/Extensions.rsc:modsInGame (the game's MessageBox with Yes and No showing), shown with
// UserInterface::ShowDialog from the pause menu's button handler, as retail's own Reload.rsc:game
// confirmation is shown from ModDialog::Apply. Yes answers 2, as every MessageBox confirm.
// ============================================================================
#include "ext/mods_persist/mods_in_game_warning.h"

#include "ext/resources/text_provider.h"
#include "Dialog/MessageDialog.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "options.h"
#include "err_line.h"

#include <new>

namespace Ext {
namespace ModsInGameWarning {

namespace {
constexpr const char* kWarningDialog = "Dialog/Extensions.rsc:modsInGame";
constexpr int kYes = 2;
}  // namespace

bool Confirm(UI::UserInterface* ui) {
    if (Opt().modalAnswer || Opt().unattendedBoot) {
        const int answer = Opt().modalAnswer ? Opt().modalAnswerValue : kYes;
        ErrLine("[ModsInGame] unattended run: warning answered %d\n", answer);
        return answer == kYes;
    }
    if (ui == nullptr || ::Object::ExternalFactory::_singleton == nullptr) return true;

    Ext::Resources::InitText();   // idempotent; the row is off under the faithful profile anyway
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kWarningDialog));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        ErrLine("[ModsInGame] %s did not load as a UI::Dialog: the Mods window opens without it\n",
                kWarningDialog);
        return true;
    }
    const UI::Dialog* rsc = static_cast<const UI::Dialog*>(ext);

    void* mem = System::MemoryAllocator::MemAlloc(sizeof(MessageDialog), 8);
    if (mem == nullptr) return true;
    MessageDialog* box = ::new (mem) MessageDialog(ui, rsc, Object::InternalPtr<UI::DialogInstance>());
    Object::InternalPtr<UI::DialogInstance> ref(box);
    // ShowDialog takes its argument's slot and writes through it; the original is released after,
    // as SaveLoadDialog::DoLoad does with its mod notices.
    Object::InternalPtr<UI::DialogInstance> show = ref;
    const int answer = ui->ShowDialog(&show);
    ref.DeletePtr();
    // One line per pause-menu Mods click (a player action, not a hot path).
    ErrLine("[ModsInGame] warning answered %d (%s)\n", answer, answer == kYes ? "open Mods" : "cancelled");
    return answer == kYes;
}

}  // namespace ModsInGameWarning
}  // namespace Ext
