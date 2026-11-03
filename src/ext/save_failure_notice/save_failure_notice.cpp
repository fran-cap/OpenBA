// ============================================================================
// src/ext/save_failure_notice/save_failure_notice.cpp -- fix.save_failure_notice (NOT RETAIL)
// ============================================================================
// docs/retail_bugs.md §27; the header has the mechanism. The notice is the game's MessageDialog
// over our Dialog/Extensions.rsc:saveFailed (the game's MessageBox with only OK showing), added
// to the game's UserInterface without a nested modal loop: state 2 of the save runs inside
// GameUpdateLoop::Update, and the box closes itself on OK (MessageDialog::OnOk).
// ============================================================================
#include "ext/save_failure_notice/save_failure_notice.h"
#include "ext/save_failure_notice/save_failure_notice_rule.h"

#include "ext/resources/text_provider.h"
#include "Game/GameUpdateLoop.h"
#include "Dialog/MessageDialog.h"
#include "ui/Dialog.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/TextElement.h"
#include "ui/UserInterface.h"
#include "io/Persistence.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "err_line.h"

#include <new>

namespace Ext {
namespace SaveFailureNotice {

namespace {

constexpr const char* kNoticeDialog = "Dialog/Extensions.rsc:saveFailed";

bool g_shownSinceSuccess = false;

void ShowNotice() {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    UI::UserInterface* ui = loop != nullptr ? loop->m_userInterface : nullptr;
    if (ui == nullptr || ::Object::ExternalFactory::_singleton == nullptr) return;

    Ext::Resources::InitText();   // idempotent; the row is off under the faithful profile anyway
    const ::Object::External* ext =
        ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kNoticeDialog));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        ErrLine("[SaveFailureNotice] %s did not load as a UI::Dialog\n", kNoticeDialog);
        return;
    }
    const UI::Dialog* rsc = static_cast<const UI::Dialog*>(ext);

    void* mem = System::MemoryAllocator::MemAlloc(sizeof(MessageDialog), 8);
    if (mem == nullptr) return;
    MessageDialog* notice = ::new (mem) MessageDialog(ui, rsc, Object::InternalPtr<UI::DialogInstance>());
    Object::InternalPtr<UI::DialogInstance> ref(notice);

    // @0: the save folder, so the player knows which folder to check.
    UI::Element* raw = notice->FindElementType(UI::TextElement::TypeId()).Get();
    const bool hasText = ::Object::IsDerivedFromNullSafe(raw, UI::TextElement::TypeId());
    const ::IO::Persistence* persistence = ::IO::Persistence::_singleton;
    // GetSavePath is the user folder; the saves sit in its Save\ (GetSaveFilename's "%sSave\%s.sav").
    const System::String userPath = persistence != nullptr ? persistence->GetSavePath() : System::String("");
    const System::String folder =
        System::String::Format("%sSave", userPath._data != nullptr ? userPath._data : "");
    if (hasText) static_cast<UI::TextElement*>(raw)->SetParameter(0, folder);

    ui->AddDialog(&ref);   // dispatches OnOpened; OK closes it
    // One line per notice (a rare event, not a hot path): which folder the box names.
    ErrLine("[SaveFailureNotice] notice opened (text element %s, folder \"%s\")\n", hasText ? "found" : "MISSING",
            folder._data != nullptr ? folder._data : "");
}

}  // namespace

void OnSaveResult(bool saved) {
    if (!saved) ErrLine("[SaveFailureNotice] the save did not reach the disk\n");
    if (!ShouldNotify(saved, g_shownSinceSuccess)) return;
    ShowNotice();
}

}  // namespace SaveFailureNotice
}  // namespace Ext
